
#pragma once

#include <memory>
#include <cstdint>
#include <vector>

namespace maple 
{
	class StorageBuffer;
}

namespace rw 
{
	struct Atomic;
	struct WorldLights;
	struct LightData;
	struct V3d;

	namespace lighting 
	{
		void prepareBuffer();
		std::shared_ptr<maple::StorageBuffer> getLightBuffer(const rw::V3d& cameraPos);
		void addLight(uint8_t type, const rw::V3d& coors, const rw::V3d& dir, float radius, float red, float green, float blue, bool castShadow);
		std::vector<LightData> enumerateLights(Atomic * atomic, WorldLights & );
		std::vector<LightData> enumerateLights();
	}
}