//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <glm/glm.hpp>

namespace maple
{
	class Texture;
	class CommandBuffer;
	/*
	* Test for reference.
	**/
	namespace path_trace
	{
		auto init(uint32_t width, uint32_t height) -> void;
		auto render(
			const glm::vec4& ambientColor,
			const glm::vec3& cameraPos,
			const glm::mat4& viewProjInv,
			const CommandBuffer* cmd, bool refresh)->std::shared_ptr <Texture>;
	}
}