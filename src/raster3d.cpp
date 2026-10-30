#include "raster3d.hpp"

#include "dds.hpp"
#include "ptx.hpp"
#include "render2d.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

namespace {

// ---------------------------------------------------------------- texture helpers

uint32_t le32(const uint8_t* p) {
	return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
	       (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

bool isPow2(uint32_t n) { return n && !(n & (n - 1)); }

inline int wrapIndex(int i, int n) {
	if (isPow2(static_cast<uint32_t>(n))) return i & (n - 1);
	int m = i % n;
	return m < 0 ? m + n : m;
}
inline int addressIndex(int i, int n, bool wrap) {
	if (wrap) return wrapIndex(i, n);
	return i < 0 ? 0 : (i >= n ? n - 1 : i);
}

// Fetches one filtered sample from a level. out is RGBA in 0..255.
inline void fetchBilinear(const Raster3DTexture::Level& level, float u, float v, bool wrapU, bool wrapV,
                          float out[4]) {
	float x = u * static_cast<float>(level.width) - 0.5f;
	float y = v * static_cast<float>(level.height) - 0.5f;
	if (!(std::fabs(x) < 1e9f)) x = 0;
	if (!(std::fabs(y) < 1e9f)) y = 0;
	const float fx0 = std::floor(x), fy0 = std::floor(y);
	const int x0 = static_cast<int>(fx0), y0 = static_cast<int>(fy0);
	const float fx = x - fx0, fy = y - fy0;
	const int w = static_cast<int>(level.width), h = static_cast<int>(level.height);
	const int xa = addressIndex(x0, w, wrapU), xb = addressIndex(x0 + 1, w, wrapU);
	const int ya = addressIndex(y0, h, wrapV), yb = addressIndex(y0 + 1, h, wrapV);
	const uint8_t* p00 = &level.rgba[(static_cast<size_t>(ya) * w + xa) * 4];
	const uint8_t* p10 = &level.rgba[(static_cast<size_t>(ya) * w + xb) * 4];
	const uint8_t* p01 = &level.rgba[(static_cast<size_t>(yb) * w + xa) * 4];
	const uint8_t* p11 = &level.rgba[(static_cast<size_t>(yb) * w + xb) * 4];
	const float w00 = (1 - fx) * (1 - fy), w10 = fx * (1 - fy), w01 = (1 - fx) * fy, w11 = fx * fy;
	for (int c = 0; c < 4; ++c) out[c] = p00[c] * w00 + p10[c] * w10 + p01[c] * w01 + p11[c] * w11;
}

inline void fetchPoint(const Raster3DTexture::Level& level, float u, float v, bool wrapU, bool wrapV,
                       float out[4]) {
	float x = u * static_cast<float>(level.width), y = v * static_cast<float>(level.height);
	if (!(std::fabs(x) < 1e9f)) x = 0;
	if (!(std::fabs(y) < 1e9f)) y = 0;
	const int w = static_cast<int>(level.width), h = static_cast<int>(level.height);
	const int xi = addressIndex(static_cast<int>(std::floor(x)), w, wrapU);
	const int yi = addressIndex(static_cast<int>(std::floor(y)), h, wrapV);
	const uint8_t* p = &level.rgba[(static_cast<size_t>(yi) * w + xi) * 4];
	for (int c = 0; c < 4; ++c) out[c] = p[c];
}

// Piecewise-linear log2, good to about 0.09; only used to pick a mip level.
inline float fastLog2(float x) {
	uint32_t bits;
	std::memcpy(&bits, &x, sizeof bits);
	return static_cast<float>(bits) * (1.0f / 8388608.0f) - 127.0f;
}

// ---------------------------------------------------------------- DXT decoding

struct Rgb { uint8_t r, g, b; };

void dxtColorBlock(const uint8_t* block, uint8_t palette[4][4]) {
	const uint16_t c0 = static_cast<uint16_t>(block[0] | (block[1] << 8));
	const uint16_t c1 = static_cast<uint16_t>(block[2] | (block[3] << 8));
	const auto unpack = [&](unsigned index, uint16_t color) {
		const unsigned r = (color >> 11) & 31, g = (color >> 5) & 63, b = color & 31;
		palette[index][0] = static_cast<uint8_t>((r << 3) | (r >> 2));
		palette[index][1] = static_cast<uint8_t>((g << 2) | (g >> 4));
		palette[index][2] = static_cast<uint8_t>((b << 3) | (b >> 2));
		palette[index][3] = 255;
	};
	unpack(0, c0);
	unpack(1, c1);
	for (unsigned c = 0; c < 3; ++c) {
		palette[2][c] = static_cast<uint8_t>((2 * palette[0][c] + palette[1][c]) / 3);
		palette[3][c] = static_cast<uint8_t>((palette[0][c] + 2 * palette[1][c]) / 3);
	}
	palette[2][3] = palette[3][3] = 255;
}

bool decodeDxt35(const uint8_t* data, size_t size, const DdsLevel& level, bool dxt5, Raster3DPixels& out, std::string& error) {
	const uint32_t height = level.height, width = level.width;
	if (!width || !height || width > 16384 || height > 16384) { error = "invalid DDS dimensions"; return false; }
	const uint64_t bw = (width + 3) / 4, bh = (height + 3) / 4;
	if (level.offset > size || bw * bh * 16 != level.size || level.size > size - level.offset) {
		error = "truncated DDS blocks"; return false;
	}
	out.width = width;
	out.height = height;
	out.rgba.assign(static_cast<size_t>(width) * height * 4, 0);
	for (uint64_t by = 0; by < bh; ++by) {
		for (uint64_t bx = 0; bx < bw; ++bx) {
			const uint8_t* block = data + level.offset + (by * bw + bx) * 16;
			uint8_t alpha[16];
			if (!dxt5) {
				for (int i = 0; i < 16; ++i) {
					const unsigned nibble = (block[i / 2] >> ((i & 1) * 4)) & 15;
					alpha[i] = static_cast<uint8_t>(nibble * 17);
				}
			} else {
				uint8_t table[8];
				table[0] = block[0];
				table[1] = block[1];
				if (table[0] > table[1]) {
					for (int i = 1; i < 7; ++i) table[1 + i] = static_cast<uint8_t>(((7 - i) * table[0] + i * table[1]) / 7);
				} else {
					for (int i = 1; i < 5; ++i) table[1 + i] = static_cast<uint8_t>(((5 - i) * table[0] + i * table[1]) / 5);
					table[6] = 0;
					table[7] = 255;
				}
				uint64_t bits = 0;
				for (int i = 0; i < 6; ++i) bits |= static_cast<uint64_t>(block[2 + i]) << (8 * i);
				for (int i = 0; i < 16; ++i) alpha[i] = table[(bits >> (3 * i)) & 7];
			}
			uint8_t palette[4][4];
			dxtColorBlock(block + 8, palette);
			const uint32_t indices = le32(block + 12);
			for (uint32_t y = 0; y < 4 && by * 4 + y < height; ++y) {
				for (uint32_t x = 0; x < 4 && bx * 4 + x < width; ++x) {
					const unsigned i = y * 4 + x;
					const unsigned index = (indices >> (2 * i)) & 3;
					uint8_t* px = &out.rgba[(static_cast<size_t>(by * 4 + y) * width + bx * 4 + x) * 4];
					px[0] = palette[index][0];
					px[1] = palette[index][1];
					px[2] = palette[index][2];
					px[3] = alpha[i];
				}
			}
		}
	}
	return true;
}

bool readFileBytes(const std::filesystem::path& path, std::vector<uint8_t>& bytes, std::string& error) {
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) { error = "cannot open " + path.string(); return false; }
	const auto length = file.tellg();
	if (length < 0 || length > 256 * 1024 * 1024) { error = "invalid file size: " + path.string(); return false; }
	bytes.resize(static_cast<size_t>(length));
	file.seekg(0);
	if (length && !file.read(reinterpret_cast<char*>(bytes.data()), length)) { error = "cannot read " + path.string(); return false; }
	return true;
}

// ---------------------------------------------------------------- clipping

struct ClipV { float p[4]; float a[7]; };

double planeDistance(const ClipV& v, int plane) {
	switch (plane) {
	case 0: return static_cast<double>(v.p[3]) + v.p[0];
	case 1: return static_cast<double>(v.p[3]) - v.p[0];
	case 2: return static_cast<double>(v.p[3]) + v.p[1];
	case 3: return static_cast<double>(v.p[3]) - v.p[1];
	case 4: return static_cast<double>(v.p[2]);
	default: return static_cast<double>(v.p[3]) - v.p[2];
	}
}

// Sutherland-Hodgman against the six D3D planes, same plane order and arithmetic as clipCameraTriangle.
int clipPolygon(const ClipV in[3], ClipV out[9]) {
	ClipV bufA[9], bufB[9];
	int count = 3;
	for (int i = 0; i < 3; ++i) bufA[i] = in[i];
	ClipV* cur = bufA;
	ClipV* next = bufB;
	for (int plane = 0; plane < 6 && count > 0; ++plane) {
		int n = 0;
		ClipV previous = cur[count - 1];
		double previousDistance = planeDistance(previous, plane);
		for (int i = 0; i < count; ++i) {
			const ClipV& current = cur[i];
			const double currentDistance = planeDistance(current, plane);
			const bool previousInside = previousDistance >= 0.0;
			const bool currentInside = currentDistance >= 0.0;
			if (previousInside != currentInside) {
				const double t = previousDistance / (previousDistance - currentDistance);
				ClipV hit;
				for (int k = 0; k < 4; ++k)
					hit.p[k] = static_cast<float>(previous.p[k] + t * (static_cast<double>(current.p[k]) - previous.p[k]));
				for (int k = 0; k < 7; ++k)
					hit.a[k] = static_cast<float>(previous.a[k] + t * (static_cast<double>(current.a[k]) - previous.a[k]));
				next[n++] = hit;
			}
			if (currentInside) next[n++] = current;
			previous = current;
			previousDistance = currentDistance;
		}
		count = n;
		std::swap(cur, next);
	}
	for (int i = 0; i < count; ++i) out[i] = cur[i];
	return count;
}

// ---------------------------------------------------------------- rasterization

struct ScreenV {
	int64_t fx, fy;   // 24.8 fixed point, pixels
	float sz;         // depth 0..1
	float invw;
	float a[7];       // attributes, not yet divided by w
};

struct Target {
	uint8_t* color;
	float* depth;
	uint32_t width, height;
	Raster3DStats* stats;
	int x0, y0, x1, y1; // scissor, [x0,x1) [y0,y1)
};

inline bool topLeft(int64_t ax, int64_t ay, int64_t bx, int64_t by) {
	const int64_t dy = by - ay, dx = bx - ax;
	return dy < 0 || (dy == 0 && dx > 0);
}

struct Shader {
	const Raster3DMaterial* m;
	bool textured, needColor, needLight;
	float tw, th;
};

// Rasterizes one screen-space triangle. v[] is wound so that its fixed-point area is positive.
void rasterize(Target& t, const Shader& s, const ScreenV v[3], int64_t area) {
	const Raster3DMaterial& m = *s.m;
	int64_t minx = std::min({v[0].fx, v[1].fx, v[2].fx}), maxx = std::max({v[0].fx, v[1].fx, v[2].fx});
	int64_t miny = std::min({v[0].fy, v[1].fy, v[2].fy}), maxy = std::max({v[0].fy, v[1].fy, v[2].fy});
	const int px0 = static_cast<int>(std::max<int64_t>(t.x0, minx >> 8)), px1 = static_cast<int>(std::min<int64_t>(t.x1, (maxx >> 8) + 1));
	const int py0 = static_cast<int>(std::max<int64_t>(t.y0, miny >> 8)), py1 = static_cast<int>(std::min<int64_t>(t.y1, (maxy >> 8) + 1));
	if (px0 >= px1 || py0 >= py1) return;
	++t.stats->trianglesRasterized;

	// Edge i is opposite vertex i: E0 = edge(v1,v2), E1 = edge(v2,v0), E2 = edge(v0,v1).
	int64_t stepX[3], stepY[3], bias[3], row[3];
	const int ai[3] = {1, 2, 0}, bi[3] = {2, 0, 1};
	const int64_t startX = static_cast<int64_t>(px0) * 256 + 128, startY = static_cast<int64_t>(py0) * 256 + 128;
	for (int i = 0; i < 3; ++i) {
		const ScreenV& A = v[ai[i]];
		const ScreenV& B = v[bi[i]];
		stepX[i] = -(B.fy - A.fy) * 256;
		stepY[i] = (B.fx - A.fx) * 256;
		bias[i] = topLeft(A.fx, A.fy, B.fx, B.fy) ? 0 : -1;
		row[i] = (B.fx - A.fx) * (startY - A.fy) - (B.fy - A.fy) * (startX - A.fx);
	}
	const float invArea = 1.0f / static_cast<float>(area);
	// Barycentric gradients per pixel (for the texture level of detail).
	float dlx[3], dly[3];
	for (int i = 0; i < 3; ++i) {
		dlx[i] = static_cast<float>(stepX[i]) * invArea;
		dly[i] = static_cast<float>(stepY[i]) * invArea;
	}
	float f[7][3];       // attribute * invw per vertex
	float g[3];          // invw per vertex
	for (int i = 0; i < 3; ++i) {
		g[i] = v[i].invw;
		for (int k = 0; k < 7; ++k) f[k][i] = m.perspectiveCorrect ? v[i].a[k] * v[i].invw : v[i].a[k];
	}
	const float gx = g[0] * dlx[0] + g[1] * dlx[1] + g[2] * dlx[2];
	const float gy = g[0] * dly[0] + g[1] * dly[1] + g[2] * dly[2];
	float fx[2], fy[2];  // d(u*invw)/dx and /dy, then v
	for (int k = 0; k < 2; ++k) {
		fx[k] = f[k][0] * dlx[0] + f[k][1] * dlx[1] + f[k][2] * dlx[2];
		fy[k] = f[k][0] * dly[0] + f[k][1] * dly[1] + f[k][2] * dly[2];
	}

	const bool blend = m.blend == Raster3DBlend::AlphaBlend;
	const bool wrapU = m.addressU == Raster3DAddress::Wrap, wrapV = m.addressV == Raster3DAddress::Wrap;
	const bool perspective = m.perspectiveCorrect;
	const int levelCount = s.textured ? static_cast<int>(m.texture->levels.size()) : 0;

	for (int y = py0; y < py1; ++y) {
		int64_t e0 = row[0], e1 = row[1], e2 = row[2];
		uint8_t* colorRow = t.color + static_cast<size_t>(y) * t.width * 4;
		float* depthRow = t.depth + static_cast<size_t>(y) * t.width;
		for (int x = px0; x < px1; ++x, e0 += stepX[0], e1 += stepX[1], e2 += stepX[2]) {
			if (((e0 + bias[0]) | (e1 + bias[1]) | (e2 + bias[2])) < 0) continue;
			++t.stats->pixelsTested;
			const float l0 = static_cast<float>(e0) * invArea, l1 = static_cast<float>(e1) * invArea,
			            l2 = static_cast<float>(e2) * invArea;
			float z = l0 * v[0].sz + l1 * v[1].sz + l2 * v[2].sz;
			z = z < 0 ? 0 : (z > 1 ? 1 : z);
			if (m.depthTest && z > depthRow[x]) { ++t.stats->pixelsDepthRejected; continue; }

			// Interpolate attributes.
			float u, vv, cr = 1, cg = 1, cb = 1, ca = 1, light = 1;
			float invW = 1;
			if (perspective) {
				invW = l0 * g[0] + l1 * g[1] + l2 * g[2];
				const float w = 1.0f / invW;
				u = (l0 * f[0][0] + l1 * f[0][1] + l2 * f[0][2]) * w;
				vv = (l0 * f[1][0] + l1 * f[1][1] + l2 * f[1][2]) * w;
				if (s.needColor) {
					cr = (l0 * f[2][0] + l1 * f[2][1] + l2 * f[2][2]) * w;
					cg = (l0 * f[3][0] + l1 * f[3][1] + l2 * f[3][2]) * w;
					cb = (l0 * f[4][0] + l1 * f[4][1] + l2 * f[4][2]) * w;
					ca = (l0 * f[5][0] + l1 * f[5][1] + l2 * f[5][2]) * w;
				}
				if (s.needLight) light = (l0 * f[6][0] + l1 * f[6][1] + l2 * f[6][2]) * w;
			} else {
				u = l0 * f[0][0] + l1 * f[0][1] + l2 * f[0][2];
				vv = l0 * f[1][0] + l1 * f[1][1] + l2 * f[1][2];
				if (s.needColor) {
					cr = l0 * f[2][0] + l1 * f[2][1] + l2 * f[2][2];
					cg = l0 * f[3][0] + l1 * f[3][1] + l2 * f[3][2];
					cb = l0 * f[4][0] + l1 * f[4][1] + l2 * f[4][2];
					ca = l0 * f[5][0] + l1 * f[5][1] + l2 * f[5][2];
				}
				if (s.needLight) light = l0 * f[6][0] + l1 * f[6][1] + l2 * f[6][2];
			}

			float texel[4] = {255, 255, 255, 255};
			if (s.textured) {
				const auto& levels = m.texture->levels;
				if (m.filter == Raster3DFilter::Point || levelCount == 1) {
					if (m.filter == Raster3DFilter::Point) fetchPoint(levels[0], u, vv, wrapU, wrapV, texel);
					else fetchBilinear(levels[0], u, vv, wrapU, wrapV, texel);
				} else {
					// Footprint from the analytic screen derivatives of u = U/W and v = V/W.
					float dudx, dudy, dvdx, dvdy;
					if (perspective) {
						const float w = 1.0f / invW;
						dudx = (fx[0] - u * gx) * w; dudy = (fy[0] - u * gy) * w;
						dvdx = (fx[1] - vv * gx) * w; dvdy = (fy[1] - vv * gy) * w;
					} else {
						dudx = fx[0]; dudy = fy[0]; dvdx = fx[1]; dvdy = fy[1];
					}
					const float rx = (dudx * s.tw) * (dudx * s.tw) + (dvdx * s.th) * (dvdx * s.th);
					const float ry = (dudy * s.tw) * (dudy * s.tw) + (dvdy * s.th) * (dvdy * s.th);
					const float rho2 = std::max(rx, ry);
					float lod = rho2 > 1e-12f ? 0.5f * fastLog2(rho2) : 0.0f;
					if (!(lod > 0)) lod = 0;
					const float maxLod = static_cast<float>(levelCount - 1);
					if (lod > maxLod) lod = maxLod;
					if (m.filter == Raster3DFilter::Bilinear) {
						fetchBilinear(levels[static_cast<size_t>(lod + 0.5f)], u, vv, wrapU, wrapV, texel);
					} else {
						const int l0i = static_cast<int>(lod);
						const float frac = lod - static_cast<float>(l0i);
						fetchBilinear(levels[static_cast<size_t>(l0i)], u, vv, wrapU, wrapV, texel);
						if (frac > 0.001f && l0i + 1 < levelCount) {
							float second[4];
							fetchBilinear(levels[static_cast<size_t>(l0i + 1)], u, vv, wrapU, wrapV, second);
							for (int c = 0; c < 4; ++c) texel[c] += (second[c] - texel[c]) * frac;
						}
					}
				}
			}
			const float shade = light;
			float r = texel[0] * m.diffuse[0] * cr * shade;
			float gch = texel[1] * m.diffuse[1] * cg * shade;
			float b = texel[2] * m.diffuse[2] * cb * shade;
			const float alpha = (texel[3] * (1.0f / 255.0f)) * m.diffuse[3] * ca;
			if (m.blend == Raster3DBlend::AlphaTest) {
				if (static_cast<int>(alpha * 255.0f + 0.5f) < m.alphaRef) { ++t.stats->pixelsAlphaRejected; continue; }
			}
			r = r > 255 ? 255 : r;
			gch = gch > 255 ? 255 : gch;
			b = b > 255 ? 255 : b;
			uint8_t* out = colorRow + static_cast<size_t>(x) * 4;
			if (blend) {
				const float a = alpha > 1 ? 1 : (alpha < 0 ? 0 : alpha);
				out[0] = static_cast<uint8_t>(out[0] + (r - out[0]) * a + 0.5f);
				out[1] = static_cast<uint8_t>(out[1] + (gch - out[1]) * a + 0.5f);
				out[2] = static_cast<uint8_t>(out[2] + (b - out[2]) * a + 0.5f);
			} else {
				out[0] = static_cast<uint8_t>(r + 0.5f);
				out[1] = static_cast<uint8_t>(gch + 0.5f);
				out[2] = static_cast<uint8_t>(b + 0.5f);
			}
			out[3] = 255;
			if (m.depthWrite) depthRow[x] = z;
			++t.stats->pixelsWritten;
		}
		row[0] += stepY[0]; row[1] += stepY[1]; row[2] += stepY[2];
	}
}

std::array<float, 16> multiply(const std::array<float, 16>& a, const std::array<float, 16>& b) {
	std::array<float, 16> r{};
	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < 4; ++j) {
			double sum = 0;
			for (int k = 0; k < 4; ++k) sum += static_cast<double>(a[i * 4 + k]) * b[k * 4 + j];
			r[i * 4 + j] = static_cast<float>(sum);
		}
	return r;
}

bool allFinite(const std::array<float, 16>& m) {
	for (float x : m) if (!std::isfinite(x)) return false;
	return true;
}

} // namespace

// ---------------------------------------------------------------- public: textures

bool Raster3DTexture::build(uint32_t width, uint32_t height, const uint8_t* rgba, size_t size,
                            Raster3DTexture& out, std::string& error) {
	out = {};
	if (!width || !height || width > 16384 || height > 16384 || !rgba ||
	    static_cast<uint64_t>(width) * height * 4 != size) {
		error = "invalid RGBA texture";
		return false;
	}
	Level base;
	base.width = width;
	base.height = height;
	base.rgba.assign(rgba, rgba + size);
	for (size_t i = 3; i < size; i += 4) if (rgba[i] != 255) { out.hasAlpha = true; break; }
	out.levels.push_back(std::move(base));
	while (out.levels.back().width > 1 || out.levels.back().height > 1) {
		const Level& src = out.levels.back();
		Level dst;
		dst.width = std::max(src.width / 2, 1u);
		dst.height = std::max(src.height / 2, 1u);
		dst.rgba.resize(static_cast<size_t>(dst.width) * dst.height * 4);
		for (uint32_t y = 0; y < dst.height; ++y) {
			for (uint32_t x = 0; x < dst.width; ++x) {
				const uint32_t sx0 = std::min(x * 2, src.width - 1), sx1 = std::min(x * 2 + 1, src.width - 1);
				const uint32_t sy0 = std::min(y * 2, src.height - 1), sy1 = std::min(y * 2 + 1, src.height - 1);
				for (int c = 0; c < 4; ++c) {
					const unsigned sum = src.rgba[(static_cast<size_t>(sy0) * src.width + sx0) * 4 + c] +
					                     src.rgba[(static_cast<size_t>(sy0) * src.width + sx1) * 4 + c] +
					                     src.rgba[(static_cast<size_t>(sy1) * src.width + sx0) * 4 + c] +
					                     src.rgba[(static_cast<size_t>(sy1) * src.width + sx1) * 4 + c];
					dst.rgba[(static_cast<size_t>(y) * dst.width + x) * 4 + c] = static_cast<uint8_t>((sum + 2) / 4);
				}
			}
		}
		out.levels.push_back(std::move(dst));
	}
	error.clear();
	return true;
}

namespace {
bool decodeDxtLevel(const uint8_t* data, size_t size, DdsImage::Format format,
                    const DdsLevel& level, Raster3DPixels& out, std::string& error) {
 if (format == DdsImage::Format::Dxt1) {
  DdsPixels pixels;
  if (!ddsDecodeDxt1(data, size, level, pixels, error)) return false;
  out.width = pixels.width; out.height = pixels.height; out.rgba = std::move(pixels.rgba);
  return true;
 }
 return decodeDxt35(data, size, level, format == DdsImage::Format::Dxt5, out, error);
}
void appendTextureLevel(Raster3DTexture& texture, Raster3DPixels&& pixels) {
 for (size_t i = 3; i < pixels.rgba.size(); i += 4)
  if (pixels.rgba[i] != 255) { texture.hasAlpha = true; break; }
 texture.levels.push_back({pixels.width, pixels.height, std::move(pixels.rgba)});
}
}

bool raster3dDecodeDxt(const uint8_t* data, size_t size, Raster3DPixels& out, std::string& error) {
 out = {};
 DdsImage image;
 return ddsParseCompressed(data, size, image, error) &&
        decodeDxtLevel(data, size, image.format, image.levels.front(), out, error);
}

bool raster3dDecodeDxtTexture(const uint8_t* data, size_t size, Raster3DTexture& out, std::string& error) {
 out = {};
 DdsImage image;
 if (!ddsParseCompressed(data, size, image, error)) return false;
 Raster3DTexture texture;
 for (const auto& level : image.levels) {
  Raster3DPixels pixels;
  if (!decodeDxtLevel(data, size, image.format, level, pixels, error)) return false;
  appendTextureLevel(texture, std::move(pixels));
 }
 out = std::move(texture);
 return true;
}

bool raster3dLoadTextureFile(const std::filesystem::path& path, Raster3DTexture& out, std::string& error) {
 std::vector<uint8_t> bytes;
 if (!readFileBytes(path, bytes, error)) return false;
 std::string extension = path.extension().string();
 std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
 if (extension == ".dds") {
  if (raster3dDecodeDxtTexture(bytes.data(), bytes.size(), out, error)) return true;
 } else if (extension == ".ptx") {
  PtxImage image{};
  if (ptxParse(bytes.data(), bytes.size(), image, error) && !image.levels.empty()) {
   Raster3DTexture texture;
   for (const auto& level : image.levels) {
    PtxPixels decoded{};
    if (!ptxDecode(bytes.data(), bytes.size(), level, decoded, error)) {
     error = path.string() + ": " + error; return false;
    }
    appendTextureLevel(texture, {decoded.width, decoded.height, std::move(decoded.rgba)});
   }
   out = std::move(texture);
   return true;
  }
 } else error = "unsupported texture extension";
 error = path.string() + ": " + error;
 return false;
}

// ---------------------------------------------------------------- public: viewport, clip

std::array<float, 2> raster3dViewportPoint(float ndcX, float ndcY, const Raster3DViewport& viewport,
                                           uint32_t outputWidth, uint32_t outputHeight) {
	return {(viewport.left + (ndcX + 1.0f) * 0.5f * viewport.width) * static_cast<float>(outputWidth),
	        (viewport.top + (1.0f - ndcY) * 0.5f * viewport.height) * static_cast<float>(outputHeight)};
}

std::vector<Raster3DClipVertex> raster3dClipTriangle(const std::array<Raster3DClipVertex, 3>& triangle) {
	ClipV in[3], out[9];
	for (int i = 0; i < 3; ++i) {
		for (int k = 0; k < 4; ++k) in[i].p[k] = triangle[static_cast<size_t>(i)].position[static_cast<size_t>(k)];
		for (int k = 0; k < 7; ++k) in[i].a[k] = triangle[static_cast<size_t>(i)].attr[static_cast<size_t>(k)];
	}
	const int n = clipPolygon(in, out);
	std::vector<Raster3DClipVertex> result(static_cast<size_t>(n));
	for (int i = 0; i < n; ++i) {
		for (int k = 0; k < 4; ++k) result[static_cast<size_t>(i)].position[static_cast<size_t>(k)] = out[i].p[k];
		for (int k = 0; k < 7; ++k) result[static_cast<size_t>(i)].attr[static_cast<size_t>(k)] = out[i].a[k];
	}
	return result;
}

// ---------------------------------------------------------------- public: Raster3D

bool Raster3D::resize(uint32_t width, uint32_t height, std::string& error) {
	if (!width || !height || width > 8192 || height > 8192) { error = "invalid framebuffer size"; return false; }
	width_ = width;
	height_ = height;
	color_.assign(static_cast<size_t>(width) * height * 4, 0);
	depth_.assign(static_cast<size_t>(width) * height, 1.0f);
	error.clear();
	return true;
}

void Raster3D::clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a, float depth) {
	for (size_t i = 0; i < color_.size(); i += 4) {
		color_[i] = r; color_[i + 1] = g; color_[i + 2] = b; color_[i + 3] = a;
	}
	std::fill(depth_.begin(), depth_.end(), depth);
}

bool Raster3D::draw(const Raster3DDraw& d, std::string& error) {
	if (color_.empty()) { error = "raster3d: framebuffer not sized"; return false; }
	const Raster3DMaterial& m = d.material;
	if (static_cast<uint8_t>(m.blend) > static_cast<uint8_t>(Raster3DBlend::AlphaBlend)) { error = "raster3d: unsupported blend state"; return false; }
	if (static_cast<uint8_t>(m.cull) > static_cast<uint8_t>(Raster3DCull::CounterClockwise)) { error = "raster3d: unsupported cull state"; return false; }
	if (static_cast<uint8_t>(m.filter) > static_cast<uint8_t>(Raster3DFilter::Trilinear)) { error = "raster3d: unsupported filter state"; return false; }
	if (static_cast<uint8_t>(m.addressU) > 1 || static_cast<uint8_t>(m.addressV) > 1) { error = "raster3d: unsupported address mode"; return false; }
	if (!d.positions || !d.vertexCount || !d.indices || !d.indexCount || d.indexCount % 3) { error = "raster3d: invalid draw"; return false; }
	if (m.texture && (m.texture->levels.empty() || !d.uvs)) { error = "raster3d: textured material needs UVs"; return false; }
	for (float c : m.diffuse) if (!std::isfinite(c)) { error = "raster3d: invalid diffuse colour"; return false; }
	if (!allFinite(d.model) || !allFinite(d.view) || !allFinite(d.projection) || !std::isfinite(d.positionScale)) {
		error = "raster3d: invalid matrix"; return false;
	}
	const Raster3DViewport& vp = d.viewport;
	if (!std::isfinite(vp.left) || !std::isfinite(vp.top) || !std::isfinite(vp.width) || !std::isfinite(vp.height) ||
	    vp.width <= 0 || vp.height <= 0) { error = "raster3d: invalid viewport"; return false; }
	for (size_t i = 0; i < d.indexCount; ++i)
		if (d.indices[i] >= d.vertexCount) { error = "raster3d: index out of range"; return false; }

	const bool useLight = d.light.enabled && d.normals;
	const bool useColor = m.useVertexColor && d.colors;
	const auto mvp = multiply(multiply(d.model, d.view), d.projection);
	if (!allFinite(mvp)) { error = "raster3d: invalid matrix"; return false; }

	// Light direction, normalised.
	float lx = d.light.toLight[0], ly = d.light.toLight[1], lz = d.light.toLight[2];
	const float ll = std::sqrt(lx * lx + ly * ly + lz * lz);
	if (useLight) {
		if (!(ll > 0) || !std::isfinite(ll)) { error = "raster3d: invalid light"; return false; }
		lx /= ll; ly /= ll; lz /= ll;
	}

	// Transform every vertex to clip space once.
	std::vector<ClipV> clip(d.vertexCount);
	for (size_t i = 0; i < d.vertexCount; ++i) {
		const float x = d.positions[i * 3] * d.positionScale, y = d.positions[i * 3 + 1] * d.positionScale,
		            z = d.positions[i * 3 + 2] * d.positionScale;
		if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) { error = "raster3d: non-finite vertex"; return false; }
		ClipV& c = clip[i];
		for (int col = 0; col < 4; ++col)
			c.p[col] = x * mvp[0 + col] + y * mvp[4 + col] + z * mvp[8 + col] + mvp[12 + col];
		c.a[0] = d.uvs ? d.uvs[i * 2] : 0.0f;
		c.a[1] = d.uvs ? d.uvs[i * 2 + 1] : 0.0f;
		if (useColor) {
			const uint8_t* p = d.colors + i * 4;
			c.a[2] = p[2] * (1.0f / 255.0f);
			c.a[3] = p[1] * (1.0f / 255.0f);
			c.a[4] = p[0] * (1.0f / 255.0f);
			c.a[5] = p[3] * (1.0f / 255.0f);
		} else {
			c.a[2] = c.a[3] = c.a[4] = c.a[5] = 1.0f;
		}
		c.a[6] = 1.0f;
		if (useLight) {
			const float nx = d.normals[i * 3], ny = d.normals[i * 3 + 1], nz = d.normals[i * 3 + 2];
			const float wx = nx * d.model[0] + ny * d.model[4] + nz * d.model[8];
			const float wy = nx * d.model[1] + ny * d.model[5] + nz * d.model[9];
			const float wz = nx * d.model[2] + ny * d.model[6] + nz * d.model[10];
			const float len = std::sqrt(wx * wx + wy * wy + wz * wz);
			const float dot = len > 0 ? (wx * lx + wy * ly + wz * lz) / len : 0.0f;
			c.a[6] = d.light.ambient + d.light.diffuse * (dot > 0 ? dot : 0.0f);
		}
		for (int k = 0; k < 4; ++k) if (!std::isfinite(c.p[k])) { error = "raster3d: non-finite clip coordinate"; return false; }
		for (int k = 0; k < 7; ++k) if (!std::isfinite(c.a[k])) { error = "raster3d: non-finite vertex attribute"; return false; }
	}

	Target target{color_.data(), depth_.data(), width_, height_, &stats_, 0, 0, static_cast<int>(width_), static_cast<int>(height_)};
	{
		// Scissor to the viewport rectangle.
		const auto lo = raster3dViewportPoint(-1, 1, vp, width_, height_);
		const auto hi = raster3dViewportPoint(1, -1, vp, width_, height_);
		target.x0 = std::max(0, static_cast<int>(std::floor(lo[0])));
		target.y0 = std::max(0, static_cast<int>(std::floor(lo[1])));
		target.x1 = std::min<int>(static_cast<int>(width_), static_cast<int>(std::ceil(hi[0])));
		target.y1 = std::min<int>(static_cast<int>(height_), static_cast<int>(std::ceil(hi[1])));
	}
	Shader shader{&m, m.texture != nullptr, useColor, useLight, 0, 0};
	if (m.texture) {
		shader.tw = static_cast<float>(m.texture->levels[0].width);
		shader.th = static_cast<float>(m.texture->levels[0].height);
	}

	const auto project = [&](const ClipV& c, ScreenV& s) -> bool {
		if (!(c.p[3] > 0)) return false;
		const float invw = 1.0f / c.p[3];
		const float nx = c.p[0] * invw, ny = c.p[1] * invw;
		const auto pt = raster3dViewportPoint(nx, ny, vp, width_, height_);
		const double sx = pt[0] * 256.0, sy = pt[1] * 256.0;
		if (!(std::fabs(sx) < 1e9) || !(std::fabs(sy) < 1e9)) return false;
		s.fx = static_cast<int64_t>(std::llround(sx));
		s.fy = static_cast<int64_t>(std::llround(sy));
		s.sz = c.p[2] * invw;
		s.invw = invw;
		for (int k = 0; k < 7; ++k) s.a[k] = c.a[k];
		return true;
	};

	const auto submit = [&](const ClipV& c0, const ClipV& c1, const ClipV& c2) {
		ScreenV s[3];
		if (!project(c0, s[0]) || !project(c1, s[1]) || !project(c2, s[2])) { ++stats_.trianglesDegenerate; return; }
		const int64_t area = (s[1].fx - s[0].fx) * (s[2].fy - s[0].fy) - (s[2].fx - s[0].fx) * (s[1].fy - s[0].fy);
		if (area == 0) { ++stats_.trianglesDegenerate; return; }
		// With y pointing down, positive area is clockwise on screen.
		const bool clockwise = area > 0;
		if ((m.cull == Raster3DCull::Clockwise && clockwise) || (m.cull == Raster3DCull::CounterClockwise && !clockwise)) {
			++stats_.trianglesCulled;
			return;
		}
		if (area < 0) { std::swap(s[1], s[2]); }
		rasterize(target, shader, s, area < 0 ? -area : area);
	};

	for (size_t i = 0; i < d.indexCount; i += 3) {
		++stats_.trianglesSubmitted;
		const ClipV& a = clip[d.indices[i]];
		const ClipV& b = clip[d.indices[i + 1]];
		const ClipV& c = clip[d.indices[i + 2]];
		bool inside = true;
		bool rejected = false;
		for (int plane = 0; plane < 6; ++plane) {
			const bool ia = planeDistance(a, plane) >= 0, ib = planeDistance(b, plane) >= 0, ic = planeDistance(c, plane) >= 0;
			if (!ia && !ib && !ic) { rejected = true; break; }
			if (!(ia && ib && ic)) inside = false;
		}
		if (rejected) { ++stats_.trianglesRejectedOutside; continue; }
		if (inside) { submit(a, b, c); continue; }
		++stats_.trianglesClipped;
		const ClipV in[3] = {a, b, c};
		ClipV poly[9];
		const int n = clipPolygon(in, poly);
		for (int j = 1; j + 1 < n; ++j) submit(poly[0], poly[j], poly[j + 1]);
	}
	error.clear();
	return true;
}

bool Raster3D::blit(Render2D& renderer, std::string& error) {
	if (color_.empty()) { error = "raster3d: framebuffer not sized"; return false; }
	if (uploadRenderer_ != &renderer || uploadWidth_ != width_ || uploadHeight_ != height_) {
		if (uploadRenderer_ == &renderer) {
			std::string ignored;
			renderer.releaseTexture(uploadTexture_, ignored);
		}
		const auto id = renderer.texture(width_, height_, color_.data(), color_.size(), error);
		if (!id) return false;
		uploadRenderer_ = &renderer;
		uploadTexture_ = *id;
		uploadWidth_ = width_;
		uploadHeight_ = height_;
	} else if (!renderer.updateTexture(uploadTexture_, width_, height_, color_.data(), color_.size(), error)) {
		return false;
	}
	int outWidth = 0, outHeight = 0;
	if (!renderer.outputSize(outWidth, outHeight, error)) return false;
	return renderer.draw(uploadTexture_, {0, 0, static_cast<float>(outWidth), static_cast<float>(outHeight)}, error);
}

bool Raster3D::saveBmp(const std::filesystem::path& path, std::string& error) const {
	if (color_.empty()) { error = "raster3d: framebuffer not sized"; return false; }
	const uint32_t rowBytes = (width_ * 3 + 3) & ~3u;
	const uint32_t imageSize = rowBytes * height_;
	std::vector<uint8_t> file(54 + imageSize, 0);
	const auto put32 = [&](size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) file[at + static_cast<size_t>(i)] = static_cast<uint8_t>(v >> (8 * i)); };
	file[0] = 'B'; file[1] = 'M';
	put32(2, 54 + imageSize);
	put32(10, 54);
	put32(14, 40);
	put32(18, width_);
	put32(22, height_); // positive: bottom-up rows
	file[26] = 1;
	file[28] = 24;
	put32(34, imageSize);
	put32(38, 2835);
	put32(42, 2835);
	for (uint32_t y = 0; y < height_; ++y) {
		uint8_t* out = &file[54 + static_cast<size_t>(height_ - 1 - y) * rowBytes];
		const uint8_t* in = &color_[static_cast<size_t>(y) * width_ * 4];
		for (uint32_t x = 0; x < width_; ++x) {
			out[x * 3 + 0] = in[x * 4 + 2];
			out[x * 3 + 1] = in[x * 4 + 1];
			out[x * 3 + 2] = in[x * 4 + 0];
		}
	}
	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	if (!stream || !stream.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()))) {
		error = "cannot write " + path.string();
		return false;
	}
	error.clear();
	return true;
}

// ---------------------------------------------------------------- public: SCX material mapping

namespace {

// Stock material types the 004befc0 switch lists, grouped by whether this host draws them.
// Names for 0-6 and 0x30-0x33 come from the CMaterial_* strings registered in 004bf800.
bool stockSwitchHasType(uint32_t type) {
	switch (type) {
	case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 0xa: case 0xb:
	case 0xc: case 0xd: case 0xe: case 0xf: case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15:
	case 0x16: case 0x17: case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e:
	case 0x1f: case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
	case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x37:
		return true;
	default:
		return false;
	}
}

// Types whose base layer this host draws, with extra layers not reproduced.
bool approximatedType(uint32_t type) {
	switch (type) {
	case 1: case 2: case 3: case 4: case 5: case 6:        // MixTA, MixVCA, ColorMixTex, TexSpecular, MixTASpecular, MixVCASpecular
	case 8: case 0x10: case 0x20:
	case 0xc: case 0x14: case 0x24:
	case 0xd: case 0x25:
	case 0xf: case 0x17: case 0x27:
	case 0x15:
	case 0x31: case 0x32: case 0x33:                       // ColorSpecular, ColorSpheremap, ColorSpecularSpheremap
		return true;
	default:
		return false;
	}
}

} // namespace

bool raster3dScxMaterial(const ScxMaterial& material, const Raster3DScxOptions& options,
                         Raster3DScxMaterial& out, std::string& error) {
	out = {};
	const uint32_t type = material.flags;
	const uint32_t function = material.functionWord;
	const auto hexString = [](uint32_t value) {
		char b[16];
		std::snprintf(b, sizeof b, "%x", value);
		return std::string(b);
	};
	const std::string label = "material '" + material.name + "' (type 0x" + hexString(type) + ")";

	// Material type. 0 (Tex) and 0x30 (Color) are drawn as is; some others as their base layer only.
	if (type == 0 || type == 0x30) {
		// exact base layer
	} else if (!stockSwitchHasType(type)) {
		// The default case of 004befc0 falls back to the Tex material (FUN_004c4a40).
		out.notes.push_back("type 0x" + hexString(type) + " is not in the stock switch; stock falls back to Tex");
	} else if (approximatedType(type)) {
		if (!options.allowApproximations) {
			error = label + ": stock type draws extra layers (specular, environment or texture mix) that are not reproduced";
			return false;
		}
		out.notes.push_back("type 0x" + hexString(type) + ": only the base layer is drawn; extra layers are not reproduced");
	} else {
		error = label + ": unsupported stock material type";
		return false;
	}

	// Function word bits.
	constexpr uint32_t kBlend = 0x40000000u, kVertexColor = 0x80000000u, kAlphaTest = 0x20000000u;
	const uint32_t unknown = function & ~(kBlend | kVertexColor | kAlphaTest);
	if (unknown && !options.ignoreUnknownFunctionBits) {
		char b[32];
		std::snprintf(b, sizeof b, "0x%08x", unknown);
		error = label + ": unsupported function word bits " + b;
		return false;
	}
	if (unknown) out.notes.push_back("ignored unknown function word bits");
	if ((function & kBlend) && (function & kAlphaTest)) {
		error = label + ": function word asks for both alpha blend and alpha test";
		return false;
	}

	Raster3DMaterial& state = out.state;
	if (function & kBlend) {
		state.blend = Raster3DBlend::AlphaBlend;
		state.depthWrite = false;
	} else if (function & kAlphaTest) {
		state.blend = Raster3DBlend::AlphaTest;
		state.alphaRef = 0x38; // 004e5890 defaults the entry word at +0xc to 0x38 when bit 0x20000000 is set
		for (const auto& property : material.properties) {
			if (property.type == 0x05000000u) {
				if (property.raw > 255) { error = label + ": alpha reference above 255"; return false; }
				state.alphaRef = static_cast<uint8_t>(property.raw);
			}
		}
	}
	if (function & kVertexColor) {
		state.useVertexColor = true;
		out.notes.push_back("function word 0x80000000: vertex colours multiply the texture; the emissive stream is not drawn");
	}

	// Base layer: map slot 0, otherwise the first of slot 1.
	const ScxTextureMap* base = nullptr;
	for (const auto& map : material.maps) if (map.slot == 0) { base = &map; break; }
	if (!base) for (const auto& map : material.maps) if (map.slot == 1) { base = &map; break; }
	if (base) {
		out.baseTextureIndex = base->textureIndex;
		out.baseSlot = base->slot;
		// Address mode stays Wrap. The stock chooses clamp or wrap per texture object (flag byte at +0x74,
		// 004c4b90), not from the SCX map, so the SCX tiling word is not used.
		// Textured materials: the stock applies the diffuse colour through D3D lighting, which is not
		// reproduced, so only the alpha is kept.
		state.diffuse = {1, 1, 1, material.diffuse[3]};
		if (material.diffuse[0] != 1 || material.diffuse[1] != 1 || material.diffuse[2] != 1)
			out.notes.push_back("diffuse colour tint not applied to a textured material");
	} else {
		state.diffuse = material.diffuse;
	}
	if (state.blend != Raster3DBlend::AlphaBlend && !base) state.diffuse[3] = 1;
	for (const auto& map : material.maps) {
		if (base && map.slot == base->slot) continue;
		out.notes.push_back("map in slot " + std::to_string(map.slot) + " (channel " + std::to_string(static_cast<int32_t>(map.channel)) +
		                    ") is not drawn");
	}
	error.clear();
	return true;
}
