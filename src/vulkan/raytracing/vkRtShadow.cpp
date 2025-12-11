//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////

#include "vkRaytracing.h"
#include <vector>
#include "AccelerationStructure.h"
#include "DescriptorSet.h"
#include "DescriptorPool.h"
#include "Shader.h"
#include "Textures.h"
#include "Console.h"
#include "Pipeline.h"
#include "StorageBuffer.h"
#include "RenderDevice.h"
#include "Vulkan/VulkanDebug.h"
#include "Tweak/Tweakable.h"
#include "vkBlueNoise.h"
#include "vkGBuffer.h"
#include "RaytracingCommon.h"
#include "../gi/DDGI.h"
#include "../vklighting.h"

namespace maple::shadow
{
	namespace
	{
		//using int32_t to pack visibility
		constexpr uint32_t RAY_TRACE_NUM_THREADS_X = 8;
		constexpr uint32_t RAY_TRACE_NUM_THREADS_Y = 4;

		constexpr uint32_t TEMPORAL_ACCUMULATION_NUM_THREADS_X = 8;
		constexpr uint32_t TEMPORAL_ACCUMULATION_NUM_THREADS_Y = 8;
	}        // namespace

	static maple::TweakFloat ShadowNormal_Bias = { "Shadow:Normal Bias", 0.1f, 0.f, 1.f };
	static maple::TweakFloat ATrousRadius = { "Shadow:ATrousRadius", 1.f, 0.f, 20.f };

	static maple::TweakFloat ReprojectionAlpha = { "Shadow:Reprojection Alpha", 0.01, 0.f, 1.f };
	static maple::TweakFloat ReprojectionMomentsAlpha = { "Shadow:Reprojection MomentsAlpha", 0.2f, 0.f, 3.f };

	static maple::TweakBool  Denoise = { "Shadow:Enable Denoise", true };
	static maple::TweakBool  ShadowEnable = { "Shadow:Enable ", true };
	static maple::TweakInt32 MaxTraceLight = { "Shadow:MaxLighting ", 2, 0, 10 };
	static maple::TweakFloat MaxTraceDistance = { "Shadow:MaxTraceDistance ", 100, 0, 1000 };
	static maple::TweakFloat SoftShadow = { "Shadow:SoftShadow ", 0, 0, 1 };

	static struct ShadowPass
	{
		Texture2D::Ptr raytraceImage;
		DescriptorSet::Ptr descriptorSets[2];
		Pipeline::Ptr pipeline;
		Shader::Ptr shadowRaytraceShader;

		Texture2D::Ptr outputs[2];        //Shadows Previous Re-projection
		Texture2D::Ptr currentMoments[2];

	}shadowPass;

	static struct AtrousFiler
	{
		struct PushConstants
		{
			int   radius = 1;
			int   stepSize = 0;
			float phiVisibility = 10.0f;
			float phiNormal = 32.f;
			float sigmaDepth = 1.f;
			float power = 1.2f;
			float near_ = 0.1;
			float far_ = 10000.f;
		} pushConsts;

		int32_t iterations = 4;

		Shader::Ptr   atrousFilerShader;
		Pipeline::Ptr atrousFilerPipeline;

		Shader::Ptr   copyTilesShader;
		Pipeline::Ptr copyTilesPipeline;

		DescriptorSet::Ptr copyWriteDescriptorSet[2];
		DescriptorSet::Ptr copyReadDescriptorSet[2];

		DescriptorSet::Ptr copyTilesSet[2];

		DescriptorSet::Ptr gBufferSet;
		DescriptorSet::Ptr inputSet;
		DescriptorSet::Ptr argsSet;

		Texture2D::Ptr atrousFilter[2];        //A-Trous Filter
	}atrous;

	static struct TemporalAccumulator
	{
		StorageBuffer::Ptr denoiseDispatchBuffer1;
		StorageBuffer::Ptr denoiseTileCoordsBuffer;        // indirect?

		StorageBuffer::Ptr shadowTileCoordsBuffer;
		StorageBuffer::Ptr shadowDispatchBuffer;        // indirect?

		Shader::Ptr   resetArgsShader;
		Pipeline::Ptr resetPipeline;

		DescriptorSet::Ptr indirectDescriptorSet;

		/*
		* 0 -> Moment/History
		* 1 -> GBuffer
		* 2 -> PrevGBuffer
		* 3 -> Args
		*/
		std::vector<DescriptorSet::Ptr> descriptorSets;
		Shader::Ptr                     reprojectionShader;
		Pipeline::Ptr                   reprojectionPipeline;

		struct PushConstants
		{
			glm::mat4 viewProjInv;
			float alpha = 0.01f;
			float momentsAlpha = 0.2f;
		} pushConsts;
	}accumulator;


	auto init(uint32_t w, uint32_t h) -> void
	{
		memset(&shadowPass, 0, sizeof(shadowPass));

		auto width = static_cast<uint32_t>(ceil(float(w) / float(RAY_TRACE_NUM_THREADS_X)));
		auto height = static_cast<uint32_t>(ceil(float(h) / float(RAY_TRACE_NUM_THREADS_Y)));
		shadowPass.raytraceImage = Texture2D::create(width, height, nullptr, { TextureFormat::R32UI, TextureFilter::Nearest });
		shadowPass.raytraceImage->setName("Shadows Ray Trace");

		shadowPass.shadowRaytraceShader = maple::Shader::create({ { maple::ShaderType::Compute, "shaders/ShadowRaytrace.comp.spv" } });
		shadowPass.descriptorSets[0]	= DescriptorSet::create({ 0, shadowPass.shadowRaytraceShader.get() });
		shadowPass.descriptorSets[1]	= DescriptorSet::create({ 1, shadowPass.shadowRaytraceShader.get() });


		shadowPass.outputs[0] = Texture2D::create();
		shadowPass.outputs[0]->buildTexture(TextureFormat::RG16F, w, h);
		shadowPass.outputs[0]->setName("Shadows Re-projection Output Ping");
		//R:Visibility - G:Variance
		shadowPass.outputs[1] = Texture2D::create();
		shadowPass.outputs[1]->buildTexture(TextureFormat::RG16F, w, h);
		shadowPass.outputs[1]->setName("Shadows Re-projection Output Pong");

		for (int32_t i = 0; i < 2; i++)
		{
			//Visibility - Visibility * Visibility HistoryLength.
			shadowPass.currentMoments[i] = Texture2D::create();
			shadowPass.currentMoments[i]->buildTexture(TextureFormat::RGBA16, w, h);
			shadowPass.currentMoments[i]->setName("Shadows Re-projection Moments " + std::to_string(i));
		}


		auto bufferSize = sizeof(glm::ivec2)
			* static_cast<uint32_t>(ceil(float(w) / float(TEMPORAL_ACCUMULATION_NUM_THREADS_X))) 
			* static_cast<uint32_t>(ceil(float(h) / float(TEMPORAL_ACCUMULATION_NUM_THREADS_Y)));

		accumulator.resetArgsShader = Shader::create({
			{ShaderType::Compute, "shaders/DenoiseReset.comp.spv" }
		});

		accumulator.denoiseTileCoordsBuffer = StorageBuffer::create(bufferSize, nullptr);
		accumulator.denoiseDispatchBuffer1 = StorageBuffer::create(sizeof(int32_t) * 3, nullptr, BufferOptions{ true, MemoryUsage::MEMORY_USAGE_GPU_ONLY });

		accumulator.shadowTileCoordsBuffer = StorageBuffer::create(bufferSize, nullptr);
		accumulator.shadowDispatchBuffer = StorageBuffer::create(sizeof(int32_t) * 3, nullptr, BufferOptions{ true, MemoryUsage::MEMORY_USAGE_GPU_ONLY });

		accumulator.indirectDescriptorSet = DescriptorSet::create({ 0, accumulator.resetArgsShader.get() });
		accumulator.indirectDescriptorSet->setStorageBuffer("DenoiseTileDispatchArgs", accumulator.denoiseDispatchBuffer1);
		accumulator.indirectDescriptorSet->setStorageBuffer("CopyTileDispatchArgs", accumulator.shadowDispatchBuffer);

		//###############
		accumulator.reprojectionShader = Shader::create({
			{ ShaderType::Compute, "shaders/ShadowReprojection.comp.spv" }
		});

		constexpr char* str[4] = { "Accumulation", "GBuffer", "PrevGBuffer", "Args" };
		accumulator.descriptorSets.resize(4);
		for (uint32_t i = 0; i < 4; i++)
		{
			accumulator.descriptorSets[i] = DescriptorSet::create({ i, accumulator.reprojectionShader.get() });
			accumulator.descriptorSets[i]->setName(str[i]);
		}

		accumulator.descriptorSets[3]->setStorageBuffer("DenoiseTileData", accumulator.denoiseTileCoordsBuffer);
		accumulator.descriptorSets[3]->setStorageBuffer("DenoiseTileDispatchArgs", accumulator.denoiseDispatchBuffer1);
		accumulator.descriptorSets[3]->setStorageBuffer("ShadowTileData", accumulator.shadowTileCoordsBuffer);
		accumulator.descriptorSets[3]->setStorageBuffer("ShadowTileDispatchArgs", accumulator.shadowDispatchBuffer);

		PipelineInfo info;
		info.shader = accumulator.reprojectionShader;
		info.pipelineName = "Reprojection";
		accumulator.reprojectionPipeline = Pipeline::get(info);

		//###############


		PipelineInfo info2;
		info2.pipelineName = "Reset-Args";
		info2.shader = accumulator.resetArgsShader;
		accumulator.resetPipeline = Pipeline::get(info2);

		////////////////////////////////////////////////////////////////////////////////////////

		atrous.iterations = 4;
		{
			atrous.atrousFilerShader = Shader::create({ { ShaderType::Compute, "shaders/ShadowAtrous.comp.spv"} });
			atrous.copyTilesShader = Shader::create({{ ShaderType::Compute, "shaders/ShadowCopyTiles.comp.spv" }});

			PipelineInfo info1;
			info1.pipelineName = "Atrous-Filer Pipeline";
			info1.shader = atrous.atrousFilerShader;
			atrous.atrousFilerPipeline = Pipeline::get(info1);

			info1.shader = atrous.copyTilesShader;
			atrous.copyTilesPipeline = Pipeline::get(info1);

			atrous.gBufferSet = DescriptorSet::create({ 1, atrous.atrousFilerShader.get() });
			atrous.argsSet = DescriptorSet::create({ 3, atrous.atrousFilerShader.get() });
			atrous.argsSet->setStorageBuffer("DenoiseTileData", accumulator.denoiseTileCoordsBuffer);

			for (uint32_t i = 0; i < 2; i++)
			{
				atrous.atrousFilter[i] = Texture2D::create();
				atrous.atrousFilter[i]->buildTexture(TextureFormat::RG16F, w, h);
				atrous.atrousFilter[i]->setName("A-Trous Filter " + std::to_string(i));

				atrous.copyWriteDescriptorSet[i] = DescriptorSet::create({ 0, atrous.atrousFilerShader.get() });
				atrous.copyWriteDescriptorSet[i]->setTexture("outColor", atrous.atrousFilter[i]);
				atrous.copyWriteDescriptorSet[i]->setName("Atrous-Write-Descriptor-" + std::to_string(i));

				atrous.copyReadDescriptorSet[i] = DescriptorSet::create({ 2, atrous.atrousFilerShader.get() });
				atrous.copyReadDescriptorSet[i]->setName("Atrous-Read-Descriptor-" + std::to_string(i));

				atrous.copyTilesSet[i] = DescriptorSet::create({ 0, atrous.copyTilesShader.get() });
				atrous.copyTilesSet[i]->setStorageBuffer("ShadowTileData", accumulator.shadowTileCoordsBuffer);
			}

			atrous.inputSet = DescriptorSet::create({ 2, atrous.atrousFilerShader.get() });
			atrous.inputSet->setTexture("uInput", shadowPass.outputs[0]);
		}
	}

	auto clear(const CommandBuffer* cmd) -> void
	{
		maple::RenderDevice::get()->clearRenderTarget(
			shadowPass.raytraceImage,
			cmd,
			{ 0,0,0,0 }
		);
	}

	static auto resetArgs(const CommandBuffer* cmd)
	{
		debug_utils::cmdBeginLabel("Shadow Denoise ResetArgs");
		accumulator.resetPipeline->bufferBarrier(cmd, { accumulator.denoiseDispatchBuffer1,  accumulator.denoiseTileCoordsBuffer, accumulator.shadowDispatchBuffer, accumulator.shadowTileCoordsBuffer },	false);
		RenderDevice::get()->dispatch(cmd, 1, 1, 1, accumulator.resetPipeline.get(), nullptr, { accumulator.indirectDescriptorSet });
		debug_utils::cmdEndLabel();
	}

	static auto accumulation(const CommandBuffer* cmd)
	{
		debug_utils::cmdBeginLabel("Raytracing Shadow Reprojection");

		accumulator.descriptorSets[0]->setTexture("outColor", shadowPass.outputs[0]);
		accumulator.descriptorSets[0]->setTexture("moment", shadowPass.currentMoments[gbuffer::getPingPong()]);
		accumulator.descriptorSets[0]->setTexture("uHistoryOutput", shadowPass.outputs[1]);        //prev
		accumulator.descriptorSets[0]->setTexture("uHistoryMoments", shadowPass.currentMoments[1 - gbuffer::getPingPong()]);
		accumulator.descriptorSets[0]->setTexture("uInput", shadowPass.raytraceImage);        //noise shadow


		accumulator.descriptorSets[1]->setTexture("uPBRSampler", gbuffer::getGBuffer2());
		accumulator.descriptorSets[1]->setTexture("uNormalSampler", gbuffer::getGBuffer1());
		accumulator.descriptorSets[1]->setTexture("uDepthSampler", gbuffer::getDepth());

		accumulator.descriptorSets[2]->setTexture("uPrevPBRSampler", gbuffer::getGBuffer2(true));
		accumulator.descriptorSets[2]->setTexture("uPrevNormalSampler", gbuffer::getGBuffer1(true));
		accumulator.descriptorSets[2]->setTexture("uPrevDepthSampler", gbuffer::getDepth(true));


		auto x = static_cast<uint32_t>(ceil(float(gbuffer::getGBuffer1()->getWidth()) / float(TEMPORAL_ACCUMULATION_NUM_THREADS_X)));
		auto y = static_cast<uint32_t>(ceil(float(gbuffer::getGBuffer1()->getHeight()) / float(TEMPORAL_ACCUMULATION_NUM_THREADS_Y)));

		RenderDevice::get()->dispatch(cmd, x, y, 1, accumulator.reprojectionPipeline.get(), &accumulator.pushConsts, accumulator.descriptorSets);

		accumulator.reprojectionPipeline->bufferBarrier(cmd,
			{ accumulator.denoiseDispatchBuffer1,
			 accumulator.denoiseTileCoordsBuffer,
			 accumulator.shadowDispatchBuffer,
			 accumulator.shadowTileCoordsBuffer },
			true);

		debug_utils::cmdEndLabel();
		return shadowPass.outputs[0];
	}

	static auto atrousFilter(const CommandBuffer* cmd)
	{
		bool    pingPong = false;
		int32_t readIdx = 0;
		int32_t writeIdx = 1;

		atrous.gBufferSet->setTexture("uNormalSampler", gbuffer::getGBuffer1());
		atrous.gBufferSet->setTexture("uPBRSampler", gbuffer::getGBuffer2());

		atrous.gBufferSet->update(cmd);
		atrous.inputSet->update(cmd);
		atrous.argsSet->update(cmd);
		for (int32_t i = 0; i < atrous.iterations; i++)
		{
			readIdx = (int32_t)pingPong;
			writeIdx = (int32_t)!pingPong;

			RenderDevice::get()->clearRenderTarget(atrous.atrousFilter[writeIdx],cmd, vec4{ 1, 1, 1, 1 });

			atrous.copyTilesSet[writeIdx]->setTexture("outColor", atrous.atrousFilter[writeIdx]);
			atrous.copyTilesSet[writeIdx]->update(cmd);

			//these coords should not denoise. so just set them as zero.
			{
				atrous.copyTilesPipeline->bind(cmd);
				RenderDevice::get()->bindDescriptorSets(atrous.copyTilesPipeline.get(),cmd, { atrous.copyTilesSet[writeIdx] });
				atrous.copyTilesPipeline->dispatchIndirect(cmd, accumulator.shadowDispatchBuffer.get());
				atrous.copyTilesPipeline->end(cmd);
			}

			{
				atrous.copyWriteDescriptorSet[writeIdx]->setTexture("outColor", atrous.atrousFilter[writeIdx]);
				atrous.copyReadDescriptorSet[readIdx]->setTexture("uInput", atrous.atrousFilter[readIdx]);

				atrous.copyWriteDescriptorSet[writeIdx]->update(cmd);
				atrous.copyReadDescriptorSet[readIdx]->update(cmd);
			}

			{
				atrous.atrousFilerPipeline->bind(cmd);
				auto pushConsts = atrous.pushConsts;
				pushConsts.stepSize = 1 << i;
				pushConsts.power = i == (atrous.iterations - 1) ? atrous.pushConsts.power : 0.0f;
				if (auto ptr = atrous.atrousFilerPipeline->getShader()->getPushConstant(0))
				{
					ptr->setData(&pushConsts);
					atrous.atrousFilerPipeline->getShader()->bindPushConstants(cmd, atrous.atrousFilerPipeline.get());
				}

				//the first time(i is zero) set the accumulation's output as current input
				RenderDevice::get()->bindDescriptorSets(atrous.atrousFilerPipeline.get(), cmd, { atrous.copyWriteDescriptorSet[writeIdx], atrous.gBufferSet, i == 0 ? atrous.inputSet : atrous.copyReadDescriptorSet[readIdx],  atrous.argsSet });

				atrous.atrousFilerPipeline->dispatchIndirect(cmd, accumulator.denoiseDispatchBuffer1.get());
				atrous.atrousFilerPipeline->end(cmd);
			}

			pingPong = !pingPong;

			if (i == 1)
			{
				Texture2D::copy(atrous.atrousFilter[writeIdx], shadowPass.outputs[1],cmd);
			}
		}

		return atrous.atrousFilter[writeIdx];
	}

	static auto denoise(const CommandBuffer* cmd)
	{
		resetArgs(cmd);

		auto out = accumulation(cmd);
		if (Denoise.var)
			return atrousFilter(cmd);
		return out;
	}

	auto render(
		const std::vector<VertexBuffer*>& vertexs,
		const std::vector<IndexBuffer*>& indices,
		const std::shared_ptr<StorageBuffer>& objectBuffer,
		const std::shared_ptr<AccelerationStructure>& topLevel,
		const std::shared_ptr<Texture>& depth,
		const std::vector<std::shared_ptr<Texture>>& textures,
		const vec3& cameraPos,
		const mat4& viewProjInv,
		float farPlane,
		float nearPlane,
		const CommandBuffer* cmd) -> std::shared_ptr<Texture>
	{
		if (vertexs.empty() || !ShadowEnable)
			return nullptr;

		accumulator.pushConsts.viewProjInv = reinterpret_cast<const glm::mat4&>(viewProjInv);

		atrous.pushConsts.far_  = farPlane;
		atrous.pushConsts.near_ = nearPlane;

		debug_utils::cmdBeginLabel("Raytracing Shadow");

		static uint32_t frames = 0;

		struct PushConsts
		{
			mat4  viewProjInv;
			vec3 cameraPos;
			float bias;

			uint32_t frames;
			uint32_t maxLight;
			float maxTraceDist;
			float softShadow;
		}pushConsts;

		pushConsts.viewProjInv = viewProjInv;
		pushConsts.cameraPos = cameraPos;
		pushConsts.bias = ShadowNormal_Bias;
		pushConsts.frames = frames++;
		pushConsts.maxLight = MaxTraceLight;
		pushConsts.maxTraceDist = MaxTraceDistance;
		pushConsts.softShadow = SoftShadow.var;

		shadowPass.descriptorSets[0]->setStorageBuffer("ObjectBuffer", objectBuffer);
		shadowPass.descriptorSets[0]->setAccelerationStructure("uTopLevelAS", topLevel);

		shadowPass.descriptorSets[1]->setTexture("uNormalSampler", gbuffer::getGBuffer1());
		shadowPass.descriptorSets[1]->setTexture("uPBRSampler", gbuffer::getGBuffer2());
		shadowPass.descriptorSets[1]->setTexture("uObjSampler", gbuffer::getGBuffer4());
		shadowPass.descriptorSets[1]->setTexture("uDepthSampler", depth);
		shadowPass.descriptorSets[1]->setTexture("outColor", shadowPass.raytraceImage);

		shadowPass.descriptorSets[1]->setTexture("uSobolSequence", blue_noise::getSobolSequence());
		shadowPass.descriptorSets[1]->setTexture("uScramblingRankingTile", blue_noise::getScramblingRanking(blue_noise::Blue_Noise_1SPP));


		PipelineInfo info{};
		info.shader = shadowPass.shadowRaytraceShader;
		auto pipeline = Pipeline::get(info);

		RenderDevice::get()->dispatch(cmd,
			static_cast<uint32_t>(ceil(float(gbuffer::getGBuffer1()->getWidth()) / float(RAY_TRACE_NUM_THREADS_X))),
			static_cast<uint32_t>(ceil(float(gbuffer::getGBuffer1()->getHeight()) / float(RAY_TRACE_NUM_THREADS_Y))), 
			1, pipeline.get(), &pushConsts, {
				shadowPass.descriptorSets[0],
				shadowPass.descriptorSets[1]
			}
		);

		debug_utils::cmdEndLabel();
		return denoise(cmd);
	}
}