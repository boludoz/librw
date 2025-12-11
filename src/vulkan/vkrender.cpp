#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../rwbase.h"

#include "../rwplg.h"

#include "../rwengine.h"

#include "../rwerror.h"

#include "../rwpipeline.h"

#include "../rwobjects.h"

#include "../rwrender.h"

#ifdef RW_VULKAN
#include "CommandBuffer.h"
#include "GraphicsContext.h"
#include "IndexBuffer.h"
#include "Pipeline.h"
#include "RenderDevice.h"
#include "SwapChain.h"
#include "Textures.h"
#include "VertexBuffer.h"
#include "UniformBuffer.h"

#ifdef ENABLE_RAYTRACING
#include "raytracing/vkRaytracing.h"
#include "AccelerationStructure.h"
#include "raytracing/vkTopLevel.h"
#endif

#include "rwvk.h"
#include "rwvkimpl.h"
#include "rwvkshader.h"
#include "vklighting.h"

namespace rw
{
	namespace vulkan
	{
#define MAX_LIGHTS

		static maple::Pipeline::Ptr currentPipeline = nullptr;

		void setPipeline(std::shared_ptr<maple::Pipeline> pipeline)
		{
			currentPipeline = pipeline;
		}

		void compareAndBind(std::shared_ptr<maple::Pipeline> pipeline)
		{
			PROFILE_FUNCTION();
			auto cmdBuffer = maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer();
			if (pipeline != currentPipeline)
			{
				if (currentPipeline != nullptr && 
					currentPipeline->getRenderPass() != pipeline->getRenderPass() &&
					pipeline->getDescription().faceId != currentPipeline->getDescription().faceId)
				{
					endLastPipeline();
				}
				currentPipeline = pipeline;
				pipeline->bind(cmdBuffer, maple::ivec4{ vkGlobals.presentOffX, vkGlobals.presentOffY, vkGlobals.presentWidth, vkGlobals.presentHeight });
			}
		}

		std::shared_ptr<maple::Pipeline> getPipeline(maple::DrawType drawType, uint32_t shaderId)
		{
			PROFILE_FUNCTION();

			int32_t faceId = (int32_t)rw::GetRenderState(rw::SKYBOX_FACE);
			maple::PipelineInfo info;
			info.shader = getShader(shaderId);
			info.drawType = drawType;
			info.faceId = faceId;

			if (faceId >= 0)
			{
				info.colorTargets[0] = skyBoxBuffer;
				info.colorTargets[1] = skyBox;
				info.depthTest = false;
			}
			else
			{
				info.depthTarget = vkGlobals.currentDepth;
				info.colorTargets[0] = vkGlobals.colorTarget;
#ifdef ENABLE_GBUFFER
				info.colorTargets[1] = vkGlobals.normalTarget;
				info.colorTargets[2] = vkGlobals.gbuffer2;
				info.colorTargets[3] = vkGlobals.gbuffer3;
				info.colorTargets[4] = vkGlobals.gbuffer4;
#endif // ENABLE_GBUFFER
				info.swapChainTarget = info.colorTargets[0] == nullptr;
			}
			initPipelineInfo(info);
			return maple::Pipeline::get(info);
		}

		static uint32_t prevIndex = -1;
		static void newFrameCheck()
		{
			PROFILE_FUNCTION();
			auto currentIndex = maple::GraphicsContext::get()->getSwapChain()->getCurrentBufferIndex();
			if (currentIndex != prevIndex)
			{ // new frame
				prevIndex = currentIndex;
			}
		}

		void endLastPipeline()
		{
			PROFILE_FUNCTION();
			auto cmdBuffer = maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer();
			if (currentPipeline != nullptr) {
				currentPipeline->end(cmdBuffer);
				if (currentPipeline->getDescription().faceId != -1)
				{
					skyBox->update(cmdBuffer, skyBoxBuffer, currentPipeline->getDescription().faceId);
				}
			}
			currentPipeline = nullptr;
		}

		void drawInst_simple(InstanceDataHeader* header, InstanceData* inst, std::shared_ptr<maple::DescriptorSet> matfx, std::shared_ptr<maple::DescriptorSet> uniform, int32_t objectId, int32_t instanceId)
		{
			PROFILE_FUNCTION();
			auto textureSet = getTextureDescriptorSet(inst->material);

			setMaterial(
				inst->material->color,
				inst->material->surfaceProps,
				inst->material->bindlessId >= getTextures().size() ? -1 : inst->material->bindlessId,
				instanceId,
				inst->offset,
				objectId
			);

			auto pipeline = getPipeline(header->primType, currentShader->shaderId);
			flushCache(pipeline->getShader(), objectId, header->meshId);
			auto cmdBuffer = maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer();
			header->vertexBufferGPU->bind(cmdBuffer, pipeline.get());
			header->indexBufferGPU->bind(cmdBuffer);
			commonSet->update(cmdBuffer);

			compareAndBind(pipeline);

			pipeline->getShader()->bindPushConstants(cmdBuffer, pipeline.get());

			maple::RenderDevice::get()->bindDescriptorSets(pipeline.get(), cmdBuffer, { textureSet, commonSet , matfx, uniform });

			pipeline->drawIndexed(cmdBuffer, inst->numIndex, 1, inst->offset, 0, 0);
		}

		// Emulate PS2 GS alpha test FB_ONLY case: failed alpha writes to frame- but not to depth buffer
		void drawInst_GSemu(InstanceDataHeader* header, InstanceData* inst, std::shared_ptr<maple::DescriptorSet> matfx, std::shared_ptr<maple::DescriptorSet> uniform, int32_t objectId, int32_t instanceId)
		{
			PROFILE_FUNCTION();
			uint32 hasAlpha;
			int alphafunc, alpharef, gsalpharef;
			int zwrite;
			hasAlpha = getAlphaBlend();
			if (hasAlpha) {
				zwrite = rw::GetRenderState(rw::ZWRITEENABLE);
				alphafunc = rw::GetRenderState(rw::ALPHATESTFUNC);
				if (zwrite) {
					alpharef = rw::GetRenderState(rw::ALPHATESTREF);
					gsalpharef = rw::GetRenderState(rw::GSALPHATESTREF);

					SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAGREATEREQUAL);
					SetRenderState(rw::ALPHATESTREF, gsalpharef);
					drawInst_simple(header, inst, matfx, uniform, objectId, instanceId);

					SetRenderState(rw::ALPHATESTFUNC, rw::ALPHALESS);
					SetRenderState(rw::ZWRITEENABLE, 0);
					drawInst_simple(header, inst, matfx, uniform, objectId, instanceId); //some issue when I use deferred shading

					SetRenderState(rw::ZWRITEENABLE, 1);
					SetRenderState(rw::ALPHATESTFUNC, alphafunc);
					SetRenderState(rw::ALPHATESTREF, alpharef);
				}
				else
				{
					SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAALWAYS);
					drawInst_simple(header, inst, matfx, uniform, objectId, instanceId);
					SetRenderState(rw::ALPHATESTFUNC, alphafunc);
				}
			}
			else
				drawInst_simple(header, inst, matfx, uniform, objectId, instanceId);
		}

		void drawInst(InstanceDataHeader* header, InstanceData* inst, std::shared_ptr<maple::DescriptorSet> matfx, std::shared_ptr<maple::DescriptorSet> uniform, int32_t objectId, int32_t instanceId)
		{
			PROFILE_FUNCTION();
			newFrameCheck();

			if (rw::GetRenderState(rw::GSALPHATEST))
				drawInst_GSemu(header, inst, matfx, uniform, objectId, instanceId);
			else
				drawInst_simple(header, inst, matfx, uniform, objectId, instanceId);

			vulkanStats.numOfObjects++;
		}

		int32 lightingCB(Atomic* atomic)
		{
			PROFILE_FUNCTION();
			WorldLights lightData{};
			std::vector<LightData> lights;
			memset(&lightData, 0, sizeof(lightData));
			lights = lighting::enumerateLights(atomic, lightData);
			return setLights(&lightData, lights);
		}

		int32 lightingCB(void)
		{
			PROFILE_FUNCTION();
			WorldLights lightData;
			Light* directionals[8];
			Light* locals[8];
			lightData.directionals = directionals;
			lightData.numDirectionals = 8;
			lightData.locals = locals;
			lightData.numLocals = 8;

			((World*)engine->currentWorld)->enumerateLights(&lightData);
			return setLights(&lightData, {});
		}

		void defaultUpdateCB(Atomic* atomic, InstanceDataHeader* header)
		{
			PROFILE_FUNCTION();
			runRenderDevice();
			newFrameCheck();
			PROFILE_FUNCTION();
			InstanceData* inst = header->inst;
			int32 n = header->numMeshes;

			while (n--) {
				//prepare descriptor first...
				auto textureSet = getTextureDescriptorSet(inst->material);
				//it should also prepare render data(ssbo or uniform), 
				//but I'm not fully sure the updated object would be rendered in the same frame...
				inst++;
			}
		}

		void defaultRenderCB(Atomic* atomic, InstanceDataHeader* header)
		{
			PROFILE_FUNCTION();

			Material* m;

			uint32 flags = atomic->geometry->flags;
			setWorldMatrix(atomic->getFrame()->getLTM());
			int32 vsBits = lightingCB(atomic);

			InstanceData* inst = header->inst;
			int32 n = header->numMeshes;

			auto ids = rw::tlas::addObject(atomic, header);
			int32_t customId = ids.first;
			int32_t instanceId = ids.second;
			while (n--) {

				m = inst->material;
				setTextureBlend(m->texture);

				rw::SetRenderState(VERTEXALPHA, inst->vertexAlpha || m->color.alpha != 0xFF);

				if ((vsBits & VSLIGHT_MASK) == 0)
				{
					if (getAlphaTest())
						defaultShader->use();
					else
						defaultShader_noAT->use();
				}
				else
				{
					if (getAlphaTest())
						defaultShader_fullLight->use();
					else
						defaultShader_fullLight_noAT->use();
				}

				drawInst(header, inst, nullptr, nullptr, customId++, instanceId);
				inst++;
			}
		}
	} // namespace vulkan
} // namespace rw

#endif
