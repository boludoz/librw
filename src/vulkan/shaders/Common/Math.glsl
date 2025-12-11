//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#ifndef MATH_H
#define MATH_H

#define PI 3.1415926535897932384626433832795
#define M_PI PI

const float PHI = 1.61803398874989484820459;  // Φ = Golden Ratio   

#include "Random.glsl"

float square(float v)
{
    return v * v;
}

vec3 square(vec3 v)
{
    return v * v;
}

vec2 directionToOctohedral(vec3 normal)
{
    vec2 p = normal.xy * (1.0f / dot(abs(normal), vec3(1.0f)));
    return normal.z > 0.0f ? p : (1.0f - abs(p.yx)) * (step(0.0f, p) * 2.0f - vec2(1.0f));
}

vec3 octohedralToDirection(vec2 e)
{
    vec3 v = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    if (v.z < 0.0)
        v.xy = (1.0 - abs(v.yx)) * (step(0.0, v.xy) * 2.0 - vec2(1.0));
    return normalize(v);
}

mat3 makeRotationMatrix(vec3 z)
{
    const vec3 ref = abs(z.z) < 0.99 ? vec3(0, 0, 1) : vec3(1, 0, 0);

    const vec3 x = normalize(cross(ref, z));
    const vec3 y = cross(z, x);

    return mat3(x, y, z);
}

mat4 makeRotationMatrix4x4(vec3 z)
{
    const vec3 ref = abs(z.z) < 0.99 ? vec3(0, 0, 1) : vec3(1, 0, 0);

    const vec3 x = normalize(cross(ref, z));
    const vec3 y = cross(z, x);

    return mat4(vec4(x,0), 
                vec4(y,0),
                vec4(z,0),
                vec4(0,0,0,1));
}

vec3 worldPositionFromDepth(vec2 texCoords, float ndcDepth, mat4 viewProjInv)
{
	// the issue is from flipY.... I think I need to disable flipY and flipY everything in final stage...
    vec2 coords = texCoords;
    //coords.y = 1 - coords.y;
    vec2 screenPos = coords * 2.0 - 1.0;
    vec4 ndcPos = vec4(screenPos, ndcDepth, 1.0);
    vec4 worldPos = viewProjInv * ndcPos;
    worldPos = worldPos / worldPos.w;
    return worldPos.xyz;
}


vec4 importanceSampleGGX(vec2 E, vec3 N, float roughness)
{
    float a  = roughness * roughness;
    float m2 = a * a;

    float phi      = 2.0f * PI * E.x;
    float cosTheta = sqrt((1.0f - E.y) / (1.0f + (m2 - 1.0f) * E.y));
    float sinTheta = sqrt(1.0f - cosTheta * cosTheta);

    vec3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    float d = (cosTheta * m2 - cosTheta) * cosTheta + 1;
    float D = m2 / (PI * d * d);

    float PDF = D * cosTheta;

    vec3 up        = abs(N.z) < 0.999f ? vec3(0.0f, 0.0f, 1.0f) : vec3(1.0f, 0.0f, 0.0f);
    vec3 tangent   = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);

    vec3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return vec4(normalize(sampleVec), PDF);
}

vec3 hemispherePointUniform(float u, float v) {
	float phi = v * 2 * PI;
	float cosTheta = 1 - u;
	float sinTheta = sqrt(1 - cosTheta * cosTheta);
	return vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

vec3 hemispherePointCos(float u, float v) {
	float phi = v * 2 * PI;
	float cosTheta = sqrt(1 - u);
	float sinTheta = sqrt(1 - cosTheta * cosTheta);
	return vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

vec3 sampleHemisphereUniform(in vec3 normal, inout Random rng)
{
	return normalize(makeRotationMatrix(normal) * hemispherePointUniform(nextFloat(rng),nextFloat(rng)));
}

vec3 sampleHemisphereCos(in vec3 normal, inout Random rng)
{
	return normalize(makeRotationMatrix(normal) * hemispherePointCos(nextFloat(rng),nextFloat(rng)));
}

#endif