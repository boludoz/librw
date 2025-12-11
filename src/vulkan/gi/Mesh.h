//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#pragma once

#include <memory>
#include <glm/glm.hpp>

namespace maple
{
	class VertexBuffer;
	class IndexBuffer;

	struct Vertex
	{
		glm::vec3   pos;
		glm::vec4   color;
		glm::vec2   texCoord;
		glm::vec3   normal;

		inline auto operator-(const Vertex& right) const -> Vertex
		{
			Vertex ret;
			ret.pos = pos - right.pos;
			ret.color = color - right.color;
			ret.texCoord = texCoord - right.texCoord;
			ret.normal = normal - right.normal;
			return ret;
		}

		inline auto operator+(const Vertex& right) const -> Vertex
		{
			Vertex ret;
			ret.pos = pos + right.pos;
			ret.color = color + right.color;
			ret.texCoord = texCoord + right.texCoord;
			ret.normal = normal + right.normal;
			return ret;
		}

		inline auto operator*(float factor) const -> Vertex
		{
			Vertex ret;
			ret.pos = pos * factor;
			ret.color = color * factor;
			ret.texCoord = texCoord * factor;
			ret.normal = normal * factor;
			return ret;
		}

		inline auto operator==(const Vertex& other) const -> bool
		{
			return pos == other.pos && color == other.color && texCoord == other.texCoord && normal == other.normal;//
		}
	};

	struct Mesh 
	{
		using Ptr = std::shared_ptr<Mesh>;
		std::shared_ptr<VertexBuffer> vertexBuffer;
		std::shared_ptr<IndexBuffer> indexBuffer;
		static auto createSphere(uint32_t xSegments = 64, uint32_t ySegments = 64)->Mesh::Ptr;
		static auto createCube()->Mesh::Ptr;
	};
};