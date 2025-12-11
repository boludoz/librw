
#include "vktoplevel.h"

#include "../../rwbase.h"

#include "../../rwplg.h"

#include "../../rwengine.h"

#include "../../rwpipeline.h"

#include "../../rwobjects.h"

#include "../../rwerror.h"

#include "../rwvk.h"
#include "../rwvkimpl.h"

#include "CommandBuffer.h"
#include "GraphicsContext.h"
#include "IndexBuffer.h"
#include "Pipeline.h"
#include "RenderDevice.h"
#include "SwapChain.h"
#include "Textures.h"
#include "VertexBuffer.h"
#include "Console.h"

#ifdef ENABLE_RAYTRACING
#include "vkRaytracing.h"
#include "AccelerationStructure.h"
#endif

#include <vector>
#include <unordered_set>
#include <map>

namespace rw
{
	namespace tlas
	{
		struct Object
		{
			Object() : atomic(nullptr), header(nullptr), instanceId(0), customId(0) {};
			Object(rw::Atomic* atomic, int32_t instanceId, int32_t customId, vulkan::InstanceDataHeader* header)
				:atomic(atomic), instanceId(instanceId), customId(customId), header(header) {}
			rw::Atomic* atomic;
			int32_t instanceId;
			int32_t customId;
			vulkan::InstanceDataHeader* header;
		};

		static maple::AccelerationStructure::Ptr topLevel;
		static std::unordered_map<size_t, Object> atomics;
		static std::unordered_map<size_t, Object> remainAtomics;
		static std::unordered_map<rw::Atomic*, std::unordered_set<size_t>> atomicsToHash;
		static std::vector<maple::VertexBuffer*> vertexBuffer;
		static std::vector<maple::IndexBuffer*> indexBuffer;
		static maple::BatchTask::Ptr batchTask;
		static std::vector<int32_t> batchIds;
		static std::unordered_set<rw::Atomic*> removedQueue;
		static int32_t customIdIndicator = 0;

		inline static auto getHash(rw::Atomic* atomic, InstanceDataHeader* header)
		{
			size_t code = 0;
			maple::hash::hashCode(code, atomic, header);
			return code;
		}

		inline static auto defragment()
		{
			remainAtomics = std::move(atomics);
			customIdIndicator = 0;
			batchIds.clear();
			vertexBuffer.clear();
			indexBuffer.clear();
			atomics.clear();
			atomicsToHash.clear();
		}

		static void* createAtomic(void* object, int32_t offset, int32_t)
		{
			*PLUGINOFFSET(void*, object, offset) = nil;
			return object;
		}

		static void* destroyAtomic(void* object, int32_t offset, int32_t)
		{
			auto obj = static_cast<rw::Atomic*>(object);
			if (obj != nullptr)
			{
				removedQueue.emplace(obj);
			}
			return object;
		}

		auto beginUpdate() -> void
		{
			if (!removedQueue.empty())
			{
				for (auto atomic : removedQueue)
				{
					for (auto hash : atomicsToHash[atomic])
					{
						atomics.erase(hash);
					}
				}
				removedQueue.clear();
			}
			defragment();
		}

		auto addObject(Atomic* atomic, vulkan::InstanceDataHeader* header) -> std::pair<int32_t, int32_t>
		{
			MAPLE_ASSERT(atomic != nullptr, "");
			MAPLE_ASSERT(vertexBuffer.size() < 2048, "");
			auto hash = getHash(atomic, header);
			auto iter2 = atomics.find(hash);
			atomicsToHash[atomic].emplace(hash);
			if (iter2 == atomics.end())
			{
				iter2 = atomics.emplace(
					std::piecewise_construct,
					std::forward_as_tuple(hash),
					std::forward_as_tuple(atomic, vertexBuffer.size(), customIdIndicator, header)).first;

				vertexBuffer.emplace_back(header->vertexBufferGPU);
				indexBuffer.emplace_back(header->indexBufferGPU);

				RawMatrix rawMatrix;
				RawMatrix rawMatrix2;
				convMatrix(&rawMatrix, atomic->getFrame()->getLTM());
				RawMatrix::transpose(&rawMatrix2, &rawMatrix);
				topLevel->updateTLAS(reinterpret_cast<maple::mat4&>(rawMatrix2),
					iter2->second.instanceId,
					iter2->second.customId,
					maple::getAccelerationStructure(header->blasId)->getDeviceAddress()
				);
				customIdIndicator += header->numMeshes;
				batchIds.emplace_back(iter2->second.instanceId);
			}
			return { iter2->second.customId, iter2->second.instanceId };
		}

		auto numberOfInstances() -> int32_t
		{
			return vertexBuffer.size();
		}

		auto getBatchTask() -> std::shared_ptr<maple::BatchTask>
		{
			return batchTask;
		}

		auto updateFrame(Frame* frame) -> void
		{
			//TODO..
		}

		auto execute(const maple::CommandBuffer* cmd) -> void
		{
			batchTask->execute(cmd);//blas cmds;

			for (auto& obj : remainAtomics)
			{
				if (removedQueue.count(obj.second.atomic) == 0)
					addObject(obj.second.atomic, obj.second.header);
			}

			remainAtomics.clear();

			if (!batchIds.empty())
			{
				std::vector<maple::BuildRange> ranges;
				uint32_t lastId = batchIds[0];
				int32_t startIndicator = 0;
				int32_t endIndicator = 1;

				for (; endIndicator < batchIds.size(); endIndicator++)
				{
					uint32_t currId = batchIds[endIndicator];
					if (currId - lastId > 1)
					{
						auto count = endIndicator - startIndicator;
						ranges.push_back({ count, batchIds[startIndicator] });
						startIndicator = endIndicator;
					}
					lastId = currId;
				}

				auto count = endIndicator - startIndicator;
				ranges.push_back({ count, batchIds[startIndicator] });
				batchIds.clear();
			}
			topLevel->copyToGPU(cmd, vertexBuffer.size(), 0);
			topLevel->build(cmd, vertexBuffer.size(), 0);
		}

		auto registerTLASPlugin() -> void
		{
			batchTask = maple::BatchTask::create();
			topLevel = maple::AccelerationStructure::createTopLevel(2048);
			Atomic::registerPlugin(sizeof(uint32_t), ID_ATOMIC, createAtomic, destroyAtomic, nullptr);
		}

		auto getVertexBuffers() -> const std::vector<maple::VertexBuffer*>&
		{
			return vertexBuffer;
		}

		auto getIndexBuffers() -> const std::vector<maple::IndexBuffer*>&
		{
			return indexBuffer;
		}

		auto getTopLevel() -> std::shared_ptr<maple::AccelerationStructure>
		{
			return topLevel;
		}
	}
}