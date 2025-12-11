//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////

#include "PathTrace.h"
#include "Textures.h"
#include "Shader.h"
#include "Pipeline.h"
#include "RenderDevice.h"
#include "RaytracingCommon.h"
#include "vkRaytracing.h"
#include "vkGBuffer.h"
#include "Tweak/Tweakable.h"
#include <glm/glm.hpp>

namespace maple
{
	namespace path_trace
	{
		static struct PathTrace
		{
			Shader::Ptr shader;
			Texture2D::Ptr outColor[2];
			DescriptorSet::Ptr descriptorSets;
			uint32_t frame = 0;
		}pathTracePass;

		static maple::TweakFloat ShadowNormal_Bias = { "PathTrace:Normal Bias", 0.1f, 0.f, 1.f };

		auto init(uint32_t width, uint32_t height) -> void
		{
			for (auto i = 0; i < 2; i++) {
				pathTracePass.outColor[i] = Texture2D::create();
				pathTracePass.outColor[i]->buildTexture(TextureFormat::RGBA16, width, height);
			}

			pathTracePass.shader = Shader::create({
				{maple::ShaderType::RayGen, "shaders/PathTrace.rgen.spv"},
				{maple::ShaderType::RayAnyHit, "shaders/PathTrace.rahit.spv"},
				{maple::ShaderType::RayCloseHit, "shaders/PathTrace.rchit.spv"},
				{maple::ShaderType::RayMiss, "shaders/PathTrace.rmiss.spv"}/*,

				{maple::ShaderType::RayAnyHit, "shaders/PathTraceShadow.rahit.spv"},
				{maple::ShaderType::RayCloseHit, "shaders/PathTraceShadow.rchit.spv"},
				{maple::ShaderType::RayMiss, "shaders/PathTraceShadow.rmiss.spv"}*/
				},
				{ {"VertexBuffer", MAX_SCENE_MESH_INSTANCE_COUNT},
				{"IndexBuffer", MAX_SCENE_MESH_INSTANCE_COUNT},
				{"uSamplers", MAX_SCENE_MATERIAL_TEXTURE_COUNT} }
			);

			pathTracePass.descriptorSets = DescriptorSet::create({ 4, pathTracePass.shader.get() });
		}

		auto render(
			const glm::vec4& ambientColor,
			const glm::vec3& cameraPos,
			const glm::mat4& viewProjInv,
			const CommandBuffer* cmd, bool refresh)->std::shared_ptr <Texture>
		{
			pathTracePass.descriptorSets->setTexture("uCurrentColor", pathTracePass.outColor[gbuffer::getPingPong()]);
			pathTracePass.descriptorSets->setTexture("uPreviousColor", pathTracePass.outColor[1 - gbuffer::getPingPong()]);
			pathTracePass.descriptorSets->update(cmd);

			PipelineInfo info;
			info.pipelineName = "PathTrace";
			info.shader = pathTracePass.shader;
			info.maxRayRecursionDepth = 8;
			auto tracePipeline = Pipeline::get(info);

			tracePipeline->bind(cmd);


			struct PushConsts
			{
				glm::mat4 invViewProj;
				glm::vec4 cameraPos;
				glm::vec4 ambientColor;
				uint32_t numFrames;
				uint32_t maxBounces;
				float accumulation;
				float shadowRayBias;
			}pushConsts;
			if (refresh) {
				pathTracePass.frame = 0;
			}
			pushConsts.maxBounces = 2;
			pushConsts.ambientColor = ambientColor;
			pushConsts.shadowRayBias = ShadowNormal_Bias;
			pushConsts.numFrames = pathTracePass.frame++;
			pushConsts.cameraPos = glm::vec4(cameraPos, 1.f);
			pushConsts.invViewProj = viewProjInv;

			info.shader->getPushConstant(0)->setData(&pushConsts);
			info.shader->bindPushConstants(cmd, tracePipeline.get());

			RenderDevice::get()->bindDescriptorSets(
				tracePipeline.get(), cmd, {
					getObjectSets(),
					getVertexSets(),
					getIndexSets(),
					getSamplerSets(),
					pathTracePass.descriptorSets
				}
			);
			tracePipeline->traceRays(cmd, pathTracePass.outColor[0]->getWidth(), pathTracePass.outColor[0]->getHeight(), 1);

			return pathTracePass.outColor[gbuffer::getPingPong()];
		}
	}
}