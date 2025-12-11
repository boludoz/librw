//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#version 460

#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require

#include "DDGICommon.glsl"

layout (set = 0, binding = 2) uniform samplerCube uSkybox;

rayPayloadInEXT GIPayload inPayload;

layout(push_constant) uniform PushConstants
{
    mat4  randomOrientation;
    vec4  ambientColor;
    uint  numFrames;
    uint  infiniteBounces;
    float intensity;
}pushConsts;

void main()
{
    vec3 envSample = texture(uSkybox, gl_WorldRayDirectionEXT).rgb; 
    inPayload.L = envSample;
}
