//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////

#ifndef BLUE_NOISE_GLSL
#define BLUE_NOISE_GLSL

//https://blog.demofox.org/2020/05/16/using-blue-noise-for-raytraced-soft-shadows/

float sampleBlueNoise(ivec2 coord, int samplerIndex, int dimension, sampler2D sobalTex, sampler2D scramblingRanking)
{
	// wrap arguments
	coord.x = coord.x % 128;
	coord.y = coord.y % 128;
	samplerIndex = samplerIndex % 256;
	dimension = dimension % 4;

	// xor index based on optimized ranking
	int rankedIndex = samplerIndex ^ int(clamp(texelFetch(scramblingRanking, ivec2(coord.x, coord.y), 0).b * 256.0f, 0.0f, 255.0f));
	
    // fetch value in sequence
	int value = int(clamp(texelFetch(sobalTex, ivec2(rankedIndex, 0), 0)[dimension] * 256.0f, 0.0f, 255.0f));
	
    // If the dimension is optimized, xor sequence value based on optimized scrambling
	value = value ^ int(clamp(texelFetch(scramblingRanking, ivec2(coord.x, coord.y), 0)[dimension % 2] * 256.0f, 0.0f, 255.0f));
	
	return (0.5f + value) / 256.0f;
}

#endif