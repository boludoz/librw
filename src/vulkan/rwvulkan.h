#ifdef RW_VULKAN
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#endif

namespace rw {

#ifdef RW_VULKAN
struct EngineOpenParams
{
	SDL_Window **window;
	bool32 fullscreen;
	int width, height;
	const char *windowtitle;
};
#endif

namespace vulkan {

void registerPlatformPlugins(void);

extern Device renderdevice;

#ifdef RW_VULKAN
struct Context
{
	SDL_Window *window;
	VkInstance instance;
	VkSurfaceKHR surface;
	VkPhysicalDevice physicalDevice;
	VkDevice device;
	bool32 rayQuerySupported;
	bool32 rayQueryEnabled;
	bool32 accelerationStructureSupported;
	bool32 bufferDeviceAddressSupported;
	bool32 deferredHostOperationsSupported;
	PFN_vkCreateAccelerationStructureKHR createAccelerationStructureKHR;
	PFN_vkDestroyAccelerationStructureKHR destroyAccelerationStructureKHR;
	PFN_vkGetAccelerationStructureBuildSizesKHR getAccelerationStructureBuildSizesKHR;
	PFN_vkCmdBuildAccelerationStructuresKHR cmdBuildAccelerationStructuresKHR;
	PFN_vkGetAccelerationStructureDeviceAddressKHR getAccelerationStructureDeviceAddressKHR;
	PFN_vkGetBufferDeviceAddressKHR getBufferDeviceAddressKHR;
	uint32 graphicsQueueFamily;
	uint32 presentQueueFamily;
	VkQueue graphicsQueue;
	VkQueue presentQueue;
	VkSwapchainKHR swapchain;
	VkFormat swapchainFormat;
	VkExtent2D swapchainExtent;
	uint32 numSwapchainImages;
	VkImage *swapchainImages;
	VkImageView *swapchainImageViews;
	VkFormat depthFormat;
	VkImage depthImage;
	VkDeviceMemory depthImageMemory;
	VkImageView depthImageView;
	VkRenderPass renderPass;
	VkRenderPass loadRenderPass;
	VkFramebuffer *framebuffers;
	VkFramebuffer *loadFramebuffers;
	VkCommandPool commandPool;
	VkCommandBuffer *commandBuffers;
	VkFence *imagesInFlight;
	VkSemaphore imageAvailable[3];
	VkSemaphore renderFinished[3];
	VkFence inFlight[3];
	uint32 currentFrame;
	uint32 currentImage;
	bool32 frameStarted;
	VkPipelineCache pipelineCache;
	VkDebugUtilsMessengerEXT debugMessenger;
};

Context *getContext(void);
VkInstance getInstance(void);
VkDevice getDevice(void);
VkPhysicalDevice getPhysicalDevice(void);
VkSurfaceKHR getSurface(void);
VkQueue getGraphicsQueue(void);
VkQueue getPresentQueue(void);
uint32 getGraphicsQueueFamily(void);
uint32 getPresentQueueFamily(void);
bool32 isRayQueryEnabled(void);
bool32 isRayTracingEnabled(void);
void setRayTracingEnabled(bool32 enabled);
int32 getPresentModePreference(void);
void setPresentModePreference(int32 preference);
// Android tears the native window down when the app leaves the foreground;
// the platform layer has to tell us so the surface can be rebuilt on return.
void notifySurfaceLost(void);
void notifyAppBackground(bool32 background);

// A range of one of the backend's large device memory blocks. Individual
// geometries and textures are carved out of these instead of each getting
// its own vkAllocateMemory, which is slow and limited in count on mobile.
struct VulkanAllocation
{
	void *block;
	uint64 offset;
	uint64 size;
	uint32 generation;
};

struct Im3DVertex
{
	V3d     position;
	V3d     normal;
	uint8   r, g, b, a;
	float32 u, v;

	void setX(float32 x) { this->position.x = x; }
	void setY(float32 y) { this->position.y = y; }
	void setZ(float32 z) { this->position.z = z; }
	void setNormalX(float32 x) { this->normal.x = x; }
	void setNormalY(float32 y) { this->normal.y = y; }
	void setNormalZ(float32 z) { this->normal.z = z; }
	void setColor(uint8 r, uint8 g, uint8 b, uint8 a) {
		this->r = r; this->g = g; this->b = b; this->a = a; }
	void setU(float32 u) { this->u = u; }
	void setV(float32 v) { this->v = v; }

	float getX(void) { return this->position.x; }
	float getY(void) { return this->position.y; }
	float getZ(void) { return this->position.z; }
	float getNormalX(void) { return this->normal.x; }
	float getNormalY(void) { return this->normal.y; }
	float getNormalZ(void) { return this->normal.z; }
	RGBA getColor(void) { return makeRGBA(this->r, this->g, this->b, this->a); }
	float getU(void) { return this->u; }
	float getV(void) { return this->v; }
};

struct Im2DVertex
{
	float32 x, y, z, w;
	uint8   r, g, b, a;
	float32 u, v;

	void setScreenX(float32 x) { this->x = x; }
	void setScreenY(float32 y) { this->y = y; }
	void setScreenZ(float32 z) { this->z = z; }
	void setCameraZ(float32 z) { this->w = z; }
	void setRecipCameraZ(float32 recipz) { this->w = 1.0f/recipz; }
	void setColor(uint8 r, uint8 g, uint8 b, uint8 a) {
		this->r = r; this->g = g; this->b = b; this->a = a; }
	void setU(float32 u, float recipz) { (void)recipz; this->u = u; }
	void setV(float32 v, float recipz) { (void)recipz; this->v = v; }

	float getScreenX(void) { return this->x; }
	float getScreenY(void) { return this->y; }
	float getScreenZ(void) { return this->z; }
	float getCameraZ(void) { return this->w; }
	float getRecipCameraZ(void) { return 1.0f/this->w; }
	RGBA getColor(void) { return makeRGBA(this->r, this->g, this->b, this->a); }
	float getU(void) { return this->u; }
	float getV(void) { return this->v; }
};

struct VulkanRaster
{
	int32 bpp;
	bool32 hasAlpha;
	int32 numLevels;
	RasterLevels *levels;
	VkImage image;
	VkDeviceMemory imageMemory;	// only for dedicated allocations
	VulkanAllocation imageAlloc;
	VkImageView imageView;
	VkSampler sampler;
	VkDescriptorSet descriptorSet;
	VkFormat imageFormat;
	VkImageLayout imageLayout;
	bool32 gpuDirty;
	bool32 gpuReady;
	bool32 uploadSubmitted;
	// camera texture render targets
	VkFramebuffer framebuffer;
	VkImageView framebufferDepthView;
	VkFormat framebufferFormat;
};

struct InstanceData
{
	uint32    numIndex;
	uint32    minVert;
	int32     numVertices;
	uint32    startIndex;
	Material *material;
	bool32    vertexAlpha;
};

struct InstanceDataHeader : rw::InstanceDataHeader
{
	uint32 serialNumber;
	uint32 numMeshes;
	uint32 primType;
	uint32 totalNumVertex;
	uint32 totalNumIndex;
	Im3DVertex *vertices;	// object space
	uint16 *indices;
	InstanceData *inst;
	// vertices and indices share one sub-allocation of a big buffer
	VkBuffer vertexBuffer;
	VkDeviceMemory vertexBufferMemory;
	VkBuffer indexBuffer;
	VkDeviceMemory indexBufferMemory;
	VulkanAllocation bufferAlloc;
	uint64 vertexOffset;
	uint64 indexOffset;
	bool32 gpuDirty;
	bool32 isSkinned;
	bool32 anyVertexAlpha;
	// list of all instanced geometries so GPU buffers can be
	// released when the device goes away
	InstanceDataHeader *prevInst;
	InstanceDataHeader *nextInst;
};

class ObjPipeline : public rw::ObjPipeline
{
public:
	void init(void);
	static ObjPipeline *create(void);

	void (*renderCB)(Atomic *atomic);
};

// Custom shaders drawn like the lit pipeline (Neo vehicle, world, gloss, rim)
//
// Vertex input is Im3DVertex (location 0 position, 1 normal, 2 color, 3 uv).
// Set 0 is texture 0, set 1 the per-atomic uniform block of lit3d.vert with
// CUSTOM_UNIFORM_VEC4S extra vec4s at the end, set 2 texture 1. Push constants
// are matColor, surfProps (x ambient, y diffuse, z lighting on), alphaRef and
// one custom vec4 per mesh. Blending and depth follow the render states.
enum { CUSTOM_UNIFORM_VEC4S = 16 };
struct CustomShader;
CustomShader *createCustomShader(const uint32 *vertSpv, size_t vertSize, const uint32 *fragSpv, size_t fragSize);
void destroyCustomShader(CustomShader *shader);
// Instances the atomic, fills and binds the standard uniforms plus `custom`
// (numVec4s of them), binds its buffers (skinned geometry is skinned first).
// Returns nil when the atomic can't be drawn.
InstanceDataHeader *customBeginAtomic(Atomic *atomic, CustomShader *shader, const float *custom, int32 numVec4s);
void customSetTexture1(Raster *raster);
void customDrawMesh(InstanceData *inst, const float *customPush);
void customEndAtomic(void);

extern int32 nativeRasterOffset;
#define GETVULKANRASTEREXT(raster) PLUGINOFFSET(rw::vulkan::VulkanRaster, raster, rw::vulkan::nativeRasterOffset)

Raster *rasterCreate(Raster *raster);
uint8 *rasterLock(Raster *raster, int32 level, int32 lockMode);
void rasterUnlock(Raster *raster, int32 level);
int32 rasterNumLevels(Raster *raster);
bool32 imageFindRasterFormat(Image *img, int32 type,
	int32 *width, int32 *height, int32 *depth, int32 *format);
bool32 rasterFromImage(Raster *raster, Image *image);
Image *rasterToImage(Raster *raster);

Texture *readNativeTexture(Stream *stream);
void writeNativeTexture(Texture *tex, Stream *stream);
uint32 getSizeNativeTexture(Texture *tex);
void registerNativeRaster(void);

ObjPipeline *makeDefaultPipeline(void);
void defaultRenderCB(Atomic *atomic);
void freeInstanceData(Geometry *geometry);
void *destroyNativeData(void *object, int32 offset, int32 size);
bool32 ensureTextureUploaded(Raster *raster);
void destroyRasterTexture(Raster *raster);
void initSkin(void);
void initMatFX(void);
#endif

}
}
