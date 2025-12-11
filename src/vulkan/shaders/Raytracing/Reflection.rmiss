//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#version 460

#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require

#include "Common.glsl"

layout(location = 0) rayPayloadInEXT ReflectionPayload inPayload;

layout (set = 0, binding = 2) uniform samplerCube uSkybox;

layout(push_constant) uniform PushConstants
{
    vec4  cameraPosition;
    mat4  viewProjInv;
    vec4  ambientColor;
    float bias;
    float trim;
    int numFrames;
    int mipmap;
}pushConsts;


void main()
{
    vec3 envSample = texture(uSkybox, gl_WorldRayDirectionEXT).rgb; 
    inPayload.color = inPayload.T * envSample;
    inPayload.rayLength = -1.0f;
}
