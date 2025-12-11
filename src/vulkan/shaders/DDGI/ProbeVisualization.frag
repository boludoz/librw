//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#version 460

#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : enable
#extension GL_EXT_scalar_block_layout : enable


#include "DDGICommon.glsl"

layout(location = 0) in vec3 inFragPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in flat int inProbeIdx;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uIrradiance;
layout(set = 0, binding = 1) uniform sampler2D uDepth;
layout(set = 0, binding = 2, scalar) uniform DDGIUBO
{
    DDGIUniform ddgi;
};
layout(set = 0, binding = 3, scalar) buffer ProbeState
{
    int8_t probeStates[];
};


layout(push_constant) uniform PushConstants
{
    float scale;
    mat4 viewProj;
}pushConsts;

void main()
{
    vec2 probeCoord = textureCoordFromDirection(normalize(inNormal),
                                                    inProbeIdx,
                                                    ddgi.irradianceTextureWidth,
                                                    ddgi.irradianceTextureHeight,
                                                    ddgi.irradianceProbeSideLength);

    if(probeStates[inProbeIdx] == DDGI_PROBE_STATE_INACTIVE)
    {
       outColor = vec4(0, 1, 0, 1.0f);
    }
    else if(probeStates[inProbeIdx] == DDGI_PROBE_STATE_OUTOF_CAMERA)
    {
       outColor = vec4(1, 0, 0, 1.0f);
    }
    else
    {
       outColor = vec4(textureLod(uIrradiance, probeCoord, 0.0f).rgb, 1.0f);
    }
}