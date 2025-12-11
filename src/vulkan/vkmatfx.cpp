#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>


#include "../rwbase.h"

#include "../rwerror.h"

#include "../rwplg.h"

#include "../rwrender.h"

#include "../rwengine.h"

#include "../rwpipeline.h"

#include "../rwobjects.h"

#include "../rwanim.h"

#include "../rwplugins.h"

#ifdef RW_VULKAN

#include "rwvk.h"

#include "rwvkshader.h"

#include "rwvkplg.h"

#include "rwvkimpl.h"

#include "Console.h"

#include "DescriptorSet.h"
#include "HashCode.h"
#include "GraphicsContext.h"
#include "SwapChain.h"
#include "UniformBuffer.h"

#include "raytracing/vkTopLevel.h"

#endif

namespace rw
{
	namespace vulkan
	{
#ifdef RW_VULKAN

		struct MatfxUniform
		{
			float fxparams[4];
			float texMatrix[16];
			float colorClamp[4];
			float envColor[4];
		};

		static int32_t MATFX_INIT_SIZE = 1000;

		static std::unordered_map<rw::Texture*, maple::DescriptorSet::Ptr> matFXSets;
		static Shader* envShader, * envShader_noAT;
		static Shader* envShader_fullLight, * envShader_fullLight_noAT;
		static maple::UniformBuffer::Ptr uniformBuffer;
		static maple::DescriptorSet::Ptr uniformSet;
		static uint32_t alignedSize = 0;

		static uint32_t prevIndex = -1;
		static uint32_t objectIndex = 0;

		static void newFrameCheck()
		{
			auto currentIndex = maple::GraphicsContext::get()->getSwapChain()->getCurrentBufferIndex();
			if (currentIndex != prevIndex)
			{ // new frame
				prevIndex = currentIndex;
				objectIndex = 0;
			}
		}

		inline std::shared_ptr<maple::DescriptorSet> getMatFXSet(rw::Texture* tex1)
		{
			if (auto iter = matFXSets.find(tex1); iter == matFXSets.end())
			{
				endLastPipeline();
				auto cmdBuffer = maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer();
				auto matFX = matFXSets.emplace(
					std::piecewise_construct,
					std::forward_as_tuple(tex1),
					std::forward_as_tuple(maple::DescriptorSet::create({ 2, getShader(envShader->shaderId).get() }))
				).first->second;
				setTexture(matFX, 1, tex1);
				matFX->update(cmdBuffer);
				matFX->setName(std::string(tex1->name) + " : MatFXSet");
				return matFX;
			}
			return matFXSets[tex1];
		}

		void matfxDefaultRender(InstanceDataHeader* header, InstanceData* inst, int32 vsBits, uint32 flags, int32_t objectId, int32_t instanceId)
		{
			Material* m;
			m = inst->material;
			const auto prevColor = m->color;

			if (flags & Geometry::MODULATE)
			{
				m->color = {255,255,255,255};
			}

			setTextureBlend(m->texture);

			rw::SetRenderState(VERTEXALPHA, inst->vertexAlpha || m->color.alpha != 0xFF);

			if ((vsBits & VSLIGHT_MASK) == 0) {
				if (getAlphaTest())
					defaultShader->use();
				else
					defaultShader_noAT->use();
			}
			else {
				if (getAlphaTest())
					defaultShader_fullLight->use();
				else
					defaultShader_fullLight_noAT->use();
			}

			drawInst(header, inst, nullptr,nullptr, objectId, instanceId);

			m->color = prevColor;
		}

		static Frame* lastEnvFrame;

		static RawMatrix normal2texcoord = {
			0.5f,  0.0f, 0.0f , 0.0f,
			0.0f, -0.5f, 0.0f , 0.0f,
			0.0f,  0.0f, 1.0f , 0.0f,
			0.5f,  0.5f, 0.0f , 1.0f
		};

		void uploadEnvMatrix(Frame* frame, MatfxUniform & u)
		{
			Matrix invMat;
			if (frame == nil)
				frame = engine->currentCamera->getFrame();

			// cache the matrix across multiple meshes
			static RawMatrix envMtx;
			// can't do it, frame matrix may change
			//	if(frame != lastEnvFrame){
			//		lastEnvFrame = frame;
			{

				RawMatrix invMtx;
				Matrix::invert(&invMat, frame->getLTM());
				convMatrix(&invMtx, &invMat);
				invMtx.pos.set(0.0f, 0.0f, 0.0f);
				float uscale = fabs(normal2texcoord.right.x);
				normal2texcoord.right.x = MatFX::envMapFlipU ? -uscale : uscale;
				RawMatrix::mult(&envMtx, &invMtx, &normal2texcoord);
			}
			memcpy(u.texMatrix, &envMtx,sizeof(RawMatrix));
		}

		void matfxEnvRender(InstanceDataHeader* header, InstanceData* inst, int32 vsBits, uint32 flags, MatFX::Env* env, int32_t objectId, int32_t instanceId)
		{
			newFrameCheck();

			Material* m;
			m = inst->material;

			if (env->tex == nil || env->coefficient == 0.0f) {
				matfxDefaultRender(header, inst, vsBits, flags, objectId, instanceId);
				return;
			}

			const auto prevColor = m->color;

			if (flags & Geometry::MODULATE)
			{
				m->color = { 255,255,255,255 };
			}

			auto sets = getMatFXSet(env->tex);

			setTextureBlend(m->texture);//set blend mode
			
			MatfxUniform matfxUniform;
			uploadEnvMatrix(env->frame, matfxUniform);
			matfxUniform.fxparams[0] = env->coefficient;
			matfxUniform.fxparams[1] = env->fbAlpha ? 0.0f : 1.0f;
			matfxUniform.fxparams[2] = matfxUniform.fxparams[3] = 0.0f;

			static float zero[4] = {0};
			static float one[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			
			if (MatFX::envMapApplyLight)
				memcpy(matfxUniform.colorClamp, zero,sizeof(float) * 4);
			else
				memcpy(matfxUniform.colorClamp, one, sizeof(float) * 4);

			RGBAf envcol[4];
			if (MatFX::envMapUseMatColor)
				convColor(envcol, &m->color);
			else
				convColor(envcol, &MatFX::envMapColor);

			memcpy(matfxUniform.envColor, envcol, sizeof(RGBAf));

			uniformBuffer->setDynamicData(sizeof(MatfxUniform), &matfxUniform, alignedSize * objectIndex);
			uniformSet->setDynamicOffset(alignedSize * objectIndex);

			MAPLE_ASSERT(objectIndex < MATFX_INIT_SIZE, "TODO.........");//resize it later if we need

			rw::SetRenderState(VERTEXALPHA, 1);
			rw::SetRenderState(SRCBLEND, BLENDONE);

			if ((vsBits & VSLIGHT_MASK) == 0) {
				if (getAlphaTest())
					envShader->use();
				else
					envShader_noAT->use();
			}
			else {
				if (getAlphaTest())
					envShader_fullLight->use();
				else
					envShader_fullLight_noAT->use();
			}

			drawInst(header, inst, sets, uniformSet, objectId, instanceId);

			rw::SetRenderState(SRCBLEND, BLENDSRCALPHA);

			m->color = prevColor;

			objectIndex++;
		}

		void matfxUpdateCB(Atomic* atomic, InstanceDataHeader* header) 
		{
			InstanceData* inst = header->inst;
			int32 n = header->numMeshes;

			while (n--) {
				MatFX* matfx = MatFX::get(inst->material);
				if (matfx != nullptr && matfx->type == MatFX::ENVMAP)
				{
					getMatFXSet(matfx->fx[0].env.tex);
				}
				else 
				{
					auto textureSet = getTextureDescriptorSet(inst->material);
				}
				inst++;
			}
		}

		void matfxRenderCB(Atomic* atomic, InstanceDataHeader* header)
		{
			uint32 flags = atomic->geometry->flags;
			setWorldMatrix(atomic->getFrame()->getLTM());
			int32 vsBits = lightingCB(atomic);

			lastEnvFrame = nil;

			InstanceData* inst = header->inst;
			int32 n = header->numMeshes;

			auto ids = rw::tlas::addObject(atomic, header);
			int32_t customId = ids.first;
			int32_t instanceId = ids.second;

			while (n--) {
				MatFX* matfx = MatFX::get(inst->material);

				if (matfx == nil)
					matfxDefaultRender(header, inst, vsBits, flags, customId++, instanceId);
				else switch (matfx->type) {
				case MatFX::ENVMAP:
					matfxEnvRender(header, inst, vsBits, flags, &matfx->fx[0].env, customId++, instanceId);
					break;
				default:
					matfxDefaultRender(header, inst, vsBits, flags, customId++, instanceId);
					break;
				}
				inst++;
			}
		}

		ObjPipeline* makeMatFXPipeline(void)
		{
			ObjPipeline* pipe = ObjPipeline::create();
			pipe->instanceCB = defaultInstanceCB;
			pipe->uninstanceCB = defaultUninstanceCB;
			pipe->renderCB = matfxRenderCB;
			pipe->beginUpdate = matfxUpdateCB;
			pipe->pluginID = ID_MATFX;
			pipe->pluginData = 0;
			return pipe;
		}

		static void* matfxOpen(void* o, int32, int32)
		{
			matFXGlobals.pipelines[PLATFORM_VULKAN] = makeMatFXPipeline();

#include "vkshaders/matfx.shader.h"
#include "vkshaders/common.shader.h"

#ifdef ENABLE_GBUFFER
#define gbuffer_define "#define ENABLE_GBUFFER\n"
#else
#define gbuffer_define ""
#endif // ENABLE_GBUFFER

			const std::string common = {(char*)__common_shader, __common_shader_len};
			const std::string defaultTxt = common + std::string{(char *)__matfx_shader, __matfx_shader_len};

			envShader = Shader::create(defaultTxt, gbuffer_define"#define VERTEX_SHADER\n", defaultTxt, gbuffer_define "#define FRAGMENT_SHADER\n", "MatfxUniform");
			assert(envShader);
			envShader_noAT = Shader::create(defaultTxt, gbuffer_define"#define VERTEX_SHADER\n", defaultTxt, gbuffer_define "#define FRAGMENT_SHADER\n #define NO_ALPHATEST\n",  "MatfxUniform" );
			assert(envShader_noAT);

			envShader_fullLight =
			    Shader::create(defaultTxt, gbuffer_define"#define VERTEX_SHADER\n#define DIRECTIONALS\n#define POINTLIGHTS\n#define SPOTLIGHTS\n", defaultTxt,
					gbuffer_define"#define FRAGMENT_SHADER\n", "MatfxUniform" );
			assert(envShader_fullLight);
			envShader_fullLight_noAT =
			    Shader::create(defaultTxt, gbuffer_define"#define VERTEX_SHADER\n#define DIRECTIONALS\n#define POINTLIGHTS\n#define SPOTLIGHTS\n", defaultTxt,
					gbuffer_define"#define FRAGMENT_SHADER\n #define NO_ALPHATEST\n",  "MatfxUniform" );
			assert(envShader_fullLight_noAT);

			alignedSize = maple::GraphicsContext::get()->alignedDynamicUboSize(sizeof(MatfxUniform));

			uniformBuffer = maple::UniformBuffer::create(MATFX_INIT_SIZE * alignedSize, nullptr);

			uniformSet = maple::DescriptorSet::create({ 3, getShader(envShader->shaderId).get() });
			uniformSet->setBuffer("MatfxUniform", uniformBuffer);
			uniformSet->initUpdate();
			return o;
		}

		static void* matfxClose(void* o, int32, int32)
		{
			((ObjPipeline*)matFXGlobals.pipelines[PLATFORM_VULKAN])->destroy();
			matFXGlobals.pipelines[PLATFORM_VULKAN] = nil;

			envShader->destroy();
			envShader = nil;
			envShader_noAT->destroy();
			envShader_noAT = nil;
			envShader_fullLight->destroy();
			envShader_fullLight = nil;
			envShader_fullLight_noAT->destroy();
			envShader_fullLight_noAT = nil;

			return o;
		}

		void initMatFX(void)
		{
			Driver::registerPlugin(PLATFORM_VULKAN, 0, ID_MATFX, matfxOpen, matfxClose);
		}
#else
		void initMatFX(void) {}
#endif

	}
}

