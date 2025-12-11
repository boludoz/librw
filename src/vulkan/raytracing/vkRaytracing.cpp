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

namespace maple
{
	namespace
	{
		//using int32_t to pack visibility
		constexpr uint32_t RAY_TRACE_NUM_THREADS_X = 8;
		constexpr uint32_t RAY_TRACE_NUM_THREADS_Y = 4;

		constexpr uint32_t TEMPORAL_ACCUMULATION_NUM_THREADS_X = 8;
		constexpr uint32_t TEMPORAL_ACCUMULATION_NUM_THREADS_Y = 8;
	}        // namespace

	static maple::TweakFloat Normal_Bias = { "Reflection:Normal Bias", 0.1f, 0.f, 1.f };
	static maple::TweakFloat Trim = { "Reflection:Trim", 1.f, 0.f, 1.f };
	static maple::TweakFloat Intensity = { "Reflection:Intensity", 0.f, 0.f, 10.f };
	static maple::TweakFloat ReflectionMirror = { "Reflection:Mirror", 0.05f , 0.f, 1.f };

	static maple::TweakFloat ATrousRadius = { "Reflection:ATrousRadius", 1.f, 0.f, 20.f };

	static maple::TweakFloat PhiColor = { "Reflection:ATrous PhiColor", 10.f, 0.f, 100.f };
	static maple::TweakFloat PhiNormal = { "Reflection:ATrous PhiNormal", 32.f, 0.f, 100.f };
	static maple::TweakFloat SigmaDepth = { "Reflection:ATrous SigmaDepth", 1.f, 0.f, 20.f };

	static maple::TweakFloat ReprojectionAlpha = { "Reflection:Reprojection Alpha", 0.01, 0.f, 1.f };
	static maple::TweakFloat ReprojectionMomentsAlpha = { "Reflection:Reprojection MomentsAlpha", 0.2f, 0.f, 3.f };

	static maple::TweakBool  Denoise = { "Reflection:Enable Denoise", true };
	static maple::TweakBool  ReflectionEnable = { "Reflection:Enable", true };

	static struct
	{
		DescriptorSet::Ptr objectSets;
		DescriptorSet::Ptr vertexSets;
		DescriptorSet::Ptr indexSets;
		DescriptorSet::Ptr textures;
		DescriptorSet::Ptr gbufferSet;
		DescriptorSet::Ptr outSets;
		DescriptorSet::Ptr noiseSets;
		Shader::Ptr reflectionShader;
		Shader::Ptr shadowShader;
		Texture2D::Ptr reflectionOut;

		Texture2D::Ptr outputs[2];
		Texture2D::Ptr currentMoments[2];

		DescriptorSet::Ptr ddgiSet;

		int32_t pingPong = 0;
	}reflectionPass;

	struct TemporalAccumulator
	{
		StorageBuffer::Ptr denoiseDispatchBuffer;
		StorageBuffer::Ptr denoiseTileCoordsBuffer;        // indirect?

		StorageBuffer::Ptr copyTileCoordsBuffer;
		StorageBuffer::Ptr copyDispatchBuffer;        // indirect?

		Shader::Ptr   resetArgsShader;
		Pipeline::Ptr resetPipeline;

		DescriptorSet::Ptr indirectDescriptorSet;

		std::vector<DescriptorSet::Ptr> descriptorSets;
		Shader::Ptr                     reprojectionShader;
		Pipeline::Ptr                   reprojectionPipeline;

		struct PushConstants
		{
			mat4 viewProjInv;
			mat4 prevViewProj;
			vec4 cameraPos;
			vec3 cameraDelta;
			float alpha = 0.01f;
			float momentsAlpha = 0.2f;
			int32_t mipmap = 0;
			float mirror = 0.05;
		} pushConsts;
	}accumulator;

	static struct AtrousFiler
	{
		struct PushConstants
		{
			int32_t radius = 1;
			int32_t stepSize = 0;
			float   phiColor = 10.0f;
			float   phiNormal = 32.f;
			float   sigmaDepth = 1.f;
			int32_t mipmap = 0;
			float mirror = 0.05;
		} pushConsts;

		int32_t iterations = 4;

		Shader::Ptr   atrousFilerShader;
		Pipeline::Ptr atrousFilerPipeline;

		Shader::Ptr   copyTilesShader;
		Pipeline::Ptr copyTilesPipeline;

		DescriptorSet::Ptr copyWriteDescriptorSet[2];
		DescriptorSet::Ptr copyReadDescriptorSet[2];

		DescriptorSet::Ptr gBufferSet;
		DescriptorSet::Ptr inputSet;
		DescriptorSet::Ptr argsSet;

		Texture2D::Ptr atrousFilter[2];        //A-Trous Filter

		DescriptorSet::Ptr copyTileDataSet;
	}atrous;

	static std::vector<AccelerationStructure::Ptr> cache;

	static DescriptorPool::Ptr descriptorPool;
	
	auto createBottomLevel(const VertexBuffer* vertexBuffer, const IndexBuffer* indexBuffer, uint32_t vertexStride, const std::vector<SubMesh>& subMeshes, std::shared_ptr<BatchTask> tasks)->uint32_t
	{
		auto id = cache.size();
		cache.push_back(AccelerationStructure::createBottomLevel(vertexBuffer, indexBuffer, vertexStride, subMeshes, tasks));
		return id;
	}

	auto getAccelerationStructure(uint32_t id) ->std::shared_ptr<AccelerationStructure>
	{
		if (id < cache.size())
			return cache[id];
		return nullptr;
	}

	auto getVertexSets() ->std::shared_ptr<DescriptorSet>
	{
		return reflectionPass.vertexSets;
	}

	auto getIndexSets() ->std::shared_ptr<DescriptorSet>
	{
		return reflectionPass.indexSets;
	}

	auto getSamplerSets() ->std::shared_ptr<DescriptorSet>
	{
		return reflectionPass.textures;
	}

	auto getObjectSets() ->std::shared_ptr<DescriptorSet>
	{
		return reflectionPass.objectSets;
	}

	auto raytracingPrepare(const std::vector<VertexBuffer*>& vertexs,
		const std::vector<IndexBuffer*>& indices,
		const std::shared_ptr<StorageBuffer>& objectBuffer,
		const std::shared_ptr<AccelerationStructure>& topLevel,
		const std::vector<std::shared_ptr<Texture>>& textures,
		const std::shared_ptr<TextureCube>& cube,
		const CommandBuffer* cmd) -> void
	{
		if (vertexs.size() == 0)
			return;
		MAPLE_ASSERT(vertexs.size() < MAX_SCENE_MESH_INSTANCE_COUNT, "must be ");

		reflectionPass.objectSets->setStorageBuffer("ObjectBuffer", objectBuffer);
		reflectionPass.objectSets->setAccelerationStructure("uTopLevelAS", topLevel);
		reflectionPass.objectSets->setTexture("uSkybox", cube);
		reflectionPass.vertexSets->setStorageBuffer("VertexBuffer", vertexs);
		reflectionPass.indexSets->setStorageBuffer("IndexBuffer", indices);
		reflectionPass.textures->setTexture("uSamplers", textures);

		reflectionPass.objectSets->update(cmd);
		reflectionPass.textures->update(cmd);
		reflectionPass.vertexSets->update(cmd);
		reflectionPass.indexSets->update(cmd);
	}

	static auto initATrous(uint32_t w, uint32_t h)
	{
		{
			PipelineInfo info;
			info.shader = accumulator.resetArgsShader;
			info.pipelineName = "Reflection-DenoiseReset";
			accumulator.resetPipeline = Pipeline::get(info);

			accumulator.reprojectionShader = Shader::create(
				{ {maple::ShaderType::Compute,"shaders/DenoiseReprojection.comp.spv"} }
			);
			constexpr char* str[4] = { "Accumulation", "GBuffer", "PrevGBuffer", "Args" };
			accumulator.descriptorSets.resize(4);
			for (uint32_t i = 0; i < 4; i++)
			{
				accumulator.descriptorSets[i] = DescriptorSet::create({ i, accumulator.reprojectionShader.get() });
				accumulator.descriptorSets[i]->setName(str[i]);
			}

			accumulator.descriptorSets[3]->setStorageBuffer("DenoiseTileData", accumulator.denoiseTileCoordsBuffer);
			accumulator.descriptorSets[3]->setStorageBuffer("DenoiseTileDispatchArgs", accumulator.denoiseDispatchBuffer);
			accumulator.descriptorSets[3]->setStorageBuffer("CopyTileData", accumulator.copyTileCoordsBuffer);
			accumulator.descriptorSets[3]->setStorageBuffer("CopyTileDispatchArgs", accumulator.copyDispatchBuffer);

			info.shader = accumulator.reprojectionShader;
			info.pipelineName = "Reflection-Reprojection";
			accumulator.reprojectionPipeline = Pipeline::get(info);
		}
		//###############

		PipelineInfo info2;
		info2.pipelineName = "Reflection-Reset-Args";
		info2.shader = accumulator.resetArgsShader;
		accumulator.resetPipeline = Pipeline::get(info2);

		accumulator.indirectDescriptorSet = DescriptorSet::create({ 0, accumulator.resetArgsShader.get() });
		accumulator.indirectDescriptorSet->setStorageBuffer("DenoiseTileDispatchArgs", accumulator.denoiseDispatchBuffer);
		accumulator.indirectDescriptorSet->setStorageBuffer("CopyTileDispatchArgs", accumulator.copyDispatchBuffer);

		{
			atrous.atrousFilerShader = Shader::create(
				{ {maple::ShaderType::Compute,"shaders/DenoiseAtrous.comp.spv"} }
			);

			atrous.copyTilesShader = Shader::create(
				{ {maple::ShaderType::Compute,"shaders/DenoiseCopyTiles.comp.spv"} }
			);

			PipelineInfo info1;

			info1.pipelineName = "Reflection-Atrous-Filer Pipeline";
			info1.shader = atrous.atrousFilerShader;
			atrous.atrousFilerPipeline = Pipeline::get(info1);

			info1.pipelineName = "Reflection-Atrous-Copy Tiles Pipeline";
			info1.shader = atrous.copyTilesShader;
			atrous.copyTilesPipeline = Pipeline::get(info1);

			atrous.gBufferSet = DescriptorSet::create({ 1, atrous.atrousFilerShader.get() });
			atrous.argsSet = DescriptorSet::create({ 3, atrous.atrousFilerShader.get() });
			atrous.argsSet->setStorageBuffer("DenoiseTileData", accumulator.denoiseTileCoordsBuffer);

			for (uint32_t i = 0; i < 2; i++)
			{
				atrous.atrousFilter[i] = Texture2D::create();
				atrous.atrousFilter[i]->buildTexture(TextureFormat::RGBA16, w, h);
				atrous.atrousFilter[i]->setName("A-Trous Filter " + std::to_string(i));

				atrous.copyWriteDescriptorSet[i] = DescriptorSet::create({ 0, atrous.atrousFilerShader.get() });
				atrous.copyWriteDescriptorSet[i]->setTexture("outColor", atrous.atrousFilter[i]);
				atrous.copyWriteDescriptorSet[i]->setName("Atrous-Write-Descriptor-" + std::to_string(i));

				atrous.copyReadDescriptorSet[i] = DescriptorSet::create({ 2, atrous.atrousFilerShader.get() });
				atrous.copyReadDescriptorSet[i]->setName("Atrous-Read-Descriptor-" + std::to_string(i));
			}

			atrous.copyTileDataSet = DescriptorSet::create({ 1, atrous.copyTilesShader.get() });
			atrous.copyTileDataSet->setStorageBuffer("CopyTileData", accumulator.copyTileCoordsBuffer);
			atrous.copyTileDataSet->setName("CopyTileData");

			atrous.inputSet = DescriptorSet::create({ 2, atrous.atrousFilerShader.get() });
			atrous.inputSet->setTexture("uInput", reflectionPass.outputs[0]);
		}
	}

	static auto initDenoise(uint32_t w, uint32_t h)
	{
		for (auto i = 0; i < 2; i++)
		{
			reflectionPass.outputs[i] = Texture2D::create();
			reflectionPass.outputs[i]->buildTexture(TextureFormat::RGBA16, w, h);
			reflectionPass.outputs[i]->setName("Reflection Re-projection Output " + std::to_string(i));

			reflectionPass.currentMoments[i] = Texture2D::create();
			reflectionPass.currentMoments[i]->buildTexture(TextureFormat::RGBA16, w, h);
			reflectionPass.currentMoments[i]->setName("Reflection Re-projection Moments " + std::to_string(i));
		}

		auto bufferSize = sizeof(ivec2) * static_cast<uint32_t>(ceil(float(w) / float(TEMPORAL_ACCUMULATION_NUM_THREADS_X)))
			* static_cast<uint32_t>(ceil(float(h) / float(TEMPORAL_ACCUMULATION_NUM_THREADS_Y)));

		accumulator.resetArgsShader = Shader::create(
			{ {maple::ShaderType::Compute, "shaders/DenoiseReset.comp.spv"} }
		);

		accumulator.denoiseTileCoordsBuffer = StorageBuffer::create(bufferSize, nullptr);
		accumulator.denoiseDispatchBuffer = StorageBuffer::create(sizeof(int32_t) * 3, nullptr, BufferOptions{ true, MemoryUsage::MEMORY_USAGE_GPU_ONLY });

		accumulator.copyTileCoordsBuffer = StorageBuffer::create(bufferSize, nullptr);
		accumulator.copyDispatchBuffer = StorageBuffer::create(sizeof(int32_t) * 3, nullptr, BufferOptions{ true, MemoryUsage::MEMORY_USAGE_GPU_ONLY });

		initATrous(w, h);
	}

	static auto resetArgs(const CommandBuffer* cmd)
	{
		debug_utils::cmdBeginLabel("Reflection Denoise ResetArgs");
		accumulator.indirectDescriptorSet->update(cmd);
		accumulator.resetPipeline->bufferBarrier(cmd,
			{ accumulator.denoiseDispatchBuffer,
			  accumulator.denoiseTileCoordsBuffer,
			  accumulator.copyDispatchBuffer,
			  accumulator.copyTileCoordsBuffer },
			false);
		accumulator.resetPipeline->bind(cmd);
		RenderDevice::get()->bindDescriptorSets(accumulator.resetPipeline.get(), cmd, { accumulator.indirectDescriptorSet });
		RenderDevice::get()->dispatch(cmd, 1, 1, 1);
		accumulator.resetPipeline->end(cmd);
		debug_utils::cmdEndLabel();
	}

	static auto accumulation(const CommandBuffer* cmd)
	{
		debug_utils::cmdBeginLabel("Reflection Reprojection");

		accumulator.descriptorSets[0]->setTexture("outColor", reflectionPass.outputs[0]);
		accumulator.descriptorSets[0]->setTexture("moment", reflectionPass.currentMoments[reflectionPass.pingPong]);
		accumulator.descriptorSets[0]->setTexture("uHistoryOutput", reflectionPass.outputs[1]);        //prev
		accumulator.descriptorSets[0]->setTexture("uHistoryMoments", reflectionPass.currentMoments[1 - reflectionPass.pingPong]);

		accumulator.descriptorSets[0]->setTexture("uInput", reflectionPass.reflectionOut);        //noised reflection

		accumulator.descriptorSets[1]->setTexture("uNormalSampler", gbuffer::getGBuffer1());
		accumulator.descriptorSets[1]->setTexture("uDepthSampler", gbuffer::getDepth());
		accumulator.descriptorSets[1]->setTexture("uPBRSampler", gbuffer::getGBuffer2());

		accumulator.descriptorSets[2]->setTexture("uPrevNormalSampler", gbuffer::getGBuffer1(true));
		accumulator.descriptorSets[2]->setTexture("uPrevDepthSampler", gbuffer::getDepth(true));
		accumulator.descriptorSets[2]->setTexture("uPrevPBRSampler", gbuffer::getGBuffer2(true));


		for (auto set : accumulator.descriptorSets)
		{
			set->update(cmd);
		}

		if (auto pushConsts = accumulator.reprojectionShader->getPushConstant(0))
		{
			accumulator.pushConsts.mirror = ReflectionMirror;
			accumulator.pushConsts.alpha = ReprojectionAlpha;
			accumulator.pushConsts.momentsAlpha = ReprojectionMomentsAlpha;
			pushConsts->setData(&accumulator.pushConsts);
		}

		accumulator.reprojectionPipeline->bind(cmd);
		accumulator.reprojectionShader->bindPushConstants(cmd, accumulator.reprojectionPipeline.get());
		RenderDevice::get()->bindDescriptorSets(accumulator.reprojectionPipeline.get(), cmd, accumulator.descriptorSets);
		auto x = static_cast<uint32_t>(ceil(float(reflectionPass.reflectionOut->getWidth()) / float(TEMPORAL_ACCUMULATION_NUM_THREADS_X)));
		auto y = static_cast<uint32_t>(ceil(float(reflectionPass.reflectionOut->getHeight()) / float(TEMPORAL_ACCUMULATION_NUM_THREADS_Y)));
		RenderDevice::get()->dispatch(cmd, x, y, 1);

		accumulator.reprojectionPipeline->bufferBarrier(cmd,
			{ accumulator.denoiseDispatchBuffer,
			 accumulator.denoiseTileCoordsBuffer,
			 accumulator.copyDispatchBuffer,
			 accumulator.copyTileCoordsBuffer },
			true);

		debug_utils::cmdEndLabel();
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
			debug_utils::cmdBeginLabel("Reflection Atrous Iterations:" + std::to_string(i));

			readIdx = (int32_t)pingPong;
			writeIdx = (int32_t)!pingPong;

			RenderDevice::get()->clearRenderTarget(atrous.atrousFilter[writeIdx], cmd, vec4{ 1, 1, 1, 1 });

			atrous.copyWriteDescriptorSet[writeIdx]->setTexture("outColor", atrous.atrousFilter[writeIdx]);
			atrous.copyReadDescriptorSet[readIdx]->setTexture("uInput", atrous.atrousFilter[readIdx]);

			atrous.copyWriteDescriptorSet[writeIdx]->update(cmd);
			atrous.copyReadDescriptorSet[readIdx]->update(cmd);

			atrous.copyTileDataSet->update(cmd);

			//these coords should not denoise. so just set them as zero.
			{
				atrous.copyTilesPipeline->bind(cmd);
				RenderDevice::get()->bindDescriptorSets(atrous.copyTilesPipeline.get(), cmd, { atrous.copyWriteDescriptorSet[writeIdx], atrous.copyTileDataSet, i == 0 ? atrous.inputSet : atrous.copyReadDescriptorSet[readIdx] });
				atrous.copyTilesPipeline->dispatchIndirect(cmd, accumulator.copyDispatchBuffer.get());
				atrous.copyTilesPipeline->end(cmd);
			}

			{
				atrous.atrousFilerPipeline->bind(cmd);
				auto& pushConsts = atrous.pushConsts;
				pushConsts.stepSize = 1 << i;
				pushConsts.radius = ATrousRadius;
				pushConsts.mirror = ReflectionMirror;
				pushConsts.phiColor = PhiColor;
				pushConsts.phiNormal = PhiNormal;
				pushConsts.sigmaDepth = SigmaDepth;

				if (auto ptr = atrous.atrousFilerPipeline->getShader()->getPushConstant(0))
				{
					ptr->setData(&pushConsts);
					atrous.atrousFilerPipeline->getShader()->bindPushConstants(cmd, atrous.atrousFilerPipeline.get());
				}

				//the first time(i is zero) set the accumulation's output as current input
				RenderDevice::get()->bindDescriptorSets(
					atrous.atrousFilerPipeline.get(),
					cmd,
					{ atrous.copyWriteDescriptorSet[writeIdx],
					 atrous.gBufferSet,
					 i == 0 ? atrous.inputSet : atrous.copyReadDescriptorSet[readIdx],
					 atrous.argsSet });

				atrous.atrousFilerPipeline->dispatchIndirect(cmd, accumulator.denoiseDispatchBuffer.get());
				atrous.atrousFilerPipeline->end(cmd);
			}
			pingPong = !pingPong;

			if (i == 1)
			{
				Texture2D::copy(atrous.atrousFilter[writeIdx], reflectionPass.outputs[1], cmd);
			}

			debug_utils::cmdEndLabel();
		}
		return atrous.atrousFilter[writeIdx];
	}

	static auto denoise(const CommandBuffer* cmd)
	{
		resetArgs(cmd);
		accumulation(cmd);
		auto out = atrousFilter(cmd);
		reflectionPass.pingPong = 1 - reflectionPass.pingPong;
		return out;
	}

	namespace reflection
	{
		auto clear(const CommandBuffer* cmd) -> void
		{
			maple::RenderDevice::get()->clearRenderTarget(
				reflectionPass.reflectionOut,
				cmd,
				{ 0,0,0,0 }
			);
		}

		auto init(uint32_t w, uint32_t h) -> void
		{
			blue_noise::init();

			reflectionPass.reflectionOut = Texture2D::create();
			reflectionPass.reflectionOut->setName("Reflection out");
			reflectionPass.reflectionOut->buildTexture(TextureFormat::RGBA16, w, h);
			reflectionPass.reflectionShader = maple::Shader::create({
				{ maple::ShaderType::RayGen, "shaders/Reflection.rgen.spv" },
				{ maple::ShaderType::RayCloseHit, "shaders/Reflection.rchit.spv"},
				{ maple::ShaderType::RayAnyHit, "shaders/Reflection.rahit.spv"},
				{ maple::ShaderType::RayMiss, "shaders/Reflection.rmiss.spv" }
				},
				{ {"VertexBuffer", MAX_SCENE_MESH_INSTANCE_COUNT},
				{"IndexBuffer", MAX_SCENE_MESH_INSTANCE_COUNT},
				{"uSamplers", MAX_SCENE_MATERIAL_TEXTURE_COUNT} }
			);


			descriptorPool = DescriptorPool::create({ 25,{{DescriptorType::ImageSampler, MAX_SCENE_MATERIAL_TEXTURE_COUNT},
																	{DescriptorType::Buffer, 3 * MAX_SCENE_MESH_INSTANCE_COUNT},
																	{DescriptorType::AccelerationStructure, 5}} });

			reflectionPass.objectSets = DescriptorSet::create({ 0, reflectionPass.reflectionShader.get() });
			reflectionPass.vertexSets = DescriptorSet::create({ 1, reflectionPass.reflectionShader.get() ,1,descriptorPool.get(),MAX_SCENE_MESH_INSTANCE_COUNT });
			reflectionPass.indexSets = DescriptorSet::create({ 2,  reflectionPass.reflectionShader.get()  ,1,descriptorPool.get(), MAX_SCENE_MESH_INSTANCE_COUNT });
			reflectionPass.textures = DescriptorSet::create({ 3,   reflectionPass.reflectionShader.get()   ,1,descriptorPool.get(), MAX_SCENE_MATERIAL_TEXTURE_COUNT });
			reflectionPass.gbufferSet = DescriptorSet::create({ 4, reflectionPass.reflectionShader.get() });
			reflectionPass.outSets = DescriptorSet::create({ 5, reflectionPass.reflectionShader.get() });
			reflectionPass.noiseSets = DescriptorSet::create({ 6, reflectionPass.reflectionShader.get() });
			reflectionPass.ddgiSet = DescriptorSet::create({ 7, reflectionPass.reflectionShader.get() });

			reflectionPass.noiseSets->setTexture("uSobolSequence", blue_noise::getSobolSequence());
			reflectionPass.noiseSets->setTexture("uScramblingRankingTile", blue_noise::getScramblingRanking(blue_noise::Blue_Noise_1SPP));

			initDenoise(w, h);
		}

		auto render(
			const vec4& ambientLight,
			const vec3& cameraPos,
			const vec3& cameraDelta,
			const mat4& viewProjInv,
			const mat4& prevViewProj,
			const CommandBuffer* cmd
		) -> std::shared_ptr<Texture>
		{
			if (!ReflectionEnable)
				return reflectionPass.reflectionOut;

			memcpy(&accumulator.pushConsts.viewProjInv, &viewProjInv, sizeof(mat4));
			memcpy(&accumulator.pushConsts.prevViewProj, &prevViewProj, sizeof(mat4));
			memcpy(&accumulator.pushConsts.cameraPos, &cameraPos, sizeof(vec3));
			memcpy(&accumulator.pushConsts.cameraDelta, &cameraDelta, sizeof(vec3));

			debug_utils::cmdBeginLabel("Raytracing Reflection");

			struct PushConsts
			{
				vec4  cameraPosition;
				mat4  viewProjInv;
				vec4 ambientLight;
				float bias;
				float trim;
				int frames;
				int mipmap;
			}pushConsts;
			static uint32_t frames = 0;

			memcpy(&pushConsts.cameraPosition, &cameraPos, sizeof(vec3));
			pushConsts.viewProjInv = viewProjInv;
			pushConsts.bias = Normal_Bias;
			pushConsts.trim = Trim;
			pushConsts.frames = frames++;
			pushConsts.mipmap = 0;
			pushConsts.ambientLight = ambientLight;

			ddgi::updateSet(reflectionPass.ddgiSet);
			reflectionPass.gbufferSet->setTexture("uNormalSampler", gbuffer::getGBuffer1());
			reflectionPass.gbufferSet->setTexture("uObjID", gbuffer::getGBuffer4());
			reflectionPass.gbufferSet->setTexture("uDepthSampler", gbuffer::getDepth());
			reflectionPass.gbufferSet->setTexture("uColorSampler", gbuffer::getGBuffer3());

			reflectionPass.outSets->setTexture("outColor", reflectionPass.reflectionOut);
			reflectionPass.reflectionShader->getPushConstant(0)->setData(&pushConsts);
			reflectionPass.gbufferSet->update(cmd);

			reflectionPass.outSets->update(cmd);
			reflectionPass.noiseSets->update(cmd);
			reflectionPass.ddgiSet->update(cmd);

			PipelineInfo info;
			info.pipelineName = "Reflection-Pipeline";
			info.shader = reflectionPass.reflectionShader;
			info.maxRayRecursionDepth = 1;
			auto tracePipeline = Pipeline::get(info);

			tracePipeline->bind(cmd);
			info.shader->bindPushConstants(cmd, tracePipeline.get());

			RenderDevice::get()->bindDescriptorSets(
				tracePipeline.get(), cmd, {
					reflectionPass.objectSets,
					reflectionPass.vertexSets,
					reflectionPass.indexSets,
					reflectionPass.textures,
					reflectionPass.gbufferSet,
					reflectionPass.outSets,
					reflectionPass.noiseSets,
					reflectionPass.ddgiSet
				}
			);
			tracePipeline->traceRays(cmd, gbuffer::getGBuffer1()->getWidth(), gbuffer::getGBuffer1()->getHeight(), 1);

			RenderDevice::get()->imageBarrier(cmd, { { reflectionPass.reflectionOut }, ShaderType::RayGen, ShaderType::Compute, AccessFlags::Write, AccessFlags::Read });

			debug_utils::cmdEndLabel();
			return Denoise ? denoise(cmd) : reflectionPass.reflectionOut;
		}
	}
}