#version 450

#define MAX_LIGHTS 32

#define PI 3.1415926535897932384626433832795
#define M_PI PI

struct Object
{
	mat4 u_world;
	vec4 u_matColor;
	vec4 u_surfProps;	// amb, spec, diff, extra	
	vec4 u_ambLight;
	ivec4 u_offsets;
	vec4 u_lightParams[MAX_LIGHTS];	// type, radius, minusCosAngle, hardSpot
	vec4 u_lightPosition[MAX_LIGHTS];
	vec4 u_lightDirection[MAX_LIGHTS];
	vec4 u_lightColor[MAX_LIGHTS];
};

#define u_fogStart (u_fogData.x)
#define u_fogEnd (u_fogData.y)
#define u_fogRange (u_fogData.z)
#define u_fogDisable (u_fogData.w)

#define surfAmbient (objects[objectId].u_surfProps.x)
#define surfSpecular (objects[objectId].u_surfProps.y)
#define surfDiffuse (objects[objectId].u_surfProps.z)
