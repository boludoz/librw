//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#pragma once

#include <memory>

namespace maple
{
	class Texture2D;
	class Texture;
	class StorageBuffer;
	class CommandBuffer;
	class UniformBuffer;
	struct mat4;
	struct vec4;
	struct ivec4;

	namespace gbuffer
	{
		auto init() -> void;
		auto render(
			std::shared_ptr<maple::UniformBuffer> cameraUniform,
			const std::shared_ptr<Texture>& color,
			const std::shared_ptr<Texture>& depth,
			const std::shared_ptr<Texture>& shadow,
			const std::shared_ptr<Texture>& reflection,
			const std::shared_ptr<Texture>& indirect,
			const std::shared_ptr<Texture>& pathOut,
			const std::shared_ptr<StorageBuffer> & ssbo,
			const mat4& viewProjInv,
			const CommandBuffer * cmd
		) -> void;

		auto getGBufferColor(bool prev = false)->std::shared_ptr<Texture>;
		auto getGBuffer1(bool prev = false)->std::shared_ptr<Texture>;
		auto getGBuffer2(bool prev = false)->std::shared_ptr<Texture>;
		auto getGBuffer3(bool prev = false)->std::shared_ptr<Texture>;
		auto getGBuffer4(bool prev = false)->std::shared_ptr<Texture>;
		auto getDepth(bool prev = false)->std::shared_ptr<Texture>;
		auto getPingPong()->uint32_t;
		auto clear(const CommandBuffer* cmd) -> void;
		auto clear(CommandBuffer* cmd, const maple::vec4& value, const maple::ivec4& rect) -> void;
	}
}