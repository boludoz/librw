

#include "../rwbase.h"

#include "../rwplg.h"

#include "../rwengine.h"

#include "../rwerror.h"

#include "../rwpipeline.h"

#include "../rwobjects.h"

#include "../rwrender.h"

#include "vklighting.h"

#include "StorageBuffer.h"
#include "Console.h"
#include "Tweak/Tweakable.h"

namespace rw
{
	constexpr static int32_t MAX_LIGHT_SIZE = 64;

	struct LightBuffer
	{
		int32_t count;
		LightData lights[MAX_LIGHT_SIZE];
	};

	static LightBuffer lightBuffer;

	static maple::StorageBuffer::Ptr lightBufferSSBO;
	static int32_t lightCount = 0;

	static maple::TweakFloat Intensity = { "Lighting:Intensity", 1.5, 0, 10 };


	static auto operator*(const rw::RGBAf& r, float o) {
		return rw::RGBAf{ r.red * o,r.green * o,r.blue * o ,r.alpha * o };
	}

	void lighting::prepareBuffer()
	{
		if (lightBufferSSBO == nullptr)
		{
			lightBufferSSBO = maple::StorageBuffer::create(sizeof(LightBuffer), nullptr, maple::BufferOptions{ false, maple::MemoryUsage::MEMORY_USAGE_CPU_TO_GPU });
			auto bufferPtr = lightBufferSSBO->mapPointer<LightBuffer>();
			memset(bufferPtr, 0, sizeof(LightBuffer));
		}

		lightCount = 0;
		lightBuffer.count = 0;

		FORLIST(lnk, ((World*)engine->currentWorld)->globalLights) {
			Light* l = Light::fromWorld(lnk);
			if (l->getType() == Light::DIRECTIONAL/* && l->sunLight == 1*/) {
				lightBuffer.lights[lightCount].distance = 0;
				lightBuffer.lights[lightCount].type = 1.0f;
				lightBuffer.lights[lightCount].color = l->color * std::pow(Intensity.var, 1.4f);
				memcpy(&lightBuffer.lights[lightCount].direction, &l->getFrame()->getLTM()->at, sizeof(V3d));
				lightBuffer.count = ++lightCount;
				break;
			}
		}
	}
	/*
	* actually, we need a light buffer for global, currently we put all light for each object.
	* refactor it later.
	*/
	std::shared_ptr<maple::StorageBuffer> lighting::getLightBuffer(const rw::V3d& cameraPos)
	{
		for (auto i = 1; i < lightBuffer.count; i++)
		{
			lightBuffer.lights[i].distance = length(sub(lightBuffer.lights[i].position, cameraPos));
		}

		std::sort(lightBuffer.lights + 1, lightBuffer.lights + lightBuffer.count, [](const LightData& left, const LightData& right) {
			return left.distance < right.distance;
			});

		auto bufferPtr = lightBufferSSBO->mapPointer<LightBuffer>();
		memcpy(bufferPtr, &lightBuffer, sizeof(LightBuffer));
		lightBufferSSBO->unmap();

		return lightBufferSSBO;
	}

	void lighting::addLight(uint8_t type, const rw::V3d& coors, const rw::V3d& dir, float radius, float red, float green, float blue, bool castShadow)
	{
		if (type == 0) //Point 
		{
			if (lightBuffer.count >= MAX_LIGHT_SIZE)
				return;

			lightBuffer.lights[lightCount].type = 2.0f;
			lightBuffer.lights[lightCount].radius = radius * 2.0;
			lightBuffer.lights[lightCount].castShadow = castShadow;
			//lightBuffer.lights[lightCount].color = { red, green, blue,1.f };
			lightBuffer.lights[lightCount].color = RGBAf{ red, green, blue,1.f } * std::pow(Intensity.var, 1.4f);
			memcpy(&lightBuffer.lights[lightCount].position, &coors, sizeof(rw::V3d));
			memcpy(&lightBuffer.lights[lightCount].direction, &dir, sizeof(rw::V3d));
			lightBuffer.count = ++lightCount;
		}
	}

	std::vector<LightData> lighting::enumerateLights(Atomic* atomic, WorldLights& worldLights)
	{
		if (worldLights.numLocals >= MAX_LIGHT_SIZE)
			return {};

		std::vector<LightData> ret;

		for (uint32_t i = 1; i < lightBuffer.count; i++)
		{
			Sphere* atomsphere = atomic->getWorldBoundingSphere();
			V3d dist = sub(lightBuffer.lights[i].position, atomsphere->center);
			float distance = length(dist);
			if (length(dist) < atomsphere->radius + lightBuffer.lights[i].radius)
			{
				ret.push_back(lightBuffer.lights[i]);
				ret.back().distance = distance;
			}
		}

		std::sort(ret.begin(), ret.end(), [](const LightData& left, const LightData& right) {
			return left.distance < right.distance;
		});


		std::sort(ret.begin(), ret.end(), [](const LightData& left, const LightData& right) {
			return left.castShadow > right.castShadow;
		});

		ret.push_back(lightBuffer.lights[0]);

		return ret;
	}

	std::vector<rw::LightData> lighting::enumerateLights()
	{
		std::vector<LightData> ret;
		for (uint32_t i = 1; i < lightBuffer.count; i++)
		{
			ret.push_back(lightBuffer.lights[i]);
		}
		return ret;
	}
}