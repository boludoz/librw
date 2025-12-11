//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#version 460

#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_ray_query : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : enable


layout(set = 4, binding = 3, scalar) buffer ProbeState
{
    int8_t probeStates[];
};

#define DDGI_CLASSIFY
#include "DDGICommon.glsl"

#include "../Common/Common.glsl"

layout(location = 0) rayPayloadInEXT GIPayload inPayload;

hitAttributeEXT vec2 hitAttribs;

////////////////////////Scene Infos////////////////////////////////
layout (set = 0, binding = 0, std430) buffer ObjectBuffer 
{
	Object objects[];
};

layout (set = 0, binding = 1) uniform accelerationStructureEXT uTopLevelAS;


/////////////////////////////////////////////////////////////////
layout (set = 1, binding = 0, scalar) readonly buffer VertexBuffer 
{
    Vertex data[];
} Vertices[];

layout (set = 2, binding = 0, scalar) readonly buffer IndexBuffer 
{
    int16_t data[];
} Indices[];

///////////////////////////////////////////////////////////////
layout (set = 3, binding = 0) uniform sampler2D uSamplers[];

///////////////////////////////////////////////////////////////

layout (set = 4, binding = 0) uniform sampler2D uIrradiance;
layout (set = 4, binding = 1) uniform sampler2D uDepth;
layout (set = 4, binding = 2, scalar) uniform DDGIUBO
{
    DDGIUniform ddgi;
};


layout(push_constant) uniform PushConstants
{
    mat4  randomOrientation;
    vec4  ambientColor;
    uint  numFrames;
    uint  infiniteBounces;
    float intensity;
}pushConsts;


//#define QUERY_DISTANCE_SUPPORT
#include "../Common/RayQuery.glsl"
#include "../Common/Bindless.glsl"

vec3 indirectLighting(in SurfaceMaterial p)
{
    vec3 Wo = -gl_WorldRayDirectionEXT;
    return  p.texColor.rgb * sampleIrradiance(ddgi, p.vertex.position, p.vertex.normal, Wo, uIrradiance, uDepth) / PI;
}

bool contains(vec3 point) 
{
    vec3 max = gridToPosition(ddgi,ddgi.probeCounts.xyz);
    vec3 min = ddgi.startPosition.xyz;

    return !(point.x < min.x || point.x > max.x || point.y < min.y || point.y > max.y || point.z < min.z || point.z > max.z);
}

void main()
{
    SurfaceMaterial surface = getSurface(gl_InstanceCustomIndexEXT, gl_GeometryIndexEXT, gl_PrimitiveID, hitAttribs, inPayload.random);
    inPayload.L = surface.diffuse.xyz;

    /*if (!isBlack(surface.emissive.rgb))
        inPayload.L += surface.emissive.rgb * surface.emissive.a;*/
    bool back = gl_HitKindEXT == gl_HitKindBackFacingTriangleEXT;

    if (pushConsts.infiniteBounces == 1)
        inPayload.L += indirectLighting(surface);

    inPayload.hitDistance = (gl_RayTminEXT + gl_HitTEXT) * (back ? -1 : 1);
}
