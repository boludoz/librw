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
	class StorageBuffer;
	class AccelerationStructure;
	class CommandBuffer;
	class Texture;
	class TextureCube;
	struct vec3;
	struct mat4;

	namespace shadow 
	{
		auto init(uint32_t w, uint32_t h) -> void;
		auto clear(const CommandBuffer* cmd) -> void;
		auto render(
			const std::vector<VertexBuffer*>& vertexs,
			const std::vector<IndexBuffer*>& indices,
			const std::shared_ptr<StorageBuffer>& objectBuffer,
			const std::shared_ptr<AccelerationStructure>& topLevel,
			const std::shared_ptr<Texture>& depth,
			const std::vector<std::shared_ptr<Texture>>& textures,
			const vec3& cameraPos,
			const mat4& viewProjInv,
			float farPlane,
			float nearPlane,
			const CommandBuffer* cmd)->std::shared_ptr<Texture>;
	}
}