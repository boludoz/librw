//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////

#include "vkBlueNoise.h"
#include "Textures.h"

#include "../../rwbase.h"

#include "../../rwplg.h"

#include "../../rwengine.h"

#include "../../rwerror.h"

#include "../../rwpipeline.h"

#include "../../rwobjects.h"

#include "../../rwrender.h"

namespace maple::blue_noise
{
	struct BlueNoise
	{
		std::shared_ptr<Texture2D> sobolSequence;
		std::shared_ptr<Texture2D> scramblingRanking[BlueNoiseSpp::Length];
		std::shared_ptr<Texture2D> ldrTextures[LDR_LEN];
	}noise;

	auto init() -> void
	{
		{
			auto image = rw::readPNG(blue_noise::SOBOL_TEXTURE);
			image->convertTo32();
			noise.sobolSequence = Texture2D::create(image->width, image->height, image->pixels);
		}
		for (int32_t i = 0; i < blue_noise::BlueNoiseSpp::Length; i++)
		{
			auto image = rw::readPNG(blue_noise::SCRAMBLING_RANKING_TEXTURES[i]);
			image->convertTo32();
			noise.scramblingRanking[i] = Texture2D::create(
				image->width, image->height, image->pixels
			);
		}

		for (int32_t i = 0; i < blue_noise::LDR_LEN; i++)
		{
			auto image = rw::readPNG(LDR_TEXTURES[i]);
			image->convertTo32();
			noise.ldrTextures[i] = Texture2D::create(
				image->width, image->height, image->pixels,
				TextureParameters{
				TextureFormat::RGBA8,
				TextureFilter::Nearest,
				TextureWrap::Repeat 
			});
		}
	}
	
	auto getSobolSequence() -> std::shared_ptr<Texture2D>
	{
		return noise.sobolSequence;
	}

	auto getScramblingRanking(BlueNoiseSpp spp) -> std::shared_ptr<Texture2D>
	{
		return noise.scramblingRanking[spp];
	}
}

