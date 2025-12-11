#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "../rwbase.h"

#include "../rwerror.h"

#include "../rwplg.h"

#include "../rwrender.h"

#include "../rwengine.h"

#include "../rwpipeline.h"

#include "../rwobjects.h"

#include "../rwanim.h"

#include "../rwplugins.h"

#include "rwvk.h"
#include "rwvkplg.h"
#include "rwvkshader.h"

#include "rwvkimpl.h"
#ifdef RW_VULKAN
#include "UniformBuffer.h"
#include "GraphicsContext.h"
#include "SwapChain.h"
#include "DescriptorSet.h"
#include "VertexBuffer.h"
#endif
namespace rw
{
	namespace vulkan
	{
#ifdef RW_VULKAN

		static Shader* skinShader, * skinShader_noAT;
		static Shader* skinShader_fullLight, * skinShader_fullLight_noAT;
		static maple::UniformBuffer::Ptr uniformBone;
		static maple::DescriptorSet::Ptr uniformSet;
		static uint32_t alignedSize = 0;
		static int32_t INIT_OBJ_SIZE = 50;

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

		void skinInstanceCB(Geometry* geo, InstanceDataHeader* header, bool32 reinstance)
		{
			AttribDesc* attribs, * a;

			bool isPrelit = true;// !!(geo->flags & Geometry::PRELIT);
			bool hasNormals = !!(geo->flags & Geometry::NORMALS);

			if (!reinstance) {
				AttribDesc tmpAttribs[14];
				uint32 stride;

				//
				// Create attribute descriptions
				//
				a = tmpAttribs;
				stride = 0;

				// Positions
				a->index = ATTRIB_POS;
				a->size = 3;
				a->type = GL_FLOAT;
				a->offset = stride;
				stride += 12;
				a++;

				// Normals
				// TODO: compress
				//if (hasNormals) 
				{
					a->index = ATTRIB_NORMAL;
					a->size = 3;
					a->type = GL_FLOAT;
					a->offset = stride;
					stride += 12;
					a++;
				}

				// Prelighting
				if (isPrelit) {
					a->index = ATTRIB_COLOR;
					a->size = 4;
					a->type = GL_FLOAT;
					a->offset = stride;
					stride += 16;
					a++;
				}

				// Texture coordinates
				for (int32 n = 0; n < geo->numTexCoordSets; n++) {
					a->index = ATTRIB_TEXCOORDS0 + n;
					a->size = 2;
					a->type = GL_FLOAT;
					a->offset = stride;
					stride += 8;
					a++;
				}

				// Weights
				a->index = ATTRIB_WEIGHTS;
				a->size = 4;
				a->type = GL_FLOAT;
				a->offset = stride;
				stride += 16;
				a++;

				// Indices
				a->index = ATTRIB_INDICES;
				a->size = 4;
				a->type = GL_FLOAT;
				a->offset = stride;
				stride += 16;
				a++;

				header->numAttribs = a - tmpAttribs;
				for (a = tmpAttribs; a != &tmpAttribs[header->numAttribs]; a++)
					a->stride = stride;
				header->attribDesc = rwNewT(AttribDesc, header->numAttribs, MEMDUR_EVENT | ID_GEOMETRY);
				memcpy(header->attribDesc, tmpAttribs, header->numAttribs * sizeof(AttribDesc));

				//
				// Allocate vertex buffer
				//
				header->vertexBuffer = rwNewT(uint8, header->totalNumVertex * stride, MEMDUR_EVENT | ID_GEOMETRY);
				assert(header->vertexBufferGPU == nullptr);
			}

			Skin* skin = Skin::get(geo);
			attribs = header->attribDesc;

			//
			// Fill vertex buffer
			//

			uint8* verts = header->vertexBuffer;

			struct Vertex
			{
				V3d pos;
				V3d normal;
				V4d color;
				V4d weights;
				V4d indices;
				V2d tex0;
			};

			// Positions
			if (!reinstance || geo->lockedSinceInst & Geometry::LOCKVERTICES) {
				for (a = attribs; a->index != ATTRIB_POS; a++)
					;
				instV3d(VERT_FLOAT3, verts + a->offset,
					geo->morphTargets[0].vertices,
					header->totalNumVertex, a->stride);
			}

			// Normals
			if ((!reinstance || geo->lockedSinceInst & Geometry::LOCKNORMALS)) {
				for (a = attribs; a->index != ATTRIB_NORMAL; a++)
					;
				instV3d(VERT_FLOAT3, verts + a->offset,geo->morphTargets[0].normals, header->totalNumVertex, a->stride);
				if (!hasNormals)
				{
					if (header->totalNumIndex > 0) {
						auto vertexBuff = reinterpret_cast<Vertex*>(verts);
						for (uint32_t i = 0; i < header->totalNumIndex; i += 3) {
							const auto a = header->indexBuffer[i];
							const auto b = header->indexBuffer[i + 1];
							const auto c = header->indexBuffer[i + 2];
							const auto normal = rw::cross(rw::sub(vertexBuff[b].pos, vertexBuff[a].pos),
								rw::sub(vertexBuff[c].pos, vertexBuff[a].pos));
							vertexBuff[a].normal = rw::add(vertexBuff[a].normal, normal);
							vertexBuff[b].normal = rw::add(vertexBuff[b].normal, normal);
							vertexBuff[c].normal = rw::add(vertexBuff[c].normal, normal);
						}

						for (uint32_t i = 0; i < header->totalNumVertex; ++i)
						{
							vertexBuff[i].normal = normalize(vertexBuff[i].normal);
						}
					}
					else
					{
						assert(0 && "Todo.........");
					}
				}
			}

			// Prelighting
			if (isPrelit && (!reinstance || geo->lockedSinceInst & Geometry::LOCKPRELIGHT)) {
				for (a = attribs; a->index != ATTRIB_COLOR; a++)
					;
				instFloatColor(VERT_RGBA, verts + a->offset,
					geo->colors == nullptr ? nullptr : geo->colors,
					header->totalNumVertex, a->stride);
			}

			// Texture coordinates
			for (int32 n = 0; n < geo->numTexCoordSets; n++) {
				if (!reinstance || geo->lockedSinceInst & (Geometry::LOCKTEXCOORDS << n)) {
					for (a = attribs; a->index != ATTRIB_TEXCOORDS0 + n; a++)
						;
					instTexCoords(VERT_FLOAT2, verts + a->offset,
						geo->texCoords[n],
						header->totalNumVertex, a->stride);
				}
			}

			// Weights
			if (!reinstance) {
				for (a = attribs; a->index != ATTRIB_WEIGHTS; a++)
					;
				float* w = skin->weights;
				instV4d(VERT_FLOAT4, verts + a->offset,
					(V4d*)w,
					header->totalNumVertex, a->stride);
			}

			// Indices
			if (!reinstance) {
				for (a = attribs; a->index != ATTRIB_INDICES; a++)
					;
				// not really colors of course but what the heck
				instFloatColor(VERT_RGBA, verts + a->offset, (RGBA*)skin->indices, header->totalNumVertex, a->stride,false);
			}

			MAPLE_ASSERT(attribs[0].stride == sizeof(Vertex), "Issue ...here");

			header->vertexBufferGPU = maple::VertexBuffer::createRaw(
				header->vertexBuffer,
				header->totalNumVertex * attribs[0].stride);
		}

		void skinUninstanceCB(Geometry* geo, InstanceDataHeader* header)
		{
			assert(0 && "can't uninstance");
		}

		static float skinMatrices[64 * 16];

		void uploadSkinMatrices(Atomic* a)
		{
			int i;
			Skin* skin = Skin::get(a->geometry);
			Matrix* m = (Matrix*)skinMatrices;
			HAnimHierarchy* hier = Skin::getHierarchy(a);

			if (hier)
			{
				Matrix* invMats = (Matrix*)skin->inverseMatrices;
				Matrix tmp;

				assert(skin->numBones == hier->numNodes);
				if (hier->flags & HAnimHierarchy::LOCALSPACEMATRICES)
				{
					for (i = 0; i < hier->numNodes; i++) {
						invMats[i].flags = 0;
						Matrix::mult(m, &invMats[i], &hier->matrices[i]);
						m++;
					}
				}
				else
				{
					Matrix invAtmMat;
					Matrix::invert(&invAtmMat, a->getFrame()->getLTM());
					for (i = 0; i < hier->numNodes; i++) {
						invMats[i].flags = 0;
						Matrix::mult(&tmp, &hier->matrices[i], &invAtmMat);
						Matrix::mult(m, &invMats[i], &tmp);
						m++;
					}
				}
			}
			else
			{
				for (i = 0; i < skin->numBones; i++) {
					m->setIdentity();
					m++;
				}
			}

			uniformBone->setDynamicData(64 * 16 * sizeof(float), skinMatrices, alignedSize * objectIndex);
		}

		void skinUpdateCB(Atomic* atomic, InstanceDataHeader* header)
		{
			defaultUpdateCB(atomic, header);
		}

		void skinRenderCB(Atomic* atomic, InstanceDataHeader* header)
		{
			newFrameCheck();
			Material* m;

			uint32 flags = atomic->geometry->flags;
			setWorldMatrix(atomic->getFrame()->getLTM());
			int32 vsBits = lightingCB(atomic);

			InstanceData* inst = header->inst;
			int32 n = header->numMeshes;

			uploadSkinMatrices(atomic);

			while (n--)
			{
				m = inst->material;
				//setMaterial(flags, m->color, m->surfaceProps);
				setTextureBlend(m->texture);

				rw::SetRenderState(VERTEXALPHA, inst->vertexAlpha || m->color.alpha != 0xFF);

				if ((vsBits & VSLIGHT_MASK) == 0) {
					if (getAlphaTest())
						skinShader->use();
					else
						skinShader_noAT->use();
				}
				else {
					if (getAlphaTest())
						skinShader_fullLight->use();
					else
						skinShader_fullLight_noAT->use();
				}

				drawInst(header, inst, uniformSet);
				inst++;
			}

			objectIndex++;
		}

		static void* skinOpen(void* o, int32, int32)
		{
			skinGlobals.pipelines[PLATFORM_VULKAN] = makeSkinPipeline();

#include "vkshaders/skin.shader.h"
#include "vkshaders/common.shader.h"

#ifdef ENABLE_GBUFFER
#define gbuffer_define "#define ENABLE_GBUFFER\n"
#else
#define gbuffer_define ""
#endif // ENABLE_GBUFFER
			const std::string common = { (char*)__common_shader, __common_shader_len };
			const std::string defaultTxt = common + std::string{ (char*)__skin_shader, __skin_shader_len };

			skinShader = Shader::create(defaultTxt, gbuffer_define"#define VERTEX_SHADER\n", defaultTxt, gbuffer_define"#define FRAGMENT_SHADER\n", "BoneBuffer");
			assert(skinShader);
			skinShader_noAT = Shader::create(defaultTxt, gbuffer_define "#define VERTEX_SHADER\n", defaultTxt, gbuffer_define"#define FRAGMENT_SHADER\n#define NO_ALPHATEST\n", "BoneBuffer");
			assert(skinShader_noAT);
			skinShader_fullLight = Shader::create(defaultTxt, gbuffer_define "#define VERTEX_SHADER\n#define DIRECTIONALS\n#define POINTLIGHTS\n#define SPOTLIGHTS\n", defaultTxt, gbuffer_define"#define FRAGMENT_SHADER\n", "BoneBuffer");
			assert(skinShader_fullLight);
			skinShader_fullLight_noAT = Shader::create(defaultTxt, gbuffer_define"#define VERTEX_SHADER\n#define DIRECTIONALS\n#define POINTLIGHTS\n#define SPOTLIGHTS\n", defaultTxt, gbuffer_define "#define FRAGMENT_SHADER\n#define NO_ALPHATEST\n", "BoneBuffer");
			assert(skinShader_fullLight_noAT);

			alignedSize = sizeof(float) * 16 * 64;//maple::GraphicsContext::get()->alignedDynamicUboSize(sizeof(float) * 16 * 64);
			uniformBone = maple::UniformBuffer::create(alignedSize * INIT_OBJ_SIZE, nullptr);
			uniformSet = maple::DescriptorSet::create({ 2, getShader(skinShader->shaderId).get() });
			uniformSet->setBuffer("BoneBuffer", uniformBone);
			uniformSet->initUpdate();
			return o;
		}

		static void* skinClose(void* o, int32, int32)
		{
			((ObjPipeline*)skinGlobals.pipelines[PLATFORM_VULKAN])->destroy();
			skinGlobals.pipelines[PLATFORM_VULKAN] = nil;

			skinShader->destroy();
			skinShader = nil;
			skinShader_noAT->destroy();
			skinShader_noAT = nil;
			skinShader_fullLight->destroy();
			skinShader_fullLight = nil;
			skinShader_fullLight_noAT->destroy();
			skinShader_fullLight_noAT = nil;

			return o;
		}

		void initSkin(void)
		{
			Driver::registerPlugin(PLATFORM_VULKAN, 0, ID_SKIN, skinOpen, skinClose);
		}

		ObjPipeline* makeSkinPipeline(void)
		{
			ObjPipeline* pipe = ObjPipeline::create();
			pipe->instanceCB = skinInstanceCB;
			pipe->uninstanceCB = skinUninstanceCB;
			pipe->renderCB = skinRenderCB;
			pipe->beginUpdate = skinUpdateCB;
			pipe->pluginID = ID_SKIN;
			pipe->pluginData = 1;
			return pipe;
		}

#else
		void initSkin(void)
		{}
#endif

	}        // namespace vulkan
}        // namespace rw
