
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace maple
{
	class CommandBuffer;
	class AccelerationStructure;
	class VertexBuffer;
	class IndexBuffer;
	class BatchTask;
};

namespace rw
{
	struct Atomic;
	struct Frame;

	namespace vulkan
	{
		struct InstanceDataHeader;
	}

	namespace tlas
	{
		auto beginUpdate() -> void;
		auto addObject(Atomic* atomic, vulkan::InstanceDataHeader* header)->std::pair<int32_t, int32_t>;
		auto updateFrame(Frame* frame) -> void;
		auto execute(const maple::CommandBuffer* cmd) -> void;
		auto registerTLASPlugin() -> void;
		auto getVertexBuffers() -> const std::vector<maple::VertexBuffer*>&;
		auto getIndexBuffers() -> const std::vector<maple::IndexBuffer*>&;
		auto getTopLevel() -> std::shared_ptr<maple::AccelerationStructure>;
		auto numberOfInstances() -> int32_t;
		auto getBatchTask() ->std::shared_ptr<maple::BatchTask>;
	}
}