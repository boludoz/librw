//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <memory>

namespace maple
{
	class Texture2D;

	namespace blue_noise
	{
		enum BlueNoiseSpp : uint8_t
		{
			Blue_Noise_1SPP,
			Blue_Noise_2SPP,
			Blue_Noise_4SPP,
			Blue_Noise_8SPP,
			Blue_Noise_16SPP,
			Blue_Noise_32SPP,
			Blue_Noise_64SPP,
			Blue_Noise_128SPP,
			Length
		};

		static constexpr const char* SOBOL_TEXTURE = "shaders/blue_noise/sobol_256_4d.png";

		static constexpr const char* SCRAMBLING_RANKING_TEXTURES[] = {
			"shaders/blue_noise/scrambling_ranking_128x128_2d_1spp.png",
			"shaders/blue_noise/scrambling_ranking_128x128_2d_2spp.png",
			"shaders/blue_noise/scrambling_ranking_128x128_2d_4spp.png",
			"shaders/blue_noise/scrambling_ranking_128x128_2d_8spp.png",
			"shaders/blue_noise/scrambling_ranking_128x128_2d_16spp.png",
			"shaders/blue_noise/scrambling_ranking_128x128_2d_32spp.png",
			"shaders/blue_noise/scrambling_ranking_128x128_2d_64spp.png",
			"shaders/blue_noise/scrambling_ranking_128x128_2d_128spp.png",
			"shaders/blue_noise/scrambling_ranking_128x128_2d_256spp.png" };

		static constexpr int32_t LDR_LEN = 16;

		static constexpr const char* LDR_TEXTURES[LDR_LEN] = {
			"shaders/blue_noise/LDR_LLL1_0.png",
			"shaders/blue_noise/LDR_LLL1_1.png",
			"shaders/blue_noise/LDR_LLL1_2.png",
			"shaders/blue_noise/LDR_LLL1_3.png",
			"shaders/blue_noise/LDR_LLL1_4.png",
			"shaders/blue_noise/LDR_LLL1_5.png",
			"shaders/blue_noise/LDR_LLL1_6.png",
			"shaders/blue_noise/LDR_LLL1_7.png",
			"shaders/blue_noise/LDR_LLL1_8.png",
			"shaders/blue_noise/LDR_LLL1_9.png",
			"shaders/blue_noise/LDR_LLL1_10.png",
			"shaders/blue_noise/LDR_LLL1_11.png",
			"shaders/blue_noise/LDR_LLL1_12.png",
			"shaders/blue_noise/LDR_LLL1_13.png",
			"shaders/blue_noise/LDR_LLL1_14.png",
			"shaders/blue_noise/LDR_LLL1_15.png",
		};

		auto init() -> void;

		auto getSobolSequence()->std::shared_ptr<Texture2D>;

		auto getScramblingRanking(BlueNoiseSpp spp)->std::shared_ptr<Texture2D>;
	}
}