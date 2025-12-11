//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#ifndef BINDLESS_GLSL
#define BINDLESS_GLSL

#include "Common.glsl"

HitInfo getHitInfo(uint geometryIndex, uint offset, uint primitiveId)
{
    HitInfo hitInfo;
    hitInfo.materialIndex   = geometryIndex;
    hitInfo.primitiveOffset = offset;
    hitInfo.primitiveId     = primitiveId;
    return hitInfo;
}

Vertex getVertex(uint meshIdx, uint vertexIdx)
{
    return Vertices[nonuniformEXT(meshIdx)].data[vertexIdx];
}

Triangle getTriangle(in Object transform, uint instanceId, in HitInfo hitInfo)
{
    Triangle tri;

    uint primitiveId = hitInfo.primitiveId * 3 + hitInfo.primitiveOffset;

    uvec3 idx = uvec3
    (
        Indices[nonuniformEXT(instanceId)].data[primitiveId], 
        Indices[nonuniformEXT(instanceId)].data[primitiveId + 1],
        Indices[nonuniformEXT(instanceId)].data[primitiveId + 2]
    );

    tri.v0 = getVertex(instanceId, idx.x);
    tri.v1 = getVertex(instanceId, idx.y);
    tri.v2 = getVertex(instanceId, idx.z);
    return tri;
}

void transformVertex(in Object transform, inout Vertex v)
{
    mat4 modelMat = transform.u_world;
    mat3 normalMat = mat3(transform.u_world);

    vec4 newPos     = modelMat * vec4(v.position,1);
    v.position      = newPos.xyz;
    v.normal.xyz    = normalMat * v.normal.xyz;
}

#define DIRECTIONALS
#define POINTLIGHTS
#define SPOTLIGHTS
#define surfAmbient (instance.u_surfProps.x)
#define surfSpecular (instance.u_surfProps.y)
#define surfDiffuse (instance.u_surfProps.z)

vec3 DoRndLight(vec3 V, vec3 N, uint objectId, inout Random rnd)
{
    vec3 color = vec3(0.0, 0.0, 0.0);

    if( objects[objectId].u_offsets.w == 0 )
        return color;
    
    uint i = nextUInt(rnd,objects[objectId].u_offsets.w);

    vec3 dir = vec3(0);
    float dist = 0;
    float l = 0;
    if(objects[objectId].u_lightParams[i].x == 1.0)
    {
        // direct
        dir = -objects[objectId].u_lightDirection[i].xyz;
        l = max(0.0, dot(N, dir));
        color += l * objects[objectId].u_lightColor[i].rgb;
        dist = 10000;
    }
    else if(objects[objectId].u_lightParams[i].x == 2.0)
    {
        // point
        dir = objects[objectId].u_lightPosition[i].xyz - V;
        dist = length(dir);
        float atten = max(0.0, (1.0 - dist/objects[objectId].u_lightParams[i].y));
        l = max(0.0, dot(N, normalize(dir)));
        color += l * objects[objectId].u_lightColor[i].rgb * atten;
    }
    float vis = 1;
#ifdef QUERY_DISTANCE_SUPPORT
    if (l > 0) 
        vis = queryVisibility(V, normalize(dir) + N * 0.1, dist * 0.8, 0);
#endif
	return vis * color * objects[objectId].u_offsets.w;//russian roulette
}

vec3 DoDynamicLight(vec3 V, vec3 N, uint objectId)
{
	vec3 color = vec3(0.0, 0.0, 0.0);
	for(int i = 0; i < objects[objectId].u_offsets.w; i++){
		if(objects[objectId].u_lightParams[i].x == 0.0)
			break;
#ifdef DIRECTIONALS
		if(objects[objectId].u_lightParams[i].x == 1.0){
			// direct
			float l = max(0.0, dot(N, -objects[objectId].u_lightDirection[i].xyz));
			color += l * objects[objectId].u_lightColor[i].rgb;
		}else
#endif
#ifdef POINTLIGHTS
		if(objects[objectId].u_lightParams[i].x == 2.0){
			// point
			vec3 dir = V - objects[objectId].u_lightPosition[i].xyz;
			float dist = length(dir);
			float atten = max(0.0, (1.0 - dist/objects[objectId].u_lightParams[i].y));
			float l = max(0.0, dot(N, -normalize(dir)));
			color += l * objects[objectId].u_lightColor[i].rgb*atten;
		}else
#endif
#ifdef SPOTLIGHTS
		if(objects[objectId].u_lightParams[i].x == 3.0){
			// spot
			vec3 dir = V - objects[objectId].u_lightPosition[i].xyz;
			float dist = length(dir);
			float atten = max(0.0, (1.0 - dist/objects[objectId].u_lightParams[i].y));
			dir /= dist;
			float l = max(0.0, dot(N, -dir));
			float pcos = dot(dir, objects[objectId].u_lightDirection[i].xyz);	// cos to point
			float ccos = -objects[objectId].u_lightParams[i].z;
			float falloff = (pcos-ccos)/(1.0-ccos);
			if(falloff < 0.0)	// outside of cone
				l = 0.0;
			l *= max(falloff, objects[objectId].u_lightParams[i].w);
			return l * objects[objectId].u_lightColor[i].rgb*atten;
		}else
#endif
			;
	}
	return color;
}

SurfaceMaterial getSurfaceNoLight(uint customIndex, uint geometryIndex, uint primitiveId, vec2 hitAttribs)
{
    const vec3 barycentrics = vec3(1.0 - hitAttribs.x - hitAttribs.y, hitAttribs.x, hitAttribs.y);

    SurfaceMaterial surface;
    surface.objectId = customIndex + geometryIndex;
    
    const Object instance = objects[customIndex + geometryIndex];

    uint primitiveOffset = instance.u_offsets.x;
    uint instanceID = instance.u_offsets.y;
    uint textureID = instance.u_offsets.z;

    const HitInfo hitInfo = getHitInfo(geometryIndex, primitiveOffset, primitiveId);
    const Triangle triangle = getTriangle(instance, instanceID, hitInfo);

    surface.vertex = interpolatedVertex(triangle, barycentrics);
    transformVertex(instance, surface.vertex);
    
    vec4 texColor = vec4(0);
    if (instance.u_offsets.z > -1)
    {
        texColor = textureLod(uSamplers[nonuniformEXT(instance.u_offsets.z)], surface.vertex.texCoord.xy, 0.0);
    }

    surface.texColor = texColor;
    surface.diffuse = texColor * instance.u_matColor;
    surface.diffuseTerm = surfDiffuse;
    surface.roughness = 1 - surfSpecular;
    surface.F0 = mix(vec3(0.03), surface.diffuse.xyz, surface.diffuseTerm);
    return surface;
}


SurfaceMaterial getSurface(uint customIndex, uint geometryIndex, uint primitiveId, vec2 hitAttribs)
{
    const vec3 barycentrics = vec3(1.0 - hitAttribs.x - hitAttribs.y, hitAttribs.x, hitAttribs.y);

    SurfaceMaterial surface;
    surface.objectId = customIndex + geometryIndex;

    const Object instance = objects[customIndex + geometryIndex];

    uint primitiveOffset =  instance.u_offsets.x;
    uint instanceID      =  instance.u_offsets.y;
    uint textureID       =  instance.u_offsets.z;

    const HitInfo hitInfo = getHitInfo(geometryIndex, primitiveOffset, primitiveId);
    const Triangle triangle = getTriangle(instance, instanceID, hitInfo);

    surface.vertex = interpolatedVertex(triangle, barycentrics);
    transformVertex(instance, surface.vertex);
    
    vec3 lightColor = DoDynamicLight(
        surface.vertex.position.xyz, 
        surface.vertex.normal.xyz, 
        customIndex + geometryIndex
    );

    vec4 texColor = vec4(0);
    if(instance.u_offsets.z > -1)
    {
        texColor = textureLod(uSamplers[nonuniformEXT(instance.u_offsets.z)], surface.vertex.texCoord.xy, 0.0);
    }
    
    vec4 color = surface.vertex.color;
    color.rgb += instance.u_ambLight.rgb * surfAmbient;
    color.rgb += lightColor * surfDiffuse;
    color = clamp(color, 0.0, 1.0);
    color *= instance.u_matColor;
    surface.diffuse = color * texColor;
    return surface;
}


SurfaceMaterial getSurface(uint customIndex, uint geometryIndex, uint primitiveId, vec2 hitAttribs, inout Random rnd)
{
    const vec3 barycentrics = vec3(1.0 - hitAttribs.x - hitAttribs.y, hitAttribs.x, hitAttribs.y);

    SurfaceMaterial surface;
    surface.objectId = customIndex + geometryIndex;

    const Object instance = objects[customIndex + geometryIndex];

    uint primitiveOffset = instance.u_offsets.x;
    uint instanceID = instance.u_offsets.y;
    uint textureID = instance.u_offsets.z;

    const HitInfo hitInfo = getHitInfo(geometryIndex, primitiveOffset, primitiveId);
    const Triangle triangle = getTriangle(instance, instanceID, hitInfo);

    surface.vertex = interpolatedVertex(triangle, barycentrics);
    transformVertex(instance, surface.vertex);

    vec3 lightColor = DoRndLight(
        surface.vertex.position.xyz,
        surface.vertex.normal.xyz,
        customIndex + geometryIndex,
        rnd
    );

    vec4 texColor = vec4(0);
    if(instance.u_offsets.z > -1)
    {
        texColor = textureLod(uSamplers[nonuniformEXT(instance.u_offsets.z)], surface.vertex.texCoord.xy, 0.0);
    }

    vec4 color = vec4(0);//baked lighting
   // color.rgb += instance.u_ambLight.rgb * surfAmbient;
  //  color.rgb *= surfDiffuse;
    color = clamp(color, 0.0, 1.0);
    color = instance.u_matColor;
    surface.diffuse = color * vec4(lightColor,1) * texColor;
    surface.texColor = texColor * instance.u_matColor;
    return surface;
}

#endif