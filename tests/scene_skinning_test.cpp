#include "scene_raster3d.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(x)                                  \
  do {                                            \
    if (!(x)) {                                   \
      std::printf("FAIL %d: %s\n", __LINE__, #x); \
      ++failures;                                 \
    }                                             \
  } while (0)
static bool near(float a, float b) { return std::fabs(a - b) < 0.00001f; }
static void weight(ScxMesh &mesh, float value) {
  uint32_t bits;
  std::memcpy(&bits, &value, 4);
  for (unsigned i = 0; i < 4; ++i) mesh.weights.push_back(uint8_t(bits >> (i * 8)));
}
int main() {
  const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  auto moved = identity;
  moved[12] = 2;
  std::vector<std::array<float, 16>> palette{identity, moved};
  ScxMesh mesh;
  mesh.vertexCount = 1;
  mesh.positions = {10, 20, 30};
  mesh.normals = {1, 0, 0};
  mesh.bonePalette = {1, 0};
  mesh.boneIndices = {0, 1, 0, 0};
  std::vector<float> p, n;
  std::string error;
  CHECK(sceneSkinMesh(mesh, palette, p, n, error));
  CHECK(p.size() == 3 && near(p[0], 30) && near(p[1], 20) && near(p[2], 30));
  CHECK(n.size() == 3 && near(n[0], 1));
  CHECK(mesh.positions[0] == 10);
  mesh.weightBytesPerVertex = 4;
  weight(mesh, 0.25f);
  CHECK(sceneSkinMesh(mesh, palette, p, n, error));
  CHECK(near(p[0], 15)); // Remaining 0.75 belongs to bone zero, not bone one.
  moved = identity;
  moved[0] = 0;
  moved[1] = 1;
  moved[4] = -1;
  moved[5] = 0;
  moved[12] = 2;
  palette[1] = moved;
  mesh.weights.clear();
  weight(mesh, 1.0f);
  CHECK(sceneSkinMesh(mesh, palette, p, n, error));
  CHECK(near(p[0], 0) && near(p[1], 10));
  CHECK(near(n[0], 0) && near(n[1], 1)); // Translation never affects normals.
  mesh.weights.clear();
  weight(mesh, 0.25f);
  CHECK(sceneSkinMesh(mesh, palette, p, n, error));
  CHECK(near(n[0], 3.0f / std::sqrt(10.0f)) && near(n[1], 1.0f / std::sqrt(10.0f)));
  mesh.weights.clear();
  weight(mesh, 1.0f);
  palette[1] = identity;
  CHECK(sceneSkinMesh(mesh, palette, p, n, error));
  CHECK(p == mesh.positions && n == mesh.normals);
  // Four influences use the local map, original byte order, and a residual
  // fourth weight. The sum is deliberately >1: stock does not clamp it.
  palette = {identity, identity, identity, identity};
  for (size_t i = 0; i < palette.size(); ++i) palette[i][12] = float(i + 1);
  mesh.bonePalette = {3, 1, 0, 2};
  mesh.boneIndices = {0, 1, 2, 3};
  mesh.weightBytesPerVertex = 12;
  mesh.weights.clear();
  weight(mesh, 0.75f); weight(mesh, 0.5f); weight(mesh, 0.25f);
  CHECK(sceneSkinMesh(mesh, palette, p, n, error));
  CHECK(near(p[0], 37.5f)); // 10 + 10*(4*.75 + 2*.5 + 1*.25 - 3*.5).
  mesh.weightBytesPerVertex = 8;
  mesh.weights.clear();
  weight(mesh, 0.25f); weight(mesh, 0.5f);
  CHECK(sceneSkinMesh(mesh, palette, p, n, error));
  CHECK(near(p[0], 32.5f)); // 4*.25 + 2*.5 + 1*.25, in metres.
  mesh.boneIndices[0] = 4;
  CHECK(!sceneSkinMesh(mesh, palette, p, n, error));
  mesh.boneIndices[0] = 0;
  mesh.bonePalette[0] = 4;
  CHECK(!sceneSkinMesh(mesh, palette, p, n, error));
  mesh.bonePalette.clear();
  CHECK(!sceneSkinMesh(mesh, palette, p, n, error));
  // Chunk 2's fixed-function path has no per-vertex indices.
  mesh.boneIndices.clear();
  mesh.bonePalette = {3, 1};
  mesh.weightBytesPerVertex = 4;
  mesh.weights.clear();
  weight(mesh, 0.25f);
  CHECK(sceneSkinMesh(mesh, palette, p, n, error));
  CHECK(near(p[0], 35));
  mesh.weights.clear();
  weight(mesh, NAN);
  CHECK(!sceneSkinMesh(mesh, palette, p, n, error));
  CHECK(p.empty() && n.empty());
  mesh.bonePalette.clear();
  mesh.weights.clear();
  mesh.weightBytesPerVertex = 0;
  CHECK(sceneSkinMesh(mesh, palette, p, n, error));
  CHECK(p.empty() && n.empty()); // Unskinned path uses original arrays.
  return failures ? 1 : 0;
}
