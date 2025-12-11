#version 450

#ifdef VERTEX_SHADER

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec4 in_color;
layout(location = 3) in vec2 in_tex0;

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

#define u_fogStart (u_fogData.x)
#define u_fogEnd (u_fogData.y)
#define u_fogRange (u_fogData.z)
#define u_fogDisable (u_fogData.w)

float DoFog(float w)
{
	return clamp((w - u_fogEnd)*u_fogRange, u_fogDisable, 1.0);
}

void main(void)
{
	vec4 Vertex = u_world * vec4(in_pos, 1.0);
	vec4 currPos = u_projView * Vertex;
	gl_Position = currPos;
	v_color = in_color;
	v_tex0 = in_tex0;
	v_fog = DoFog(gl_Position.w);

#ifdef ENABLE_GBUFFER
	v_defaultColor = in_color;
	v_gbuffer = vec4(Normal, surfSpecular);
	v_currPos = currPos;
	v_prevPos = u_projViewPrev * Vertex;
#endif
}

#else

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

layout(set = 0, binding = 0) uniform sampler2D tex0;

layout(location = 0) out vec4 fragColor;

#ifdef ENABLE_GBUFFER
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outGBuffer2;
layout(location = 3) out vec4 outGBuffer3;
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
	vec4 color = v_color * texColor;
	color.rgb = mix(u_fogColor.rgb, color.rgb, v_fog);
	DoAlphaTest(color.a);
	fragColor = color;
#ifdef ENABLE_GBUFFER
	//vec2 a = (v_currPos.xy / v_currPos.w) * 0.5 + 0.5;
	//vec2 b = (v_prevPos.xy / v_prevPos.w) * 0.5 + 0.5;

	//float linearZ 	= gl_FragCoord.z /  gl_FragCoord.w;
    //float curvature = computeCurvature(linearZ);
	//  objId , curvature, linearZ, specular 
	//outNormal = vec4(directionToOctohedral(v_gbuffer.xyz), b - a);
	//outGBuffer2 = vec4(meshId, curvature, linearZ, v_gbuffer.a);
	//outGBuffer3 = vec4(v_defaultColor.rgb * texColor.rgb, 1);
#endif
}

#endif