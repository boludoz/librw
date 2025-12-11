//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////

#include "Mesh.h"
#include <vector>
#define _USE_MATH_DEFINES
#include <math.h>
#include "VertexBuffer.h"
#include "IndexBuffer.h"

namespace maple
{
	auto Mesh::createSphere(uint32_t xSegments, uint32_t ySegments) -> Mesh::Ptr
	{
		std::vector<Vertex> data;
		float               sectorCount = static_cast<float>(xSegments);
		float               stackCount = static_cast<float>(ySegments);
		float               sectorStep = 2 * M_PI / sectorCount;
		float               stackStep = M_PI / stackCount;
		float               radius = 1.0f;

		for (int i = 0; i <= stackCount; ++i)
		{
			float stackAngle = M_PI / 2 - i * stackStep;
			float xy = radius * cos(stackAngle);
			float z = radius * sin(stackAngle);

			for (int32_t j = 0; j <= sectorCount; ++j)
			{
				float sectorAngle = j * sectorStep;
				float x = xy * cosf(sectorAngle);
				float y = xy * sinf(sectorAngle);

				float s = static_cast<float>(j / sectorCount);
				float t = static_cast<float>(i / stackCount);

				Vertex& vertex = data.emplace_back();
				vertex.pos = glm::vec3(x, y, z);
				vertex.texCoord = glm::vec2(s, t);
				vertex.normal = glm::normalize(glm::vec3(x, y, z));
				vertex.color = glm::vec4(1.f);
			}
		}

		std::vector<uint32_t> indices;
		uint32_t              k1, k2;
		for (uint32_t i = 0; i < stackCount; ++i)
		{
			k1 = i * (static_cast<uint32_t>(sectorCount) + 1U);
			k2 = k1 + static_cast<uint32_t>(sectorCount) + 1U;

			for (uint32_t j = 0; j < sectorCount; ++j, ++k1, ++k2)
			{
				if (i != 0)
				{
					indices.push_back(k1);
					indices.push_back(k2);
					indices.push_back(k1 + 1);
				}

				if (i != (stackCount - 1))
				{
					indices.push_back(k1 + 1);
					indices.push_back(k2);
					indices.push_back(k2 + 1);
				}
			}
		}

		auto mesh = std::make_shared<Mesh>();
		mesh->indexBuffer = IndexBuffer::create(indices.data(), indices.size());
		mesh->vertexBuffer = VertexBuffer::create(data.data(), data.size() * sizeof(Vertex));
		return mesh;
	}

	auto Mesh::createCube() -> Mesh::Ptr
	{
		std::vector<Vertex> data;
		data.resize(24);

		data[0].pos = glm::vec3(1.0f, 1.0f, 1.0f);
		data[0].color = glm::vec4(1.0f);
		data[0].normal = glm::vec3(0.0f, 0.0f, 1.0f);

		data[1].pos = glm::vec3(-1.0f, 1.0f, 1.0f);
		data[1].color = glm::vec4(1.0f);
		data[1].normal = glm::vec3(0.0f, 0.0f, 1.0f);

		data[2].pos = glm::vec3(-1.0f, -1.0f, 1.0f);
		data[2].color = glm::vec4(1.0f);
		data[2].normal = glm::vec3(0.0f, 0.0f, 1.0f);

		data[3].pos = glm::vec3(1.0f, -1.0f, 1.0f);
		data[3].color = glm::vec4(1.0f);
		data[3].normal = glm::vec3(0.0f, 0.0f, 1.0f);

		data[4].pos = glm::vec3(1.0f, 1.0f, 1.0f);
		data[4].color = glm::vec4(1.0f);
		data[4].normal = glm::vec3(1.0f, 0.0f, 0.0f);

		data[5].pos = glm::vec3(1.0f, -1.0f, 1.0f);
		data[5].color = glm::vec4(1.0f);
		data[5].normal = glm::vec3(1.0f, 0.0f, 0.0f);

		data[6].pos = glm::vec3(1.0f, -1.0f, -1.0f);
		data[6].color = glm::vec4(1.0f);
		data[6].normal = glm::vec3(1.0f, 0.0f, 0.0f);

		data[7].pos = glm::vec3(1.0f, 1.0f, -1.0f);
		data[7].color = glm::vec4(1.0f);
		data[7].texCoord = glm::vec2(0.0f, 1.0f);
		data[7].normal = glm::vec3(1.0f, 0.0f, 0.0f);

		data[8].pos = glm::vec3(1.0f, 1.0f, 1.0f);
		data[8].color = glm::vec4(1.0f);
		data[8].normal = glm::vec3(0.0f, 1.0f, 0.0f);

		data[9].pos = glm::vec3(1.0f, 1.0f, -1.0f);
		data[9].color = glm::vec4(1.0f);
		data[9].normal = glm::vec3(0.0f, 1.0f, 0.0f);

		data[10].pos = glm::vec3(-1.0f, 1.0f, -1.0f);
		data[10].color = glm::vec4(1.0f);
		data[10].texCoord = glm::vec2(0.0f, 1.0f);
		data[10].normal = glm::vec3(0.0f, 1.0f, 0.0f);

		data[11].pos = glm::vec3(-1.0f, 1.0f, 1.0f);
		data[11].color = glm::vec4(1.0f);
		data[11].normal = glm::vec3(0.0f, 1.0f, 0.0f);

		data[12].pos = glm::vec3(-1.0f, 1.0f, 1.0f);
		data[12].color = glm::vec4(1.0f);
		data[12].normal = glm::vec3(-1.0f, 0.0f, 0.0f);

		data[13].pos = glm::vec3(-1.0f, 1.0f, -1.0f);
		data[13].color = glm::vec4(1.0f);
		data[13].normal = glm::vec3(-1.0f, 0.0f, 0.0f);

		data[14].pos = glm::vec3(-1.0f, -1.0f, -1.0f);
		data[14].color = glm::vec4(1.0f);
		data[14].normal = glm::vec3(-1.0f, 0.0f, 0.0f);

		data[15].pos = glm::vec3(-1.0f, -1.0f, 1.0f);
		data[15].color = glm::vec4(1.0f);
		data[15].normal = glm::vec3(-1.0f, 0.0f, 0.0f);

		data[16].pos = glm::vec3(-1.0f, -1.0f, -1.0f);
		data[16].color = glm::vec4(1.0f);
		data[16].normal = glm::vec3(0.0f, -1.0f, 0.0f);

		data[17].pos = glm::vec3(1.0f, -1.0f, -1.0f);
		data[17].color = glm::vec4(1.0f);
		data[17].normal = glm::vec3(0.0f, -1.0f, 0.0f);

		data[18].pos = glm::vec3(1.0f, -1.0f, 1.0f);
		data[18].color = glm::vec4(1.0f);
		data[18].normal = glm::vec3(0.0f, -1.0f, 0.0f);

		data[19].pos = glm::vec3(-1.0f, -1.0f, 1.0f);
		data[19].color = glm::vec4(1.0f);
		data[19].normal = glm::vec3(0.0f, -1.0f, 0.0f);

		data[20].pos = glm::vec3(1.0f, -1.0f, -1.0f);
		data[20].color = glm::vec4(1.0f);
		data[20].normal = glm::vec3(0.0f, 0.0f, -1.0f);

		data[21].pos = glm::vec3(-1.0f, -1.0f, -1.0f);
		data[21].color = glm::vec4(1.0f);
		data[21].normal = glm::vec3(0.0f, 0.0f, -1.0f);

		data[22].pos = glm::vec3(-1.0f, 1.0f, -1.0f);
		data[22].color = glm::vec4(1.0f);
		data[22].normal = glm::vec3(0.0f, 0.0f, -1.0f);

		data[23].pos = glm::vec3(1.0f, 1.0f, -1.0f);
		data[23].color = glm::vec4(1.0f);
		data[23].normal = glm::vec3(0.0f, 0.0f, -1.0f);

		for (int i = 0; i < 6; i++)
		{
			data[i * 4 + 0].texCoord = glm::vec2(0.0f, 0.0f);
			data[i * 4 + 1].texCoord = glm::vec2(1.0f, 0.0f);
			data[i * 4 + 2].texCoord = glm::vec2(1.0f, 1.0f);
			data[i * 4 + 3].texCoord = glm::vec2(0.0f, 1.0f);
		}

		std::vector<uint32_t> indices{
			0, 1, 2,
			0, 2, 3,
			4, 5, 6,
			4, 6, 7,
			8, 9, 10,
			8, 10, 11,
			12, 13, 14,
			12, 14, 15,
			16, 17, 18,
			16, 18, 19,
			20, 21, 22,
			20, 22, 23 };

		auto mesh = std::make_shared<Mesh>();
		mesh->indexBuffer = IndexBuffer::create(indices.data(), indices.size());
		mesh->vertexBuffer = VertexBuffer::create(data.data(), data.size() * sizeof(Vertex));
		return mesh;
	}
}