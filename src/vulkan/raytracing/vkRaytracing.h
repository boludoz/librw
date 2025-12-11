//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace maple
{
	class VertexBuffer;
	class IndexBuffer;
	class BatchTask;
	class AccelerationStructure;
	class CommandBuffer;
	class StorageBuffer;
	class DescriptorSet;
	class Texture;
	class TextureCube;
	struct vec3;
	struct vec4;
	struct mat4;
	struct SubMesh;

	auto createBottomLevel(const VertexBuffer* vertexBuffer, const IndexBuffer* indexBuffer, uint32_t vertexStride, const std::vector<SubMesh>& subMeshes, std::shared_ptr<BatchTask> tasks)->uint32_t;
	auto getAccelerationStructure(uint32_t id)->std::shared_ptr<AccelerationStructure>;

	auto getVertexSets()->std::shared_ptr<DescriptorSet>;
	auto getIndexSets()->std::shared_ptr<DescriptorSet>;
	auto getSamplerSets()->std::shared_ptr<DescriptorSet>;

	auto getObjectSets() ->std::shared_ptr<DescriptorSet>;

	auto raytracingPrepare(const std::vector<VertexBuffer*>& vertexs,
		const std::vector<IndexBuffer*>& indices,
		const std::shared_ptr<StorageBuffer>& objectBuffer,
		const std::shared_ptr<AccelerationStructure>& topLevel,
		const std::vector<std::shared_ptr<Texture>>& textures,
		const std::shared_ptr<TextureCube> &cube,
		const CommandBuffer* cmd) -> void;

	namespace reflection 
	{
		auto clear(const CommandBuffer* cmd) -> void;

		auto init(uint32_t w, uint32_t h) -> void;

		auto render(
			const vec4& ambientLight,
			const vec3& cameraPos,
			const vec3& cameraDelta,
			const mat4& viewProjInv,
			const mat4& prevViewProj,
			const CommandBuffer* cmd
		)->std::shared_ptr<Texture>;
	}
}