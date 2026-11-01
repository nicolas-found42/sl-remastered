#include "render2d.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <limits>

std::unique_ptr<Render2D> Render2D::forWindow(SDL_Window* window, std::string& error) {
	if (!window) { error = "missing window"; return nullptr; }
	SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
	if (!renderer) { error = SDL_GetError(); return nullptr; }
	error.clear();
	return std::unique_ptr<Render2D>(new Render2D(renderer));
}

std::unique_ptr<Render2D> Render2D::forSurface(SDL_Surface* surface, std::string& error) {
	if (!surface) { error = "missing surface"; return nullptr; }
	SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
	if (!renderer) { error = SDL_GetError(); return nullptr; }
	error.clear();
	return std::unique_ptr<Render2D>(new Render2D(renderer));
}

Render2D::~Render2D() {
	for (SDL_Texture* texture : textures_) SDL_DestroyTexture(texture);
	SDL_DestroyRenderer(renderer_);
}

std::optional<size_t> Render2D::texture(uint32_t width, uint32_t height, const uint8_t* rgba, size_t size, std::string& error) {
	if (!width || !height || width > 16384 || height > 16384 || !rgba ||
		static_cast<uint64_t>(width) * height * 4 != size) {
		error = "invalid RGBA texture";
		return std::nullopt;
	}
	SDL_Texture* result = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
		static_cast<int>(width), static_cast<int>(height));
	if (!result) { error = SDL_GetError(); return std::nullopt; }
	if (!SDL_UpdateTexture(result, nullptr, rgba, static_cast<int>(width * 4)) ||
		!SDL_SetTextureBlendMode(result, SDL_BLENDMODE_BLEND) || !SDL_SetTextureScaleMode(result, SDL_SCALEMODE_NEAREST)) {
		error = SDL_GetError();
		SDL_DestroyTexture(result);
		return std::nullopt;
	}
	textures_.push_back(result);
	error.clear();
	return textures_.size() - 1;
}

bool Render2D::updateTexture(size_t textureId, uint32_t width, uint32_t height,
                             const uint8_t* rgba, size_t size, std::string& error) {
	if (textureId >= textures_.size() || !textures_[textureId] || !width || !height || width > 16384 ||
	    height > 16384 || !rgba || static_cast<uint64_t>(width) * height * 4 != size) {
		error = "invalid RGBA texture update"; return false;
	}
	float actualWidth, actualHeight;
	if (!SDL_GetTextureSize(textures_[textureId], &actualWidth, &actualHeight)) { error = SDL_GetError(); return false; }
	if (actualWidth != static_cast<float>(width) || actualHeight != static_cast<float>(height)) {
		error = "texture update changes dimensions"; return false;
	}
	if (!SDL_UpdateTexture(textures_[textureId], nullptr, rgba, static_cast<int>(width * 4))) { error = SDL_GetError(); return false; }
	error.clear(); return true;
}

bool Render2D::releaseTexture(size_t textureId, std::string& error) {
	if (textureId >= textures_.size() || !textures_[textureId]) { error = "invalid texture"; return false; }
	const bool flushed = SDL_FlushRenderer(renderer_);
	if (!flushed) error = SDL_GetError();
	SDL_DestroyTexture(textures_[textureId]);
	textures_[textureId] = nullptr;
	return flushed;
}

bool Render2D::clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a, std::string& error) {
	if (!SDL_SetRenderDrawColor(renderer_, r, g, b, a) || !SDL_RenderClear(renderer_)) {
		error = SDL_GetError(); return false;
	}
	error.clear(); return true;
}

bool Render2D::draw(size_t textureId, RenderRect rect, std::string& error) {
	if (textureId >= textures_.size() || !textures_[textureId] || !std::isfinite(rect.x) || !std::isfinite(rect.y) ||
		!std::isfinite(rect.width) || !std::isfinite(rect.height) || rect.width <= 0 || rect.height <= 0) {
		error = "invalid texture or rectangle"; return false;
	}
	const SDL_FRect target{rect.x, rect.y, rect.width, rect.height};
	if (!SDL_RenderTexture(renderer_, textures_[textureId], nullptr, &target)) { error = SDL_GetError(); return false; }
	error.clear(); return true;
}

bool Render2D::drawMesh(size_t textureId, const std::vector<RenderVertex>& vertices,
                        const std::vector<uint32_t>& indices, std::string& error) {
	if (textureId >= textures_.size() || !textures_[textureId] || vertices.empty() || indices.empty() || indices.size() % 3 ||
	    vertices.size() > static_cast<size_t>(std::numeric_limits<int>::max()) ||
	    indices.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
		error = "invalid mesh"; return false;
	}
	std::vector<SDL_Vertex> converted;
	converted.reserve(vertices.size());
	for (const auto& v : vertices) {
		if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.u) || !std::isfinite(v.v)) {
			error = "invalid mesh vertex"; return false;
		}
		converted.push_back({{v.x, v.y}, {((v.color >> 16) & 255) / 255.0f, ((v.color >> 8) & 255) / 255.0f,
		                                      (v.color & 255) / 255.0f, (v.color >> 24) / 255.0f}, {v.u, v.v}});
	}
	std::vector<int> convertedIndices;
	convertedIndices.reserve(indices.size());
	for (uint32_t index : indices) {
		if (index >= vertices.size()) { error = "invalid mesh index"; return false; }
		convertedIndices.push_back(static_cast<int>(index));
	}
	if (!SDL_RenderGeometry(renderer_, textures_[textureId], converted.data(), static_cast<int>(converted.size()),
	                        convertedIndices.data(), static_cast<int>(convertedIndices.size()))) {
		error = SDL_GetError(); return false;
	}
	error.clear(); return true;
}

bool Render2D::drawMeshAdditive(size_t textureId, const std::vector<RenderVertex>& vertices,
                                const std::vector<uint32_t>& indices, std::string& error) {
	if (textureId >= textures_.size() || !textures_[textureId]) { error = "invalid texture"; return false; }
	SDL_BlendMode previous;
	if (!SDL_GetTextureBlendMode(textures_[textureId], &previous) ||
	    !SDL_SetTextureBlendMode(textures_[textureId], SDL_BLENDMODE_ADD)) {
		error = SDL_GetError(); return false;
	}
	const bool drawn = drawMesh(textureId, vertices, indices, error);
	if (!SDL_SetTextureBlendMode(textures_[textureId], previous)) {
		if (drawn) error = SDL_GetError();
		return false;
	}
	return drawn;
}

bool Render2D::present(std::string& error) {
	if (!SDL_RenderPresent(renderer_)) { error = SDL_GetError(); return false; }
	error.clear(); return true;
}

bool Render2D::saveFrame(const std::string& path, std::string& error) {
	SDL_Surface* frame = SDL_RenderReadPixels(renderer_, nullptr);
	if (!frame) { error = SDL_GetError(); return false; }
	const bool saved = SDL_SaveBMP(frame, path.c_str());
	if (!saved) error = SDL_GetError();
	SDL_DestroySurface(frame);
	if (saved) error.clear();
	return saved;
}

bool Render2D::outputSize(int& width, int& height, std::string& error) const {
  SDL_RendererLogicalPresentation mode;
  if (!SDL_GetRenderLogicalPresentation(renderer_, &width, &height, &mode)) { error = SDL_GetError(); return false; }
  // SDL's current output size is the physical presentation rectangle, not
  // the logical content dimensions (e.g. 1265x949 for 800x600 fullscreen).
  if (mode == SDL_LOGICAL_PRESENTATION_DISABLED && !SDL_GetRenderOutputSize(renderer_, &width, &height)) { error = SDL_GetError(); return false; }
  error.clear(); return true;
}

uint32_t Render2D::maxTextureSize() const {
	// texture() rejects sides above 16384, so that is the host's limit even when SDL (the software
	// renderer) reports none.
	constexpr uint32_t hostLimit = 16384;
	const SDL_PropertiesID properties = SDL_GetRendererProperties(renderer_);
	const Sint64 reported = properties ? SDL_GetNumberProperty(properties, SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0) : 0;
	if (reported <= 0) return hostLimit;
	return static_cast<uint32_t>(std::min<Sint64>(reported, hostLimit));
}

bool Render2D::presentation(int width, int height, std::string& error) {
  if (!SDL_SetRenderLogicalPresentation(renderer_, width, height, SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
    error = SDL_GetError(); return false;
  }
  error.clear(); return true;
}
bool Render2D::windowToRender(float x, float y, float& renderX, float& renderY) const {
  return SDL_RenderCoordinatesFromWindow(renderer_, x, y, &renderX, &renderY);
}
bool Render2D::renderToWindow(float x, float y, float& windowX, float& windowY) const {
  return SDL_RenderCoordinatesToWindow(renderer_, x, y, &windowX, &windowY);
}
