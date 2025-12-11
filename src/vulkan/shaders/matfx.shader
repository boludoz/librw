
#ifdef VERTEX_SHADER

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec4 in_color;
layout(location = 3) in vec2 in_tex0;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_tex0;
layout(location = 2) out vec2 v_tex1;
layout(location = 3) out float v_fog;

#ifdef ENABLE_GBUFFER

layout(location = 4) out vec4 v_gbuffer;
layout(location = 5) out vec4 v_currPos;
layout(location = 6) out vec4 v_prevPos;
layout(location = 7) out vec3 v_fragPos;


vec2 directionToOctohedral(vec3 normal)
{
    vec2 p = normal.xy * (1.0f / dot(abs(normal), vec3(1.0f)));
    return normal.z > 0.0f ? p : (1.0f - abs(p.yx)) * (step(0.0f, p) * 2.0f - vec2(1.0f));
}

#endif

//128 bytes
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

layout(set = 3, binding = 0, std140) uniform MatfxUniform
{
	vec4 u_fxparams;
	mat4 u_texMatrix;
	vec4 u_colorClamp;
	vec4 u_envColor;
};


float DoFog(float w)
{
	return clamp((w - u_fogEnd)*u_fogRange, u_fogDisable, 1.0);
}

void main(void)
{
	//mat4 u_world = objects[objectId].u_world;
	vec4 Vertex = u_world * vec4(in_pos, 1.0);
	v_fragPos = Vertex.xyz;
	vec4 currPos = u_projView * Vertex;
	gl_Position = currPos;
	vec3 Normal = mat3(u_world) * in_normal;

	v_tex0 = in_tex0;
	v_tex1 = (u_texMatrix * vec4(Normal, 1.0)).xy;
	v_color = in_color;
	v_color = clamp(v_color, 0.0, 1.0);
	v_fog = DoFog(gl_Position.w);
	
#ifdef ENABLE_GBUFFER
	v_gbuffer = vec4(Normal, surfSpecular);
	v_currPos = currPos;
	v_prevPos = u_projViewPrev * Vertex;
#endif
}

#else

layout(set = 0, binding = 0) uniform sampler2D tex0;

layout(set = 1, binding = 0, std140) readonly buffer ObjectBuffer
{
	Object objects[];
};

layout(set = 1, binding = 1, std140) uniform CameraUniform
{
	vec4 cameraPos;
};

layout(set = 2, binding = 0) uniform sampler2D tex1;

layout(set = 3, binding = 0, std140) uniform MatfxUniform
{
	vec4 u_fxparams;
	mat4 u_texMatrix;
	vec4 u_colorClamp;
	vec4 u_envColor;
};

#define shininess (u_fxparams.x)
#define disableFBA (u_fxparams.y)

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_tex0;
layout(location = 2) in vec2 v_tex1;
layout(location = 3) in float v_fog;

#ifdef ENABLE_GBUFFER
layout(location = 4) in vec4 v_gbuffer;
layout(location = 5) in vec4 v_currPos;
layout(location = 6) in vec4 v_prevPos;
layout(location = 7) in vec3 v_fragPos;

#endif

layout(location = 0) out vec4 fragColor;

#ifdef ENABLE_GBUFFER
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outGBuffer2;
layout(location = 3) out vec4 outGBuffer3;
layout(location = 4) out vec4 outGBuffer4;

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


const float EPSILON = 0.00001;
// GGX/Towbridge-Reitz normal distribution function.
// Uses Disney's reparametrization of alpha = roughness^2
float ndfGGX(float cosLh, float roughness)
{
	float alpha = roughness * roughness;
	float alphaSq = alpha * alpha;
	
	float denom = (cosLh * cosLh) * (alphaSq - 1.0) + 1.0;
	return alphaSq / (M_PI * denom * denom);
}

// Single term for separable Schlick-GGX below.
float gaSchlickG1(float cosTheta, float k)
{
	return cosTheta / (cosTheta * (1.0 - k) + k);
}

// Schlick-GGX approximation of geometric attenuation function using Smith's method.
float gaSchlickGGX(float cosLi, float NdotV, float roughness)
{
	float r = roughness + 1.0;
	float k = (r * r) / 8.0; // Epic suggests using this roughness remapping for analytic lights.
	return gaSchlickG1(cosLi, k) * gaSchlickG1(NdotV, k);
}

// Shlick's approximation of the Fresnel factor.
vec3 fresnelSchlick(vec3 F0, float cosTheta)
{
  	return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 BRDF(in vec3 diffuse, in vec3 normal, in float roughness, in vec3 view, in vec3 halfV, in vec3 lightDir, in vec3 F0, float metallic)
{
	float cosLi = max(0.0, dot(normal, lightDir));
    float cosLh = max(0.0, dot(normal, halfV));
    float NdotV = max(0.0, dot(normal, view));

    vec3 F  = fresnelSchlick(F0, max(dot(halfV, view), 0.0));
    float D = ndfGGX(cosLh, roughness);
    float G = gaSchlickGGX(cosLi, NdotV, roughness);

	vec3 kd = (1.0 - F) * (1.0 - metallic);
	
	vec3 diffuseBRDF = kd * diffuse.xyz / M_PI;
    vec3 specularBRDF = (F * D * G) / max(EPSILON, 4.0 * cosLi * NdotV);
    return diffuseBRDF + specularBRDF;
}

vec3 DoDynamicLight(vec3 V, vec3 N, vec3 diffuse, float roughness, vec3 F0, float metallic)
{
	vec3 ret = vec3(0.0, 0.0, 0.0);
	for(int i = 0; i < MAX_LIGHTS; i++){
		if(objects[objectId].u_lightParams[i].x == 0.0)
			break;
		vec3 color = vec3(0.0, 0.0, 0.0);
		vec3 dir = vec3(0);
		float atten = 1;
#ifdef DIRECTIONALS
		if(objects[objectId].u_lightParams[i].x == 1.0){
			// direct
			dir = -objects[objectId].u_lightDirection[i].xyz;
			float l = max(0.0, dot(N, dir));
			color = l * objects[objectId].u_lightColor[i].rgb;
		}else
#endif
#ifdef POINTLIGHTS
		if(objects[objectId].u_lightParams[i].x == 2.0){
			// point
			dir = objects[objectId].u_lightPosition[i].xyz - V;
			float dist = length(dir);
			atten = max(0.0, (1.0 - dist/objects[objectId].u_lightParams[i].y));
			float l = max(0.0, dot(N, normalize(dir)));
			color = l * objects[objectId].u_lightColor[i].rgb*atten;
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
			color = l * objects[objectId].u_lightColor[i].rgb*atten;
		}else
#endif
			;
		dir = normalize(dir);
		vec3 view = normalize(cameraPos.xyz - V);
		vec3 halfV = normalize(dir + view);
        vec3 brdf = BRDF(diffuse, N, roughness, view, halfV, dir, F0, metallic);
        float cosTheta = clamp(dot(N, dir), 0.0, 1.0);
        ret += brdf * cosTheta * color * pow(2, 1.4);
	}
	return ret;
}


void main(void)
{
	vec4 texColor= texture(tex0, vec2(v_tex0.x, 1.0-v_tex0.y));
	vec4 defaultColor = objects[objectId].u_matColor * texColor;
	vec4 lightColor = defaultColor;
	lightColor.a = v_color.a;

	vec3 F0 = mix(vec3(0.03), lightColor.xyz, surfDiffuse);
	lightColor.rgb = DoDynamicLight(v_fragPos, v_gbuffer.xyz, lightColor.xyz, 1 - surfSpecular, F0, surfDiffuse);

	vec4 v_envColor = max(lightColor, u_colorClamp) * u_envColor;

	vec4 pass1 = lightColor;
	vec4 pass2 = v_envColor * shininess * texture(tex1, vec2(v_tex1.x, 1.0-v_tex1.y));

	pass1.rgb = mix(u_fogColor.rgb, pass1.rgb, v_fog);
	pass2.rgb = mix(vec3(0.0, 0.0, 0.0), pass2.rgb, v_fog);

	float fba = max(pass1.a, disableFBA);
	vec4 color;
	color.rgb = pass1.rgb * pass1.a + pass2.rgb * fba;
	color.a = pass1.a;

	DoAlphaTest(color.a);

	fragColor = color;

#ifdef ENABLE_GBUFFER
	vec2 a = (v_currPos.xy / v_currPos.w) * 0.5 + 0.5;
	vec2 b = (v_prevPos.xy / v_prevPos.w) * 0.5 + 0.5;

	float linearZ 	= gl_FragCoord.z /  gl_FragCoord.w;
    float curvature = computeCurvature(linearZ);
	//  objId , curvature, linearZ, specular 
	outNormal   = vec4(directionToOctohedral(v_gbuffer.xyz), b-a);
	outGBuffer2 = vec4(meshId, curvature, linearZ, v_gbuffer.a);
	outGBuffer3 = vec4(defaultColor.rgb, color.a);
	outGBuffer4 = vec4(objectId);
#endif
}

#endif