//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require

#include "../Common/Common.glsl"

layout(location = 0) rayPayloadInEXT PathTracePayload inPayload;

layout (set = 0, binding = 2) uniform samplerCube uSkybox;

layout(push_constant) uniform PushConsts
{
    mat4 invViewProj;
    vec4 cameraPos;
    vec4 ambientColor;
    uint numFrames;
    uint maxBounces;
    float accumulation;
    float shadowRayBias;
} pushConsts;

void main()
{
    vec3 envSample = texture(uSkybox, gl_WorldRayDirectionEXT).rgb; 

    if (inPayload.depth == 0)
        inPayload.L = envSample;
    else
        inPayload.L = inPayload.T * envSample;
}
