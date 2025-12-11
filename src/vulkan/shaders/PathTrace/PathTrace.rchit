//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#version 460
#extension GL_ARB_shading_language_420pack : enable
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : enable
#extension GL_EXT_ray_query : enable

#include "../Common/Common.glsl"
#include "../Raytracing/Common.glsl"

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

hitAttributeEXT vec2 hitAttribs;

layout(location = 0) rayPayloadInEXT PathTracePayload inPayload;
layout(location = 1) rayPayloadEXT PathTracePayload indirectPayload;


#include "../Common/Bindless.glsl"
#include "../Common/RayQuery.glsl"
#include "BRDF.glsl"
vec3 doDynamicLight(vec3 V, vec3 N, uint objectId, in SurfaceMaterial p)
{
	vec3 color = vec3(0.0, 0.0, 0.0);

    if(objects[objectId].u_offsets.w > 0)
    {
        uint i =  nextUInt(inPayload.random,objects[objectId].u_offsets.w);
        vec3 dir;
        float tmin      = 0.0001;
        float tmax      = 10000.0;
        vec3 c = vec3(0);
		if(objects[objectId].u_lightParams[i].x == 1.0){
			// direct
            dir = -objects[objectId].u_lightDirection[i].xyz;
			float l = max(0.0, dot(N, -objects[objectId].u_lightDirection[i].xyz));
			c = l * objects[objectId].u_lightColor[i].rgb;
		}
        else if(objects[objectId].u_lightParams[i].x == 2.0){
			// point
			dir = objects[objectId].u_lightPosition[i].xyz - V;
			float dist = length(dir);
            tmax = dist;
			float atten = max(0.0, (1.0 - dist/objects[objectId].u_lightParams[i].y));
			float l = max(0.0, dot(N, normalize(dir)));
			c = l * objects[objectId].u_lightColor[i].rgb*atten;
		}

        uint rayFlags = gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT;

        if (inPayload.depth == 0)
            rayFlags = 0;

        float vis = queryVisibility(V + N * pushConsts.shadowRayBias, normalize(dir), tmax * 0.8, rayFlags);
        //color += c * vis;
        vec3 view = -gl_WorldRayDirectionEXT;
        vec3 halfV = vec3(0.0f);
        halfV = normalize(dir + view);
        vec3 brdf = BRDF(p, view, halfV, dir);
        float cosTheta = clamp(dot(N, dir), 0.0, 1.0);
        vec3 L = inPayload.T * brdf * cosTheta * c * vis *  objects[objectId].u_offsets.w * pow(2, 1.4); 
        color += L;
    }
	return color;
}



vec3 indirectLighting(in SurfaceMaterial p)
{
    vec3 Wo = -gl_WorldRayDirectionEXT;
    vec3 Wi;
    float pdf;

    vec3 brdf = sampleBRDF(p, Wo, inPayload.random, Wi, pdf);
    //

    float cosTheta = clamp(dot(p.vertex.normal, Wi), 0.0, 1.0);

    indirectPayload.L = vec3(0.0f);
    indirectPayload.T = inPayload.T *  (brdf * cosTheta) / pdf;

    // Russian roulette
    float probability = max(indirectPayload.T.r, max(indirectPayload.T.g, indirectPayload.T.b));
    if (nextFloat(inPayload.random) > probability)
        return vec3(0.0f);
 
    // Add the energy we 'lose' by randomly terminating paths
    indirectPayload.T *= 1.0f / probability;

    indirectPayload.depth = inPayload.depth + 1;
    indirectPayload.random = inPayload.random;

    uint  rayFlags = gl_RayFlagsOpaqueEXT;
    uint  cullMask = 0xFF;
    float tmin      = 0.0001;
    float tmax      = 10000.0;  
    vec3 origin = p.vertex.position.xyz;
    // Trace Ray
    traceRayEXT(uTopLevelAS, 
            rayFlags, 
            cullMask, 
            PATH_TRACE_CLOSEST_HIT_SHADER_IDX, 
            0, 
            PATH_TRACE_MISS_SHADER_IDX, 
            origin, 
            tmin, 
            Wi, 
            tmax, 
            1);

    return indirectPayload.L;
}

void main()
{
    SurfaceMaterial surface = getSurfaceNoLight(gl_InstanceCustomIndexEXT, gl_GeometryIndexEXT, gl_PrimitiveID, hitAttribs);
    //inPayload.L = vec3(0.f);
    //inPayload.L += surface.texColor.rgb;
    inPayload.L += doDynamicLight(surface.vertex.position, surface.vertex.normal, surface.objectId, surface) ;

    if ((inPayload.depth + 1) < pushConsts.maxBounces)
         inPayload.L += indirectLighting(surface);
}
