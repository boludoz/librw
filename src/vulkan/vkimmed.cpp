#include <assert.h>
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
#	include "rwvk.h"
#	include "rwvkimpl.h"
#	include "rwvkshader.h"

#include "GraphicsContext.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Shader.h"
#include "SwapChain.h"
#include "CommandBuffer.h"
#include "Pipeline.h"
#include "RenderDevice.h"
#include "Textures.h"
#include "RenderPass.h"

namespace rw
{
	namespace vulkan
	{
		//simple way to implement. still have performance issue.

		Shader *im2dOverrideShader;


		RGBA im3dMaterialColor = { 255, 255, 255, 255 };
		SurfaceProperties im3dSurfaceProps = { 1.0f, 1.0f, 1.0f };

#define STARTINDICES 20000
#define STARTVERTICES 20000

		static maple::DrawType primTypeMap[] = {
			maple::DrawType::Point,	// invalid
			maple::DrawType::Lines,
			maple::DrawType::Lines_Strip,
			maple::DrawType::Triangle,
			maple::DrawType::TriangleStrip,
			maple::DrawType::Triangle_Fan,
			maple::DrawType::Point
		};

		static Shader* im2dShader;
		static Shader* im3dShader;

		static maple::VertexBuffer::Ptr vertexBuffer;
		static maple::IndexBuffer::Ptr indexBuffer;
		static uint32_t vertexIndex = 0;
		static uint32_t indicesIndex = 0;
		static uint32_t prevIndex = -1;
		
		static maple::VertexBuffer::Ptr vertexBuffer3D;
		static maple::IndexBuffer::Ptr indexBuffer3D;
		static maple::DescriptorSet::Ptr im3dSet;
		static maple::DescriptorSet::Ptr currentIm3dSet;

		static uint32_t vertexIndex3D = 0;
		static uint32_t indicesIndex3D = 0;

		static void newFrameCheck() 
		{
			auto currentIndex = maple::GraphicsContext::get()->getSwapChain()->getCurrentBufferIndex();
			if(currentIndex != prevIndex) { // new frame
				vertexIndex = 0;
				indicesIndex = 0;
				vertexIndex3D = 0;
				indicesIndex3D = 0;
				prevIndex = currentIndex;
			}
		}

		void openIm2D(uint32_t width, uint32_t height)
		{
#include "vkshaders/im2d.shader.h"
			const std::string defaultTxt{ (char*)__im2d_shader, __im2d_shader_len };
			im2dShader = Shader::create(defaultTxt, "#define VERTEX_SHADER\n", defaultTxt, "#define FRAGMENT_SHADER\n");
			vertexBuffer = maple::VertexBuffer::create(nullptr, sizeof(Im2DVertex) * STARTVERTICES);
			indexBuffer = maple::IndexBuffer::create((uint16_t*)nullptr, STARTINDICES);
		}

		void closeIm2D(void)
		{
			im2dShader->destroy();
			im2dShader = nil;
		}

		static Im2DVertex tmpprimbuf[3];

		void im2DRenderLine(void* vertices, int32 numVertices, int32 vert1, int32 vert2)
		{
			PROFILE_FUNCTION();
			Im2DVertex* verts = (Im2DVertex*)vertices;
			tmpprimbuf[0] = verts[vert1];
			tmpprimbuf[1] = verts[vert2];
			im2DRenderPrimitive(PRIMTYPELINELIST, tmpprimbuf, 2);
		}

		void im2DRenderTriangle(void* vertices, int32 numVertices, int32 vert1, int32 vert2, int32 vert3)
		{
			PROFILE_FUNCTION();
			Im2DVertex* verts = (Im2DVertex*)vertices;
			tmpprimbuf[0] = verts[vert1];
			tmpprimbuf[1] = verts[vert2];
			tmpprimbuf[2] = verts[vert3];
			im2DRenderPrimitive(PRIMTYPETRILIST, tmpprimbuf, 3);
		}

		maple::DescriptorSet::Ptr im2DSetXform()
		{
			PROFILE_FUNCTION();
			float xform[4];

			Camera* cam;
			cam = (Camera*)engine->currentCamera;

			int32_t faceId = (int32_t)GetRenderState(SKYBOX_FACE);
			if (faceId == -1) 
			{
				xform[0] = 2.0f / cam->frameBuffer->width;
				xform[1] = -2.0f / cam->frameBuffer->height;
			}
			else 
			{
				xform[0] = 2.0f  / 512;
				xform[1] = -2.0f / 512;
			}
			xform[2] = -1.0f;
			xform[3] = 1.0f;
			const auto &fog = flushFog();

			if (auto pushConststs = getShader(currentShader->shaderId)->getPushConstant(0)) 
			{ 
				pushConststs->setValue("u_xform", xform);
				pushConststs->setValue("u_alphaRef", &fog);
				pushConststs->setValue("u_fogData", &fog.fogStart);
				pushConststs->setValue("u_fogColor", &fog.fogColor);
			}

			rw::Raster* rast = (rw::Raster*)rw::GetRenderStatePtr(rw::TEXTURERASTER);
			if (rast != nullptr)
			{
				auto vkRst = GET_VULKAN_RASTEREXT(rast);
				
				if(getTexture(vkRst->textureId)->getName() == "" && strcmp(rast->name,"") != 0)
				{
					getTexture(vkRst->textureId)->setName(rast->name);
				}
				return getTextureDescriptorSet(vkRst->textureId);
			}
			return getDefaultDescriptorSet();
		}

		void im2DRenderPrimitive(PrimitiveType primType, void* vertices, int32 numVertices)
		{
			PROFILE_FUNCTION();
			newFrameCheck();

			auto mappedVertexBuffer = vertexBuffer->getPointer<Im2DVertex>();
			memcpy(&mappedVertexBuffer[vertexIndex], vertices, numVertices * sizeof(Im2DVertex));
			auto cmdBuffer = maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer();

			vertexBuffer->releasePointer();

			if(im2dOverrideShader)
				im2dOverrideShader->use();
			else
				im2dShader->use();

			auto objSet = im2DSetXform();
			objSet->update(cmdBuffer);

			auto pipeline = getPipeline(primTypeMap[primType], currentShader->shaderId);
			compareAndBind(pipeline);

			vertexBuffer->bind(cmdBuffer, nullptr);
			pipeline->getShader()->bindPushConstants(cmdBuffer, pipeline.get());
			maple::RenderDevice::get()->bindDescriptorSet(pipeline.get(), cmdBuffer, 0, objSet);
			maple::RenderDevice::get()->drawArraysInternal(cmdBuffer, primTypeMap[primType], numVertices, vertexIndex);

			vertexIndex += numVertices;

			MAPLE_ASSERT(vertexIndex < STARTVERTICES, "out of bounds");
		}

		void im2DRenderIndexedPrimitive(PrimitiveType primType,
			void* vertices, int32 numVertices,
			void* indices, int32 numIndices)
		{
			PROFILE_FUNCTION();
			newFrameCheck();

			auto mappedVertexBuffer = vertexBuffer->getPointer<Im2DVertex>();
			memcpy(&mappedVertexBuffer[vertexIndex], vertices, numVertices * sizeof(Im2DVertex));

			auto mappedIndexBuffer = indexBuffer->getPointer<uint16_t>();
			memcpy(&mappedIndexBuffer[indicesIndex], indices, numIndices * sizeof(uint16_t));

			auto cmdBuffer = maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer();

			vertexBuffer->releasePointer();
			indexBuffer->releasePointer();

			if(im2dOverrideShader)
				im2dOverrideShader->use();
			else
				im2dShader->use();

			auto pipeline = getPipeline(primTypeMap[primType], currentShader->shaderId);
			auto objSet = im2DSetXform();
			objSet->update(cmdBuffer);

			compareAndBind(pipeline);

			vertexBuffer->bind(cmdBuffer, nullptr);
			indexBuffer->bind(cmdBuffer);

			pipeline->bind(cmdBuffer);
			pipeline->getShader()->bindPushConstants(cmdBuffer, pipeline.get());
			maple::RenderDevice::get()->bindDescriptorSet(pipeline.get(), cmdBuffer, 0, objSet);
			maple::RenderDevice::get()->drawIndexedInternal(cmdBuffer, primTypeMap[primType], numIndices, indicesIndex, vertexIndex);

			indicesIndex += numIndices;
			vertexIndex += numVertices;

			MAPLE_ASSERT(vertexIndex < STARTVERTICES, "out of bounds");
		}

		void openIm3D(void)
		{
#include "vkshaders/im3d.shader.h"
			const std::string defaultTxt{ (char*)__im3d_shader, __im3d_shader_len };
			im3dShader = Shader::create(defaultTxt, "#define VERTEX_SHADER\n", defaultTxt, "#define FRAGMENT_SHADER\n");

			vertexBuffer3D = maple::VertexBuffer::create(nullptr, sizeof(Im3DVertex) * STARTVERTICES);
			indexBuffer3D = maple::IndexBuffer::create((uint16_t*)nullptr, STARTINDICES);
			im3dSet = maple::DescriptorSet::create({ 0,getShader(im3dShader->shaderId).get() });
			im3dSet->setTexture("tex0", maple::Texture2D::getTexture1X1White());
		}

		void closeIm3D(void)
		{

		}

		static int32_t g_numVertices;

		//begin
		void im3DTransform(void* vertices, int32 numVertices, Matrix* world, uint32 flags)
		{
			PROFILE_FUNCTION();
			newFrameCheck();
			auto mappedVertexBuffer = vertexBuffer3D->getPointer<Im3DVertex>();
			memcpy(&mappedVertexBuffer[vertexIndex3D], vertices, numVertices * sizeof(Im3DVertex));

			vertexBuffer3D->releasePointer();

			if (world == nil) {
				static Matrix ident;
				ident.setIdentity();
				world = &ident;
			}

			setWorldMatrix(world);

			if (flags & im3d::LIGHTING)
			{
				/*setMaterial(materialSet, im3dMaterialColor, im3dSurfaceProps);*/
				MAPLE_ASSERT(false, "TODO..");
				int32 vsBits = lightingCB();
				defaultShader_fullLight->use();
			}
			else
			{
				im3dShader->use();
			}

			rw::Raster* rast = (rw::Raster*)rw::GetRenderStatePtr(rw::TEXTURERASTER);

			g_numVertices = numVertices;

			if (rast != nullptr)
			{
				auto vkRst = GET_VULKAN_RASTEREXT(rast);
				currentIm3dSet = getTextureDescriptorSet(vkRst->textureId);
			}
			else
			{
				currentIm3dSet = im3dSet;
			}

			if ((flags & im3d::VERTEXUV) == 0)
			{
				currentIm3dSet = im3dSet;
			}
		}

		void im3DRenderPrimitive(PrimitiveType primType)
		{
			PROFILE_FUNCTION();
			auto pipeline = getPipeline(primTypeMap[primType], currentShader->shaderId);
			auto cmdBuffer = maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer();
			
			currentIm3dSet->update(cmdBuffer);
			compareAndBind(pipeline);
			vertexBuffer3D->bind(cmdBuffer, nullptr);
			flushCache(getShader(currentShader->shaderId), -1, -1);
			pipeline->getShader()->bindPushConstants(cmdBuffer, pipeline.get());

			if (defaultShader_fullLight == currentShader)
			{
				//maple::RenderDevice::get()->bindDescriptorSets(pipeline.get(), cmdBuffer, { commonSet, materialSet, objectSet });
				MAPLE_ASSERT(false, "Not support now");
			}
			else
			{
				maple::RenderDevice::get()->bindDescriptorSet(pipeline.get(), cmdBuffer, 0, currentIm3dSet);
			}

			maple::RenderDevice::get()->drawArraysInternal(cmdBuffer, primTypeMap[primType], g_numVertices, vertexIndex3D);

			vertexIndex3D += g_numVertices;

			MAPLE_ASSERT(vertexIndex3D < STARTVERTICES, "");
		}

		void im3DRenderIndexedPrimitive(PrimitiveType primType, void* indices, int32 numIndices)
		{
			
			PROFILE_FUNCTION();
			auto pipeline = getPipeline(primTypeMap[primType], currentShader->shaderId);

			compareAndBind(pipeline);

			auto cmdBuffer = maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer();

			auto mappedIndexBuffer = indexBuffer3D->getPointer<uint16_t>();
			memcpy(&mappedIndexBuffer[indicesIndex3D], indices, numIndices * sizeof(uint16_t));

			indexBuffer3D->releasePointer();

			indexBuffer3D->bind(cmdBuffer);
			vertexBuffer3D->bind(cmdBuffer, nullptr);

			flushCache(getShader(currentShader->shaderId), -1, -1);

			currentIm3dSet->update(cmdBuffer);

			pipeline->getShader()->bindPushConstants(cmdBuffer, pipeline.get());

			if (defaultShader_fullLight == currentShader)
			{
				MAPLE_ASSERT(false, "TODO...")
				//maple::RenderDevice::get()->bindDescriptorSets(pipeline.get(), cmdBuffer, { commonSet, materialSet, objectSet });
			}
			else
			{
				maple::RenderDevice::get()->bindDescriptorSet(pipeline.get(), cmdBuffer, 0, currentIm3dSet);
			}

			maple::RenderDevice::get()->drawIndexedInternal(cmdBuffer, primTypeMap[primType], numIndices, indicesIndex3D, vertexIndex3D);
			//pipeline->end(cmdBuffer);
			indicesIndex3D += numIndices;
			vertexIndex3D += g_numVertices;

			MAPLE_ASSERT(vertexIndex3D < STARTVERTICES, "");
		}

		void im3DEnd(void)
		{

		}

		void imFlush()
		{
		}
	}        // namespace vulkan
}        // namespace rw

#endif
