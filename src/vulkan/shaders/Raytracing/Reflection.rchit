//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_ray_query : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : enable
#include "Common.glsl"
#include "../Common/Common.glsl"
#include "../Common/Math.glsl"

layout(location = 0) rayPayloadInEXT ReflectionPayload inPayload;

hitAttributeEXT vec2 hitAttribs;

////////////////////////Scene Infos////////////////////////////////
layout (set = 0, binding = 0, std430) buffer ObjectBuffer 
{
	Object objects[];
};

layout (set = 0, binding = 1) uniform accelerationStructureEXT uTopLevelAS;

layout (set = 0, binding = 2) uniform samplerCube uSkybox;

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

#include "../Common/Bindless.glsl"
#include "../Common/RayQuery.glsl"
#include "../PathTrace/BRDF.glsl"

vec3 doDynamicLight(vec3 V, vec3 N, uint objectId, in SurfaceMaterial p)
{
	vec3 color = vec3(0.0, 0.0, 0.0);
    
    if(objects[objectId].u_offsets.w == 0)
        return color;

    uint i = nextUInt(inPayload.random, objects[objectId].u_offsets.w);

	//for(int i = 0; i < objects[objectId].u_offsets.w; i++)
    {
        vec3 dir;
        float tmin      = 0.0001;
        float tmax      = 10000.0;
        vec3 c = vec3(0);
        float l = 1;
		if(objects[objectId].u_lightParams[i].x == 1.0){
			// direct
            dir = -objects[objectId].u_lightDirection[i].xyz;
			l = max(0.0, dot(N, dir));
			c = l * objects[objectId].u_lightColor[i].rgb;
		}
        else if(objects[objectId].u_lightParams[i].x == 2.0){
			// point
			dir = objects[objectId].u_lightPosition[i].xyz - V;
			float dist = length(dir);
            tmax = dist;
			float atten = max(0.0, (1.0 - dist/objects[objectId].u_lightParams[i].y));
			l = max(0.0, dot(N, normalize(dir)));
			c = l * objects[objectId].u_lightColor[i].rgb*atten;
		}

        uint rayFlags = gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT;
        float vis = 1;
        vec3 L = vec3(0);
        if(l > 0)
        {
            dir = normalize(dir);
            vis = queryVisibility(V + N * 0.1, normalize(dir), tmax * 0.9, rayFlags);
            vec3 view = -gl_WorldRayDirectionEXT;
            vec3 halfV = vec3(0.0f);
            halfV = normalize(dir + view);
            vec3 brdf = BRDF(p, view, halfV, dir);
            float cosTheta = clamp(dot(N, dir), 0.0, 1.0);
            L = inPayload.T * brdf * cosTheta * c * vis * objects[objectId].u_offsets.w; 
        }
    
        color += L;
	}
	return color;
}


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
    SurfaceMaterial surface = getSurfaceNoLight(gl_InstanceCustomIndexEXT, gl_GeometryIndexEXT, gl_PrimitiveID, hitAttribs);
    inPayload.color.rgb += doDynamicLight(surface.vertex.position,surface.vertex.normal,surface.objectId,surface);
    inPayload.rayLength = gl_RayTminEXT + gl_HitTEXT;
}