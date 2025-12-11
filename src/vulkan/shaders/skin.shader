

#ifdef VERTEX_SHADER

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec4 in_color;
layout(location = 3) in vec2 in_tex0;
layout(location = 4) in	vec4 in_weights;
layout(location = 5) in	vec4 in_indices;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_tex0;
layout(location = 2) out float v_fog;

#ifdef ENABLE_GBUFFER

layout(location = 3) out vec4 v_gbuffer;
layout(location = 4) out vec4 v_currPos;
layout(location = 5) out vec4 v_prevPos;
layout(location = 6) out vec4 v_defaultColor;
#endif

layout(push_constant) uniform PushConsts 
{
	mat4 u_projView;
	mat4 u_projViewPrev;
	vec2 u_alphaRef;
	uint objectId;
	uint meshId;
	vec4 u_fogData;
	vec4 u_fogColor;
	mat4 u_world;
};

layout(set = 1, binding = 0, std140) readonly buffer ObjectBuffer
{
	Object objects[];
};

layout(set = 1, binding = 1, std140) uniform CameraUniform
{
	vec4 cameraPos;
};

layout(set = 2, binding = 0, std140) uniform BoneBuffer
{
	mat4 u_boneMatrices[64];
};

vec3 specularLight(vec3 V, vec3 N, vec3 dir, vec3 lightColor)
{
#if defined(DIRECTIONALS) || defined(POINTLIGHTS) || defined(SPOTLIGHTS) 
	vec3 viewDir = normalize(cameraPos.xyz - V);
	vec3 halfDir = normalize(dir + viewDir);  
	float spec = pow(max(dot(N, halfDir), 0.0), 32);
	vec3 specular = spec * lightColor * surfSpecular;
	return specular;
#else
	return vec3(0);
#endif
}

vec3 DoDynamicLight(vec3 V, vec3 N)
{
	vec3 color = vec3(0.0, 0.0, 0.0);
	for(int i = 0; i < MAX_LIGHTS; i++){
		if(objects[objectId].u_lightParams[i].x == 0.0)
			break;

		vec3 dir = vec3(0);
		float atten = 1;
#ifdef DIRECTIONALS
		if(objects[objectId].u_lightParams[i].x == 1.0){
			// direct
			dir = -objects[objectId].u_lightDirection[i].xyz;
			float l = max(0.0, dot(N, dir));
			color += l * objects[objectId].u_lightColor[i].rgb * surfDiffuse;
		}else
#endif
#ifdef POINTLIGHTS
		if(objects[objectId].u_lightParams[i].x == 2.0){
			// point
			dir = objects[objectId].u_lightPosition[i].xyz - V;
			float dist = length(dir);
			atten = max(0.0, (1.0 - dist/objects[objectId].u_lightParams[i].y));
			float l = max(0.0, dot(N, normalize(dir)));
			color += l * objects[objectId].u_lightColor[i].rgb*atten * surfDiffuse;
		}else
#endif
#ifdef SPOTLIGHTS
		if(objects[objectId].u_lightParams[i].x == 3.0){
			// spot
			dir = V - objects[objectId].u_lightPosition[i].xyz;
			float dist = length(dir);
			atten = max(0.0, (1.0 - dist/objects[objectId].u_lightParams[i].y));
			dir /= dist;
			float l = max(0.0, dot(N, -dir));
			float pcos = dot(dir, objects[objectId].u_lightDirection[i].xyz);	// cos to point
			float ccos = -objects[objectId].u_lightParams[i].z;
			float falloff = (pcos-ccos)/(1.0-ccos);
			if(falloff < 0.0)	// outside of cone
				l = 0.0;
			l *= max(falloff, objects[objectId].u_lightParams[i].w);
			return l * objects[objectId].u_lightColor[i].rgb*atten * surfDiffuse;
		}else
#endif
			;
		color += specularLight(V, N, dir,objects[objectId].u_lightColor[i].rgb);
	}
	return color;
}

float DoFog(float w)
{
	return clamp((w - u_fogEnd)*u_fogRange, u_fogDisable, 1.0);
}

void main(void)
{
	vec3 SkinVertex = vec3(0.0, 0.0, 0.0);
	vec3 SkinNormal = vec3(0.0, 0.0, 0.0);
	for(int i = 0; i < 4; i++){
		SkinVertex += (u_boneMatrices[int(in_indices[i])] * vec4(in_pos, 1.0)).xyz * in_weights[i];
		SkinNormal += (mat3(u_boneMatrices[int(in_indices[i])]) * in_normal) * in_weights[i];
	}

	//mat4 u_world = objects[objectId].u_world;
	vec4 Vertex = u_world * vec4(SkinVertex, 1.0);
	vec4 currPos = u_projView * Vertex;
	gl_Position = currPos;
	vec3 Normal = mat3(u_world) * SkinNormal;

	v_tex0 = in_tex0;

	v_color = vec4(1,1,1,in_color.a);
	v_defaultColor = v_color * objects[objectId].u_matColor;
	
	v_color.rgb += DoDynamicLight(Vertex.xyz, Normal);
	v_color = clamp(v_color, 0.0, 1.0);
	v_color *= objects[objectId].u_matColor;
	v_fog = DoFog(gl_Position.z);
	
#ifdef ENABLE_GBUFFER
	v_gbuffer = vec4(Normal, surfSpecular);
	v_currPos = currPos;
	v_prevPos = u_projViewPrev * Vertex;
#endif
}

#else

layout(set = 0, binding = 0) uniform sampler2D tex0;

layout(location = 0) out vec4 fragColor;
#ifdef ENABLE_GBUFFER
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outGBuffer2;
layout(location = 3) out vec4 outGBuffer3;
layout(location = 4) out vec4 outGBuffer4;
#endif


layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_tex0;
layout(location = 2) in float v_fog;

#ifdef ENABLE_GBUFFER
layout(location = 3) in vec4 v_gbuffer;
layout(location = 4) in vec4 v_currPos;
layout(location = 5) in vec4 v_prevPos;
layout(location = 6) in vec4 v_defaultColor;

vec2 directionToOctohedral(vec3 normal)
{
    vec2 p = normal.xy * (1.0f / dot(abs(normal), vec3(1.0f)));
    return normal.z > 0.0f ? p : (1.0f - abs(p.yx)) * (step(0.0f, p) * 2.0f - vec2(1.0f));
}

float computeCurvature(float depth)
{
    vec3 dx = dFdx(v_gbuffer.xyz);
    vec3 dy = dFdy(v_gbuffer.xyz);

    float x = dot(dx, dx);
    float y = dot(dy, dy);

    return pow(max(x, y), 0.5f);
}
#endif


layout(push_constant) uniform PushConsts 
{
	mat4 u_projView;
	mat4 u_projViewPrev;
	vec2 u_alphaRef;
	uint objectId;
	uint meshId;
	vec4 u_fogData;
	vec4 u_fogColor;
	mat4 u_world;
};


void DoAlphaTest(float a)
{
#ifndef NO_ALPHATEST
	if(a < u_alphaRef.x || a >= u_alphaRef.y)
		discard;
#endif
}

void main(void)
{
	vec4 texColor = texture(tex0, vec2(v_tex0.x, v_tex0.y));
	vec4 color = v_color*texColor;
	color.rgb = mix(u_fogColor.rgb, color.rgb, v_fog);
	DoAlphaTest(color.a);
	fragColor = color;
#ifdef ENABLE_GBUFFER
	vec2 a = (v_currPos.xy / v_currPos.w) * 0.5 + 0.5;
	vec2 b = (v_prevPos.xy / v_prevPos.w) * 0.5 + 0.5;

	float linearZ 	= gl_FragCoord.z /  gl_FragCoord.w;
    float curvature = computeCurvature(linearZ);
	//  objId , curvature, linearZ, specular 
	outNormal = vec4(directionToOctohedral(v_gbuffer.xyz), b-a);
	outGBuffer2 = vec4(meshId, curvature, linearZ, v_gbuffer.a);
	outGBuffer3 = vec4(v_defaultColor.rgb, objectId);
	outGBuffer4 = vec4(objectId);
#endif
}

#endif