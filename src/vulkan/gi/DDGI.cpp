//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////

#include "DDGI.h"
#include "Shader.h"
#include "Pipeline.h"
#include "DescriptorSet.h"
#include "RenderDevice.h"
#include "Textures.h"
#include "UniformBuffer.h"
#include "Randomizer.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Mesh.h"
#include "StorageBuffer.h"
#include "Tweak/Tweakable.h"

#include "Vulkan/VulkanDebug.h"
#include "../raytracing/vkGBuffer.h"
#include "../raytracing/vkRaytracing.h"
#include "../raytracing/RaytracingCommon.h"

#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace maple
{
	static maple::TweakBool VisualEnable = { "DDGI:Probe Debugger", false };
	static maple::TweakBool InfiniteBounces = { "DDGI:Infinite Bounces", true };
	static maple::TweakFloat VisualScale = { "DDGI:Probe Scale",0.1f ,0 ,5 };
	static maple::TweakBool DDGIEnable = { "DDGI:Enable", true };
	static maple::TweakBool DDGIRayEnable = { "DDGI:Rays Debug", false };
	static maple::TweakInt32 DebugProbe = { "DDGI:Debug Probe Id", 0, 0, 0 };
	static maple::TweakFloat Intensity = { "DDGI:Intensity", 2, 0, 10.f };
	static maple::TweakFloat ZOffset = { "DDGI:Z Offset", 0.0, -1000.f, 1000.f };
	static maple::TweakBool DClassify = { "DDGI:Classify",true };
	static maple::TweakFloat Threshold = { "DDGI:Classify Threshold",0.45,0,1 };
	static maple::TweakFloat Hysteresis = { "DDGI:Hysteresis", 0.998, 0, 1 };

	namespace ddgi
	{
		constexpr uint32_t IrradianceOctSize = 8;
		constexpr uint32_t DepthOctSize = 16;

		struct DDGIUniform
		{
			glm::vec4  startPosition;
			glm::vec4  step;        //align
			glm::ivec4 probeCounts;
			glm::ivec4 scrollOffset;

			float maxDistance = 5.f;
			float depthSharpness = 50.f;
			float hysteresis = 0.98f;
			float normalBias = 1.0f;

			float   ddgiGamma = 5.f;
			int32_t irradianceProbeSideLength = IrradianceOctSize;
			int32_t irradianceTextureWidth;
			int32_t irradianceTextureHeight;

			int32_t depthProbeSideLength = DepthOctSize;
			int32_t depthTextureWidth;
			int32_t depthTextureHeight;
			int32_t raysPerProbe = 128;
		};

		struct IrradianceVolume
		{
			float   probeDistance = 5.0f;
			int32_t raysPerProbe = 64;
			float   hysteresis = 0.98f;
			float   intensity = 1.2f;
			float   normalBias = 0.1f;
			float   depthSharpness = 50.f;
			float   ddgiGamma = 5.f;

			float width;
			float height;
			bool enable = true;
		};        // namespace ddgi


		static IrradianceVolume volume;
		static DDGIUniform uniform;
		static UniformBuffer::Ptr uniformBuffer;

		static struct DDGIPipeline
		{
			//raytrace pass.
			Texture2D::Ptr radiance;
			Texture2D::Ptr directionDepth;

			Texture2D::Ptr irradiance[2];
			Texture2D::Ptr depth[2];

			int32_t    frames = 0;
			Randomizer rand;

			glm::ivec3 counts;

			Texture2D::Ptr outColor;

			StorageBuffer::Ptr probeState;

		}ddgiInternal;

		static struct RaytracePass
		{
			Shader::Ptr   shader;
			Pipeline::Ptr pipeline;

			DescriptorSet::Ptr samplerDescriptor;
			DescriptorSet::Ptr outpuDescriptor;

			struct PushConstants
			{
				glm::mat4 randomOrientation;
				glm::vec4 ambientColor;
				uint32_t  numFrames;
				uint32_t  infiniteBounces = 1;
				float     intensity;
			} pushConsts;
			bool firstFrame = true;
		}raytracePass;

		static struct ProbeUpdatePass
		{
			Shader::Ptr irradanceShader;
			Shader::Ptr depthShader;

			Pipeline::Ptr irradianceProbePipeline;
			Pipeline::Ptr depthProbePipeline;

			struct PushConsts
			{
				uint32_t firstFrame = 0;
			} pushConsts;

			DescriptorSet::Ptr inputDescriptor[2];//ping pong
			DescriptorSet::Ptr radianceDepthDescriptor;
		}probeUpdatePass;

		static struct BorderUpdatePass
		{
			Shader::Ptr irradanceShader;
			Shader::Ptr depthShader;

			Pipeline::Ptr irradianceProbePipeline;
			Pipeline::Ptr depthProbePipeline;

			DescriptorSet::Ptr irradianceDescriptors;
			DescriptorSet::Ptr depthDescriptors;
		}borderUpdatePass;

		static struct IrradianceSets
		{
			DescriptorSet::Ptr depthDescriptor[2];
			DescriptorSet::Ptr irradianceDescriptor[2];
		}irradianceSets;

		static struct SampleProbePass
		{
			Shader::Ptr shader;

			Pipeline::Ptr pipeline;

			DescriptorSet::Ptr descriptors[2];

			DescriptorSet::Ptr commonSet;

		}sampleProbePass;

		static struct Classify
		{
			Shader::Ptr classifyShader;
			Shader::Ptr classifyResetShader;

			DescriptorSet::Ptr classifySet;
			DescriptorSet::Ptr classifyReset;
			struct {
				glm::vec4 planes[6];
				float threshold;
			}pushConsts;
		}classifyPass;

		static struct VisualizationPipeline
		{
			Shader::Ptr   shader;
			Mesh::Ptr     sphere;
			Texture::Ptr  nullTexture;

			std::vector<DescriptorSet::Ptr> descriptorSets;

			Shader::Ptr                     rayShader;
			Mesh::Ptr                       cube;
			std::vector<DescriptorSet::Ptr> raysDescriptorSets;
		}visualization;

		inline static auto initializeVisualize()
		{
			visualization.shader = Shader::create({
				{ ShaderType::Vertex,"shaders/ProbeVisualization.vert.spv" },
				{ ShaderType::Fragment,"shaders/ProbeVisualization.frag.spv" }
				});
			visualization.sphere = Mesh::createSphere();
			visualization.nullTexture = Texture2D::getTexture1X1White();
			for (uint32_t i = 0; i < 1; i++)
			{
				visualization.descriptorSets.emplace_back(DescriptorSet::create({ i, visualization.shader.get() }));
			}

			//#######################################################
			visualization.rayShader = Shader::create({
				{ ShaderType::Vertex, "shaders/RaysVisualization.vert.spv" },
				{ ShaderType::Fragment,"shaders/RaysVisualization.frag.spv" }
				});
			visualization.cube = Mesh::createCube();
			visualization.raysDescriptorSets.emplace_back(DescriptorSet::create({ 0, visualization.rayShader.get() }));

			visualization.descriptorSets[0]->setStorageBuffer("ProbeState", ddgiInternal.probeState);
		}

		inline static auto initializeProbeGrid()
		{
			uint32_t totalProbes = uniform.probeCounts.x * uniform.probeCounts.y * uniform.probeCounts.z;
			MAPLE_ASSERT(totalProbes < std::numeric_limits<int16_t>::max(), "too many probes");
			{
				ddgiInternal.radiance = Texture2D::create();
				ddgiInternal.radiance->setName("DDGI Raytrace Radiance");
				ddgiInternal.radiance->buildTexture(TextureFormat::RGBA16, volume.raysPerProbe, totalProbes);

				ddgiInternal.directionDepth = Texture2D::create();
				ddgiInternal.directionDepth->setName("DDGI Raytrace Direction Depth");
				ddgiInternal.directionDepth->buildTexture(TextureFormat::RGBA16, volume.raysPerProbe, totalProbes);
			}

			{
				// 1-pixel of padding surrounding each probe, 1-pixel padding surrounding entire texture for alignment.
				const int32_t irradianceWidth = (IrradianceOctSize + 2) * uniform.probeCounts.x * uniform.probeCounts.y + 2;
				const int32_t irradianceHeight = (IrradianceOctSize + 2) * uniform.probeCounts.z + 2;
				const int32_t depthWidth = (DepthOctSize + 2) * uniform.probeCounts.x * uniform.probeCounts.y + 2;
				const int32_t depthHeight = (DepthOctSize + 2) * uniform.probeCounts.z + 2;

				uniform.irradianceTextureWidth = irradianceWidth;
				uniform.irradianceTextureHeight = irradianceHeight;

				uniform.depthTextureWidth = depthWidth;
				uniform.depthTextureHeight = depthHeight;

				for (int32_t i = 0; i < 2; i++)
				{
					ddgiInternal.irradiance[i] = Texture2D::create();
					ddgiInternal.depth[i] = Texture2D::create();

					std::vector<uint8_t> data;
					data.resize(sizeof(uint8_t) * 2 * 2, 0);
					ddgiInternal.depth[i]->setData(data.data());
					ddgiInternal.depth[i]->buildTexture(TextureFormat::RG16F, depthWidth, depthHeight);
					ddgiInternal.depth[i]->setName("DDGI Depth Probe Grid " + std::to_string(i));

					ddgiInternal.irradiance[i]->buildTexture(TextureFormat::RGBA16, irradianceWidth, irradianceHeight);
					ddgiInternal.irradiance[i]->setName("DDGI Irradiance Probe Grid " + std::to_string(i));

					data.resize(sizeof(uint8_t) * 2 * 4, 0);
					ddgiInternal.irradiance[i]->setData(data.data());
				}
			}
		}

		inline static auto initializeClassify()
		{
			classifyPass.classifyShader = Shader::create({
				{ ShaderType::Compute, "shaders/ProbeClassify.comp.spv" }
				});

			classifyPass.classifySet = DescriptorSet::create({ 0,classifyPass.classifyShader.get() });
			classifyPass.classifySet->setTexture("iDirectionDistance", ddgiInternal.directionDepth);
			classifyPass.classifySet->setStorageBuffer("ProbeState", ddgiInternal.probeState);
			classifyPass.classifySet->setBuffer("DDGIUBO", uniformBuffer);

			/*	classifyPass.classifyResetShader = Shader::create({
					{ ShaderType::Compute, "shaders/ProbeClassifyReset.comp.spv" }
					});

				classifyPass.classifyReset = DescriptorSet::create({ 0,classifyPass.classifyResetShader.get() });
				classifyPass.classifyReset->setStorageBuffer("ProbeState", ddgiInternal.probeState);
				classifyPass.classifyReset->setBuffer("DDGIUBO", uniformBuffer);*/
		}

		inline static auto traceRays(const CommandBuffer* cmd)
		{
			maple::debug_utils::cmdBeginLabel("DDGI TraceRays");
			raytracePass.samplerDescriptor->setTexture("uIrradiance", ddgiInternal.irradiance[1 - gbuffer::getPingPong()]);
			raytracePass.samplerDescriptor->setTexture("uDepth", ddgiInternal.depth[1 - gbuffer::getPingPong()]);
			raytracePass.samplerDescriptor->update(cmd);

			raytracePass.outpuDescriptor->setTexture("iRadiance", ddgiInternal.radiance);
			raytracePass.outpuDescriptor->setTexture("iDirectionDistance", ddgiInternal.directionDepth);
			raytracePass.outpuDescriptor->update(cmd);

			raytracePass.pushConsts.infiniteBounces = (InfiniteBounces.var && ddgiInternal.frames != 0) ? 1 : 0;
			raytracePass.pushConsts.intensity = Intensity.var;
			//volume.intensity;
			raytracePass.pushConsts.numFrames = ddgiInternal.frames;

			auto vec3 = glm::normalize(glm::vec3(
				ddgiInternal.rand.nextReal(-1.f, 1.f),
				ddgiInternal.rand.nextReal(-1.f, 1.f),
				ddgiInternal.rand.nextReal(-1.f, 1.f))
			);

			auto pipeline = raytracePass.pipeline;

			raytracePass.pushConsts.randomOrientation = glm::mat4_cast(
				glm::angleAxis(ddgiInternal.rand.nextReal(0.f, 1.f) * float(M_PI) * 2.0f, vec3)
			);

			raytracePass.pipeline->bind(cmd);
			for (auto& push : pipeline->getShader()->getPushConstants())
			{
				push.setData(&raytracePass.pushConsts);
			}

			pipeline->getShader()->bindPushConstants(cmd, pipeline.get());

			RenderDevice::get()->bindDescriptorSets(pipeline.get(), cmd, {
					getObjectSets(),
					getVertexSets(),
					getIndexSets(),
					getSamplerSets(),
					raytracePass.samplerDescriptor,
					raytracePass.outpuDescriptor
				});

			uint32_t probleCounts = uniform.probeCounts.x * uniform.probeCounts.y * uniform.probeCounts.z;
			raytracePass.pipeline->traceRays(cmd, uniform.raysPerProbe, probleCounts, 1);
			raytracePass.pipeline->end(cmd);

			RenderDevice::get()->imageBarrier(cmd, { {ddgiInternal.radiance, ddgiInternal.directionDepth},
																ShaderType::RayGen,
																ShaderType::Compute,
																AccessFlags::Write,
																AccessFlags::Read });
			maple::debug_utils::cmdEndLabel();
		}

		inline static auto probeUpdate(const CommandBuffer* cmd, Pipeline::Ptr pipeline, DescriptorSet::Ptr descriptor)
		{
			probeUpdatePass.pushConsts.firstFrame = ddgiInternal.frames == 0 ? 1 : 0;
			const uint32_t dispatchX = static_cast<uint32_t>(uniform.probeCounts.x * uniform.probeCounts.y);
			const uint32_t dispatchY = static_cast<uint32_t>(uniform.probeCounts.z);
			RenderDevice::get()->dispatch(cmd, dispatchX, dispatchY, 1, pipeline.get(), &probeUpdatePass.pushConsts, {
				descriptor,
				probeUpdatePass.inputDescriptor[gbuffer::getPingPong()],
				probeUpdatePass.radianceDepthDescriptor
				});
		}

		inline static auto probeUpdate(const CommandBuffer* cmd)
		{
			maple::debug_utils::cmdBeginLabel("DDGI ProbeUpdate");
			auto writeIdx = 1 - gbuffer::getPingPong();
			maple::debug_utils::cmdBeginLabel("DDGI ProbeUpdate Irradiance");
			probeUpdate(cmd, probeUpdatePass.irradianceProbePipeline, irradianceSets.irradianceDescriptor[writeIdx]);
			maple::debug_utils::cmdEndLabel();

			maple::debug_utils::cmdBeginLabel("DDGI ProbeUpdate-Depth");
			probeUpdate(cmd, probeUpdatePass.depthProbePipeline, irradianceSets.depthDescriptor[writeIdx]);
			RenderDevice::get()->memoryBarrier(cmd, ShaderType::Compute, ShaderType::Compute, AccessFlags::Write, AccessFlags::ReadWrite);
			maple::debug_utils::cmdEndLabel();
			maple::debug_utils::cmdEndLabel();
		}

		inline static auto borderUpdate(const CommandBuffer* cmd, Pipeline::Ptr pipeline, DescriptorSet::Ptr descriptor)
		{
			const uint32_t dispatchX = static_cast<uint32_t>(uniform.probeCounts.x * uniform.probeCounts.y);
			const uint32_t dispatchY = static_cast<uint32_t>(uniform.probeCounts.z);
			RenderDevice::get()->dispatch(cmd, dispatchX, dispatchY, 1, pipeline.get(), nullptr, { descriptor });
		}

		inline static auto borderUpdate(const CommandBuffer* cmd)
		{
			maple::debug_utils::cmdBeginLabel("DDGI BorderUpdate");
			auto writeIdx = 1 - gbuffer::getPingPong();
			maple::debug_utils::cmdBeginLabel("DDGI Irradiance BorderUpdate");
			borderUpdate(cmd, borderUpdatePass.irradianceProbePipeline, irradianceSets.irradianceDescriptor[writeIdx]);
			maple::debug_utils::cmdEndLabel();

			maple::debug_utils::cmdBeginLabel("DDGI Depth BorderUpdate");
			borderUpdate(cmd, borderUpdatePass.depthProbePipeline, irradianceSets.depthDescriptor[writeIdx]);
			RenderDevice::get()->memoryBarrier(cmd, ShaderType::Compute, ShaderType::Compute, AccessFlags::Write, AccessFlags::ReadWrite);
			maple::debug_utils::cmdEndLabel();
			maple::debug_utils::cmdEndLabel();
		}

		inline static auto sampleProbe(const CommandBuffer* cmd, const glm::vec3& cameraPosition, const glm::mat4& viewProjInv)
		{
			maple::debug_utils::cmdBeginLabel("DDGI SampleProbe");


			auto writeIdx = /*1 - */gbuffer::getPingPong();

			sampleProbePass.descriptors[0]->setTexture("outColor", ddgiInternal.outColor);
			sampleProbePass.descriptors[1]->setTexture("uDepthSampler", gbuffer::getDepth());
			sampleProbePass.descriptors[1]->setTexture("uNormalSampler", gbuffer::getGBuffer1());
			sampleProbePass.descriptors[1]->setTexture("uColorSampler", gbuffer::getGBuffer3());

			sampleProbePass.commonSet->setTexture("uIrradiance", ddgiInternal.irradiance[writeIdx]);
			sampleProbePass.commonSet->setTexture("uDepth", ddgiInternal.depth[writeIdx]);
			sampleProbePass.commonSet->setUniform("DDGIUBO", "ddgi", &uniform);
			sampleProbePass.commonSet->setStorageBuffer("ProbeState", ddgiInternal.probeState);
			struct Consts
			{
				glm::vec4 cameraPosition;
				glm::mat4 viewProjInv;
				float intensity;
			}pushConst;

			pushConst.cameraPosition = glm::vec4(cameraPosition, 1.f);
			pushConst.viewProjInv = viewProjInv;
			pushConst.intensity = Intensity.var;
			const uint32_t dispatchX = static_cast<uint32_t>(std::ceil(float(volume.width) / 32.f));
			const uint32_t dispatchY = static_cast<uint32_t>(std::ceil(float(volume.height) / 32.f));
			RenderDevice::get()->dispatch(cmd, dispatchX, dispatchY, 1, sampleProbePass.pipeline.get(), &pushConst, {
				sampleProbePass.descriptors[0],sampleProbePass.descriptors[1],sampleProbePass.commonSet
				});
			maple::debug_utils::cmdEndLabel();
		}

		inline static auto classify(const CommandBuffer* cmd)
		{
			const uint32_t dispatchX = (uniform.probeCounts.x * uniform.probeCounts.y * uniform.probeCounts.z + 31) / 32;
			/*{
				PipelineInfo info;
				info.pipelineName = "Classify-Reset";
				info.shader = classifyPass.classifyResetShader;
				auto pipeline = Pipeline::get(info);
				RenderDevice::get()->dispatch(cmd, dispatchX, 1, 1, pipeline.get(), nullptr, { classifyPass.classifyReset });
			}*/
			{
				PipelineInfo info;
				info.pipelineName = "Classify";
				info.shader = classifyPass.classifyShader;
				auto pipeline = Pipeline::get(info);
				RenderDevice::get()->dispatch(cmd, dispatchX, 1, 1, pipeline.get(), &classifyPass.pushConsts, { classifyPass.classifySet });
			}
		}

		inline static auto debugRays(const CommandBuffer* cmd, const glm::mat4& viewProj)
		{
			if (DDGIRayEnable.var)
			{
				debug_utils::cmdBeginLabel("Probe-Rays");

				auto counts = uniform.probeCounts.x * uniform.probeCounts.y * uniform.probeCounts.z;

				DebugProbe.upperBound = counts;

				PipelineInfo info;
				info.shader = visualization.rayShader;
				info.pipelineName = "Rays-Debugs";

				info.polygonMode = PolygonMode::Fill;
				info.blendMode = BlendMode::SrcAlpha;
				info.dstBlendMode = BlendMode::OneMinusSrcAlpha;
				info.clearTargets = false;
				info.swapChainTarget = false;
				info.transparencyEnabled = true;
				info.depthTest = true;
				info.depthFunc = StencilType::LessOrEqual;
				info.depthTarget = gbuffer::getDepth();
				info.colorTargets[0] = gbuffer::getGBufferColor();

				auto rayPipeline = Pipeline::get(info);

				visualization.raysDescriptorSets[0]->setBuffer("DDGIUBO", uniformBuffer);
				visualization.raysDescriptorSets[0]->setTexture("uInputRadiance", ddgiInternal.radiance);
				visualization.raysDescriptorSets[0]->setTexture("uInputDirectionDepth", ddgiInternal.directionDepth);
				visualization.raysDescriptorSets[0]->update(cmd);

				rayPipeline->bind(cmd);

				struct PushConsts
				{
					glm::mat4 viewProj;
					float scale;
					int32_t probeId;
				}consts;

				consts.scale = VisualScale;
				consts.probeId = DebugProbe;
				consts.viewProj = viewProj;

				if (auto push = visualization.rayShader->getPushConstant(0))
				{
					push->setData(&consts);
				}

				maple::RenderDevice::get()->bindDescriptorSets(rayPipeline.get(), cmd, visualization.raysDescriptorSets);
				visualization.rayShader->bindPushConstants(cmd, rayPipeline.get());
				visualization.cube->indexBuffer->bind(cmd);
				visualization.cube->vertexBuffer->bind(cmd, rayPipeline.get());
				rayPipeline->drawIndexed(cmd, visualization.cube->indexBuffer->getCount(), uniform.raysPerProbe, 0, 0, 0);
				rayPipeline->end(cmd);

				debug_utils::cmdEndLabel();
			}
		}

		auto init(uint32_t width, uint32_t height) -> void
		{
			ddgiInternal.outColor = Texture2D::create(width, height, nullptr, { TextureFormat::RGBA16 });

			ddgiInternal.counts = { 40, 40, 10 }; // 9000 * 128

			int32_t totalCount = ddgiInternal.counts.x * ddgiInternal.counts.y * ddgiInternal.counts.z;

			int32_t bytes = totalCount * sizeof(int8_t);

			std::vector<int8_t> initBuffer(totalCount, 0);

			ddgiInternal.probeState = StorageBuffer::create(bytes, initBuffer.data(), { false, MemoryUsage::MEMORY_USAGE_GPU_ONLY });

			volume.width = width;
			volume.height = height;

			{
				raytracePass.shader = Shader::create({
					{ maple::ShaderType::RayGen, "shaders/DDGI.rgen.spv" },
					{ maple::ShaderType::RayCloseHit, "shaders/DDGI.rchit.spv"},
					{ maple::ShaderType::RayMiss, "shaders/DDGI.rmiss.spv" }
					}, { {"VertexBuffer", MAX_SCENE_MESH_INSTANCE_COUNT},
						{"IndexBuffer", MAX_SCENE_MESH_INSTANCE_COUNT},
						{"uSamplers", MAX_SCENE_MATERIAL_TEXTURE_COUNT} }
				);

				PipelineInfo info;
				info.pipelineName = "DDGI";
				info.shader = raytracePass.shader;
				info.maxRayRecursionDepth = 1;
				raytracePass.pipeline = Pipeline::get(info);

				raytracePass.samplerDescriptor = DescriptorSet::create({ 4, raytracePass.shader.get() });
				raytracePass.outpuDescriptor = DescriptorSet::create({ 5, raytracePass.shader.get() });
			}

			{
				PipelineInfo info;
				probeUpdatePass.irradanceShader = Shader::create({ {ShaderType::Compute, "shaders/IrradianceProbeUpdate.comp.spv"} });
				info.pipelineName = "IrradianceProbeUpdatePipeline";
				info.shader = probeUpdatePass.irradanceShader;
				probeUpdatePass.irradianceProbePipeline = Pipeline::get(info);
			}

			{
				PipelineInfo info;
				probeUpdatePass.depthShader = Shader::create({ {ShaderType::Compute, "shaders/DepthProbeUpdate.comp.spv"} });
				info.pipelineName = "DepthProbeUpdatePipeline";
				info.shader = probeUpdatePass.depthShader;
				probeUpdatePass.depthProbePipeline = Pipeline::get(info);
			}

			probeUpdatePass.inputDescriptor[0] = DescriptorSet::create({ 1, probeUpdatePass.irradanceShader.get() });
			probeUpdatePass.inputDescriptor[1] = DescriptorSet::create({ 1, probeUpdatePass.irradanceShader.get() });
			probeUpdatePass.radianceDepthDescriptor = DescriptorSet::create({ 2, probeUpdatePass.irradanceShader.get() });


			{
				PipelineInfo info;
				borderUpdatePass.irradanceShader = Shader::create({ {ShaderType::Compute, "shaders/IrradianceBorderUpdate.comp.spv"} });
				info.pipelineName = "IrradianceBorderUpdatePipeline";
				info.shader = borderUpdatePass.irradanceShader;
				borderUpdatePass.irradianceProbePipeline = Pipeline::get(info);
			}

			{
				PipelineInfo info;
				borderUpdatePass.depthShader = Shader::create({ {ShaderType::Compute, "shaders/DepthBorderUpdate.comp.spv"} });
				info.pipelineName = "DepthBorderUpdatePipeline";
				info.shader = borderUpdatePass.depthShader;
				borderUpdatePass.depthProbePipeline = Pipeline::get(info);
			}

			{
				PipelineInfo info;
				sampleProbePass.shader = Shader::create({ {ShaderType::Compute, "shaders/SampleProbe.comp.spv"} });
				info.pipelineName = "SampleProbePipeline";
				info.shader = sampleProbePass.shader;
				sampleProbePass.pipeline = Pipeline::get(info);
				for (uint32_t i = 0; i < 2; i++)
				{
					sampleProbePass.descriptors[i] = DescriptorSet::create({ i , sampleProbePass.shader.get() });
				}
			}

			sampleProbePass.commonSet = DescriptorSet::create({ 2 , sampleProbePass.shader.get() });

			PipelineInfo info;
			{
				uniform.probeCounts = glm::ivec4(ddgiInternal.counts, 1);
				LOGI("ProbeCounts : {},{},{}", uniform.probeCounts.x, uniform.probeCounts.y, uniform.probeCounts.z);
				uint32_t totalProbes = uniform.probeCounts.x * uniform.probeCounts.y * uniform.probeCounts.z;
				uniform.startPosition = {};
				uniform.step = glm::vec4(volume.probeDistance);
				uniform.maxDistance = volume.probeDistance * 1.5f;
				uniform.depthSharpness = volume.depthSharpness;
				uniform.hysteresis = volume.hysteresis;
				uniform.normalBias = volume.normalBias;
				uniform.ddgiGamma = volume.ddgiGamma;
				uniform.irradianceTextureWidth = (IrradianceOctSize + 2) * uniform.probeCounts.x * uniform.probeCounts.y + 2;
				uniform.irradianceTextureHeight = (IrradianceOctSize + 2) * uniform.probeCounts.z + 2;
				uniform.depthTextureWidth = (DepthOctSize + 2) * uniform.probeCounts.x * uniform.probeCounts.y + 2;
				uniform.depthTextureHeight = (DepthOctSize + 2) * uniform.probeCounts.z + 2;
			}

			uniformBuffer = UniformBuffer::create(sizeof(DDGIUniform), &uniform);

			raytracePass.samplerDescriptor->setBuffer("DDGIUBO", uniformBuffer);
			raytracePass.samplerDescriptor->setStorageBuffer("ProbeState", ddgiInternal.probeState);
			sampleProbePass.commonSet->setBuffer("DDGIUBO", uniformBuffer);
			initializeVisualize();
			initializeProbeGrid();

			//////////////////////////////

			probeUpdatePass.radianceDepthDescriptor->setTexture("uInputRadiance", ddgiInternal.radiance);
			probeUpdatePass.radianceDepthDescriptor->setTexture("uInputDirectionDepth", ddgiInternal.directionDepth);
			probeUpdatePass.radianceDepthDescriptor->setStorageBuffer("ProbeState", ddgiInternal.probeState);
			probeUpdatePass.radianceDepthDescriptor->setBuffer("DDGIUBO", uniformBuffer);

			for (auto i = 0; i < 2; i++)
			{
				probeUpdatePass.inputDescriptor[i]->setTexture("uInputIrradiance", ddgiInternal.irradiance[i]);
				probeUpdatePass.inputDescriptor[i]->setTexture("uInputDepth", ddgiInternal.depth[i]);
			}

			for (auto i = 0; i < 2; i++)
			{
				irradianceSets.depthDescriptor[i] = DescriptorSet::create({ 0, probeUpdatePass.depthShader.get() });
				irradianceSets.irradianceDescriptor[i] = DescriptorSet::create({ 0 , probeUpdatePass.irradanceShader.get() });

				irradianceSets.depthDescriptor[i]->setTexture("uOutDepth", ddgiInternal.depth[i]);
				irradianceSets.irradianceDescriptor[i]->setTexture("uOutIrradiance", ddgiInternal.irradiance[i]);
			}

			initializeClassify();
		}

		auto updateSet(const std::shared_ptr< DescriptorSet>& set) -> void
		{
			set->setTexture("uIrradiance", ddgiInternal.irradiance[1 - gbuffer::getPingPong()]);
			set->setTexture("uDepth", ddgiInternal.depth[1 - gbuffer::getPingPong()]);
			set->setBuffer("DDGIUBO", uniformBuffer);
		}

		auto renderDebugProbe(const CommandBuffer* cmd,
			const glm::vec3& startPosition,
			const glm::mat4& viewProj) -> void
		{
			if (VisualEnable.var)
			{
				debug_utils::cmdBeginLabel("Probe-Visualization");

				PipelineInfo info;
				info.shader = visualization.shader;
				info.pipelineName = "Probe-Visualization";
				info.polygonMode = PolygonMode::Fill;
				info.blendMode = BlendMode::Zero;
				info.dstBlendMode = BlendMode::One;
				info.clearTargets = false;
				info.swapChainTarget = false;
				info.transparencyEnabled = false;
				info.depthTest = true;
				info.depthFunc = StencilType::LessOrEqual;
				info.depthTarget = gbuffer::getDepth();
				info.colorTargets[0] = gbuffer::getGBufferColor();
				auto pipeline = Pipeline::get(info);

				visualization.descriptorSets[0]->setBuffer("DDGIUBO", uniformBuffer);
				visualization.descriptorSets[0]->setTexture("uIrradiance", ddgiInternal.irradiance[1 - gbuffer::getPingPong()]);
				visualization.descriptorSets[0]->setTexture("uDepth", ddgiInternal.depth[1 - gbuffer::getPingPong()]);
				visualization.descriptorSets[0]->update(cmd);

				pipeline->bind(cmd);

				if (auto push = visualization.shader->getPushConstant(0))
				{
					push->setValue("scale", &VisualScale.var);
					push->setValue("viewProj", &viewProj);
				}

				maple::RenderDevice::get()->bindDescriptorSets(pipeline.get(), cmd, visualization.descriptorSets);

				visualization.shader->bindPushConstants(cmd, pipeline.get());
				visualization.sphere->indexBuffer->bind(cmd);
				visualization.sphere->vertexBuffer->bind(cmd, pipeline.get());

				auto probeCount = uniform.probeCounts.x * uniform.probeCounts.y * uniform.probeCounts.z;
				pipeline->drawIndexed(cmd, visualization.sphere->indexBuffer->getCount(), probeCount, 0, 0, 0);
				pipeline->end(cmd);

				debug_utils::cmdEndLabel();
			}

			debugRays(cmd, viewProj);
		}

		auto render(const CommandBuffer* cmd,
			const glm::vec3& startPosition,
			const glm::mat4& viewProjInv,
			const glm::vec4* frustums,
			const glm::vec4& ambientColor) ->std::shared_ptr<Texture>
		{
			if (!DDGIEnable.var)
				return nullptr;

			glm::vec3 offset = {};
			glm::vec3 boxSize = glm::vec3(uniform.probeCounts - glm::ivec4(2, 2, 2, 0)) * volume.probeDistance / 2.f;

			for (int32_t axis = 0; axis < 3; axis++)
			{
				float distance = volume.probeDistance;
				const float value = startPosition[axis] / distance;
				const int32_t scroll = value >= 0.0f ? (int32_t)std::floor(value) : (int32_t)std::ceil(value);
				offset[axis] = scroll * distance;
				uniform.scrollOffset[axis] = (scroll % uniform.probeCounts[axis]) * -1;
				if (uniform.scrollOffset[axis] < 0)
					uniform.scrollOffset[axis] += uniform.probeCounts[axis];
			}

			uniform.startPosition = glm::vec4(offset - boxSize + glm::vec3{ 0, 0, ZOffset.var }, 1);
			uniformBuffer->setData(&uniform);

			memcpy(classifyPass.pushConsts.planes, frustums, sizeof(glm::vec4) * 6);
			classifyPass.pushConsts.threshold = Threshold.var;

			if (ddgiInternal.frames == 0)
			{
				for (auto i = 0; i < 2; i++)
				{
					RenderDevice::get()->clearRenderTarget(ddgiInternal.irradiance[i], cmd);
					RenderDevice::get()->clearRenderTarget(ddgiInternal.depth[i], cmd);
				}
				RenderDevice::get()->clearRenderTarget(ddgiInternal.radiance, cmd);
				RenderDevice::get()->clearRenderTarget(ddgiInternal.directionDepth, cmd);
			}

			raytracePass.pushConsts.ambientColor = ambientColor;
			uniform.hysteresis = Hysteresis.var;

			maple::debug_utils::cmdBeginLabel("DDGI Begin");
			traceRays(cmd);
			probeUpdate(cmd);
			borderUpdate(cmd);
			if (DClassify.var)
				classify(cmd);
			sampleProbe(cmd, startPosition, viewProjInv);
			maple::debug_utils::cmdEndLabel();
			ddgiInternal.frames++;
			return ddgiInternal.outColor;
		}
	}
}