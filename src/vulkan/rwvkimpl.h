
namespace maple
{
	class TextureDepth;
	class Texture2D;
	class DescriptorSet;
	class StorageBuffer;
	class BatchTask;
	class AccelerationStructure;
	class IndexBuffer;
	class VertexBuffer;
}
#include <string>
namespace rw
{
	namespace vulkan
	{
#ifdef RW_VULKAN

		extern uint32 im2DVbo, im2DIbo;
		void          openIm2D(uint32_t width, uint32_t height);
		void          closeIm2D(void);
		void          im2DRenderLine(void* vertices, int32 numVertices,
			int32 vert1, int32 vert2);
		void          im2DRenderTriangle(void* vertices, int32 numVertices,
			int32 vert1, int32 vert2, int32 vert3);
		void          im2DRenderPrimitive(PrimitiveType primType,
			void* vertices, int32 numVertices);
		void          im2DRenderIndexedPrimitive(PrimitiveType primType,
			void* vertices, int32 numVertices, void* indices, int32 numIndices);

		void openIm3D(void);
		void closeIm3D(void);
		void im3DTransform(void* vertices, int32 numVertices, Matrix* world, uint32 flags);
		void im3DRenderPrimitive(PrimitiveType primType);
		void im3DRenderIndexedPrimitive(PrimitiveType primType, void* indices, int32 numIndices);
		void im3DEnd(void);
		void imFlush();

// DisplayMode and VkGlobals are defined conditionally based on windowing system
#ifdef LIBRW_SDL2
		struct DisplayMode
		{
			SDL_DisplayMode mode;
			int32       depth;
			uint32      flags;
		};

		struct VkGlobals
		{
			SDL_Window** pWindow;
			SDL_Window*  window;

			int numDisplays;
			int currentDisplay;

			DisplayMode* modes;
			int          numModes;
			int          currentMode;
			int          presentWidth, presentHeight;
			int          presentOffX, presentOffY;

			// for opening the window
			int         winWidth, winHeight;
			std::string winTitle;
			uint32      numSamples;

			std::shared_ptr<maple::Texture> currentDepth;
			std::shared_ptr<maple::Texture> colorTarget;
#ifdef ENABLE_GBUFFER
			std::shared_ptr<maple::Texture> normalTarget;
			std::shared_ptr<maple::Texture> gbuffer2;
			std::shared_ptr<maple::Texture> gbuffer3;
			std::shared_ptr<maple::Texture> gbuffer4;
#endif
		};

#elif defined(LIBRW_SDL3)
		struct DisplayMode
		{
			SDL_DisplayMode mode;
			int32       depth;
			uint32      flags;
		};

		struct VkGlobals
		{
			SDL_Window** pWindow;
			SDL_Window*  window;

			int numDisplays;
			int currentDisplay;

			DisplayMode* modes;
			int          numModes;
			int          currentMode;
			int          presentWidth, presentHeight;
			int          presentOffX, presentOffY;

			// for opening the window
			int         winWidth, winHeight;
			std::string winTitle;
			uint32      numSamples;

			std::shared_ptr<maple::Texture> currentDepth;
			std::shared_ptr<maple::Texture> colorTarget;
#ifdef ENABLE_GBUFFER
			std::shared_ptr<maple::Texture> normalTarget;
			std::shared_ptr<maple::Texture> gbuffer2;
			std::shared_ptr<maple::Texture> gbuffer3;
			std::shared_ptr<maple::Texture> gbuffer4;
#endif
		};

#elif defined(LIBRW_GLFW)
		struct DisplayMode
		{
			GLFWvidmode mode;
			int32       depth;
			uint32      flags;
		};

		struct VkGlobals
		{
			GLFWwindow** pWindow;
			GLFWwindow* window;

			GLFWmonitor* monitor;
			int          numMonitors;
			int          currentMonitor;

			DisplayMode* modes;
			int          numModes;
			int          currentMode;
			int          presentWidth, presentHeight;
			int          presentOffX, presentOffY;

			// for opening the window
			int         winWidth, winHeight;
			std::string winTitle;
			uint32      numSamples;

			std::shared_ptr<maple::Texture> currentDepth;
			std::shared_ptr<maple::Texture> colorTarget;
#ifdef ENABLE_GBUFFER
			std::shared_ptr<maple::Texture> normalTarget;
			std::shared_ptr<maple::Texture> gbuffer2;
			std::shared_ptr<maple::Texture> gbuffer3;
			std::shared_ptr<maple::Texture> gbuffer4;
#endif
		};
#endif // LIBRW_GLFW

		extern VkGlobals vkGlobals;
		extern std::shared_ptr <maple::DescriptorSet> commonSet;
		extern std::shared_ptr <maple::StorageBuffer> ssboLighting[3];
#endif

		Raster* rasterCreate(Raster* raster);
		uint8* rasterLock(Raster*, int32 level, int32 lockMode);
		void    rasterUnlock(Raster*, int32);
		int32   rasterNumLevels(Raster*);
		bool32  imageFindRasterFormat(Image* img, int32 type,
			int32* width, int32* height, int32* depth, int32* format);
		bool32  rasterFromImage(Raster* raster, Image* image);
		Image* rasterToImage(Raster* raster);

		void rasterSetTextureName(Raster *raster, const char * name);

	}        // namespace vulkan
}        // namespace rw
