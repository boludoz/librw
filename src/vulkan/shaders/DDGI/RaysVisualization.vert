//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#version 460

#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : require

#include "DDGICommon.glsl"
#include "../Common/Random.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;

layout(location = 0) out vec3 outColor;

layout(set = 0, binding = 0) uniform sampler2D uInputRadiance;
layout(set = 0, binding = 1) uniform sampler2D uInputDirectionDepth;
layout(set = 0, binding = 2, scalar) uniform DDGIUBO
{
	DDGIUniform ddgi;
};


layout(push_constant) uniform PushConstants
{
	mat4 viewProj;
	float scale;
	int probeID;
}pushConsts;

// ------------------------------------------------------------------------
// MAIN -------------------------------------------------------------------
// IndexCount * RaysPerProbe;
// ------------------------------------------------------------------------

void main()
{
	ivec3 gridCoord = probeIndexToGridCoord(ddgi, pushConsts.probeID);
	vec3 probePosition = gridCoordToPosition(ddgi, gridCoord);

	int rayId = gl_InstanceIndex;/// pushConsts.indexCount;
	ivec2 texelID = ivec2(rayId,  pushConsts.probeID);
 	vec4 rayInfo  = texelFetch(uInputDirectionDepth, texelID, 0);
 	vec3 radiance = texelFetch(uInputRadiance, texelID, 0).xyz;

	mat4 modelMat = mat4(1.0);

    modelMat[0][0] = 0.04;
    modelMat[1][1] = 0.04;
    modelMat[2][2] = rayInfo.w > 0 ? rayInfo.w : 0;
	modelMat[2][2] *= 0.5;//because the cube is 2， half is one

    modelMat = makeRotationMatrix4x4(rayInfo.xyz) * modelMat;

    mat4 translationMat = mat4(1.0);
    translationMat[3][0] = probePosition.x;
    translationMat[3][1] = probePosition.y;
    translationMat[3][2] = probePosition.z;

    modelMat = translationMat * modelMat;
	gl_Position = pushConsts.viewProj *  modelMat  * vec4(inPosition  + vec3(0,0,1), 1.0f);
	outColor = radiance.xyz;
}