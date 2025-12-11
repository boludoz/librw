//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#ifndef COMMON_RAY_GLSL
#define COMMON_RAY_GLSL

#include "Random.glsl"

#define PATH_TRACE_CLOSEST_HIT_SHADER_IDX 0
#define PATH_TRACE_MISS_SHADER_IDX 0

#define VISIBILITY_CLOSEST_HIT_SHADER_IDX 1
#define VISIBILITY_MISS_SHADER_IDX 1
#define RADIANCE_CLAMP_COLOR vec3(1.0f)

#define MAX_LIGHTS 32

struct Vertex
{
    vec3 position;
    vec3 normal;
    vec4 color;
    vec2 texCoord;
};


struct Triangle
{
    Vertex v0;
    Vertex v1;
    Vertex v2;
};

struct Object
{
    mat4 u_world;
	vec4 u_matColor;
	vec4 u_surfProps; // amb, spec, diff, extra	
	vec4 u_ambLight;  // a is textureID
    ivec4 u_offsets;  // indexOffset, instanceId ,textureId
	vec4 u_lightParams[MAX_LIGHTS];	// type, radius, minusCosAngle, hardSpot
	vec4 u_lightPosition[MAX_LIGHTS];
	vec4 u_lightDirection[MAX_LIGHTS];
	vec4 u_lightColor[MAX_LIGHTS];
};

struct LightData 
{
    float type;
    float radius;
    float minusCosAngle;
    float hardSpot;
    vec4 position;
    vec4 direction;
    vec4 color;
};

struct HitInfo
{
    uint materialIndex;
    uint primitiveOffset;
    uint primitiveId;
};

struct PathTracePayload
{
    vec3 L;
    vec3 T;
    uint depth;
    Random random;
};

struct SurfaceMaterial
{
    Vertex vertex;
    vec4 diffuse;
    uint objectId;
    vec4 texColor;
    vec3 ambColor;
    float roughness;
    float diffuseTerm;
    vec3 F0;
};

Vertex interpolatedVertex(in Triangle tri, in vec3 barycentrics)
{
    Vertex o;
    o.position = tri.v0.position.xyz * barycentrics.x + tri.v1.position.xyz * barycentrics.y + tri.v2.position.xyz * barycentrics.z;
    o.color = tri.v0.color * barycentrics.x + tri.v1.color * barycentrics.y + tri.v2.color * barycentrics.z;
    o.texCoord.xy = tri.v0.texCoord.xy * barycentrics.x + tri.v1.texCoord.xy * barycentrics.y + tri.v2.texCoord.xy * barycentrics.z;
    o.normal.xyz = normalize(tri.v0.normal.xyz * barycentrics.x + tri.v1.normal.xyz * barycentrics.y + tri.v2.normal.xyz * barycentrics.z);
    return o;
}

bool isBlack(vec3 c)
{
    return c.x == 0.0f && c.y == 0.0f && c.z == 0.0f;
}

vec4 gammaCorrectTexture(vec4 samp)
{
	return vec4(pow(samp.rgb, vec3(2.2)), samp.a);
}

#endif