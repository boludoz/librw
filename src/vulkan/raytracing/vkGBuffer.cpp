//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////

#ifdef RW_VULKAN

#include "../../rwbase.h"

#include "../../rwplg.h"

#include "../../rwengine.h"

#include "../../rwpipeline.h"

#include "../../rwobjects.h"

#include "../../rwerror.h"

#include "../../rwrender.h"

#include "vkGBuffer.h"
#include "Textures.h"
#include "Shader.h"
#include "Pipeline.h"
#include "RenderDevice.h"
#include "CommandBuffer.h"
#include "Tweak/Tweakable.h"

#include "../rwvk.h"
#include "../rwvkimpl.h"

namespace maple
{
	struct GBuffer
	{
		std::shared_ptr<Texture2D> colorBuffer[2];
		std::shared_ptr<Texture2D> normalBuffer[2];//normal (rg) motionVector(ba)
		std::shared_ptr<Texture2D> gbuffer2[2];//objId, curvature, linearZ, specular
		std::shared_ptr<Texture2D> gbuffer3[2];//
		std::shared_ptr<Texture2D> gbuffer4[2];//

		std::shared_ptr<TextureDepth> depth[2];
	};
	static GBuffer buffer;
	static Shader::Ptr deferredShading;
	static std::shared_ptr<maple::DescriptorSet> deferredSet;
	static int32_t ping = 0;

	enum GBufferTypes
	{
		Color,
		PathTrace,
		Reflection,
		BaseColor,
		Visibility,
		Length
	};

	static maple::TweakInt32 GBufferType = { "GBuffer:GBufferTypes", 0, 0, GBufferTypes::Length - 1 };
	static maple::TweakFloat Intensity = { "GBuffer:Intensity", 1,0,10 };

	namespace gbuffer
	{
		auto init() -> void
		{
			for (auto i = 0; i < 2; i++)
			{
				buffer.colorBuffer[i] = Texture2D::create(rw::vulkan::vkGlobals.winWidth, rw::vulkan::vkGlobals.winHeight, nullptr);
				buffer.colorBuffer[i]->setName("GBufferColor" + std::to_string(i));
				buffer.depth[i] = TextureDepth::create(rw::vulkan::vkGlobals.winWidth, rw::vulkan::vkGlobals.winHeight, true);
				buffer.depth[i]->setName("GBufferDepth" + std::to_string(i));
				buffer.normalBuffer[i] = Texture2D::create(rw::vulkan::vkGlobals.winWidth, rw::vulkan::vkGlobals.winHeight, nullptr, maple::TextureParameters{ TextureFormat::RGBA16 });
				buffer.normalBuffer[i]->setName("GBufferNormal" + std::to_string(i));
				buffer.gbuffer2[i] = Texture2D::create(rw::vulkan::vkGlobals.winWidth, rw::vulkan::vkGlobals.winHeight, nullptr, maple::TextureParameters{ TextureFormat::RGBA16 });
				buffer.gbuffer2[i]->setName("GBufferPBR" + std::to_string(i));

				buffer.gbuffer3[i] = Texture2D::create(rw::vulkan::vkGlobals.winWidth, rw::vulkan::vkGlobals.winHeight, nullptr, maple::TextureParameters{ TextureFormat::RGBA8 });
				buffer.gbuffer3[i]->setName("GBuffer3" + std::to_string(i));

				buffer.gbuffer4[i] = Texture2D::create(rw::vulkan::vkGlobals.winWidth, rw::vulkan::vkGlobals.winHeight, nullptr, maple::TextureParameters{ TextureFormat::RGBA16 });
				buffer.gbuffer4[i]->setName("GBuffer4" + std::to_string(i));
			}

			deferredShading = Shader::create(
				{
					{maple::ShaderType::Vertex,"shaders/DeferredShading.vert.spv"} ,
					{maple::ShaderType::Fragment,"shaders/DeferredShading.frag.spv"}
				}
			);
			deferredSet = maple::DescriptorSet::create({ 0, deferredShading.get() });
		}

		auto render(
			std::shared_ptr<maple::UniformBuffer> cameraUniform,
			const std::shared_ptr<Texture>& color,
			const std::shared_ptr<Texture>& depth,
			const std::shared_ptr<Texture>& shadow,
			const std::shared_ptr<Texture>& reflection,
			const std::shared_ptr<Texture>& indirect,
			const std::shared_ptr<Texture>& pathOut,
			const std::shared_ptr<StorageBuffer>& ssbo,
			const mat4& viewProjInv,
			const CommandBuffer* cmd) -> void
		{
			maple::PipelineInfo info{};
			info.pipelineName = "DeferredShading";
			info.shader = deferredShading;
			info.cullMode = maple::CullMode::None;
			info.transparencyEnabled = false;
			info.clearTargets = true;
			info.swapChainTarget = true;

			auto pipeline = maple::Pipeline::get(info);

			deferredSet->setTexture("uColor",
				GBufferType.var == Reflection? reflection == nullptr ? maple::Texture2D::getTexture1X1Black() : reflection : 
				GBufferType.var == BaseColor ? buffer.gbuffer3[ping] :
				GBufferType.var == PathTrace ? pathOut == nullptr ? maple::Texture2D::getTexture1X1Black() : pathOut : buffer.colorBuffer[ping]
			);
			deferredSet->setTexture("uNormal", buffer.normalBuffer[ping]);
			deferredSet->setTexture("uPBR", buffer.gbuffer2[ping]);
			deferredSet->setTexture("uObjID", buffer.gbuffer4[ping]);
			deferredSet->setTexture("uDepth", buffer.depth[ping]);
			deferredSet->setTexture("uShadow", shadow == nullptr ? maple::Texture2D::getTexture1X1White() : shadow);
			deferredSet->setBuffer("CameraUniform", cameraUniform);
			deferredSet->setTexture("uReflection", reflection == nullptr ? maple::Texture2D::getTexture1X1Black() : reflection);
			deferredSet->setTexture("uIndirect", indirect == nullptr ? maple::Texture2D::getTexture1X1Black() : indirect);
			deferredSet->setStorageBuffer("ObjectBuffer", ssbo);
			deferredSet->update(cmd);
			pipeline->bind(cmd);

			struct {
				mat4 viewProjInv;
				int32_t var;
				float intensity;
			}consts;
			consts.var = GBufferType;
			consts.viewProjInv = viewProjInv;
			consts.intensity = Intensity;

			info.shader->getPushConstant(0)->setData(&consts);
			info.shader->bindPushConstants(cmd, pipeline.get());
			maple::RenderDevice::get()->bindDescriptorSets(pipeline.get(), cmd, { deferredSet });
			maple::RenderDevice::get()->drawArraysInternal(cmd, maple::DrawType::Triangle, 3);
			pipeline->end(cmd);
			ping = 1 - ping;
		}

		auto getGBufferColor(bool prev)->std::shared_ptr<Texture>
		{
			return buffer.colorBuffer[prev ? 1 - ping : ping];
		}

		auto getGBuffer1(bool prev)->std::shared_ptr<Texture>
		{
			return buffer.normalBuffer[prev ? 1 - ping : ping];
		}

		auto getGBuffer2(bool prev)->std::shared_ptr<Texture>
		{
			return buffer.gbuffer2[prev ? 1 - ping : ping];
		}

		auto getDepth(bool prev) ->std::shared_ptr<Texture>
		{
			return buffer.depth[prev ? 1 - ping : ping];
		}

		auto getGBuffer3(bool prev)->std::shared_ptr<Texture>
		{
			return buffer.gbuffer3[prev ? 1 - ping : ping];
		}

		auto getGBuffer4(bool prev /*= false*/) ->std::shared_ptr<Texture>
		{
			return buffer.gbuffer4[prev ? 1 - ping : ping];
		}

		auto getPingPong() ->uint32_t
		{
			return ping;
		}

		auto clear(const CommandBuffer* cmdBuffer) -> void
		{
			maple::RenderDevice::get()->clearRenderTarget(maple::gbuffer::getGBuffer1(), cmdBuffer);
			maple::RenderDevice::get()->clearRenderTarget(maple::gbuffer::getGBuffer2(), cmdBuffer);
			maple::RenderDevice::get()->clearRenderTarget(maple::gbuffer::getGBuffer3(), cmdBuffer);
			maple::RenderDevice::get()->clearRenderTarget(maple::gbuffer::getGBuffer4(), cmdBuffer);
		}

		auto clear(CommandBuffer* cmdBuffer, const maple::vec4& value, const maple::ivec4& r) -> void
		{
			cmdBuffer->clearAttachments(maple::gbuffer::getGBuffer1(), value, r);
			cmdBuffer->clearAttachments(maple::gbuffer::getGBuffer2(), value, r);
			cmdBuffer->clearAttachments(maple::gbuffer::getGBuffer3(), value, r);
			cmdBuffer->clearAttachments(maple::gbuffer::getGBuffer4(), value, r);
		}
	};
}

#endif