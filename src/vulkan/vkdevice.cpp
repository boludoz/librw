#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../rwbase.h"

#include "../rwplg.h"

#include "../rwengine.h"

#include "../rwpipeline.h"

#include "../rwobjects.h"

#include "../rwerror.h"

#include "../rwrender.h"


#ifdef RW_VULKAN

#include "rwvk.h"

#include "rwvkimpl.h"

#include "rwvkshader.h"

#include "vklighting.h"

#include "raytracing/vkTopLevel.h"
#include "raytracing/vkGBuffer.h"

#define PLUGIN_ID 0

#include "CommandBuffer.h"
#include "DescriptorSet.h"
#include "GraphicsContext.h"
#include "ImGuiRenderer.h"
#include "Pipeline.h"
#include "RenderDevice.h"
#include "Sampler.h"
#include "Shader.h"
#include "ShaderCompiler.h"
#include "SwapChain.h"
#include "Textures.h"
#include "UniformBuffer.h"
#include "StorageBuffer.h"
#include "SwapChain.h"
#include "BatchTask.h"
#include "AccelerationStructure.h"

#ifdef EMBEDDED_IMGUI
#include "../extras/ImGuiWin.h"
#include "../extras/TweakWin.h"
#endif // EMBEDDED_IMGUI

#include "Tweak/Tweakable.h"

#ifdef ENABLE_RAYTRACING
#include "raytracing/vkRaytracing.h"
#include "raytracing/vkRtShadow.h"
#include "raytracing/vkTopLevel.h"
#include "raytracing/PathTrace.h"
#include "Vulkan/VulkanDebug.h"
#include "gi/DDGI.h"
#endif // ENABLE_RAYTRACING

struct ImDrawData;

namespace rw
{
	void showSystemMouse(bool show)
	{
#ifdef LIBRW_SDL2
		SDL_ShowCursor(show ? SDL_ENABLE : SDL_DISABLE);
#elif defined(LIBRW_SDL3)
		if(show) SDL_ShowCursor();
		else SDL_HideCursor();
#elif defined(LIBRW_GLFW)
		glfwSetInputMode(vulkan::vkGlobals.window, GLFW_CURSOR, show ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
#endif
	}

	namespace vulkan
	{

		static maple::TextureFilter filterConvMap_NoMIP[] = {
			maple::TextureFilter::Linear, maple::TextureFilter::Nearest, maple::TextureFilter::Linear, maple::TextureFilter::Nearest,
			maple::TextureFilter::Linear, maple::TextureFilter::Nearest, maple::TextureFilter::Linear };

		static maple::TextureFilter filterConvMap_MIP[] = { maple::TextureFilter::Linear,  maple::TextureFilter::Nearest, maple::TextureFilter::Linear,
														   maple::TextureFilter::Nearest, maple::TextureFilter::Linear,  maple::TextureFilter::Nearest,
														   maple::TextureFilter::Linear };

		static maple::TextureWrap addressConvMap[] = { maple::TextureWrap::ClampToEdge, maple::TextureWrap::Repeat, maple::TextureWrap::MirroredRepeat,
													  maple::TextureWrap::ClampToEdge, maple::TextureWrap::ClampToBorder };

		static maple::ImGuiRenderer::Ptr imRender;

		std::shared_ptr<maple::StorageBuffer> ssboLighting[3];

		VkGlobals vkGlobals;

		int32 alphaFunc;
		float32 alphaRef;

		VulkanStats vulkanStats;

		static RawMatrix projViewInv;

		static maple::TweakFloat Specular = { "Material:Specular", 0.0, 0, 1 };
		static maple::TweakFloat Metalic = { "Material:Metalic", 0.0, 0, 1 };

		struct PushConsts {
			float32 projView[16];
			float32 projViewPrev[16];
			FogData fog;//pad[0] obj idx
			RawMatrix world;//just using for im3d
		};

		struct UniformObject
		{
			RawMatrix world;
			RGBAf matColor;
			RGBAf surfProps;	// amb, spec, diff, extra	
			RGBAf ambLight;
			int32_t offsets[4];
			struct {
				float type;
				float radius;
				float minusCosAngle;
				float hardSpot;
			} lightParams[MAX_LIGHTS];
			V4d lightPosition[MAX_LIGHTS];
			V4d lightDirection[MAX_LIGHTS];
			RGBAf lightColor[MAX_LIGHTS];
		};

		std::shared_ptr<maple::DescriptorSet> commonSet;

		static UniformObject uniformObject;
		static PushConsts pushConsts;

		Shader* defaultShader, * defaultShader_noAT;
		Shader* defaultShader_fullLight, * defaultShader_fullLight_noAT;

		std::shared_ptr<maple::UniformBuffer> cameraUniform;

		std::shared_ptr<maple::TextureCube> skyBox;

		std::shared_ptr<maple::Texture2D> skyBoxBuffer;

		static bool32 stateDirty = 1;
		static bool32 sceneDirty = 1;
		static bool32 objectDirty = 1;

		struct RwRasterStateCache {
			Raster* raster;
			Texture::Addressing addressingU;
			Texture::Addressing addressingV;
			Texture::FilterMode filter;
		};

#define MAXNUMSTAGES 8

		// cached RW render states
		struct RwStateCache {
			bool32 vertexAlpha;
			uint32 alphaTestEnable;
			uint32 alphaFunc;
			bool32 textureAlpha;
			bool32 blendEnable;
			uint32 srcblend, destblend;
			uint32 zwrite;
			uint32 ztest;
			uint32 cullmode;
			uint32 stencilenable;
			uint32 stencilpass;
			uint32 stencilfail;
			uint32 stencilzfail;
			uint32 stencilfunc;
			uint32 stencilref;
			uint32 stencilmask;
			uint32 stencilwritemask;
			uint32 fogEnable;
			float32 fogStart;
			float32 fogEnd;

			// emulation of PS2 GS
			bool32 gsalpha;
			uint32 gsalpharef;
			int32_t skyboxFace;
			float32 rain;
			RwRasterStateCache texstage[MAXNUMSTAGES];
		};
		static RwStateCache rwStateCache;

		enum {
			// actual gl states
			RWGL_BLEND,
			RWGL_SRCBLEND,
			RWGL_DESTBLEND,
			RWGL_DEPTHTEST,
			RWGL_DEPTHFUNC,
			RWGL_DEPTHMASK,
			RWGL_CULL,
			RWGL_CULLFACE,
			RWGL_STENCIL,
			RWGL_STENCILFUNC,
			RWGL_STENCILFAIL,
			RWGL_STENCILZFAIL,
			RWGL_STENCILPASS,
			RWGL_STENCILREF,
			RWGL_STENCILMASK,
			RWGL_STENCILWRITEMASK,

			// uniforms
			RWGL_ALPHAFUNC,
			RWGL_ALPHAREF,
			RWGL_FOG,
			RWGL_FOGSTART,
			RWGL_FOGEND,
			RWGL_FOGCOLOR,

			RWGL_NUM_STATES
		};
		static bool uniformStateDirty[RWGL_NUM_STATES];

		struct GlState {
			bool32 blendEnable;
			uint32 srcblend, destblend;

			bool32 depthTest;
			uint32 depthFunc;

			uint32 depthMask;

			bool32 cullEnable;
			uint32 cullFace;

			bool32 stencilEnable;
			// glStencilFunc
			uint32 stencilFunc;
			uint32 stencilRef;
			uint32 stencilMask;
			// glStencilOp
			uint32 stencilPass;
			uint32 stencilFail;
			uint32 stencilZFail;
			// glStencilMask
			uint32 stencilWriteMask;
		};
		static GlState curGlState, oldGlState;

		static int32 activeTexture;
		static uint32 boundTexture[MAXNUMSTAGES];

		static maple::BlendMode blendMap[] = {
			maple::BlendMode::One,
			maple::BlendMode::Zero,
			maple::BlendMode::One,
			maple::BlendMode::SrcColor,
			maple::BlendMode::OneMinusSrcColor,
			maple::BlendMode::SrcAlpha,
			maple::BlendMode::OneMinusSrcAlpha,
			maple::BlendMode::DstAlpha,
			maple::BlendMode::OneMinusDstAlpha,
			maple::BlendMode::DstColor,
			maple::BlendMode::OneMinusDstColor,
			maple::BlendMode::SrcAlphaSaturate,
		};

		static uint32 stencilOpMap[] = { 0 };

		static uint32 stencilFuncMap[] = { 0 };

		static float maxAnisotropy;

		/*
		 * GL state cache
		 */

		void setVKRenderState(uint32 state, uint32 value)
		{
			switch (state) {
			case RWGL_BLEND: curGlState.blendEnable = value; break;
			case RWGL_SRCBLEND:
				curGlState.srcblend = value;
				if (oldGlState.srcblend == 2 && value == 5)
				{
					int32_t i = 0;
				}
				break;
			case RWGL_DESTBLEND:
				curGlState.destblend = value;
				break;
			case RWGL_DEPTHTEST: curGlState.depthTest = value; break;
			case RWGL_DEPTHFUNC: curGlState.depthFunc = value; break;
			case RWGL_DEPTHMASK: curGlState.depthMask = value; break;
			case RWGL_CULL: curGlState.cullEnable = value; break;
			case RWGL_CULLFACE: curGlState.cullFace = value; break;
			case RWGL_STENCIL: curGlState.stencilEnable = value; break;
			case RWGL_STENCILFUNC: curGlState.stencilFunc = value; break;
			case RWGL_STENCILFAIL: curGlState.stencilFail = value; break;
			case RWGL_STENCILZFAIL: curGlState.stencilZFail = value; break;
			case RWGL_STENCILPASS: curGlState.stencilPass = value; break;
			case RWGL_STENCILREF: curGlState.stencilRef = value; break;
			case RWGL_STENCILMASK: curGlState.stencilMask = value; break;
			case RWGL_STENCILWRITEMASK: curGlState.stencilWriteMask = value; break;
			}
		}

		void setAlphaBlend(bool32 enable)
		{
			if (rwStateCache.blendEnable != enable) {
				rwStateCache.blendEnable = enable;
				setVKRenderState(RWGL_BLEND, enable);
			}
		}

		bool32 getAlphaBlend(void) { return rwStateCache.blendEnable; }

		bool32 getAlphaTest(void) { return rwStateCache.alphaTestEnable; }

		uint32_t getDepthFunc() { return curGlState.depthFunc; }

		static void setDepthTest(bool32 enable)
		{
			if (rwStateCache.ztest != enable) {
				rwStateCache.ztest = enable;
				if (rwStateCache.zwrite && !enable) {
					// If we still want to write, enable but set mode to always
					setVKRenderState(RWGL_DEPTHTEST, true);
					setVKRenderState(RWGL_DEPTHFUNC, (uint32_t)maple::StencilType::Always);
				}
				else {
					setVKRenderState(RWGL_DEPTHTEST, rwStateCache.ztest);
					setVKRenderState(RWGL_DEPTHFUNC, (uint32_t)maple::StencilType::LessOrEqual);
				}
			}
		}

		static void setDepthWrite(bool32 enable)
		{
			enable = enable ? 1 : 0;
			if (rwStateCache.zwrite != enable) {
				rwStateCache.zwrite = enable;
				if (enable && !rwStateCache.ztest) {
					// Have to switch on ztest so writing can work
					setVKRenderState(RWGL_DEPTHTEST, true);
					setVKRenderState(RWGL_DEPTHFUNC, (uint32_t)maple::StencilType::Always);
				}
				setVKRenderState(RWGL_DEPTHMASK, rwStateCache.zwrite);
			}
		}

		static void setAlphaTest(bool32 enable)
		{
			uint32 shaderfunc;
			if (rwStateCache.alphaTestEnable != enable) {
				rwStateCache.alphaTestEnable = enable;
				shaderfunc = rwStateCache.alphaTestEnable ? rwStateCache.alphaFunc : ALPHAALWAYS;
				if (alphaFunc != shaderfunc) {
					alphaFunc = shaderfunc;
					uniformStateDirty[RWGL_ALPHAFUNC] = true;
					stateDirty = 1;
				}
			}
		}

		static void setAlphaTestFunction(uint32 function)
		{
			uint32 shaderfunc;
			if (rwStateCache.alphaFunc != function) {
				rwStateCache.alphaFunc = function;
				shaderfunc = rwStateCache.alphaTestEnable ? rwStateCache.alphaFunc : ALPHAALWAYS;
				if (alphaFunc != shaderfunc) {
					alphaFunc = shaderfunc;
					uniformStateDirty[RWGL_ALPHAFUNC] = true;
					stateDirty = 1;
				}
			}
		}

		static void setVertexAlpha(bool32 enable)
		{
			if (rwStateCache.vertexAlpha != enable) {
				if (!rwStateCache.textureAlpha) {
					setAlphaBlend(enable);
					setAlphaTest(enable);
				}
				rwStateCache.vertexAlpha = enable;
			}
		}

		void bindFramebuffer(uint32 fbo) {}

		static void setFilterMode(uint32 stage, int32 filter, int32 maxAniso = 1)
		{
			if (rwStateCache.texstage[stage].filter != (Texture::FilterMode)filter) {
				rwStateCache.texstage[stage].filter = (Texture::FilterMode)filter;

				Raster* raster = rwStateCache.texstage[stage].raster;
				if (raster) {
					VulkanRaster* natras = PLUGINOFFSET(VulkanRaster, rwStateCache.texstage[stage].raster, nativeRasterOffset);
					if (natras->filterMode != filter) {

						getTexture(natras->textureId)
							->setSampler(maple::Sampler::create(
								filterConvMap_MIP[filter], addressConvMap[rwStateCache.texstage[stage].addressingU],
								addressConvMap[rwStateCache.texstage[stage].addressingV], natras->maxAnisotropy, natras->numLevels));

						natras->filterMode = filter;
					}
					if (natras->maxAnisotropy != maxAniso) {
						getTexture(natras->textureId)
							->setSampler(maple::Sampler::create(
								filterConvMap_MIP[filter], addressConvMap[rwStateCache.texstage[stage].addressingU],
								addressConvMap[rwStateCache.texstage[stage].addressingV], maxAniso, natras->numLevels));
						natras->maxAnisotropy = maxAniso;
					}
				}
			}
		}

		/**
		 * stage textureId.
		 */
		static void setAddressU(uint32 stage, int32 addressing)
		{
			if (rwStateCache.texstage[stage].addressingU != (Texture::Addressing)addressing) {
				rwStateCache.texstage[stage].addressingU = (Texture::Addressing)addressing;
			}
		}

		static void setAddressV(uint32 stage, int32 addressing)
		{
			if (rwStateCache.texstage[stage].addressingV != (Texture::Addressing)addressing) {
				rwStateCache.texstage[stage].addressingV = (Texture::Addressing)addressing;
				Raster* raster = rwStateCache.texstage[stage].raster;
			}
		}

		static void setRasterStageOnly(uint32 stage, Raster* raster)
		{
			bool32 alpha;
			if (raster != rwStateCache.texstage[stage].raster) {
				rwStateCache.texstage[stage].raster = raster;

				if (raster) {
					assert(raster->platform == PLATFORM_VULKAN);
					VulkanRaster* natras = PLUGINOFFSET(VulkanRaster, raster, nativeRasterOffset);

					rwStateCache.texstage[stage].filter = (rw::Texture::FilterMode)natras->filterMode;
					rwStateCache.texstage[stage].addressingU = (rw::Texture::Addressing)natras->addressU;
					rwStateCache.texstage[stage].addressingV = (rw::Texture::Addressing)natras->addressV;

					alpha = natras->hasAlpha;
				}
				else {
					alpha = 0;
				}

				if (stage == 0) {
					if (alpha != rwStateCache.textureAlpha) {
						rwStateCache.textureAlpha = alpha;
						if (!rwStateCache.vertexAlpha) {
							setAlphaBlend(alpha);
							setAlphaTest(alpha);
						}
					}
				}
			}
		}

		static void setRasterStage(uint32 stage, Raster* raster)
		{
			PROFILE_FUNCTION();
			bool32 alpha;
			if (raster != rwStateCache.texstage[stage].raster) {
				rwStateCache.texstage[stage].raster = raster;

				if (raster) {
					VulkanRaster* natras = PLUGINOFFSET(VulkanRaster, raster, nativeRasterOffset);
					alpha = natras->hasAlpha;

					uint32 filter = rwStateCache.texstage[stage].filter;
					uint32 addrU = rwStateCache.texstage[stage].addressingU;
					uint32 addrV = rwStateCache.texstage[stage].addressingV;

					if (natras->filterMode != filter) { natras->filterMode = filter; }
					if (natras->addressU != addrU) { natras->addressU = addrU; }
					if (natras->addressV != addrV) { natras->addressV = addrV; }

					getTexture(natras->textureId)
						->setSampler(maple::Sampler::create(
							filterConvMap_MIP[filter],
							addressConvMap[addrU],
							addressConvMap[addrV],
							natras->maxAnisotropy,
							natras->numLevels));
				}
				else {
					alpha = 0;
				}

				if (stage == 0) {
					if (alpha != rwStateCache.textureAlpha) {
						rwStateCache.textureAlpha = alpha;
						if (!rwStateCache.vertexAlpha) {
							setAlphaBlend(alpha);
							setAlphaTest(alpha);
						}
					}
				}
			}
		}

		void evictRaster(Raster* raster)
		{
			PROFILE_FUNCTION();
			int i;
			for (i = 0; i < MAXNUMSTAGES; i++) {
				// assert(rwStateCache.texstage[i].raster != raster);
				if (rwStateCache.texstage[i].raster != raster) continue;
				setRasterStage(i, nil);
			}
		}

		void setTextureBlend(Texture* tex)
		{
			if (tex == nil || tex->raster == nil) {
				setRasterStage(0, nil);
			}
			else {
				VulkanRaster* natras = nullptr;
				if (tex != nullptr) { natras = PLUGINOFFSET(VulkanRaster, tex->raster, nativeRasterOffset); }

				if (natras->hasAlpha != rwStateCache.textureAlpha) {
					rwStateCache.textureAlpha = natras->hasAlpha;
					if (!rwStateCache.vertexAlpha) {
						setAlphaBlend(natras->hasAlpha);
						setAlphaTest(natras->hasAlpha);
					}
				}
			}
		}

		void setTexture(std::shared_ptr<maple::DescriptorSet> sets, int32 stage, Texture* tex)
		{
			PROFILE_FUNCTION();
			if (tex == nil || tex->raster == nil) {
				setRasterStage(stage, nil);
				auto texture = maple::Texture2D::getTexture1X1White();
				sets->setTexture("tex" + std::to_string(stage), texture);
			}
			else {
				VulkanRaster* natras = nullptr;
				if (tex != nullptr) { natras = PLUGINOFFSET(VulkanRaster, tex->raster, nativeRasterOffset); }

				setRasterStageOnly(stage, tex->raster);

				setAddressU(stage, tex->getAddressU());
				setAddressV(stage, tex->getAddressV());
				setFilterMode(stage, tex->getFilter(), tex->getMaxAnisotropy());

				getTexture(natras->textureId)
					->setSampler(maple::Sampler::create(
						filterConvMap_MIP[tex->getFilter()], addressConvMap[rwStateCache.texstage[stage].addressingU],
						addressConvMap[rwStateCache.texstage[stage].addressingV], natras->maxAnisotropy, natras->numLevels));

				if (stage == 0) {
					if (natras->hasAlpha != rwStateCache.textureAlpha) {
						rwStateCache.textureAlpha = natras->hasAlpha;
						if (!rwStateCache.vertexAlpha) {
							setAlphaBlend(natras->hasAlpha);
							setAlphaTest(natras->hasAlpha);
						}
					}
				}

				sets->setTexture("tex" + std::to_string(stage), getTexture(natras->textureId));
			}
		}

		static void setRenderState(int32 state, void* pvalue)
		{
			PROFILE_FUNCTION();
			uint32 value = (uint32)(uintptr)pvalue;
			switch (state) {
			case TEXTURERASTER: setRasterStage(0, (Raster*)pvalue); break;
			case TEXTUREADDRESS:
				setAddressU(0, value);
				setAddressV(0, value);
				break;
			case TEXTUREADDRESSU: setAddressU(0, value); break;
			case TEXTUREADDRESSV: setAddressV(0, value); break;
			case TEXTUREFILTER: setFilterMode(0, value); break;
			case VERTEXALPHA: setVertexAlpha(value); break;
			case SRCBLEND:
				if (rwStateCache.srcblend != value) {
					rwStateCache.srcblend = value;
					setVKRenderState(RWGL_SRCBLEND, (uint32_t)blendMap[rwStateCache.srcblend]);
				}
				break;
			case DESTBLEND:
				if (rwStateCache.destblend != value) {
					rwStateCache.destblend = value;
					setVKRenderState(RWGL_DESTBLEND, (uint32_t)blendMap[rwStateCache.destblend]);
				}
				break;
			case ZTESTENABLE: setDepthTest(value); break;
			case ZWRITEENABLE: setDepthWrite(value); break;
			case FOGENABLE:
				if (rwStateCache.fogEnable != value) {
					rwStateCache.fogEnable = value;
					uniformStateDirty[RWGL_FOG] = true;
					stateDirty = 1;
				}
				break;
			case FOGCOLOR:
				// no cache check here...too lazy
				RGBA c;
				c.red = value;
				c.green = value >> 8;
				c.blue = value >> 16;
				c.alpha = value >> 24;
				convColor(&pushConsts.fog.fogColor, &c);
				uniformStateDirty[RWGL_FOGCOLOR] = true;
				stateDirty = 1;
				break;
			case CULLMODE:
				if (rwStateCache.cullmode != value) {
					rwStateCache.cullmode = value;
					if (rwStateCache.cullmode == CULLNONE) {
						setVKRenderState(RWGL_CULL, false);
						setVKRenderState(RWGL_CULLFACE, (uint32_t)maple::CullMode::None);
					}
					else {
						setVKRenderState(RWGL_CULL, true);
						auto cullMode = rwStateCache.cullmode == CULLBACK ? maple::CullMode::Back : maple::CullMode::Front;
						setVKRenderState(RWGL_CULLFACE, (uint32_t)cullMode);
					}
				}
				break;

			case STENCILENABLE:
				if (rwStateCache.stencilenable != value) {
					rwStateCache.stencilenable = value;
					setVKRenderState(RWGL_STENCIL, value);
				}
				break;
			case STENCILFAIL:
				if (rwStateCache.stencilfail != value) {
					rwStateCache.stencilfail = value;
					setVKRenderState(RWGL_STENCILFAIL, stencilOpMap[value]);
				}
				break;
			case STENCILZFAIL:
				if (rwStateCache.stencilzfail != value) {
					rwStateCache.stencilzfail = value;
					setVKRenderState(RWGL_STENCILZFAIL, stencilOpMap[value]);
				}
				break;
			case STENCILPASS:
				if (rwStateCache.stencilpass != value) {
					rwStateCache.stencilpass = value;
					setVKRenderState(RWGL_STENCILPASS, stencilOpMap[value]);
				}
				break;
			case STENCILFUNCTION:
				if (rwStateCache.stencilfunc != value) {
					rwStateCache.stencilfunc = value;
					setVKRenderState(RWGL_STENCILFUNC, stencilFuncMap[value]);
				}
				break;
			case STENCILFUNCTIONREF:
				if (rwStateCache.stencilref != value) {
					rwStateCache.stencilref = value;
					setVKRenderState(RWGL_STENCILREF, value);
				}
				break;
			case STENCILFUNCTIONMASK:
				if (rwStateCache.stencilmask != value) {
					rwStateCache.stencilmask = value;
					setVKRenderState(RWGL_STENCILMASK, value);
				}
				break;
			case STENCILFUNCTIONWRITEMASK:
				if (rwStateCache.stencilwritemask != value) {
					rwStateCache.stencilwritemask = value;
					setVKRenderState(RWGL_STENCILWRITEMASK, value);
				}
				break;

			case ALPHATESTFUNC: setAlphaTestFunction(value); break;
			case ALPHATESTREF:
				if (alphaRef != value / 255.0f) {
					alphaRef = value / 255.0f;
					uniformStateDirty[RWGL_ALPHAREF] = true;
					stateDirty = 1;
				}
				break;
			case GSALPHATEST: rwStateCache.gsalpha = value; break;
			case GSALPHATESTREF: rwStateCache.gsalpharef = value; break;
			case SKYBOX_FACE:rwStateCache.skyboxFace = value; break;

			case RAIN_INTENSITY: {
				rwStateCache.rain = *reinterpret_cast<float*>(&value);
			}
			}
		}

		static void* getRenderState(int32 state)
		{
			PROFILE_FUNCTION();
			uint32 val;
			RGBA rgba;
			switch (state) {
			case TEXTURERASTER: return rwStateCache.texstage[0].raster;
			case TEXTUREADDRESS:
				if (rwStateCache.texstage[0].addressingU == rwStateCache.texstage[0].addressingV)
					val = rwStateCache.texstage[0].addressingU;
				else
					val = 0; // invalid
				break;
			case TEXTUREADDRESSU: val = rwStateCache.texstage[0].addressingU; break;
			case TEXTUREADDRESSV: val = rwStateCache.texstage[0].addressingV; break;
			case TEXTUREFILTER: val = rwStateCache.texstage[0].filter; break;

			case VERTEXALPHA: val = rwStateCache.vertexAlpha; break;
			case SRCBLEND: val = rwStateCache.srcblend; break;
			case DESTBLEND: val = rwStateCache.destblend; break;
			case ZTESTENABLE: val = rwStateCache.ztest; break;
			case ZWRITEENABLE: val = rwStateCache.zwrite; break;
			case FOGENABLE: val = rwStateCache.fogEnable; break;
			case FOGCOLOR:
				convColor(&rgba, &pushConsts.fog.fogColor);
				val = RWRGBAINT(rgba.red, rgba.green, rgba.blue, rgba.alpha);
				break;
			case CULLMODE: val = rwStateCache.cullmode; break;

			case STENCILENABLE: val = rwStateCache.stencilenable; break;
			case STENCILFAIL: val = rwStateCache.stencilfail; break;
			case STENCILZFAIL: val = rwStateCache.stencilzfail; break;
			case STENCILPASS: val = rwStateCache.stencilpass; break;
			case STENCILFUNCTION: val = rwStateCache.stencilfunc; break;
			case STENCILFUNCTIONREF: val = rwStateCache.stencilref; break;
			case STENCILFUNCTIONMASK: val = rwStateCache.stencilmask; break;
			case STENCILFUNCTIONWRITEMASK: val = rwStateCache.stencilwritemask; break;

			case ALPHATESTFUNC: val = rwStateCache.alphaFunc; break;
			case ALPHATESTREF: val = (uint32)(alphaRef * 255.0f); break;
			case GSALPHATEST: val = rwStateCache.gsalpha; break;
			case GSALPHATESTREF: val = rwStateCache.gsalpharef; break;
			case SKYBOX_FACE: val = rwStateCache.skyboxFace; break;
			case RAIN_INTENSITY: val = *reinterpret_cast<uint32_t*>(&rwStateCache.rain); break;
			default: val = 0;
			}
			return (void*)(uintptr)val;
		}

		static void resetRenderState(void)
		{
			PROFILE_FUNCTION();
			rwStateCache.alphaFunc = ALPHAGREATEREQUAL;
			alphaFunc = 0;
			alphaRef = 10.0f / 255.0f;
			uniformStateDirty[RWGL_ALPHAREF] = true;
			pushConsts.fog.fogDisable = 1.0f;
			pushConsts.fog.fogStart = 0.0f;
			pushConsts.fog.fogEnd = 0.0f;
			pushConsts.fog.fogRange = 0.0f;
			pushConsts.fog.fogColor = { 1.0f, 1.0f, 1.0f, 1.0f };
			rwStateCache.gsalpha = 0;
			rwStateCache.gsalpharef = 128;
			rwStateCache.rain = 0;
			stateDirty = 1;

			rwStateCache.vertexAlpha = 0;
			rwStateCache.textureAlpha = 0;
			rwStateCache.alphaTestEnable = 0;
			rwStateCache.skyboxFace = -1;

			memset(&oldGlState, 0xFE, sizeof(oldGlState));

			rwStateCache.blendEnable = 0;
			setVKRenderState(RWGL_BLEND, false);
			rwStateCache.srcblend = BLENDSRCALPHA;
			rwStateCache.destblend = BLENDINVSRCALPHA;
			setVKRenderState(RWGL_SRCBLEND, (uint32_t)blendMap[rwStateCache.srcblend]);
			setVKRenderState(RWGL_DESTBLEND, (uint32_t)blendMap[rwStateCache.destblend]);

			rwStateCache.zwrite = 1;
			setVKRenderState(RWGL_DEPTHMASK, rwStateCache.zwrite);

			rwStateCache.ztest = 1;
			setVKRenderState(RWGL_DEPTHTEST, true);
			setVKRenderState(RWGL_DEPTHFUNC, (uint32_t)maple::StencilType::LessOrEqual);

			rwStateCache.cullmode = CULLNONE;
			setVKRenderState(RWGL_CULL, false);
			setVKRenderState(RWGL_CULLFACE, (uint32_t)maple::CullMode::None);

			rwStateCache.stencilenable = 0;
			setVKRenderState(RWGL_STENCIL, 0);
			rwStateCache.stencilfail = STENCILKEEP;
			setVKRenderState(RWGL_STENCILFAIL, (uint32_t)maple::StencilType::Keep);
			rwStateCache.stencilzfail = STENCILKEEP;
			setVKRenderState(RWGL_STENCILZFAIL, (uint32_t)maple::StencilType::Keep);
			rwStateCache.stencilpass = STENCILKEEP;
			setVKRenderState(RWGL_STENCILPASS, (uint32_t)maple::StencilType::Keep);
			rwStateCache.stencilfunc = STENCILALWAYS;
			setVKRenderState(RWGL_STENCILFUNC, (uint32_t)maple::StencilType::Always);
			rwStateCache.stencilref = 0;
			setVKRenderState(RWGL_STENCILREF, 0);
			rwStateCache.stencilmask = 0xFFFFFFFF;
			setVKRenderState(RWGL_STENCILMASK, 0xFFFFFFFF);
			rwStateCache.stencilwritemask = 0xFFFFFFFF;
			setVKRenderState(RWGL_STENCILWRITEMASK, 0xFFFFFFFF);
		}

		void setWorldMatrix(Matrix* mat)
		{
			PROFILE_FUNCTION();
			convMatrix(&pushConsts.world, mat);
			uniformObject.world = pushConsts.world;
			objectDirty = 1;
		}

		int32 setLights(WorldLights* lightData, const std::vector<LightData>& lights)
		{
			PROFILE_FUNCTION();
			int i, n;
			Light* l;
			int32 bits;

			auto textureId = uniformObject.ambLight.alpha;

			uniformObject.ambLight = lightData->ambient;
			uniformObject.ambLight.alpha = textureId;

			bits = 0;

			if (lightData->numAmbients) bits |= VSLIGHT_AMBIENT;

			n = 0;
			for (i = 0; i < lightData->numDirectionals && i < 8; i++) {
				l = lightData->directionals[i];
				uniformObject.lightParams[n].type = 1.0f;
				uniformObject.lightColor[n] = l->color;
				memcpy(&uniformObject.lightDirection[n], &l->getFrame()->getLTM()->at, sizeof(V3d));
				bits |= VSLIGHT_DIRECT;
				n++;
				if (n >= MAX_LIGHTS) goto out;
			}

			for (i = 0; i < lightData->numLocals; i++) {
				Light* l = lightData->locals[i];

				switch (l->getType()) {
				case Light::POINT:
					uniformObject.lightParams[n].type = 2.0f;
					uniformObject.lightParams[n].radius = l->radius;
					uniformObject.lightColor[n] = l->color;
					memcpy(&uniformObject.lightPosition[n], &l->getFrame()->getLTM()->pos, sizeof(V3d));
					bits |= VSLIGHT_POINT;
					n++;
					if (n >= MAX_LIGHTS) goto out;
					break;
				case Light::SPOT:
				case Light::SOFTSPOT:
					uniformObject.lightParams[n].type = 3.0f;
					uniformObject.lightParams[n].minusCosAngle = l->minusCosAngle;
					uniformObject.lightParams[n].radius = l->radius;
					uniformObject.lightColor[n] = l->color;
					memcpy(&uniformObject.lightPosition[n], &l->getFrame()->getLTM()->pos, sizeof(V3d));
					memcpy(&uniformObject.lightDirection[n], &l->getFrame()->getLTM()->at, sizeof(V3d));
					// lower bound of falloff
					if (l->getType() == Light::SOFTSPOT)
						uniformObject.lightParams[n].hardSpot = 0.0f;
					else
						uniformObject.lightParams[n].hardSpot = 1.0f;
					bits |= VSLIGHT_SPOT;
					n++;
					if (n >= MAX_LIGHTS) goto out;
					break;
				}
			}

			for (auto& points : lights)
			{
				uniformObject.lightParams[n].type = points.type;
				uniformObject.lightParams[n].radius = points.radius;
				uniformObject.lightColor[n] = points.color;
				memcpy(&uniformObject.lightPosition[n], &points.position, sizeof(V3d));
				memcpy(&uniformObject.lightDirection[n], &points.direction, sizeof(V3d));
				bits |= VSLIGHT_POINT;
				n++;
				if (n >= MAX_LIGHTS) goto out;
			}

			uniformObject.lightParams[n].type = 0.0f;
		out:
			uniformObject.offsets[3] = n;
			objectDirty = 1;
			return bits;
		}

		void setProjectionMatrix(float32* mat)
		{
			//memcpy(&pushConsts.proj, mat, 64);
			sceneDirty = 1;
		}

		void setViewMatrix(float32* mat)
		{
			//memcpy(&pushConsts.view, mat, 64);
			sceneDirty = 1;
		}

		Shader* lastShaderUploaded;

		void setMaterial(const RGBA& color, const SurfaceProperties& surfaceprops,
			int32 textureId,
			int32 instanceId,
			int32 indexOffset,
			int32 objectId,
			float extraSurfProp)
		{
			PROFILE_FUNCTION();

			uniformObject.offsets[0] = indexOffset;
			uniformObject.offsets[1] = instanceId;
			uniformObject.offsets[2] = textureId;
			//uniformObject.offsets[3] = objectId;

			uint32_t rain = GetRenderState(rw::RAIN_INTENSITY);
			float rain2 = *reinterpret_cast<float*>(&rain);

			convColor(&uniformObject.matColor, &color);
			uniformObject.ambLight.alpha = textureId;
			float surfProps[4];
			surfProps[0] = surfaceprops.ambient;
			surfProps[1] = std::clamp(surfaceprops.specular + rain2 + Specular.var, 0.f, 1.f);
			surfProps[2] = std::clamp(surfaceprops.diffuse + Metalic.var, 0.f, 1.f);
			surfProps[3] = extraSurfProp;
			memcpy(&uniformObject.surfProps, surfProps, sizeof(float) * 4);
		}

		void initPipelineInfo(maple::PipelineInfo& info)
		{
			PROFILE_FUNCTION();
			if (oldGlState.blendEnable != curGlState.blendEnable) { oldGlState.blendEnable = curGlState.blendEnable; }

			if (oldGlState.srcblend != curGlState.srcblend || oldGlState.destblend != curGlState.destblend) {
				oldGlState.srcblend = curGlState.srcblend;
				oldGlState.destblend = curGlState.destblend;
			}

			if (oldGlState.depthTest != curGlState.depthTest) { oldGlState.depthTest = curGlState.depthTest; }
			if (oldGlState.depthFunc != curGlState.depthFunc) { oldGlState.depthFunc = curGlState.depthFunc; }
			if (oldGlState.depthMask != curGlState.depthMask) { oldGlState.depthMask = curGlState.depthMask; }

			if (oldGlState.stencilEnable != curGlState.stencilEnable) { oldGlState.stencilEnable = curGlState.stencilEnable; }
			if (oldGlState.stencilFunc != curGlState.stencilFunc || oldGlState.stencilRef != curGlState.stencilRef ||
				oldGlState.stencilMask != curGlState.stencilMask) {
				oldGlState.stencilFunc = curGlState.stencilFunc;
				oldGlState.stencilRef = curGlState.stencilRef;
				oldGlState.stencilMask = curGlState.stencilMask;
			}
			if (oldGlState.stencilPass != curGlState.stencilPass || oldGlState.stencilFail != curGlState.stencilFail ||
				oldGlState.stencilZFail != curGlState.stencilZFail) {
				oldGlState.stencilPass = curGlState.stencilPass;
				oldGlState.stencilFail = curGlState.stencilFail;
				oldGlState.stencilZFail = curGlState.stencilZFail;
			}
			if (oldGlState.stencilWriteMask != curGlState.stencilWriteMask) { oldGlState.stencilWriteMask = curGlState.stencilWriteMask; }

			if (oldGlState.cullEnable != curGlState.cullEnable) { oldGlState.cullEnable = curGlState.cullEnable; }

			if (oldGlState.cullFace != curGlState.cullFace) { oldGlState.cullFace = curGlState.cullFace; }

			info.transparencyEnabled = oldGlState.blendEnable;
			info.depthTest = curGlState.depthTest;
			info.depthWriteEnable = curGlState.depthMask;
			info.depthFunc = (maple::StencilType)getDepthFunc();
			info.cullMode = (maple::CullMode)curGlState.cullFace;
			info.blendMode = (maple::BlendMode)curGlState.srcblend;
			info.dstBlendMode = (maple::BlendMode)curGlState.destblend;
		}

		void flushCache(std::shared_ptr<maple::Shader> shader, uint32_t objectIndex, int32_t meshId)
		{
			PROFILE_FUNCTION();
			auto bufferIdx = maple::GraphicsContext::get()->getSwapChain()->getCurrentBufferIndex();

			if (objectIndex != -1)
			{
				objectDirty = 0;
				auto ssbo = ssboLighting[bufferIdx]->mapPointer<UniformObject>();
				memcpy(&ssbo[objectIndex], &uniformObject, sizeof(UniformObject));
				ssboLighting[bufferIdx]->unmap();
			}

			pushConsts.fog.objectId = objectIndex;
			pushConsts.fog.meshId = meshId;

			switch (alphaFunc) {
			case ALPHAALWAYS:
			default:
				pushConsts.fog.alphaRefLow = -1000.0f;
				pushConsts.fog.alphaRefHigh = 1000.0f;
				break;
			case ALPHAGREATEREQUAL:
				pushConsts.fog.alphaRefLow = alphaRef;
				pushConsts.fog.alphaRefHigh = 1000.0f;
				break;
			case ALPHALESS:
				pushConsts.fog.alphaRefLow = -1000.0f;
				pushConsts.fog.alphaRefHigh = alphaRef;
				break;
			}
			pushConsts.fog.fogDisable = rwStateCache.fogEnable ? 0.0f : 1.0f;
			pushConsts.fog.fogStart = rwStateCache.fogStart;
			pushConsts.fog.fogEnd = rwStateCache.fogEnd;
			pushConsts.fog.fogRange = 1.0f / (rwStateCache.fogStart - rwStateCache.fogEnd);

			if (shader != nullptr)
			{
				if (auto consts = shader->getPushConstant(0))
				{
					consts->setData(&pushConsts);
				}
			}
		}

		const FogData& flushFog()
		{
			PROFILE_FUNCTION();
			// if (stateDirty)
			{
				switch (alphaFunc) {
				case ALPHAALWAYS:
				default:
					pushConsts.fog.alphaRefLow = -1000.0f;
					pushConsts.fog.alphaRefHigh = 1000.0f;
					break;
				case ALPHAGREATEREQUAL:
					pushConsts.fog.alphaRefLow = alphaRef;
					pushConsts.fog.alphaRefHigh = 1000.0f;
					break;
				case ALPHALESS:
					pushConsts.fog.alphaRefLow = -1000.0f;
					pushConsts.fog.alphaRefHigh = alphaRef;
					break;
				}
				pushConsts.fog.fogDisable = rwStateCache.fogEnable ? 0.0f : 1.0f;
				pushConsts.fog.fogStart = rwStateCache.fogStart;
				pushConsts.fog.fogEnd = rwStateCache.fogEnd;
				pushConsts.fog.fogRange = 1.0f / (rwStateCache.fogStart - rwStateCache.fogEnd);
				stateDirty = 0;
			}
			return pushConsts.fog;
		}

		static void setFrameBuffer(Camera* cam)
		{
			PROFILE_FUNCTION();
			Raster* fbuf = cam->frameBuffer->parent;
			Raster* zbuf = cam->zBuffer->parent;
			assert(fbuf);

			VulkanRaster* natras2 = GET_VULKAN_RASTEREXT(fbuf);
			VulkanRaster* natras = GET_VULKAN_RASTEREXT(zbuf);

			assert(fbuf->type == Raster::CAMERA || fbuf->type == Raster::CAMERATEXTURE);

			vkGlobals.colorTarget = getTexture(natras2->textureId);
#ifdef ENABLE_GBUFFER
			vkGlobals.currentDepth = maple::gbuffer::getDepth();

			if (vkGlobals.colorTarget == nullptr)
			{
				vkGlobals.colorTarget = maple::gbuffer::getGBufferColor();
				vkGlobals.normalTarget = maple::gbuffer::getGBuffer1();
				vkGlobals.gbuffer2 = maple::gbuffer::getGBuffer2();
				vkGlobals.gbuffer3 = maple::gbuffer::getGBuffer3();
				vkGlobals.gbuffer4 = maple::gbuffer::getGBuffer4();
			}
#else
			vkGlobals.currentDepth = getTexture(natras->textureId);
#endif
		}

		static Rect getFramebufferRect(Raster* frameBuffer)
		{
			PROFILE_FUNCTION();
			Rect r;
			Raster* fb = frameBuffer->parent;
			if (fb->type == Raster::CAMERA) {
				glfwGetFramebufferSize(vkGlobals.window, &r.w, &r.h);
			}
			else {
				r.w = fb->width;
				r.h = fb->height;
			}
			r.x = 0;
			r.y = 0;

			// Got a subraster
			if (frameBuffer != fb) {
				r.x = frameBuffer->offsetX;
				// GL y offset is from bottom
				r.y = frameBuffer->offsetY;
				r.w = frameBuffer->width;
				r.h = frameBuffer->height;
			}

			return r;
		}

		static void setViewport(Raster* frameBuffer)
		{
			PROFILE_FUNCTION();
			Rect r = getFramebufferRect(frameBuffer);
			if (r.w != vkGlobals.presentWidth || r.h != vkGlobals.presentHeight || r.x != vkGlobals.presentOffX || r.y != vkGlobals.presentOffY) {
				vkGlobals.presentWidth = r.w;
				vkGlobals.presentHeight = r.h;
				vkGlobals.presentOffX = r.x;
				vkGlobals.presentOffY = r.y;
			}
		}
		// using this one to indicate. because we only need vulkan begin once each frame.
		// so when the first camera executes begin update or clear we can know that we need to render next frame.
		// and reset it in showRaster
		static bool executeOnceFlag = false;
		static V3d cameraPos = { };
		static V3d cameraPosLast = {};
		static float farPlane = 0;
		static float nearPlane = 0;
		static glm::vec4 planes[6] = {};

		static void beginUpdate(Camera* cam)
		{
			memcpy(planes, cam->frustumPlanes, sizeof(glm::vec4) * 6);
			
			rw::tlas::beginUpdate();

			farPlane = cam->farPlane;
			nearPlane = cam->nearPlane;

			PROFILE_FUNCTION();
			runRenderDevice();
			float view[16], proj[16];
			// View Matrix
			Matrix inv;
			Matrix::invert(&inv, cam->getFrame()->getLTM());
			cameraPosLast = cameraPos;
			cameraPos = cam->getFrame()->getLTM()->pos;

			cameraUniform->setData(&cameraPos);
			// Since we're looking into positive Z,
			// flip X to ge a left handed view space.
			view[0] = -inv.right.x;
			view[1] = inv.right.y;
			view[2] = inv.right.z;
			view[3] = 0.0f;
			view[4] = -inv.up.x;
			view[5] = inv.up.y;
			view[6] = inv.up.z;
			view[7] = 0.0f;
			view[8] = -inv.at.x;
			view[9] = inv.at.y;
			view[10] = inv.at.z;
			view[11] = 0.0f;
			view[12] = -inv.pos.x;
			view[13] = inv.pos.y;
			view[14] = inv.pos.z;
			view[15] = 1.0f;
			memcpy(&cam->devView, &view, sizeof(RawMatrix));
			setViewMatrix(view);

			// Projection Matrix
			float32 invwx = 1.0f / cam->viewWindow.x;
			float32 invwy = 1.0f / cam->viewWindow.y;
			float32 invz = 1.0f / (cam->farPlane - cam->nearPlane);

			/**
			 * [][][][]
			 * [][][][]
			 * [][][2,2][]
			 * [][][][]
			 */

			proj[0] = invwx;
			proj[1] = 0.0f;
			proj[2] = 0.0f;
			proj[3] = 0.0f;

			proj[4] = 0.0f;
			proj[5] = invwy;
			proj[6] = 0.0f;
			proj[7] = 0.0f;

			proj[8] = cam->viewOffset.x * invwx;
			proj[9] = cam->viewOffset.y * invwy;
			proj[12] = -proj[8];
			proj[13] = -proj[9];
			if (cam->projection == Camera::PERSPECTIVE) {
				// proj[10] = (cam->farPlane + cam->nearPlane) * invz;
				proj[10] = cam->farPlane * invz;
				proj[11] = 1.0f;

				// proj[14] = -2.0f * cam->nearPlane * cam->farPlane * invz;
				proj[14] = -cam->nearPlane * cam->farPlane * invz;
				proj[15] = 0.0f;
			}
			else {
				proj[10] = 2.0f * invz;
				proj[11] = 0.0f;

				proj[14] = -(cam->farPlane + cam->nearPlane) * invz;
				proj[15] = 1.0f;
			}

			memcpy(&cam->devProj, &proj, sizeof(RawMatrix));
			setProjectionMatrix(proj);

			auto& rawProj = reinterpret_cast<RawMatrix&>(proj);
			auto& rawView = reinterpret_cast<RawMatrix&>(view);

			memcpy(pushConsts.projViewPrev, pushConsts.projView, sizeof(RawMatrix));

			RawMatrix::mult(
				reinterpret_cast<RawMatrix*>(pushConsts.projView),
				&rawView,
				&rawProj);

			if (rwStateCache.fogStart != cam->fogPlane)
			{
				rwStateCache.fogStart = cam->fogPlane;
				uniformStateDirty[RWGL_FOGSTART] = true;
				stateDirty = 1;
			}

			if (rwStateCache.fogEnd != cam->farPlane)
			{
				rwStateCache.fogEnd = cam->farPlane;
				uniformStateDirty[RWGL_FOGEND] = true;
				stateDirty = 1;
			}

			setFrameBuffer(cam);

			setViewport(cam->frameBuffer);
		}

		static void endUpdate(Camera* cam)
		{
			endLastPipeline();

#ifdef EMBEDDED_IMGUI
			maple::ImGuiWinManager::render();
#endif
		}

		void runRenderDevice()
		{
			PROFILE_FUNCTION();
			if (!executeOnceFlag) {
				maple::RenderDevice::get()->begin();
#ifdef EMBEDDED_IMGUI
				imRender->newFrame();
#endif // EMBEDDED_IMGUI
				executeOnceFlag = true;
				int32_t index = maple::GraphicsContext::get()->getSwapChain()->getCurrentBufferIndex();
				commonSet->setStorageBuffer("ObjectBuffer", ssboLighting[index]);
				commonSet->setBuffer("CameraUniform", cameraUniform);
				commonSet->update(maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer());
			}
		}

		static void clearCamera(Camera* cam, RGBA* col, uint32 mode)
		{
			PROFILE_FUNCTION();
			runRenderDevice();
			VulkanRaster* natras = GET_VULKAN_RASTEREXT(cam->zBuffer);
			VulkanRaster* natras2 = GET_VULKAN_RASTEREXT(cam->frameBuffer);

			auto cmdBuffer = maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer();

			MAPLE_ASSERT(cmdBuffer != nullptr, "CMD Buffer should not be null");

			bool setScissor = cam->frameBuffer != cam->frameBuffer->parent;
			if (setScissor) {
				auto pipeline = getPipeline(maple::DrawType::Triangle, currentShader->shaderId);

				pipeline->bind(cmdBuffer);

				Rect r = getFramebufferRect(cam->frameBuffer);
				if (mode & Camera::CLEARIMAGE)
				{
					auto target = getTexture(natras2->textureId);

					if (target == nullptr) { target = maple::GraphicsContext::get()->getSwapChain()->getCurrentImage(); }

					if (target != nullptr) {
						cmdBuffer->clearAttachments(target,
							{ col->red / 255.f, col->green / 255.f, col->blue / 255.f, col->alpha / 255.f },
							{ r.x, r.y, r.w, r.h });


						cmdBuffer->clearAttachments(maple::gbuffer::getGBufferColor(),
							{ col->red / 255.f, col->green / 255.f, col->blue / 255.f, col->alpha / 255.f },
							{ r.x, r.y, r.w, r.h });
					}
				}

				if (mode & Camera::CLEARZ) {
					if (getTexture(natras->textureId) != nullptr)
						cmdBuffer->clearAttachments(getTexture(natras->textureId), { 1, 1, 1, 1 }, { r.x, r.y, r.w, r.h });
#ifdef ENABLE_GBUFFER
					else
						cmdBuffer->clearAttachments(maple::gbuffer::getDepth(), { 1, 1, 1, 1 }, { r.x, r.y, r.w, r.h });

					maple::gbuffer::clear(cmdBuffer, { 0,0,0,0 }, { r.x, r.y, r.w, r.h });
					maple::reflection::clear(cmdBuffer);
					maple::shadow::clear(cmdBuffer);
#endif // ENABLE_GBUFFER
				}

				pipeline->end(cmdBuffer);
			}
			else
			{
				if (mode & Camera::CLEARIMAGE)
				{
					auto target = getTexture(natras2->textureId);

					if (target == nullptr) { target = maple::GraphicsContext::get()->getSwapChain()->getCurrentImage(); }

					if (target != nullptr) {
						maple::RenderDevice::get()->clearRenderTarget(
							target, cmdBuffer, { col->red / 255.f, col->green / 255.f, col->blue / 255.f, col->alpha / 255.f });
					}
				}

				if (mode & Camera::CLEARZ)
				{
					if (natras->textureId != -1)
						maple::RenderDevice::get()->clearRenderTarget(getTexture(natras->textureId), cmdBuffer, { 1, 1, 1, 1 });
#ifdef ENABLE_GBUFFER
					else
						maple::RenderDevice::get()->clearRenderTarget(maple::gbuffer::getDepth(), cmdBuffer, { 1, 1, 1, 1 });

					maple::gbuffer::clear(cmdBuffer);
					maple::reflection::clear(cmdBuffer);
					maple::shadow::clear(cmdBuffer);
#endif // ENABLE_GBUFFER
				}

				if (mode & Camera::CLEARSTENCIL) {}
			}
		}

		static maple::TweakBool EnablePathTrace = { "PathTrace:Enable", false };

		static void showRaster(Raster* raster, uint32 flags)
		{
			PROFILE_FUNCTION();
			auto bufferIdx = maple::GraphicsContext::get()->getSwapChain()->getCurrentBufferIndex();
			auto cmd = maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer();
#ifdef ENABLE_RAYTRACING
			maple::debug_utils::cmdBeginLabel("Raytracing Stuffs...");

			rw::tlas::execute(cmd);
			maple::Texture::Ptr reflection;
			maple::Texture::Ptr shadow;
			maple::Texture::Ptr indirect;
			maple::Texture::Ptr pathTrace;
			RawMatrix viewProjInv;
			RawMatrix::invert(&viewProjInv, reinterpret_cast<RawMatrix*>(pushConsts.projView));

			auto& vertexBuffers = rw::tlas::getVertexBuffers();
			auto& indexBuffers = rw::tlas::getIndexBuffers();
			auto topLevel = rw::tlas::getTopLevel();

			raytracingPrepare(vertexBuffers, indexBuffers, 
				ssboLighting[bufferIdx], 
				topLevel, 
				getTextures(), 
				skyBox,
				cmd);

			if (vertexBuffers.size() > 0)
			{
				rw::RGBAf ambientColor = {};
				FORLIST(lnk, ((World*)engine->currentWorld)->globalLights) {
					Light* l = Light::fromWorld(lnk);
					if (l->getType() == Light::AMBIENT) {
						ambientColor = l->color;
						break;
					}
				}

				auto deltaCameraPos = sub(cameraPos, cameraPosLast);

				if (EnablePathTrace.var) 
				{
					pathTrace = maple::path_trace::render(
						reinterpret_cast<glm::vec4&>(ambientColor),
						reinterpret_cast<glm::vec3&>(cameraPos),
						reinterpret_cast<glm::mat4&>(viewProjInv), 
						cmd, deltaCameraPos.x > 0 || deltaCameraPos.y > 0 || deltaCameraPos.z > 0);
				}
				else 
				{
					indirect = maple::ddgi::render(cmd,
						reinterpret_cast<glm::vec3&>(cameraPos),
						reinterpret_cast<glm::mat4&>(viewProjInv),
						planes,
						reinterpret_cast<glm::vec4&>(ambientColor)
					);

					shadow = maple::shadow::render(
						vertexBuffers, indexBuffers, ssboLighting[bufferIdx], topLevel,
						vkGlobals.currentDepth, getTextures(),
						reinterpret_cast<maple::vec3&>(cameraPos),
						reinterpret_cast<maple::mat4&>(viewProjInv),
						farPlane,
						nearPlane,
						cmd);

					reflection = maple::reflection::render(
						reinterpret_cast<maple::vec4&>(ambientColor),
						reinterpret_cast<maple::vec3&>(cameraPos),
						reinterpret_cast<maple::vec3&>(deltaCameraPos),
						reinterpret_cast<maple::mat4&>(viewProjInv),
						reinterpret_cast<maple::mat4&>(pushConsts.projViewPrev),
						cmd
					);
				}
			}

#endif // ENABLE_RAYTRACING
			maple::ddgi::renderDebugProbe(
				maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer(),
				reinterpret_cast<glm::vec3&>(cameraPos),
				*reinterpret_cast<glm::mat4*>(pushConsts.projView)
			);
#ifdef ENABLE_GBUFFER
			//execute deferred shading... maybe..
			maple::debug_utils::cmdEndLabel();
			if (vkGlobals.colorTarget != nullptr)
			{
				maple::gbuffer::render(
					cameraUniform,
					vkGlobals.colorTarget,
					vkGlobals.currentDepth,
					shadow,
					reflection,
					indirect,
					pathTrace,
					ssboLighting[bufferIdx],
					reinterpret_cast<maple::mat4&>(viewProjInv),
					cmd
				);
			}
#endif

#ifdef EMBEDDED_IMGUI
			imRender->render(nullptr);
#endif // EMBEDDED_IMGUI
			maple::RenderDevice::get()->presentInternal();
			executeOnceFlag = false;
			vulkanStats.numOfObjects = 0;
		}

		static bool32 rasterRenderFast(Raster* raster, int32 x, int32 y)
		{
			PROFILE_FUNCTION();
			endLastPipeline();
			Raster* src = raster;
			Raster* dst = Raster::getCurrentContext();
			VulkanRaster* natdst = PLUGINOFFSET(VulkanRaster, dst, nativeRasterOffset);
			VulkanRaster* natsrc = PLUGINOFFSET(VulkanRaster, src, nativeRasterOffset);
			auto cmdBuffer = maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer();

			switch (dst->type) {
			case Raster::NORMAL:
			case Raster::TEXTURE:
			case Raster::CAMERATEXTURE:
				switch (src->type) {
				case Raster::CAMERA:

					auto texture = natsrc->textureId == -1 ?
#ifdef ENABLE_GBUFFER
						maple::gbuffer::getGBufferColor()
#else

						maple::GraphicsContext::get()->getSwapChain()->getCurrentImage()
#endif
						: getTexture(natsrc->textureId);

					maple::Texture2D::copy(std::static_pointer_cast<maple::Texture2D>(texture),
						std::static_pointer_cast<maple::Texture2D>(getTexture(natdst->textureId)), cmdBuffer,
						x,
						(dst->height - src->height) - y
						, 0, 0, src->width, src->height);
					return 1;
				}
				break;
			}
			return 0;
		}

// =====================================================
// SDL2-specific device functions
// =====================================================
#ifdef LIBRW_SDL2
		static void addVideoMode(int displayIndex, int modeIndex)
		{
			int i;
			SDL_DisplayMode mode;

			SDL_GetDisplayMode(displayIndex, modeIndex, &mode);

			for(i = 1; i < vkGlobals.numModes; i++){
				if(vkGlobals.modes[i].mode.w == mode.w &&
				   vkGlobals.modes[i].mode.h == mode.h &&
				   vkGlobals.modes[i].mode.format == mode.format){
					if(mode.refresh_rate > vkGlobals.modes[i].mode.refresh_rate)
						vkGlobals.modes[i].mode.refresh_rate = mode.refresh_rate;
					return;
				}
			}
			vkGlobals.modes[vkGlobals.numModes].mode = mode;
			vkGlobals.modes[vkGlobals.numModes].flags = VIDEOMODEEXCLUSIVE;
			vkGlobals.numModes++;
		}

		static void makeVideoModeList(int displayIndex)
		{
			int i, num, depth;
			num = SDL_GetNumDisplayModes(displayIndex);
			rwFree(vkGlobals.modes);
			vkGlobals.modes = rwNewT(DisplayMode, num+1, ID_DRIVER | MEMDUR_EVENT);
			SDL_GetCurrentDisplayMode(displayIndex, &vkGlobals.modes[0].mode);
			vkGlobals.modes[0].flags = 0;
			vkGlobals.numModes = 1;
			for(i = 0; i < num; i++)
				addVideoMode(displayIndex, i);
			for(i = 0; i < vkGlobals.numModes; i++){
				depth = SDL_BITSPERPIXEL(vkGlobals.modes[i].mode.format);
				for(vkGlobals.modes[i].depth = 1; vkGlobals.modes[i].depth < depth; vkGlobals.modes[i].depth <<= 1);
			}
		}

		static int openSDL2(EngineOpenParams *openparams)
		{
			vkGlobals.winWidth = openparams->width;
			vkGlobals.winHeight = openparams->height;
			vkGlobals.winTitle = openparams->windowtitle;
			vkGlobals.pWindow = openparams->window;
			if(SDL_InitSubSystem(SDL_INIT_VIDEO)){
				RWERROR((ERR_GENERAL, SDL_GetError()));
				return 0;
			}
			vkGlobals.currentDisplay = 0;
			vkGlobals.numDisplays = SDL_GetNumVideoDisplays();
			makeVideoModeList(vkGlobals.currentDisplay);
			return 1;
		}

		static int closeSDL2(void) { SDL_QuitSubSystem(SDL_INIT_VIDEO); return 1; }

		static int startSDL2(void)
		{
			SDL_Window *win;
			DisplayMode *mode;
			maple::ShaderCompiler::init();
			mode = &vkGlobals.modes[vkGlobals.currentMode];
			if(mode->flags & VIDEOMODEEXCLUSIVE) {
				win = SDL_CreateWindow(vkGlobals.winTitle.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, mode->mode.w, mode->mode.h, SDL_WINDOW_VULKAN | SDL_WINDOW_FULLSCREEN);
				vkGlobals.winWidth = mode->mode.w;
				vkGlobals.winHeight = mode->mode.h;
			} else {
				win = SDL_CreateWindow(vkGlobals.winTitle.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, vkGlobals.winWidth, vkGlobals.winHeight, SDL_WINDOW_VULKAN);
			}
			if(win == nil){ RWERROR((ERR_GENERAL, SDL_GetError())); return 0; }
			vkGlobals.window = win;
			*vkGlobals.pWindow = win;
			vkGlobals.presentWidth = 0;
			vkGlobals.presentHeight = 0;
			vkGlobals.presentOffX = 0;
			vkGlobals.presentOffY = 0;
			return 1;
		}

		static int stopSDL2(void) { maple::ShaderCompiler::finalize(); SDL_DestroyWindow(vkGlobals.window); return 1; }

		static int deviceSystemSDL2(DeviceReq req, void *arg, int32 n)
		{
			VideoMode *rwmode;
			switch(req){
			case DEVICEOPEN: return openSDL2((EngineOpenParams*)arg);
			case DEVICECLOSE: return closeSDL2();
			case DEVICEINIT: return startSDL2() && initVulkan();
			case DEVICETERM: return termVulkan() && stopSDL2();
			case DEVICEFINALIZE: return finalizeVulkan();
			case DEVICEGETNUMSUBSYSTEMS: return vkGlobals.numDisplays;
			case DEVICEGETCURRENTSUBSYSTEM: return vkGlobals.currentDisplay;
			case DEVICESETSUBSYSTEM:
				if (n >= SDL_GetNumVideoDisplays()) return 0;
				vkGlobals.currentDisplay = n;
				return 1;
			case DEVICEGETSUBSSYSTEMINFO: {
				const char *display_name = SDL_GetDisplayName(n);
				if (display_name == nil) return 0;
				strncpy(((SubSystemInfo*)arg)->name, display_name, sizeof(SubSystemInfo::name));
				return 1;
			}
			case DEVICEGETNUMVIDEOMODES: return vkGlobals.numModes;
			case DEVICEGETCURRENTVIDEOMODE: return vkGlobals.currentMode;
			case DEVICESETVIDEOMODE:
				if(n >= vkGlobals.numModes) return 0;
				vkGlobals.currentMode = n;
				return 1;
			case DEVICEGETVIDEOMODEINFO:
				rwmode = (VideoMode*)arg;
				rwmode->width = vkGlobals.modes[n].mode.w;
				rwmode->height = vkGlobals.modes[n].mode.h;
				rwmode->depth = vkGlobals.modes[n].depth;
				rwmode->flags = vkGlobals.modes[n].flags;
				return 1;
			case DEVICEGETMAXMULTISAMPLINGLEVELS: return maple::GraphicsContext::get()->getCaps().maxSamples;
			case DEVICEGETMULTISAMPLINGLEVELS: return vkGlobals.numSamples == 0 ? 1 : vkGlobals.numSamples;
			case DEVICESETMULTISAMPLINGLEVELS: vkGlobals.numSamples = (uint32)n; return 1;
			default: assert(0 && "not implemented"); return 0;
			}
			return 1;
		}
#endif // LIBRW_SDL2

// =====================================================
// SDL3-specific device functions
// =====================================================
#ifdef LIBRW_SDL3
		static void addVideoMode(const SDL_DisplayMode *mode)
		{
			int i;
			for(i = 1; i < vkGlobals.numModes; i++){
				if(vkGlobals.modes[i].mode.w == mode->w &&
				   vkGlobals.modes[i].mode.h == mode->h &&
				   vkGlobals.modes[i].mode.format == mode->format){
					if(mode->refresh_rate > vkGlobals.modes[i].mode.refresh_rate)
						vkGlobals.modes[i].mode.refresh_rate = mode->refresh_rate;
					return;
				}
			}
			vkGlobals.modes[vkGlobals.numModes].mode = *mode;
			vkGlobals.modes[vkGlobals.numModes].flags = VIDEOMODEEXCLUSIVE;
			vkGlobals.numModes++;
		}

		static void makeVideoModeList(SDL_DisplayID displayIndex, SDL_DisplayID *displays)
		{
			int i, num, depth;
			const SDL_DisplayMode *currentMode;
			SDL_DisplayMode **modes;
			currentMode = SDL_GetCurrentDisplayMode(displays[displayIndex]);
			modes = SDL_GetFullscreenDisplayModes(displayIndex, &num);
			rwFree(vkGlobals.modes);
			vkGlobals.modes = rwNewT(DisplayMode, num+(currentMode != NULL ? 1 : 0)+1, ID_DRIVER | MEMDUR_EVENT);
			vkGlobals.numModes = 0;
			if (currentMode) {
				vkGlobals.modes[0].mode = *currentMode;
#if defined(__ANDROID__) || defined(ANDROID)
				vkGlobals.modes[0].flags = VIDEOMODEEXCLUSIVE;
#else
				vkGlobals.modes[0].flags = 0;
#endif
				vkGlobals.numModes = 1;
			}
			for(i = 0; i < num; i++) addVideoMode(modes[i]);
			for(i = 0; i < vkGlobals.numModes; i++){
				depth = SDL_BITSPERPIXEL(vkGlobals.modes[i].mode.format);
				for(vkGlobals.modes[i].depth = 1; vkGlobals.modes[i].depth < depth; vkGlobals.modes[i].depth <<= 1);
			}
			SDL_free(modes);
		}

		static int openSDL3(EngineOpenParams *openparams)
		{
			vkGlobals.winWidth = openparams->width;
			vkGlobals.winHeight = openparams->height;
			vkGlobals.winTitle = openparams->windowtitle;
			vkGlobals.pWindow = openparams->window;
			if (!SDL_InitSubSystem(SDL_INIT_VIDEO)){
				RWERROR((ERR_GENERAL, SDL_GetError()));
				return 0;
			}
			vkGlobals.currentDisplay = 0;
			SDL_DisplayID *displays = SDL_GetDisplays(&vkGlobals.numDisplays);
			if (vkGlobals.currentDisplay >= vkGlobals.numDisplays) {
				RWERROR((ERR_GENERAL, SDL_GetError()));
				return 0;
			}
			makeVideoModeList(vkGlobals.currentDisplay, displays);
			SDL_free(displays);
			return 1;
		}

		static int closeSDL3(void) { SDL_QuitSubSystem(SDL_INIT_VIDEO); return 1; }

		static int startSDL3(void)
		{
			SDL_Window *win;
			DisplayMode *mode;
			maple::ShaderCompiler::init();
			mode = &vkGlobals.modes[vkGlobals.currentMode];
			SDL_WindowFlags flags = SDL_WINDOW_VULKAN;
			if(mode->flags & VIDEOMODEEXCLUSIVE) {
				flags |= SDL_WINDOW_FULLSCREEN;
				vkGlobals.winWidth = mode->mode.w;
				vkGlobals.winHeight = mode->mode.h;
			}
			win = SDL_CreateWindow(vkGlobals.winTitle.c_str(), vkGlobals.winWidth, vkGlobals.winHeight, flags);
			if(win == nil){ RWERROR((ERR_GENERAL, SDL_GetError())); return 0; }
			vkGlobals.window = win;
			*vkGlobals.pWindow = win;
			vkGlobals.presentWidth = 0;
			vkGlobals.presentHeight = 0;
			vkGlobals.presentOffX = 0;
			vkGlobals.presentOffY = 0;
			return 1;
		}

		static int stopSDL3(void) { maple::ShaderCompiler::finalize(); SDL_DestroyWindow(vkGlobals.window); return 1; }

		static int deviceSystemSDL3(DeviceReq req, void *arg, int32 n)
		{
			VideoMode *rwmode;
			switch(req){
			case DEVICEOPEN: return openSDL3((EngineOpenParams*)arg);
			case DEVICECLOSE: return closeSDL3();
			case DEVICEINIT: return startSDL3() && initVulkan();
			case DEVICETERM: return termVulkan() && stopSDL3();
			case DEVICEFINALIZE: return finalizeVulkan();
			case DEVICEGETNUMSUBSYSTEMS: return vkGlobals.numDisplays;
			case DEVICEGETCURRENTSUBSYSTEM: return vkGlobals.currentDisplay;
			case DEVICESETSUBSYSTEM:
				if (n >= vkGlobals.numDisplays) return 0;
				vkGlobals.currentDisplay = n;
				return 1;
			case DEVICEGETSUBSSYSTEMINFO: {
				const char *display_name = SDL_GetDisplayName(n);
				if (display_name == nil) return 0;
				strncpy(((SubSystemInfo*)arg)->name, display_name, sizeof(SubSystemInfo::name));
				return 1;
			}
			case DEVICEGETNUMVIDEOMODES: return vkGlobals.numModes;
			case DEVICEGETCURRENTVIDEOMODE: return vkGlobals.currentMode;
			case DEVICESETVIDEOMODE:
				if(n >= vkGlobals.numModes) return 0;
				vkGlobals.currentMode = n;
				return 1;
			case DEVICEGETVIDEOMODEINFO:
				rwmode = (VideoMode*)arg;
				rwmode->width = vkGlobals.modes[n].mode.w;
				rwmode->height = vkGlobals.modes[n].mode.h;
				rwmode->depth = vkGlobals.modes[n].depth;
				rwmode->flags = vkGlobals.modes[n].flags;
				return 1;
			case DEVICEGETMAXMULTISAMPLINGLEVELS: return maple::GraphicsContext::get()->getCaps().maxSamples;
			case DEVICEGETMULTISAMPLINGLEVELS: return vkGlobals.numSamples == 0 ? 1 : vkGlobals.numSamples;
			case DEVICESETMULTISAMPLINGLEVELS: vkGlobals.numSamples = (uint32)n; return 1;
			default: assert(0 && "not implemented"); return 0;
			}
			return 1;
		}
#endif // LIBRW_SDL3

// =====================================================
// GLFW-specific device functions
// =====================================================
#ifdef LIBRW_GLFW
		static void addVideoMode(const GLFWvidmode* mode)
		{
			int i;

			for (i = 1; i < vkGlobals.numModes; i++) {
				if (vkGlobals.modes[i].mode.width == mode->width && vkGlobals.modes[i].mode.height == mode->height &&
					vkGlobals.modes[i].mode.redBits == mode->redBits && vkGlobals.modes[i].mode.greenBits == mode->greenBits &&
					vkGlobals.modes[i].mode.blueBits == mode->blueBits) {
					// had this mode already, remember highest refresh rate
					if (mode->refreshRate > vkGlobals.modes[i].mode.refreshRate) vkGlobals.modes[i].mode.refreshRate = mode->refreshRate;
					return;
				}
			}

			// none found, add
			vkGlobals.modes[vkGlobals.numModes].mode = *mode;
			vkGlobals.modes[vkGlobals.numModes].flags = VIDEOMODEEXCLUSIVE;
			vkGlobals.numModes++;
		}

		static void makeVideoModeList(void)
		{
			int i, num;
			const GLFWvidmode* modes;

			modes = glfwGetVideoModes(vkGlobals.monitor, &num);
			rwFree(vkGlobals.modes);
			vkGlobals.modes = rwNewT(DisplayMode, num + 1, ID_DRIVER | MEMDUR_EVENT);

			vkGlobals.modes[0].mode = *glfwGetVideoMode(vkGlobals.monitor);
			vkGlobals.modes[0].flags = 0;
			vkGlobals.numModes = 1;

			for (i = 0; i < num; i++) addVideoMode(&modes[i]);

			for (i = 0; i < vkGlobals.numModes; i++) {
				num = vkGlobals.modes[i].mode.redBits + vkGlobals.modes[i].mode.greenBits + vkGlobals.modes[i].mode.blueBits;
				// set depth to power of two
				for (vkGlobals.modes[i].depth = 1; vkGlobals.modes[i].depth < num; vkGlobals.modes[i].depth <<= 1)
					;
			}
		}

		static int openGLFW(EngineOpenParams* openparams)
		{
			vkGlobals.winWidth = openparams->width;
			vkGlobals.winHeight = openparams->height;
			vkGlobals.winTitle = openparams->windowtitle + std::string(" (Vulkan and Raytracing-ON)");
			vkGlobals.pWindow = openparams->window;

			/* Init GLFW */
			if (!glfwInit()) {
				RWERROR((ERR_GENERAL, "glfwInit() failed"));
				return 0;
			}

			vkGlobals.monitor = glfwGetMonitors(&vkGlobals.numMonitors)[0];

			makeVideoModeList();

			return 1;
		}

		static int closeGLFW(void)
		{
			glfwTerminate();
			return 1;
		}

		static void glfwerr(int error, const char* desc) { fprintf(stderr, "GLFW Error: %s\n", desc); }

		static int startGLFW(void)
		{
			GLFWwindow* win;
			DisplayMode* mode;

			maple::ShaderCompiler::init();

			mode = &vkGlobals.modes[vkGlobals.currentMode];

			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

			glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

			glfwSetErrorCallback(glfwerr);
			glfwWindowHint(GLFW_RED_BITS, mode->mode.redBits);
			glfwWindowHint(GLFW_GREEN_BITS, mode->mode.greenBits);
			glfwWindowHint(GLFW_BLUE_BITS, mode->mode.blueBits);
			glfwWindowHint(GLFW_REFRESH_RATE, mode->mode.refreshRate);

			// GLX will round up to 2x or 4x if you ask for multisampling on with 1 sample
			// So only apply the SAMPLES hint if we actually want multisampling
			if (vkGlobals.numSamples > 1) glfwWindowHint(GLFW_SAMPLES, vkGlobals.numSamples);

			vkGlobals.winWidth = 1920;
			vkGlobals.winHeight = 1080;

			// if(mode->flags & VIDEOMODEEXCLUSIVE) {
			//	win = glfwCreateWindow(mode->mode.width, mode->mode.height, vkGlobals.winTitle.c_str(), nil, nil); // vkGlobals.monitor
			//	vkGlobals.winWidth = mode->mode.width;
			//	vkGlobals.winHeight = mode->mode.height;
			//} else
			win = glfwCreateWindow(vkGlobals.winWidth, vkGlobals.winHeight, vkGlobals.winTitle.c_str(), nil, nil);

			if (win == nil) {
				RWERROR((ERR_GENERAL, "glfwCreateWindow() failed"));
				return 0;
			}

			glfwMakeContextCurrent(win);

			vkGlobals.window = win;
			*vkGlobals.pWindow = win;
			vkGlobals.presentWidth = 0;
			vkGlobals.presentHeight = 0;
			vkGlobals.presentOffX = 0;
			vkGlobals.presentOffY = 0;
			return 1;
		}

		static int stopGLFW(void)
		{
			maple::ShaderCompiler::finalize();
			glfwDestroyWindow(vkGlobals.window);
			return 1;
		}

		static int initVulkan(void)
		{
			RawMatrix::setIdentity(reinterpret_cast<RawMatrix*>(pushConsts.projView));
			registerVkCache();
			resetRenderState();
			maple::Console::init();
			maple::GraphicsContext::get()->init(vkGlobals.window, vkGlobals.winWidth, vkGlobals.winHeight);
			maple::RenderDevice::get()->init();

			skyBox = maple::TextureCube::create(512);
			skyBoxBuffer = maple::Texture2D::create(512, 512, nullptr);

#ifdef ENABLE_RAYTRACING
			tlas::registerTLASPlugin();
			maple::reflection::init(vkGlobals.winWidth, vkGlobals.winHeight);
			maple::shadow::init(vkGlobals.winWidth, vkGlobals.winHeight);
			maple::ddgi::init(vkGlobals.winWidth, vkGlobals.winHeight);
			maple::path_trace::init(vkGlobals.winWidth, vkGlobals.winHeight);
#endif // ENABLE_RAYTRACING

			imRender = maple::ImGuiRenderer::create(vkGlobals.winWidth, vkGlobals.winHeight, false);
			imRender->init();

#ifdef EMBEDDED_IMGUI
			maple::ImGuiWinManager::add(std::make_shared<maple::TweakWin>());
			maple::ImGuiWinManager::add(std::make_shared<maple::ObjectsWin>());
#endif // EMBEDDED_IMGUI

#include "vkshaders/default.shader.h"
#include "vkshaders/common.shader.h"

			const std::string common = { (char*)__common_shader, __common_shader_len };
			const std::string defaultTxt = common + std::string{ (char*)__default_shader, __default_shader_len };

#ifdef ENABLE_GBUFFER
#define gbuffer_define "#define ENABLE_GBUFFER\n"
#else
#define gbuffer_define ""
#endif // ENABLE_GBUFFER

			defaultShader = Shader::create(defaultTxt, gbuffer_define "#define VERTEX_SHADER\n", defaultTxt, gbuffer_define "#define FRAGMENT_SHADER\n");

			defaultShader_noAT =
				Shader::create(defaultTxt, gbuffer_define "#define VERTEX_SHADER\n", defaultTxt, gbuffer_define "#define FRAGMENT_SHADER\n #define NO_ALPHATEST\n");

			defaultShader_fullLight =
				Shader::create(defaultTxt, gbuffer_define "#define VERTEX_SHADER\n#define DIRECTIONALS\n#define POINTLIGHTS\n#define SPOTLIGHTS\n", defaultTxt,
					gbuffer_define "#define FRAGMENT_SHADER\n#define DIRECTIONALS\n#define POINTLIGHTS\n#define SPOTLIGHTS\n");
			defaultShader_fullLight_noAT =
				Shader::create(defaultTxt, gbuffer_define "#define VERTEX_SHADER\n#define DIRECTIONALS\n#define POINTLIGHTS\n#define SPOTLIGHTS\n", defaultTxt,
					gbuffer_define "#define FRAGMENT_SHADER\n#define NO_ALPHATEST\n#define DIRECTIONALS\n#define POINTLIGHTS\n#define SPOTLIGHTS\n");

#ifdef ENABLE_GBUFFER
			maple::gbuffer::init();
#endif // ENABLE_GBUFFER

			commonSet = maple::DescriptorSet::create({ 1, getShader(defaultShader->shaderId).get() });
			commonSet->setName("CommonSet for Object Buffer");
			using namespace maple;

			for (auto i = 0; i < 3; i++)
			{
				ssboLighting[i] = StorageBuffer::create(MAX_OBJECT_SIZE * sizeof(UniformObject), nullptr, BufferOptions{ false, MemoryUsage::MEMORY_USAGE_CPU_TO_GPU });
			}

			if (cameraUniform == nullptr)
				cameraUniform = maple::UniformBuffer::create(sizeof(maple::vec4), nullptr);

			openIm2D(vkGlobals.winWidth, vkGlobals.winHeight);
			openIm3D();
			return 1;
		}

		static int termVulkan(void)
		{
			closeIm3D();
			closeIm2D();

			defaultShader->destroy();
			defaultShader = nil;
			defaultShader_noAT->destroy();
			defaultShader_noAT = nil;
			defaultShader_fullLight->destroy();
			defaultShader_fullLight = nil;
			defaultShader_fullLight_noAT->destroy();
			defaultShader_fullLight_noAT = nil;
			return 1;
		}

		static int finalizeVulkan(void) { return 1; }

		static int deviceSystemGLFW(DeviceReq req, void* arg, int32 n)
		{
			GLFWmonitor** monitors;
			VideoMode* rwmode;

			switch (req) {
			case DEVICEOPEN: return openGLFW((EngineOpenParams*)arg);
			case DEVICECLOSE: return closeGLFW();

			case DEVICEINIT: return startGLFW() && initVulkan();
			case DEVICETERM: return termVulkan() && stopGLFW();

			case DEVICEFINALIZE: return finalizeVulkan();
			case DEVICEGETNUMSUBSYSTEMS: return vkGlobals.numMonitors;
			case DEVICEGETCURRENTSUBSYSTEM: return vkGlobals.currentMonitor;
			case DEVICESETSUBSYSTEM:
				monitors = glfwGetMonitors(&vkGlobals.numMonitors);
				if (n >= vkGlobals.numMonitors) return 0;
				vkGlobals.currentMonitor = n;
				vkGlobals.monitor = monitors[vkGlobals.currentMonitor];
				return 1;

			case DEVICEGETSUBSSYSTEMINFO:
				monitors = glfwGetMonitors(&vkGlobals.numMonitors);
				if (n >= vkGlobals.numMonitors) return 0;
				strncpy(((SubSystemInfo*)arg)->name, glfwGetMonitorName(monitors[n]), sizeof(SubSystemInfo::name));
				return 1;
			case DEVICEGETNUMVIDEOMODES: return vkGlobals.numModes;
			case DEVICEGETCURRENTVIDEOMODE: return vkGlobals.currentMode;

			case DEVICESETVIDEOMODE:
				if (n >= vkGlobals.numModes) return 0;
				vkGlobals.currentMode = n;
				return 1;
			case DEVICEGETVIDEOMODEINFO:
				rwmode = (VideoMode*)arg;
				rwmode->width = vkGlobals.modes[n].mode.width;
				rwmode->height = vkGlobals.modes[n].mode.height;
				rwmode->depth = vkGlobals.modes[n].depth;
				rwmode->flags = vkGlobals.modes[n].flags;
				return 1;
			case DEVICEGETMAXMULTISAMPLINGLEVELS: {
				return maple::GraphicsContext::get()->getCaps().maxSamples;
			}
			case DEVICEGETMULTISAMPLINGLEVELS:
				if (vkGlobals.numSamples == 0) return 1;
				return vkGlobals.numSamples;
			case DEVICESETMULTISAMPLINGLEVELS: vkGlobals.numSamples = (uint32)n; return 1;
			default: assert(0 && "not implemented"); return 0;
			}
			return 1;
		}
#endif // LIBRW_GLFW

		Device renderdevice = { -1.0f,
							   1.0f,
							   vulkan::beginUpdate,
							   vulkan::endUpdate,
							   vulkan::clearCamera,
							   vulkan::showRaster,
							   vulkan::rasterRenderFast,
							   vulkan::setRenderState,
							   vulkan::getRenderState,
							   vulkan::im2DRenderLine,
							   vulkan::im2DRenderTriangle,
							   vulkan::im2DRenderPrimitive,
							   vulkan::im2DRenderIndexedPrimitive,
							   vulkan::im3DTransform,
							   vulkan::im3DRenderPrimitive,
							   vulkan::im3DRenderIndexedPrimitive,
							   vulkan::im3DEnd,
#ifdef LIBRW_SDL2
							   vulkan::deviceSystemSDL2
#elif defined(LIBRW_SDL3)
							   vulkan::deviceSystemSDL3
#elif defined(LIBRW_GLFW)
							   vulkan::deviceSystemGLFW
#else
							   nullptr // not implemented
#endif
							   };
	} // namespace vulkan
} // namespace rw

void ImGui_ImplRW_RenderDrawLists(ImDrawData*)
{
	rw::vulkan::imFlush();
	rw::vulkan::imRender->render(nullptr);
}
#endif
