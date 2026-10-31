#include "raster3d.hpp"
#include "resource_world.hpp"
#include "scene_raster3d.hpp"

#include "render2d.hpp"
#include "world_model.hpp"

#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <random>

namespace {

int failures = 0;
#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,    \
                   #condition);                                                \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

constexpr uint32_t W = 160, H = 120;

struct Px {
  uint8_t r, g, b;
};
Px pixelAt(const Raster3D &r, uint32_t x, uint32_t y) {
  const uint8_t *p = &r.pixels()[(static_cast<size_t>(y) * r.width() + x) * 4];
  return {p[0], p[1], p[2]};
}
bool near(Px a, Px b, int tolerance = 1) {
  return std::abs(a.r - b.r) <= tolerance && std::abs(a.g - b.g) <= tolerance &&
         std::abs(a.b - b.b) <= tolerance;
}

Raster3DTexture checker(uint32_t cells, uint32_t texelsPerCell, Px a, Px b,
                        uint8_t alpha = 255) {
  const uint32_t size = cells * texelsPerCell;
  std::vector<uint8_t> rgba(static_cast<size_t>(size) * size * 4);
  for (uint32_t y = 0; y < size; ++y)
    for (uint32_t x = 0; x < size; ++x) {
      const bool first = ((x / texelsPerCell) + (y / texelsPerCell)) % 2 == 0;
      const Px c = first ? a : b;
      uint8_t *p = &rgba[(static_cast<size_t>(y) * size + x) * 4];
      p[0] = c.r;
      p[1] = c.g;
      p[2] = c.b;
      p[3] = alpha;
    }
  Raster3DTexture texture;
  std::string error;
  if (!Raster3DTexture::build(size, size, rgba.data(), rgba.size(), texture,
                              error)) {
    std::fprintf(stderr, "texture: %s\n", error.c_str());
    std::exit(1);
  }
  return texture;
}

// Camera at the origin looking down -Z, world matrix identity moved to (0,
// height, 0).
struct Camera {
  std::array<float, 16> view, projection;
  float cot, aspect;
};
Camera makeCamera(float height, float fov, uint32_t width,
                  uint32_t pixelHeight) {
  CameraResource resource =
      makeCameraResource(0, fov, 0.01f, 10.0f, 0, 1, 0, 0);
  std::array<float, 16> world{
      {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, height, 0, 1}};
  const float aspect =
      static_cast<float>(width) / static_cast<float>(pixelHeight);
  const auto state = cameraViewState(resource, world, aspect);
  if (!state) {
    std::fprintf(stderr, "camera state failed\n");
    std::exit(1);
  }
  return {state->view, state->projection, state->projection[5], aspect};
}

struct Floor {
  std::vector<float> positions, uvs;
  std::vector<uint32_t> indices;
};
// Ground quad x in [-4, 4], z in [-1.5, -9], y = 0; u runs along x, v along
// depth.
Floor makeFloor() {
  Floor f;
  f.positions = {-4, 0, -1.5f, 4, 0, -1.5f, 4, 0, -9, -4, 0, -9};
  f.uvs = {0, 0, 1, 0, 1, 1, 0, 1};
  f.indices = {0, 1, 2, 0, 2, 3};
  return f;
}

Raster3DDraw drawFor(const Floor &f, const Camera &camera) {
  Raster3DDraw d;
  d.positions = f.positions.data();
  d.vertexCount = f.positions.size() / 3;
  d.uvs = f.uvs.data();
  d.indices = f.indices.data();
  d.indexCount = f.indices.size();
  d.view = camera.view;
  d.projection = camera.projection;
  return d;
}

// Analytic expected checker cell for a pixel centre, from intersecting the view
// ray with the plane. Returns false when the ray misses the quad or lands
// within `margin` of a cell edge.
bool analyticCell(const Camera &camera, uint32_t px, uint32_t py,
                  uint32_t cells, float margin, int &parity) {
  const float nx = (static_cast<float>(px) + 0.5f) / W * 2 - 1;
  const float ny = 1 - (static_cast<float>(py) + 0.5f) / H * 2;
  if (!(ny < -1e-4f))
    return false;
  const float d = -camera.cot / ny; // plane is 1 below the camera: yv = -1
  const float xv = nx * d / (camera.cot / camera.aspect);
  const float u = (xv + 4) / 8;
  const float v = (d - 1.5f) / 7.5f;
  if (u < 0 || u >= 1 || v < 0 || v >= 1)
    return false;
  const float su = u * static_cast<float>(cells),
              sv = v * static_cast<float>(cells);
  const float fu = su - std::floor(su), fv = sv - std::floor(sv);
  if (fu < margin || fu > 1 - margin || fv < margin || fv > 1 - margin)
    return false;
  parity =
      (static_cast<int>(std::floor(su)) + static_cast<int>(std::floor(sv))) % 2;
  return true;
}

void testPerspectiveChecker() {
  const Px colorA{230, 30, 30}, colorB{30, 30, 230};
  const auto texture = checker(8, 1, colorA, colorB);
  const auto camera = makeCamera(1.0f, 60.0f, W, H);
  const auto floorMesh = makeFloor();
  std::string error;
  Raster3D perspective, affine;
  CHECK(perspective.resize(W, H, error));
  CHECK(affine.resize(W, H, error));
  perspective.clear(0, 0, 0);
  affine.clear(0, 0, 0);
  Raster3DDraw d = drawFor(floorMesh, camera);
  d.material.texture = &texture;
  d.material.filter = Raster3DFilter::Point;
  CHECK(perspective.draw(d, error));
  d.material.perspectiveCorrect = false;
  CHECK(affine.draw(d, error));

  size_t checked = 0, wrongPerspective = 0, wrongAffine = 0;
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) {
      int parity = 0;
      if (!analyticCell(camera, x, y, 8, 0.03f, parity))
        continue;
      const Px expected = parity == 0 ? colorA : colorB;
      ++checked;
      if (!near(pixelAt(perspective, x, y), expected, 0))
        ++wrongPerspective;
      if (!near(pixelAt(affine, x, y), expected, 0))
        ++wrongAffine;
    }
  std::printf(
      "checker: %zu pixels checked, perspective wrong %zu, affine wrong %zu\n",
      checked, wrongPerspective, wrongAffine);
  CHECK(checked > 2000);
  CHECK(wrongPerspective == 0);
  CHECK(wrongAffine > checked / 10); // the affine control must visibly
                                     // disagree, or this test proves nothing
}

void testMipmapAverages() {
  // 1-texel cells (64 x 64): far away the filtered result must approach the
  // average, point sampling must not.
  const Px colorA{255, 255, 255}, colorB{0, 0, 0};
  const auto texture = checker(64, 1, colorA, colorB);
  const auto camera = makeCamera(1.0f, 60.0f, W, H);
  const auto floorMesh = makeFloor();
  std::string error;
  Raster3D filtered, point;
  CHECK(filtered.resize(W, H, error) && point.resize(W, H, error));
  filtered.clear(0, 0, 0);
  point.clear(0, 0, 0);
  Raster3DDraw d = drawFor(floorMesh, camera);
  d.material.texture = &texture;
  d.material.filter = Raster3DFilter::Trilinear;
  CHECK(filtered.draw(d, error));
  d.material.filter = Raster3DFilter::Point;
  CHECK(point.draw(d, error));
  // Rows near the horizon see the far end of the floor. Measure the spread
  // there.
  int farSpreadFiltered = 0, farSpreadPoint = 0;
  for (uint32_t y = 72; y < 75; ++y)
    for (uint32_t x = 60; x < 100; ++x) {
      farSpreadFiltered = std::max(farSpreadFiltered,
                                   std::abs(pixelAt(filtered, x, y).r - 128));
      farSpreadPoint =
          std::max(farSpreadPoint, std::abs(pixelAt(point, x, y).r - 128));
    }
  std::printf(
      "mip: far rows max deviation from mid grey: trilinear %d, point %d\n",
      farSpreadFiltered, farSpreadPoint);
  CHECK(farSpreadPoint > 100);
  CHECK(farSpreadFiltered < 60);
}

std::array<float, 16> identity() {
  return {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
}

// Draws a triangle given directly in clip space (identity matrices, w = 1).
bool drawClipTriangle(Raster3D &r, const float p[9], Raster3DMaterial material,
                      std::string &error, Raster3DViewport viewport = {}) {
  const uint32_t idx[3] = {0, 1, 2};
  Raster3DDraw d;
  d.positions = p;
  d.vertexCount = 3;
  d.indices = idx;
  d.indexCount = 3;
  d.material = material;
  d.viewport = viewport;
  return r.draw(d, error);
}

void testDepth() {
  const auto camera = makeCamera(0.0f, 60.0f, W, H);
  const float nearTri[9] = {-1, -1, -3, 1, -1, -3, 0, 1.5f, -3};
  const float farTri[9] = {-3, -3, -6, 3, -3, -6, 0, 4.5f, -6};
  const uint32_t idx[3] = {0, 1, 2};
  std::string error;
  const auto drawTri = [&](Raster3D &r, const float *p,
                           std::array<float, 4> color) {
    Raster3DDraw d;
    d.positions = p;
    d.vertexCount = 3;
    d.indices = idx;
    d.indexCount = 3;
    d.view = camera.view;
    d.projection = camera.projection;
    d.material.diffuse = color;
    return r.draw(d, error);
  };
  Raster3D a, b;
  CHECK(a.resize(W, H, error) && b.resize(W, H, error));
  a.clear(0, 0, 0);
  b.clear(0, 0, 0);
  CHECK(drawTri(a, nearTri, {1, 0, 0, 1}) && drawTri(a, farTri, {0, 0, 1, 1}));
  CHECK(drawTri(b, farTri, {0, 0, 1, 1}) && drawTri(b, nearTri, {1, 0, 0, 1}));
  CHECK(a.pixels() == b.pixels());
  CHECK(near(pixelAt(a, W / 2, H / 2), {255, 0, 0}, 0));
  // A far-only pixel (outside the near triangle but inside the far one) is
  // blue.
  bool sawBlue = false;
  for (uint32_t x = 0; x < W; ++x)
    if (near(pixelAt(a, x, H / 2 + 30), {0, 0, 255}, 0))
      sawBlue = true;
  CHECK(sawBlue);
  // With the depth test off, draw order decides.
  Raster3D c;
  CHECK(c.resize(W, H, error));
  c.clear(0, 0, 0);
  Raster3DDraw d;
  d.positions = nearTri;
  d.vertexCount = 3;
  d.indices = idx;
  d.indexCount = 3;
  d.view = camera.view;
  d.projection = camera.projection;
  d.material.diffuse = {1, 0, 0, 1};
  CHECK(c.draw(d, error));
  d.positions = farTri;
  d.material.diffuse = {0, 0, 1, 1};
  d.material.depthTest = false;
  CHECK(c.draw(d, error));
  CHECK(near(pixelAt(c, W / 2, H / 2), {0, 0, 255}, 0));
  // A depth-test-only draw leaves the depth buffer alone.
  Raster3D e;
  CHECK(e.resize(W, H, error));
  e.clear(0, 0, 0);
  d.material.depthTest = true;
  d.material.depthWrite = false;
  CHECK(e.draw(d, error));
  CHECK(e.depth()[(H / 2) * W + W / 2] == 1.0f);
}

void testAlpha() {
  // 2x1 texture: left texel transparent white, right texel opaque white.
  const uint8_t texels[8] = {255, 255, 255, 0, 255, 255, 255, 255};
  Raster3DTexture texture;
  std::string error;
  CHECK(Raster3DTexture::build(2, 1, texels, sizeof texels, texture, error));
  CHECK(texture.hasAlpha);
  const float quad[12] = {-1, -1, 0.5f, 1, -1, 0.5f, 1, 1, 0.5f, -1, 1, 0.5f};
  const float uv[8] = {0, 0, 1, 0, 1, 1, 0, 1};
  const uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
  Raster3DDraw d;
  d.positions = quad;
  d.vertexCount = 4;
  d.uvs = uv;
  d.indices = idx;
  d.indexCount = 6;
  d.material.texture = &texture;
  d.material.filter = Raster3DFilter::Point;
  d.material.addressU = d.material.addressV = Raster3DAddress::Clamp;
  Raster3D r;
  CHECK(r.resize(W, H, error));
  r.clear(255, 0, 0);
  d.material.blend = Raster3DBlend::AlphaTest;
  CHECK(r.draw(d, error));
  CHECK(near(pixelAt(r, 10, 60), {255, 0, 0}, 0)); // transparent half rejected
  CHECK(near(pixelAt(r, 150, 60), {255, 255, 255}, 0));
  CHECK(r.stats().pixelsAlphaRejected > 0);
  // Reference above the texel alpha rejects everything.
  r.clear(255, 0, 0);
  d.material.alphaRef = 255;
  CHECK(r.draw(d, error));
  CHECK(near(pixelAt(r, 150, 60), {255, 255, 255}, 0));
  // Blend with diffuse alpha 0.5 over red.
  r.clear(255, 0, 0);
  d.material.blend = Raster3DBlend::AlphaBlend;
  d.material.depthWrite = false;
  d.material.diffuse = {1, 1, 1, 128.0f / 255.0f};
  CHECK(r.draw(d, error));
  CHECK(near(pixelAt(r, 150, 60), {255, 128, 128}, 1));
  CHECK(near(pixelAt(r, 10, 60), {255, 0, 0},
             0)); // texel alpha 0 contributes nothing
  // Opaque ignores alpha entirely.
  r.clear(255, 0, 0);
  d.material.blend = Raster3DBlend::Opaque;
  d.material.diffuse = {1, 1, 1, 0};
  CHECK(r.draw(d, error));
  CHECK(near(pixelAt(r, 10, 60), {255, 255, 255}, 0));
}

void testCullAndFill() {
  std::string error;
  Raster3D r;
  CHECK(r.resize(W, H, error));
  const float ccw[9] = {
      -0.5f, -0.5f, 0.5f, 0.5f, -0.5f,
      0.5f,  0,     0.5f, 0.5f}; // counter-clockwise as seen on screen
  const float cw[9] = {-0.5f, -0.5f, 0.5f, 0, 0.5f, 0.5f, 0.5f, -0.5f, 0.5f};
  Raster3DMaterial m;
  m.diffuse = {1, 1, 1, 1};
  const auto count = [&](const float *tri, Raster3DCull cull) {
    r.clear(0, 0, 0);
    m.cull = cull;
    std::string e;
    CHECK(drawClipTriangle(r, tri, m, e));
    size_t n = 0;
    for (size_t i = 0; i < r.pixels().size(); i += 4)
      if (r.pixels()[i])
        ++n;
    return n;
  };
  CHECK(count(ccw, Raster3DCull::None) > 1000);
  CHECK(count(ccw, Raster3DCull::None) == count(cw, Raster3DCull::None));
  CHECK(count(ccw, Raster3DCull::CounterClockwise) == 0);
  CHECK(count(ccw, Raster3DCull::Clockwise) > 1000);
  CHECK(count(cw, Raster3DCull::Clockwise) == 0);
  CHECK(count(cw, Raster3DCull::CounterClockwise) > 1000);

  // Exact coverage: a quad from NDC -0.5..0.5 covers pixels x 40..119 and
  // y 30..89 and nothing else.
  const float quad[12] = {-0.5f, -0.5f, 0.5f, 0.5f,  -0.5f, 0.5f,
                          0.5f,  0.5f,  0.5f, -0.5f, 0.5f,  0.5f};
  const uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
  Raster3DDraw d;
  d.positions = quad;
  d.vertexCount = 4;
  d.indices = idx;
  d.indexCount = 6;
  d.material.blend =
      Raster3DBlend::AlphaBlend; // a double-covered pixel would blend twice
  d.material.depthWrite = false;
  d.material.diffuse = {1, 1, 1, 0.5f};
  r.clear(0, 0, 0);
  CHECK(r.draw(d, error));
  size_t covered = 0, doubled = 0, outside = 0;
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) {
      const bool inside = x >= 40 && x < 120 && y >= 30 && y < 90;
      const int v = pixelAt(r, x, y).r;
      if (inside && v == 128)
        ++covered;
      else if (inside && v == 192)
        ++doubled;
      else if (!inside && v != 0)
        ++outside;
    }
  std::printf("fill: covered %zu of 4800, doubled %zu, outside %zu\n", covered,
              doubled, outside);
  CHECK(covered == 80 * 60);
  CHECK(doubled == 0);
  CHECK(outside == 0);
  // Viewport rectangle: the right half of the frame only.
  r.clear(0, 0, 0);
  Raster3DViewport right{0.5f, 0, 0.5f, 1};
  const float full[9] = {-1, -1, 0.5f, 3, -1, 0.5f, -1, 3, 0.5f};
  Raster3DMaterial solid;
  solid.diffuse = {1, 1, 1, 1};
  CHECK(drawClipTriangle(r, full, solid, error, right));
  CHECK(near(pixelAt(r, 20, 60), {0, 0, 0}, 0));
  CHECK(near(pixelAt(r, 100, 60), {255, 255, 255}, 0));
}

void testViewportMapping() {
  ViewportInstance viewport{};
  viewport.left = 0.1f;
  viewport.top = 0.2f;
  viewport.width = 0.7f;
  viewport.height = 0.6f;
  const float points[][2] = {{0, 0}, {-1, 1}, {1, -1}, {0.25f, -0.5f}};
  for (const auto &p : points) {
    const auto stock =
        cameraViewportPoint({p[0], p[1], 0.5f}, viewport, 640, 480);
    const auto ours =
        raster3dViewportPoint(p[0], p[1], {0.1f, 0.2f, 0.7f, 0.6f}, 640, 480);
    CHECK(stock.has_value());
    if (stock)
      CHECK(stock->at(0) == ours[0] && stock->at(1) == ours[1]);
  }
}

void testClipAgainstStock() {
  std::mt19937 rng(12345);
  std::uniform_real_distribution<float> coord(-3, 3), w(-1, 4), uv(0, 1);
  size_t compared = 0;
  for (int i = 0; i < 2000; ++i) {
    std::array<CameraClipVertex, 3> stock;
    std::array<Raster3DClipVertex, 3> ours;
    for (size_t k = 0; k < 3; ++k) {
      const float p[4] = {coord(rng), coord(rng), coord(rng), w(rng)};
      const float tu = uv(rng), tv = uv(rng);
      stock[k].position = {p[0], p[1], p[2], p[3]};
      stock[k].uv = {tu, tv};
      ours[k].position = {p[0], p[1], p[2], p[3]};
      ours[k].attr = {tu, tv, 0, 0, 0, 0, 0};
    }
    const auto expected = clipCameraTriangle(stock);
    const auto actual = raster3dClipTriangle(ours);
    CHECK(expected.has_value());
    if (!expected)
      continue;
    CHECK(expected->size() == actual.size());
    if (expected->size() != actual.size())
      continue;
    for (size_t k = 0; k < actual.size(); ++k) {
      for (size_t c = 0; c < 4; ++c)
        CHECK(expected->at(k).position[c] == actual[k].position[c]);
      for (size_t c = 0; c < 2; ++c)
        CHECK(expected->at(k).uv[c] == actual[k].attr[c]);
    }
    ++compared;
  }
  CHECK(compared == 2000);
  std::printf("clip: %zu random triangles match clipCameraTriangle\n",
              compared);

  // A triangle with a vertex behind the camera still draws its visible part.
  const auto camera = makeCamera(0.0f, 60.0f, W, H);
  const float tri[9] = {-1, -1, -3, 1, -1,
                        -3, 0,  0,  2}; // apex behind the camera
  const uint32_t idx[3] = {0, 1, 2};
  Raster3DDraw d;
  d.positions = tri;
  d.vertexCount = 3;
  d.indices = idx;
  d.indexCount = 3;
  d.view = camera.view;
  d.projection = camera.projection;
  d.material.diffuse = {1, 1, 1, 1};
  Raster3D r;
  std::string error;
  CHECK(r.resize(W, H, error));
  r.clear(0, 0, 0);
  CHECK(r.draw(d, error));
  CHECK(r.stats().trianglesClipped == 1 && r.stats().pixelsWritten > 100);
  // Entirely behind the camera draws nothing.
  const float behind[9] = {-1, -1, 3, 1, -1, 3, 0, 1, 3};
  d.positions = behind;
  r.resetStats();
  CHECK(r.draw(d, error));
  CHECK(r.stats().trianglesRejectedOutside == 1 &&
        r.stats().pixelsWritten == 0);
}

void testInvalidInput() {
  std::string error;
  Raster3D r;
  CHECK(!r.resize(0, 10, error) && !error.empty());
  CHECK(r.resize(8, 8, error));
  const float tri[9] = {-1, -1, 0.5f, 1, -1, 0.5f, 0, 1, 0.5f};
  const uint32_t idx[3] = {0, 1, 2};
  Raster3DDraw d;
  d.positions = tri;
  d.vertexCount = 3;
  d.indices = idx;
  d.indexCount = 3;
  CHECK(r.draw(d, error));
  Raster3DDraw bad = d;
  bad.material.blend = static_cast<Raster3DBlend>(9);
  CHECK(!r.draw(bad, error) && error.find("blend") != std::string::npos);
  bad = d;
  bad.material.cull = static_cast<Raster3DCull>(9);
  CHECK(!r.draw(bad, error) && error.find("cull") != std::string::npos);
  bad = d;
  bad.material.filter = static_cast<Raster3DFilter>(9);
  CHECK(!r.draw(bad, error));
  bad = d;
  const uint32_t badIdx[3] = {0, 1, 7};
  bad.indices = badIdx;
  CHECK(!r.draw(bad, error) && error.find("index") != std::string::npos);
  bad = d;
  bad.indexCount = 2;
  CHECK(!r.draw(bad, error));
  const float nan[9] = {NAN, 0, 0, 1, 0, 0, 0, 1, 0};
  bad = d;
  bad.positions = nan;
  CHECK(!r.draw(bad, error) && error.find("non-finite") != std::string::npos);
  bad = d;
  const auto texture = checker(2, 1, {0, 0, 0}, {255, 255, 255});
  bad.material.texture = &texture; // no UVs supplied
  CHECK(!r.draw(bad, error) && error.find("UV") != std::string::npos);
  bad = d;
  bad.projection[0] = INFINITY;
  CHECK(!r.draw(bad, error));
  Raster3D empty;
  CHECK(!empty.draw(d, error));
}

ScxMaterial scxMaterial(uint32_t type, uint32_t function) {
  ScxMaterial m;
  m.name = "test";
  m.flags = type;
  m.functionWord = function;
  m.diffuse = {0.5f, 0.5f, 0.5f, 1};
  m.maps.push_back({0, 0, 0, 3, 1, 1, 0, 0});
  return m;
}

void testScxMaterial() {
  std::string error;
  Raster3DScxMaterial out;
  Raster3DScxOptions strict, lenient;
  lenient.allowApproximations = true;
  CHECK(raster3dScxMaterial(scxMaterial(0, 0), strict, out, error));
  CHECK(out.state.blend == Raster3DBlend::Opaque && out.baseTextureIndex == 0u);
  CHECK(raster3dScxMaterial(scxMaterial(0, 0x40000000), strict, out, error));
  CHECK(out.state.blend == Raster3DBlend::AlphaBlend && !out.state.depthWrite);
  CHECK(raster3dScxMaterial(scxMaterial(0, 0x20000000), strict, out, error));
  CHECK(out.state.blend == Raster3DBlend::AlphaTest &&
        out.state.alphaRef == 0x38);
  auto tested = scxMaterial(0, 0x20000000);
  tested.properties.push_back({0x05000000u, 0x19});
  CHECK(raster3dScxMaterial(tested, strict, out, error) &&
        out.state.alphaRef == 0x19);
  CHECK(raster3dScxMaterial(scxMaterial(0, 0x80000000), strict, out, error) &&
        out.state.useVertexColor);
  CHECK(!raster3dScxMaterial(scxMaterial(0, 0x10000000), strict, out, error) &&
        error.find("function word") != std::string::npos);
  Raster3DScxOptions ignore;
  ignore.ignoreUnknownFunctionBits = true;
  CHECK(raster3dScxMaterial(scxMaterial(0, 0x10000000), ignore, out, error));
  CHECK(!raster3dScxMaterial(scxMaterial(0, 0x60000000), strict, out, error));
  // Approximated types fail unless allowed.
  CHECK(!raster3dScxMaterial(scxMaterial(0x15, 0), strict, out, error) &&
        error.find("not reproduced") != std::string::npos);
  CHECK(raster3dScxMaterial(scxMaterial(0x15, 0), lenient, out, error) &&
        !out.notes.empty());
  // Listed in the stock switch but not covered: fail. Not in the switch: stock
  // falls back to Tex.
  CHECK(!raster3dScxMaterial(scxMaterial(7, 0), lenient, out, error) &&
        error.find("unsupported") != std::string::npos);
  CHECK(raster3dScxMaterial(scxMaterial(0x100, 0), strict, out, error) &&
        !out.notes.empty());
  // No texture map: flat colour with the diffuse alpha only when blending.
  ScxMaterial flat = scxMaterial(0x30, 0);
  flat.maps.clear();
  flat.diffuse = {0.2f, 0.3f, 0.4f, 0.5f};
  CHECK(raster3dScxMaterial(flat, strict, out, error));
  CHECK(!out.baseTextureIndex && out.state.diffuse[0] == 0.2f &&
        out.state.diffuse[3] == 1.0f);
  flat.functionWord = 0x40000000;
  CHECK(raster3dScxMaterial(flat, strict, out, error) &&
        out.state.diffuse[3] == 0.5f);
}

void put32(std::vector<uint8_t> &b, size_t at, uint32_t v) {
  for (int i = 0; i < 4; ++i)
    b[at + static_cast<size_t>(i)] = static_cast<uint8_t>(v >> (8 * i));
}

void testDxt() {
  // One 4x4 block of DXT5: alpha 255 / 0 interpolated, colour block red / blue
  // with known indices.
  std::vector<uint8_t> file(128 + 16, 0);
  put32(file, 0, 0x20534444);
  put32(file, 4, 124);
  put32(file, 12, 4);
  put32(file, 16, 4);
  put32(file, 28, 1);
  put32(file, 76, 32);
  put32(file, 80, 4);
  put32(file, 84, 0x35545844);
  uint8_t *block = &file[128];
  block[0] = 255;
  block[1] = 0;
  // Alpha indices: pixel 0 -> 0 (255), pixel 1 -> 1 (0), pixel 2 -> 2 (7/7
  // weights: (6*255+0)/7 = 218).
  uint64_t bits = 0 | (1ull << 3) | (2ull << 6);
  for (int i = 0; i < 6; ++i)
    block[2 + i] = static_cast<uint8_t>(bits >> (8 * i));
  block[8] = 0x00;
  block[9] = 0xf8; // c0 = 0xf800 red
  block[10] = 0x1f;
  block[11] = 0x00; // c1 = 0x001f blue
  put32(file, 128 + 12,
        0x00000024); // pixel0 -> 0, pixel1 -> 1, pixel2 -> 2 (bits: 00 01 10)
  Raster3DPixels pixels;
  std::string error;
  CHECK(raster3dDecodeDxt(file.data(), file.size(), pixels, error));
  CHECK(pixels.width == 4 && pixels.height == 4);
  CHECK(pixels.rgba[0] == 255 && pixels.rgba[1] == 0 && pixels.rgba[2] == 0 &&
        pixels.rgba[3] == 255);
  CHECK(pixels.rgba[4] == 0 && pixels.rgba[5] == 0 && pixels.rgba[6] == 255 &&
        pixels.rgba[7] == 0);
  CHECK(pixels.rgba[8] == 170 && pixels.rgba[10] == 85 &&
        pixels.rgba[11] == 218); // 2/3 red + 1/3 blue
  // DXT3: explicit 4-bit alpha.
  std::vector<uint8_t> dxt3 = file;
  put32(dxt3, 84, 0x33545844);
  std::fill(dxt3.begin() + 128, dxt3.begin() + 136, 0);
  dxt3[128] = 0xf0; // pixel 0 alpha 0, pixel 1 alpha 15 -> 255
  CHECK(raster3dDecodeDxt(dxt3.data(), dxt3.size(), pixels, error));
  CHECK(pixels.rgba[3] == 0 && pixels.rgba[7] == 255);
  // Unsupported fourcc fails explicitly.
  put32(dxt3, 84, 0x30545844);
  CHECK(!raster3dDecodeDxt(dxt3.data(), dxt3.size(), pixels, error));
  CHECK(!raster3dDecodeDxt(file.data(), 100, pixels, error));
  for (uint32_t format : {0x31545844u, 0x33545844u, 0x35545844u}) {
    const size_t blockSize = format == 0x31545844u ? 8 : 16;
    auto mip = file;
    mip.resize(128 + blockSize * 2, 0);
    put32(mip, 84, format);
    put32(mip, 28, 2);
    auto *lower = mip.data() + 128 + blockSize;
    std::fill(lower, lower + blockSize, 0);
    const size_t colorOffset = blockSize == 8 ? 0 : 8;
    if (format == 0x33545844u)
      std::fill(lower, lower + 8, 255);
    if (format == 0x35545844u)
      lower[0] = 255;
    lower[colorOffset] = 0xe0;
    lower[colorOffset + 1] = 0x07;
    Raster3DTexture texture;
    CHECK(raster3dDecodeDxtTexture(mip.data(), mip.size(), texture, error));
    CHECK(texture.levels.size() == 2 && texture.levels[1].width == 2 &&
          texture.levels[1].height == 2);
    CHECK(texture.levels[1].rgba ==
          std::vector<uint8_t>({0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255,
                                0, 255, 0, 255}));
    CHECK(
        !raster3dDecodeDxtTexture(mip.data(), mip.size() - 1, texture, error));
    CHECK(texture.levels.empty());
  }
}

void testPresent() {
  using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
  Surface surface(SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_RGBA32),
                  SDL_DestroySurface);
  CHECK(surface != nullptr);
  if (!surface)
    return;
  std::string error;
  auto renderer = Render2D::forSurface(surface.get(), error);
  CHECK(renderer != nullptr);
  if (!renderer)
    return;
  Raster3D r;
  CHECK(r.resize(8, 8, error));
  r.clear(10, 20, 30);
  CHECK(renderer->clear(0, 0, 0, 255, error) && r.blit(*renderer, error) &&
        renderer->present(error));
  const auto readPixel = [&](int x, int y) {
    const auto *p = static_cast<const uint8_t *>(surface->pixels) +
                    y * surface->pitch + x * 4;
    return Px{p[0], p[1], p[2]};
  };
  CHECK(SDL_LockSurface(surface.get()));
  CHECK(near(readPixel(3, 3), {10, 20, 30}, 0));
  SDL_UnlockSurface(surface.get());
  r.clear(200, 100, 50); // second blit takes the update path
  CHECK(renderer->clear(0, 0, 0, 255, error) && r.blit(*renderer, error) &&
        renderer->present(error));
  CHECK(SDL_LockSurface(surface.get()));
  CHECK(near(readPixel(7, 7), {200, 100, 50}, 0));
  SDL_UnlockSurface(surface.get());
  // Resizing re-creates the texture.
  CHECK(r.resize(4, 4, error));
  r.clear(1, 2, 3);
  CHECK(renderer->clear(0, 0, 0, 255, error) && r.blit(*renderer, error) &&
        renderer->present(error));
  CHECK(SDL_LockSurface(surface.get()));
  CHECK(near(readPixel(5, 5), {1, 2, 3}, 0));
  SDL_UnlockSurface(surface.get());
}

void testStoredTextureLevels() {
  Raster3DTexture texture;
  std::string error;
  CHECK(raster3dLoadTextureFile(
      ".scratch/2005-extract/Program Files/Street Legal Racing - "
      "Redline/cars/fake_racers/Badge/textures/Badge_frame.dds",
      texture, error));
  CHECK(texture.levels.size() == 8);
  if (texture.levels.size() != 8)
    return;
  CHECK(texture.levels.front().width == 512 &&
        texture.levels.back().width == 4);
  for (const auto &level : texture.levels)
    CHECK(level.rgba.size() ==
          static_cast<size_t>(level.width) * level.height * 4);
}

void testBmp() {
  std::string error;
  Raster3D r;
  CHECK(r.resize(5, 3, error));
  r.clear(9, 8, 7);
  const auto path =
      std::filesystem::temp_directory_path() / "raster3d_test.bmp";
  CHECK(r.saveBmp(path, error));
  std::error_code ec;
  CHECK(std::filesystem::file_size(path, ec) ==
        54 + 16 * 3); // 5 px * 3 = 15 -> 16 bytes per row
  SDL_Surface *loaded = SDL_LoadBMP(path.string().c_str());
  CHECK(loaded != nullptr);
  if (loaded) {
    SDL_Surface *rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    CHECK(rgba && loaded->w == 5 && loaded->h == 3);
    if (rgba) {
      const auto *p = static_cast<const uint8_t *>(rgba->pixels);
      CHECK(p[0] == 9 && p[1] == 8 && p[2] == 7);
      SDL_DestroySurface(rgba);
    }
    SDL_DestroySurface(loaded);
  }
  std::filesystem::remove(path, ec);
}

// Real data, when this machine has the extracted 2005 install: a car chassis
// through the full path.
void testRealModel() {
  const std::filesystem::path chassis =
      ".scratch/2005-extract/Program Files/Street Legal Racing - "
      "Redline/cars/racers/Einvagen_data/meshes/chassis.scx";
  std::error_code ec;
  if (!std::filesystem::exists(chassis, ec)) {
    std::puts("real model: skipped (no extracted 2005 install)");
    return;
  }
  const auto loaded = scxLoadFile(chassis.string());
  CHECK(loaded.ok);
  if (!loaded.ok)
    return;
  CHECK(loaded.model.meshes.size() == 3 && loaded.model.materials.size() == 3);
  Raster3DScxOptions strict;
  Raster3DScxMaterial out;
  std::string error;
  // The chassis uses types 4 and 0x15, which draw extra layers: strict mode
  // must refuse them.
  CHECK(!raster3dScxMaterial(loaded.model.materials[0], strict, out, error));
  strict.allowApproximations = true;
  Raster3D r;
  CHECK(r.resize(W, H, error));
  r.clear(0, 0, 0);
  const auto camera = makeCamera(0.0f, 60.0f, W, H);
  std::array<float, 16> model = identity();
  model[14] = -60; // stock world units are decimetres: the chassis is 378 mesh
                   // units long, 37.8 after the 0.1 scale
  for (const auto &mesh : loaded.model.meshes) {
    CHECK(raster3dScxMaterial(loaded.model.materials[*mesh.materialIndex],
                              strict, out, error));
    Raster3DDraw d;
    d.positions = mesh.positions.data();
    d.vertexCount = mesh.vertexCount;
    d.uvs = mesh.uv1.data();
    d.indices = mesh.indices.data();
    d.indexCount = mesh.indices.size();
    d.positionScale = 0.1f;
    d.model = model;
    d.view = camera.view;
    d.projection = camera.projection;
    d.material = out.state;
    d.material.texture = nullptr;
    d.material.diffuse = {0.8f, 0.8f, 0.8f, 1};
    CHECK(r.draw(d, error));
  }
  CHECK(r.stats().pixelsWritten > 1000);
  std::printf("real model: chassis.scx drew %llu pixels from %llu triangles\n",
              static_cast<unsigned long long>(r.stats().pixelsWritten),
              static_cast<unsigned long long>(r.stats().trianglesSubmitted));
}

} // namespace

// Fixed-function state record and combiner for the stock ground passes (mode 3
// / mode 0).
namespace {

void testGroundFvfPackedSplit() {
  // FVF 0x15003f: 3f position, 3f normal, two packed words, three float2 UVs
  // (56 bytes).
  std::vector<uint8_t> bytes(56 * 2, 0);
  auto writeVertex = [&](size_t v, const float *pos, const float *nrm,
                         uint32_t w0, uint32_t w1, const float *uv) {
    uint8_t *p = bytes.data() + v * 56;
    std::memcpy(p, pos, 12);
    std::memcpy(p + 12, nrm, 12);
    std::memcpy(p + 24, &w0, 4);
    std::memcpy(p + 28, &w1, 4);
    std::memcpy(p + 32, uv, 24);
  };
  const float pos[3] = {1, 2, 3}, nrm[3] = {0, 1, 0};
  const float uv[6] = {0.25f, 0.5f, 0.75f, 0.125f, 1.0f, 0.0f};
  const uint32_t w0 = 0xAA112233, w1 = 0xBB445566; // POLY source words [6], [7]
  writeVertex(0, pos, nrm, w0, w1, uv);
  writeVertex(1, pos, nrm, 0xff000000, 0x00ffffff, uv);
  std::string error;
  std::vector<Raster3DGroundVertex> out;
  CHECK(raster3dGroundFvf(bytes.data(), 2, out, error));
  CHECK(out.size() == 2);
  CHECK(out[0].position[0] == 1 && out[0].position[1] == 2 &&
        out[0].position[2] == 3);
  CHECK(out[0].normal[1] == 1);
  CHECK(out[0].uv[0][0] == 0.25f && out[0].uv[1][1] == 0.125f &&
        out[0].uv[2][0] == 1.0f);
  // The stock split: diffuse = word0 & 0xff000000; specular = (word0 &
  // 0xffffff) | (word1 << 24).
  CHECK(out[0].diffusePacked() == 0xAA000000);
  CHECK(out[0].specularPacked() ==
        0x66112233u); // RGB from word0, top byte from word1's low byte
  CHECK(out[1].diffusePacked() == 0xff000000);
  // Only word1's low byte survives the 32-bit left shift; word0 supplies
  // zero RGB here (00526da0), so the result is alpha-only.
  CHECK(out[1].specularPacked() == 0xff000000u);
  CHECK(!raster3dGroundFvf(nullptr, 1, out, error));
  bytes[3] = 0x7f;
  bytes[2] = 0x80;
  bytes[1] = 0;
  bytes[0] = 0; // NaN in the first position
  CHECK(!raster3dGroundFvf(bytes.data(), 1, out, error));
}

void testCombiner() {
  std::string error;
  Raster3DCombinerInput in;
  in.diffuse = {0.8f, 0.2f, 0.4f, 0.5f};
  in.texture = {1.0f, 0.5f, 0.25f, 0.2f};
  in.specular = {0.1f, 0.2f, 0.3f, 1.0f};
  in.factor = {0.9f, 0.1f, 0.1f, 1.0f};

  // Stage 0 Disable yields the diffuse colour (D3D first-stage convention).
  Raster3DTexStage disable0;
  disable0.colorOp = Raster3DTexOp::Disable;
  disable0.alphaOp = Raster3DTexOp::Disable;
  std::array<float, 4> cur{{0, 0, 0, 0}};
  CHECK(raster3dCombineStage(disable0, 0, in, cur, error));
  CHECK(cur == in.diffuse);

  // Modulate with TEXTURE x CURRENT on both streams.
  Raster3DTexStage mod;
  mod.colorOp = mod.alphaOp = Raster3DTexOp::Modulate;
  mod.colorArg1 = mod.alphaArg1 = Raster3DTexArg::Texture;
  mod.colorArg2 = mod.alphaArg2 = Raster3DTexArg::Current;
  mod.textureBound = true;
  cur = in.diffuse;
  CHECK(raster3dCombineStage(mod, 0, in, cur, error));
  CHECK(std::abs(cur[0] - 0.8f) < 1e-6f && std::abs(cur[1] - 0.1f) < 1e-6f);
  CHECK(std::abs(cur[3] - 0.5f * 0.2f) < 1e-6f);

  // Add clamps to 1; TFactor and Specular resolve; Complement inverts.
  Raster3DTexStage add;
  add.colorOp = Raster3DTexOp::Add;
  add.colorArg1 = Raster3DTexArg::TFactor;
  add.colorArg2 = Raster3DTexArg::Specular;
  add.alphaOp = Raster3DTexOp::SelectArg1;
  add.alphaArg1 = static_cast<Raster3DTexArg>(
      static_cast<uint32_t>(Raster3DTexArg::AlphaReplicate) |
      static_cast<uint32_t>(Raster3DTexArg::Texture));
  add.textureBound = true;
  cur = {{0, 0, 0, 0}};
  CHECK(raster3dCombineStage(add, 1, in, cur, error));
  CHECK(std::abs(cur[0] - 1.0f) < 1e-6f); // 0.9 + 0.1 clamps
  CHECK(std::abs(cur[1] - 0.3f) < 1e-6f); // 0.1 + 0.2
  CHECK(std::abs(cur[3] - 0.2f) < 1e-6f); // texture alpha

  // BlendTextureAlpha interpolates towards arg2 by the texture alpha, all
  // channels.
  Raster3DTexStage blend;
  blend.colorOp = Raster3DTexOp::BlendTextureAlpha;
  blend.colorArg1 = Raster3DTexArg::Texture;
  blend.colorArg2 = Raster3DTexArg::Diffuse;
  blend.alphaOp = Raster3DTexOp::BlendTextureAlpha;
  blend.alphaArg1 = Raster3DTexArg::Texture;
  blend.alphaArg2 = Raster3DTexArg::Diffuse;
  blend.textureBound = true;
  cur = {{0, 0, 0, 0}};
  CHECK(raster3dCombineStage(blend, 0, in, cur, error));
  CHECK(std::abs(cur[0] - (0.2f * 1.0f + 0.8f * 0.8f)) < 1e-6f);
  CHECK(std::abs(cur[3] - (0.2f * 0.2f + 0.8f * 0.5f)) < 1e-6f);

  Raster3DTexStage multiplyAdd;
  multiplyAdd.colorOp = Raster3DTexOp::MultiplyAdd;
  multiplyAdd.colorArg0 = Raster3DTexArg::Specular;
  multiplyAdd.colorArg1 = Raster3DTexArg::Diffuse;
  multiplyAdd.colorArg2 = Raster3DTexArg::Temp;
  multiplyAdd.alphaOp = Raster3DTexOp::SelectArg1;
  multiplyAdd.alphaArg1 = Raster3DTexArg::Diffuse;
  in.temp = {0.5f, 0.5f, 0.5f, 0.5f};
  in.tempDefined = true;
  CHECK(raster3dCombineStage(multiplyAdd, 1, in, cur, error));
  CHECK(std::abs(cur[0] - 0.5f) < 1e-6f);
  CHECK(std::abs(cur[1] - 0.3f) < 1e-6f);
  multiplyAdd.colorArg0.reset();
  CHECK(!raster3dCombineStage(multiplyAdd, 1, in, cur, error));

  // Failures: unknown-marked slot, unset op, TEXTURE with no binding, unknown
  // arg value.
  Raster3DTexStage marked = mod;
  marked.unknown = 1; // colorOp branch-dependent
  cur = in.diffuse;
  CHECK(!raster3dCombineStage(marked, 0, in, cur, error));
  Raster3DTexStage unset;
  CHECK(!raster3dCombineStage(unset, 0, in, cur, error));
  Raster3DTexStage unbound = mod;
  unbound.textureBound = false;
  CHECK(!raster3dCombineStage(unbound, 0, in, cur, error));
  Raster3DTexStage badArg = mod;
  badArg.colorArg1 = static_cast<Raster3DTexArg>(6); // unsupported D3DTA
  CHECK(!raster3dCombineStage(badArg, 0, in, cur, error));
}

void testGroundStateMachine() {
  std::string error;
  Raster3DGroundStateMachine machine;
  Raster3DFixedState s;
  std::array<float, 4> cur;

  // Lighting-mode cache (FUN_004bb5f0): same mode without force is a no-op; a
  // change applies and sets dirty 0x08; turning it off clears the tracked
  // slots.
  CHECK(machine.applyLightingMode(s, true, false, error));
  CHECK(s.lighting && s.dirtyFlags & 0x08);
  s.lightSlotEnabled[2] = true;
  s.dirtyFlags = 0;
  CHECK(machine.applyLightingMode(s, true, false,
                                  error)); // cached: no-op, dirty stays clear
  CHECK(!(s.dirtyFlags & 0x08) && s.lightSlotEnabled[2]);
  CHECK(machine.applyLightingMode(s, false, false, error));
  CHECK(s.lighting && *s.lighting == false);
  CHECK(!s.lightSlotEnabled[2]);
  CHECK(machine.applyLightingMode(s, true, true, error)); // forced reapply
  CHECK(*s.lighting && s.dirtyFlags & 0x08);

  // No-primary mode resets the stage pair through FUN_004e9460(0).
  s = Raster3DFixedState();
  s.stages[1].colorOp = Raster3DTexOp::Add;
  s.stages[1].colorArg1 = Raster3DTexArg::Diffuse;
  s.stages[1].colorArg2 = Raster3DTexArg::Specular;
  s.stages[0].textureBound = true;
  s.stages[1].textureBound = true;
  s.lighting = true;

  // Mode 0 without a projector decision fails; with one, the no-primary branch
  // writes stage 0 MODULATE and disables stage 1.
  Raster3DMode0Input m0;
  m0.projector = Raster3DMode0Input::Projector::Unknown;
  CHECK(!machine.applyMode0(s, m0, error));
  m0.projector = Raster3DMode0Input::Projector::None;
  m0.defaultBlendStates = std::array<uint32_t, 5>{0, 0, 1, 0, 0};
  m0.resetSampler = std::array<uint32_t, 3>{1, 1, 0};
  CHECK(machine.applyMode0(s, m0, error));
  CHECK(s.stages[0].colorOp == Raster3DTexOp::Modulate &&
        s.stages[0].alphaOp == Raster3DTexOp::Modulate);
  CHECK(s.stages[1].colorOp == Raster3DTexOp::Disable);
  CHECK(s.stages[1].alphaOp == Raster3DTexOp::Disable);
  CHECK(!s.stages[0].textureBound && !s.stages[1].textureBound);
  CHECK(s.lighting && !*s.lighting);
  CHECK(s.raw.size() == 10 && s.raw[5][1] == 0x0e && s.raw[5][2] == 1);
  // Stage 0 args were never written by any applied pass: consuming them must
  // fail.
  CHECK(!raster3dCombineStage(s.stages[0], 0, Raster3DCombinerInput{}, cur,
                              error));

  // A projector is not a texture; the projector branch must not invent its
  // two textures or transforms.
  s = Raster3DFixedState();
  Raster3DMode0Input dual;
  dual.projector = Raster3DMode0Input::Projector::Present;
  dual.dualTexture = true;
  CHECK(!machine.applyMode0(s, dual, error));
  CHECK(!s.stages[0].textureBound && !s.stages[2].textureBound);
  Raster3DTexture t1, t2;
  CHECK(Raster3DTexture::build(1, 1, std::vector<uint8_t>(4, 255).data(), 4, t1,
                               error));
  CHECK(Raster3DTexture::build(1, 1, std::vector<uint8_t>(4, 255).data(), 4, t2,
                               error));
  const std::array<float, 16> identity{
      {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
  Raster3DProjectorBinding first, second;
  first.textures = {&t1, &t2};
  second.textures = {&t2, &t1};
  first.transforms = {identity, identity};
  second.transforms = {identity, identity};
  first.light0 = std::array<uint32_t, 26>{};
  first.material = std::array<uint32_t, 17>{};
  second.light0 = std::array<uint32_t, 26>{};
  second.material = std::array<uint32_t, 17>{};
  (*second.light0)[0] = 7;
  dual.primary = &first;
  dual.secondary = &second;
  dual.alphaTestStateActive = false;
  dual.alphaReference = 0x38;
  int hooks = 0;
  dual.projectorModeHookContext = &hooks;
  dual.projectorModeHook = [](void *context, Raster3DFixedState &) {
    ++*static_cast<int *>(context);
  };
  CHECK(machine.applyMode0(s, dual, error));
  CHECK(hooks == 2 && s.lighting && *s.lighting);
  CHECK(s.stages[0].boundTexture == &t1 && s.stages[1].boundTexture == &t2);
  CHECK(s.stages[2].boundTexture == &t2 && s.stages[3].boundTexture == &t1);
  CHECK(s.textureTransforms[0] == identity && s.deviceLight0 &&
        (*s.deviceLight0)[0] == 7);
  CHECK(s.stages[2].resultArg == Raster3DTexArg::Temp);
  CHECK(s.stages[3].colorOp == Raster3DTexOp::MultiplyAdd);
  CHECK(s.stages[3].colorArg2 == Raster3DTexArg::Temp);
  s.stages[2].textureBound = true;
  s.stages[3].textureBound = true;
  m0.dualTexture = true;
  CHECK(machine.applyMode0(s, m0, error));
  CHECK(!s.stages[2].textureBound && !s.stages[3].textureBound);
  CHECK(s.stages[3].colorOp == Raster3DTexOp::Disable);
  m0.dualTexture = false;

  // A pass cannot choose capability, material layout, texture resources, or
  // factor without observed inputs.
  s = Raster3DFixedState();
  Raster3DMode3Input m3;
  CHECK(!machine.applyMode3(s, m3, error));
  m3.capability00617928 = 0;
  m3.materialMapAt4c = false;
  m3.sourceTexturePresent = {true, false};
  CHECK(!machine.applyMode3(s, m3, error));
  m3.textures[0] = &t1;
  m3.textures[1] = &t2; // actual resolved fallback for absent material slot
  m3.textureFactorAlpha = 0x80;
  CHECK(machine.applyMode3(s, m3, error));
  CHECK(s.stages[0].colorOp == Raster3DTexOp::SelectArg1);
  CHECK(s.stages[1].colorOp == Raster3DTexOp::BlendTextureAlpha);
  CHECK(s.textureFactor == 0x80000000u);
  CHECK(s.stages[0].textureBound && s.stages[1].textureBound);
  CHECK(s.samplerRaw.size() == 4); // slot 0 address U/V and both min filters
  for (int i = 0; i < 100; ++i) {
    CHECK(machine.applyMode3(s, m3, error));
    CHECK(machine.applyMode0(s, m0, error));
  }
  CHECK(s.samplerRaw.size() == 12 && s.raw.size() == 10);

  s = Raster3DFixedState();
  m3.capability00617928 = 2;
  CHECK(machine.applyMode3(s, m3, error));
  CHECK(s.stages[2].colorOp == Raster3DTexOp::Modulate);
  CHECK(s.stages[2].colorArg1 == Raster3DTexArg::Diffuse);
  m3.passOrdinal = 1;
  CHECK(machine.applyMode3(s, m3, error));
  CHECK(s.stages[0].colorOp == Raster3DTexOp::Modulate);
  CHECK(s.stages[2].colorOp == Raster3DTexOp::Disable);
  CHECK(s.stages[2].colorArg1 == Raster3DTexArg::Texture);

  s = Raster3DFixedState();
  m3.capability00617928 = 0;
  m3.passOrdinal = 1;
  s.stages[0].textureBound = s.stages[1].textureBound = true;
  CHECK(machine.applyMode3(s, m3, error));
  CHECK(s.stages[0].colorOp == Raster3DTexOp::SelectArg2);
  CHECK(s.stages[1].colorOp == Raster3DTexOp::Disable);
  CHECK(!s.stages[0].textureBound && !s.stages[1].textureBound);
  m3.materialMapAt4c = true;
  CHECK(!machine.applyMode3(s, m3, error));
  m3.capability00617930 = 2;
  m3.passOrdinal = 0;
  CHECK(!machine.applyMode3(s, m3, error)); // third device texture required
  m3.textures[2] = &t1;
  CHECK(machine.applyMode3(s, m3, error));
  CHECK(s.stages[2].colorOp == Raster3DTexOp::Add);
  CHECK(s.stages[2].colorArg2 == Raster3DTexArg::Diffuse);
  CHECK(s.stages[2].resultArg == Raster3DTexArg::Temp);
  CHECK(s.stages[3].colorArg1 == Raster3DTexArg::Temp);
  m3.passOrdinal = 1;
  CHECK(machine.applyMode3(s, m3, error));
  CHECK(!s.stages[2].textureBound);
  CHECK(s.stages[3].colorOp == Raster3DTexOp::Disable);
}

void testFixedMultiStageDraw() {
  std::string error;
  Raster3D raster;
  CHECK(raster.resize(8, 8, error));
  raster.clear(0, 0, 0, 0);
  const uint8_t pixel0[] = {64, 32, 0, 128};
  const uint8_t pixel1[] = {32, 64, 0, 255};
  Raster3DTexture t0, t1;
  CHECK(Raster3DTexture::build(1, 1, pixel0, sizeof pixel0, t0, error));
  CHECK(Raster3DTexture::build(1, 1, pixel1, sizeof pixel1, t1, error));
  Raster3DFixedState state;
  state.textureFactor = 0xffffffffu;
  state.lighting = false;
  state.samplerRaw = {{0, 1, 1}, {0, 2, 1}, {0, 5, 1}, {0, 6, 1}, {0, 7, 0},
                      {1, 1, 1}, {1, 2, 1}, {1, 5, 1}, {1, 6, 1}, {1, 7, 0}};
  auto &a = state.stages[0];
  a.textureBound = true;
  a.boundTexture = &t0;
  a.colorOp = Raster3DTexOp::Modulate;
  a.resultArg = Raster3DTexArg::Current;
  a.colorArg1 = Raster3DTexArg::Texture;
  a.colorArg2 = Raster3DTexArg::Diffuse;
  a.alphaOp = Raster3DTexOp::SelectArg1;
  a.alphaArg1 = Raster3DTexArg::Texture;
  auto &b = state.stages[1];
  b.textureBound = true;
  b.boundTexture = &t1;
  b.colorOp = Raster3DTexOp::Add;
  b.resultArg = Raster3DTexArg::Current;
  b.colorArg1 = Raster3DTexArg::Texture;
  b.colorArg2 = Raster3DTexArg::Current;
  b.alphaOp = Raster3DTexOp::SelectArg1;
  b.alphaArg1 = Raster3DTexArg::Current;
  state.stages[2].colorOp = Raster3DTexOp::Disable;
  const float positions[] = {-1, -1, .5f, 1, -1, .5f, 0, 1, .5f};
  const uint32_t indices[] = {0, 1, 2};
  const uint8_t colors[] = {255, 255, 255, 255, 255, 255,
                            255, 255, 255, 255, 255, 255};
  const uint8_t specular[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  float coords[] = {.5f, .5f, 1, .5f, .5f, 1, .5f, .5f, 1};
  Raster3DFixedDraw fixed;
  fixed.state = &state;
  fixed.lightingResolved = true;
  fixed.specular = specular;
  fixed.stages[0].texture = &t0;
  fixed.stages[0].coordinates = coords;
  fixed.stages[1].texture = &t1;
  fixed.stages[1].coordinates = coords;
  Raster3DDraw draw;
  draw.positions = positions;
  draw.vertexCount = 3;
  draw.indices = indices;
  draw.indexCount = 3;
  draw.colors = colors;
  draw.fixed = &fixed;
  CHECK(raster.draw(draw, error));
  const size_t center = (4 * 8 + 4) * 4;
  CHECK(raster.pixels()[center] == 96 && raster.pixels()[center + 1] == 96);
  CHECK(raster.pixels()[center + 3] == 128);
  a.resultArg = Raster3DTexArg::Temp;
  b.colorArg2 = Raster3DTexArg::Temp;
  b.alphaArg1 = Raster3DTexArg::Temp;
  raster.clear(0, 0, 0, 0);
  CHECK(raster.draw(draw, error));
  CHECK(raster.pixels()[center] == 96 && raster.pixels()[center + 3] == 128);
  a.colorArg1 = Raster3DTexArg::Temp;
  CHECK(!raster.draw(draw, error)); // TEMP is not initialized at stage 0
  a.colorArg1 = Raster3DTexArg::Texture;
  a.resultArg = Raster3DTexArg::Current;
  b.colorArg2 = Raster3DTexArg::Current;
  b.alphaArg1 = Raster3DTexArg::Current;
  a.resultArg.reset();
  CHECK(!raster.draw(draw, error));
  a.resultArg = Raster3DTexArg::Current;
  draw.material.blend = Raster3DBlend::AlphaBlend;
  CHECK(!raster.draw(draw, error));
  draw.material.blend = Raster3DBlend::Opaque;
  state.samplerRaw[0][2] = 3;
  CHECK(!raster.draw(draw, error));
  state.samplerRaw[0][2] = 1;
  state.stages[0].boundTexture = &t1;
  CHECK(!raster.draw(draw, error));
  state.stages[0].boundTexture = &t0;
  t0.levels = {
      {2, 2, {64, 32, 0, 255, 64, 32, 0, 255, 64, 32, 0, 255, 64, 32, 0, 255}},
      {1, 1, {0, 128, 0, 255}}};
  state.samplerRaw[4][2] = 1;
  fixed.stages[0].mipFilter = Raster3DMipFilter::Point;
  fixed.stages[0].mipLodBias = 0.0f;
  fixed.stages[0].maxMipLevel = 0;
  state.samplerRaw.push_back({0, 8, 0});
  state.samplerRaw.push_back({0, 9, 0});
  coords[3] = 64;
  raster.clear(0, 0, 0, 0);
  CHECK(raster.draw(draw, error));
  CHECK(raster.pixels()[center] == 32 && raster.pixels()[center + 1] == 192);
  coords[2] = 0;
  CHECK(!raster.draw(draw, error));
  const float camera[] = {2, 4, 2};
  std::array<float, 16> matrix{
      {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 2, 0, 1}};
  float projected[3]{};
  CHECK(
      raster3dProjectorCoordinates(camera, 1, matrix, true, projected, error));
  CHECK(projected[0] == 3 && projected[1] == 6 && projected[2] == 2);
  CHECK(
      raster3dProjectorCoordinates(camera, 1, matrix, false, projected, error));
  CHECK(projected[0] == 3 && projected[1] == 6 && projected[2] == 1);
}

void testGroundSubmission() {
  GamePolygonGroup group;
  group.records.resize(3);
  const float positions[3][3] = {{-1, -1, 0.5f}, {1, -1, 0.5f}, {0, 1, 0.5f}};
  for (size_t i = 0; i < 3; ++i) {
    std::memcpy(group.records[i].data(), positions[i], sizeof positions[i]);
    const float normal = 1.0f;
    std::memcpy(&group.records[i][5], &normal, sizeof normal);
  }
  group.triples.push_back({0, 1, 2});
  ScxMesh mesh;
  std::string error;
  CHECK(sceneGroundMesh(group, mesh, error));
  CHECK(mesh.vertexCount == 3 && mesh.indices.size() == 3);
  Raster3D raster;
  CHECK(raster.resize(16, 16, error));
  raster.clear(0, 0, 0, 0);
  Raster3DDraw draw;
  draw.positions = mesh.positions.data();
  draw.vertexCount = mesh.vertexCount;
  draw.indices = mesh.indices.data();
  draw.indexCount = mesh.indices.size();
  CHECK(raster.draw(draw, error));
  CHECK(raster.stats().trianglesSubmitted == 1);
  CHECK(raster.stats().pixelsWritten > 0);
  raster.clear(0, 0, 0, 0);
  draw.material.fogNear = 0.1f;
  draw.material.fogFar = 0.9f;
  draw.material.fogColor = {0, 0, 1};
  CHECK(raster.draw(draw, error));
  CHECK(near(pixelAt(raster, 8, 8), {0, 0, 255}));
  group.triples[0][2] = 3;
  CHECK(!sceneGroundMesh(group, mesh, error));
}

} // namespace

int main() {
  testPerspectiveChecker();
  testMipmapAverages();
  testDepth();
  testAlpha();
  testCullAndFill();
  testViewportMapping();
  testClipAgainstStock();
  testInvalidInput();
  testScxMaterial();
  testDxt();
  testStoredTextureLevels();
  testPresent();
  testBmp();
  testRealModel();
  testGroundFvfPackedSplit();
  testCombiner();
  testGroundStateMachine();
  testFixedMultiStageDraw();
  testGroundSubmission();
  if (failures) {
    std::fprintf(stderr, "raster3d: %d check(s) failed\n", failures);
    return 1;
  }
  std::puts("raster3d: ok");
  return 0;
}
