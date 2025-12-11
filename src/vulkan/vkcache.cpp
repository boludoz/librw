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

#ifdef RW_VULKAN

#include "rwvk.h"
#include "rwvkshader.h"
#include "raytracing/RaytracingCommon.h"

#include <unordered_map>
#include "DescriptorSet.h"
#include "GraphicsContext.h"
#include "CommandBuffer.h"
#include "SwapChain.h"
#include "HashCode.h"
#include "Textures.h"

namespace rw 
{
	namespace vulkan 
	{

		static std::vector<maple::DescriptorSet::Ptr> pools;
		static std::unordered_map<uint32_t, maple::DescriptorSet::Ptr> usedSets;
		static std::unordered_map<uint32_t, int32_t> bindlessId;
		static std::vector<maple::Texture::Ptr> textureQueue;

		static maple::DescriptorSet::Ptr defaultSet;
		static constexpr int32_t TEX0_SLOT = 0;

		static int32 textureOffset;
#define GET_TEXTURE(texture) PLUGINOFFSET(int32, texture, textureOffset)

		void resetCache() 
		{
			usedSets.clear();

			textureQueue.clear();
			textureQueue.push_back(maple::Texture2D::getTexture1X1White());

			bindlessId.clear();
			vulkanStats.currentUsedSets = 0;
		}

		//TODO...Ring buffer later...
		inline static auto getSetFromPool()
		{
			if (vulkanStats.currentUsedSets >= pools.size()) 
			{
				pools.push_back(maple::DescriptorSet::create({ TEX0_SLOT, getShader(defaultShader->shaderId).get() }));
			}
			return pools[vulkanStats.currentUsedSets++];
		}

		std::shared_ptr<maple::DescriptorSet> getTextureDescriptorSet(uint32_t textureId)
		{
			if (auto iter = usedSets.find(textureId); iter == usedSets.end())
			{
				endLastPipeline();
				auto cmdBuffer = maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer();
				auto textureSet = getSetFromPool();
				textureSet->setTexture("tex0", getTexture(textureId));
				textureSet->update(cmdBuffer);
				usedSets[textureId] = textureSet;
				return textureSet;
			}
			return usedSets[textureId];
		}

		std::shared_ptr<maple::DescriptorSet> getDefaultDescriptorSet()
		{
			if (defaultSet == nullptr)
			{
				endLastPipeline();
				auto cmdBuffer = maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer();
				defaultSet = maple::DescriptorSet::create({ TEX0_SLOT, getShader(defaultShader->shaderId).get() });
				defaultSet->setTexture("tex0", maple::Texture2D::getTexture1X1White());
				defaultSet->update(cmdBuffer);
			}
			return defaultSet;
		}

		std::shared_ptr<maple::DescriptorSet> getTextureDescriptorSet(Material* material)
		{
			if (textureQueue.size() >= maple::MAX_SCENE_MATERIAL_TEXTURE_COUNT) {
				resetCache();
			}

			if (material->texture != nullptr) 
			{
				auto vkRst = GET_VULKAN_RASTEREXT(material->texture->raster);
				if (auto iter = usedSets.find(vkRst->textureId); iter == usedSets.end())
				{
					endLastPipeline();
					auto cmdBuffer = maple::GraphicsContext::get()->getSwapChain()->getCurrentCommandBuffer();
					auto textureSet = getSetFromPool();
					setTexture(textureSet, 0, material->texture);
					textureSet->update(cmdBuffer);
					material->bindlessId = static_cast<rw::int32>(textureQueue.size());
					usedSets[vkRst->textureId] = textureSet;
					bindlessId[vkRst->textureId] = material->bindlessId;
					textureQueue.emplace_back(getTexture(vkRst->textureId));
					return textureSet;
				}
				material->bindlessId = bindlessId[vkRst->textureId];
				return usedSets[vkRst->textureId];
			}
			material->bindlessId = 0;
			return getDefaultDescriptorSet();
		}

		std::vector<std::shared_ptr<maple::Texture>>& getTextures()
		{
			return textureQueue;
		}

		//TODO... we should remove texture from pool. but no need now ?
		static void* destroyTexture(void* object, int32 offset, int32)
		{
			return object;
		}

		void registerVkCache()
		{
			rw::Texture::registerPlugin(sizeof(uint32_t), ID_TEXTURE, nullptr, destroyTexture, nullptr);
		}
	}
}

#endif