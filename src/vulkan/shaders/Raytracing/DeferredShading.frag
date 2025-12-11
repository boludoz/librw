#version 450
#extension GL_GOOGLE_include_directive : require

#include "../Common/Math.glsl"
#include "../Common/Common.glsl"
#include "../PathTrace/BRDF.glsl"

layout(set = 0, binding = 0) uniform sampler2D uColor;
layout(set = 0, binding = 1) uniform sampler2D uNormal;
layout(set = 0, binding = 2) uniform sampler2D uPBR;
layout(set = 0, binding = 3) uniform sampler2D uObjID;
layout(set = 0, binding = 4) uniform sampler2D uDepth;
layout(set = 0, binding = 5) uniform sampler2D uReflection;
layout(set = 0, binding = 6) uniform sampler2D uShadow;
layout(set = 0, binding = 7) uniform sampler2D uIndirect;

layout(set = 0, binding = 8, std140) readonly buffer ObjectBuffer
{
	Object objects[];
};

layout(set = 0, binding = 9) uniform CameraUniform
{
	vec4 cameraPos;
};

layout (location = 0) in vec2 inUV;
layout (location = 0) out vec4 fragColor;

layout(push_constant) uniform PushConsts 
{
	mat4 viewProjInv;
	int type;
	float intensity;
};

#define surfDiffuse (objects[objectId].u_surfProps.z)

#define Color 0
#define PathTrace 1
#define Reflection 2
#define BaseColor 3
#define Visibility 4
		

void main()
{
	vec2 texCoord = vec2(inUV.x, 1 - inUV.y);

	vec4 outColor = texture(uColor, texCoord);
	float depth = texture(uDepth, texCoord).r;
	float specular = texture(uPBR, texCoord).a;
	float meshId = texture(uPBR, texCoord).r;
	vec3 P = worldPositionFromDepth(texCoord, depth, viewProjInv);
	vec3 N = octohedralToDirection(texture(uNormal, texCoord).xy);

	if(type == 0)
	{
		float visibility = meshId > 0 && depth != 1.0 ? texture(uShadow,texCoord).r : 1;

		/*int objectId = int(texture(uObjID, texCoord).a);
		SurfaceMaterial surface;
		surface.vertex.position = P;
		surface.vertex.normal = N;
		surface.objectId = objectId;
		surface.diffuse = texture(uColorSampler,texCoord);
		surface.roughness = 1 - specular;
		surface.diffuseTerm = surfDiffuse;
		surface.F0 = mix(vec3(0.03), surface.diffuse.xyz, surface.diffuseTerm);*/

		outColor.rgb = outColor.rgb * visibility + texture(uIndirect, texCoord).rgb + texture(uReflection, texCoord).rgb  ;
	}

	if(type == 4)
	{
		outColor.rgb = vec3( texture(uShadow,texCoord).r );
	}
	fragColor = outColor;
}