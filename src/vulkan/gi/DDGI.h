//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#pragma once

#include <memory>
#include <cstdint>
#include <glm/glm.hpp>

namespace maple
{
	class Texture;
	class DescriptorSet;
	class CommandBuffer;

	namespace ddgi
	{
		auto updateSet(const std::shared_ptr< DescriptorSet>& set) -> void;

		auto renderDebugProbe(const CommandBuffer* cmd,
			const glm::vec3& startPosition,
			const glm::mat4& viewProj) -> void;

		auto init(uint32_t width, uint32_t height) -> void;

		auto render(const CommandBuffer* cmd, 
			const glm::vec3& startPosition,
			const glm::mat4& viewProjInv,
			const glm::vec4 * frustums,
			const glm::vec4& ambientColor
		) -> std::shared_ptr<Texture>;
	}        // namespace maple
}