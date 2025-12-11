//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#ifndef REFLECTION_COMMON
#define REFLECTION_COMMON

#define DDGI_REFLECTIONS_ROUGHNESS_THRESHOLD 0.2f
#define DDGI_MIRROR_ROUGHNESS_THRESHOLD 0.98f

#include "../Common/Random.glsl"

struct ReflectionPayload
{
    vec3 color;
    vec3 T;
    Random random;
    float rayLength;
};

/**
* get luminance, 0.299, 0.587, 0.114 is the Grayscale
*/
float luminance(vec3 rgb)
{
    return max(dot(rgb, vec3(0.299, 0.587, 0.114)), 0.0001);
}

#endif