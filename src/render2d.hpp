#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct SDL_Renderer;
struct SDL_Surface;
struct SDL_Texture;
struct SDL_Window;

struct RenderRect { float x, y, width, height; };
struct RenderVertex { float x, y, u, v; uint32_t color = 0xffffffffu; };

// Use on the main thread. The window or surface must outlive the renderer.
class Render2D {
public:
	static std::unique_ptr<Render2D> forWindow(SDL_Window* window, std::string& error);
	static std::unique_ptr<Render2D> forSurface(SDL_Surface* surface, std::string& error);
  // Owns an accelerated offscreen target. The optional coordinate window is
  // never presented; it supplies the coordinate space of synthetic input.
  static std::unique_ptr<Render2D> forOffscreen(int width, int height, SDL_Window* coordinateWindow, std::string& error);
	~Render2D();
	Render2D(const Render2D&) = delete;
	Render2D& operator=(const Render2D&) = delete;

	std::optional<size_t> texture(uint32_t width, uint32_t height, const uint8_t* rgba, size_t size, std::string& error);
	bool releaseTexture(size_t textureId, std::string& error);
	bool updateTexture(size_t textureId, uint32_t width, uint32_t height,
	                   const uint8_t* rgba, size_t size, std::string& error);
	bool clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a, std::string& error);
	bool draw(size_t textureId, RenderRect rect, std::string& error);
	bool drawMesh(size_t textureId, const std::vector<RenderVertex>& vertices,
	              const std::vector<uint32_t>& indices, std::string& error);
	bool drawMeshAdditive(size_t textureId, const std::vector<RenderVertex>& vertices,
	                      const std::vector<uint32_t>& indices, std::string& error);
	bool present(std::string& error);
	bool saveFrame(const std::string& path, std::string& error);
	bool outputSize(int& width, int& height, std::string& error) const;
	bool presentation(int width, int height, std::string& error);
	bool windowToRender(float x, float y, float& renderX, float& renderY) const;
	bool renderToWindow(float x, float y, float& windowX, float& windowY) const;
	// Largest texture side this host accepts: SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER capped at 16384.
	uint32_t maxTextureSize() const;

private:
	explicit Render2D(SDL_Renderer* renderer) : renderer_(renderer) {}
	SDL_Renderer* renderer_;
  SDL_Texture* backing_ = nullptr;
  SDL_Window* coordinateWindow_ = nullptr;
	std::vector<SDL_Texture*> textures_;
};
