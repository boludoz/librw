//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#ifndef BRDF_GLSL
#define BRDF_GLSL

#include "../Common/Common.glsl"
#include "../Common/Math.glsl"


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

vec3 fresnelSchlickRoughness(vec3 F0, float cosTheta, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(max(1.0 - cosTheta, 0.0), 5.0);
}


vec3 BRDF(in SurfaceMaterial p, in vec3 view, in vec3 halfV, in vec3 lightDir)
{
	float cosLi = max(0.0, dot(p.vertex.normal, lightDir));
    float cosLh = max(0.0, dot(p.vertex.normal, halfV));
    float NdotV = max(0.0, dot(p.vertex.normal, view));

    vec3 F  = fresnelSchlick(p.F0, max(dot(halfV, view), 0.0));
    float D = ndfGGX(cosLh,p.roughness);
    float G = gaSchlickGGX(cosLi,NdotV,p.roughness);

	vec3 kd = (1.0 - F) * (1.0 - p.diffuseTerm);
	vec3 diffuseBRDF = kd * p.diffuse.xyz / M_PI;
    vec3 specularBRDF = (F * D * G) / max(EPSILON, 4.0 * cosLi * NdotV);
    return diffuseBRDF + specularBRDF;
}

vec3 sampleCosineLobe(in vec3 n, in vec2 r)
{
    vec2 randSample = max(vec2(0.00001f), r);

    const float phi = 2.0f * M_PI * randSample.y;

    const float cosTheta = sqrt(randSample.x);
    const float sinTheta = sqrt(1 - randSample.x);

    vec3 t = vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

    return normalize( makeRotationMatrix(n) * t  );
}

float pdfCosineLobe(in float ndotl)
{
    return ndotl / M_PI;
}

vec3 sampleLambert(in vec3 n, in vec2 r)
{
    return sampleCosineLobe(n, r);
}

vec3 sampleGGX(in vec3 n, in float alpha, in vec2 Xi)
{
    float phi = 2.0 * M_PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (alpha * alpha - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    vec3 d;

    d.x = sinTheta * cos(phi);
    d.y = sinTheta * sin(phi);
    d.z = cosTheta;

    return normalize( makeRotationMatrix(n) * d  );
}

float pdfNDFGGX(in float alpha, in float ndoth, in float vdoth)
{
    return ndfGGX(ndoth, alpha) * ndoth / max(EPSILON, (4.0 * vdoth));
}

float pdfBRDF(in SurfaceMaterial p, in vec3 Wo, in vec3 Wh, in vec3 Wi)
{
    float NdotL = max(dot(p.vertex.normal, Wi), 0.0);
    float NdotV = max(dot(p.vertex.normal, Wo), 0.0);
    float NdotH = max(dot(p.vertex.normal, Wh), 0.0);
    float VdotH = max(dot(Wi, Wh), 0.0);

    float pd = pdfCosineLobe(NdotL);
    float ps = pdfNDFGGX(p.roughness, NdotH, VdotH);

    return mix(pd, ps, 0.5);
}


vec3 sampleBRDF(in SurfaceMaterial p, in vec3 Wo, in Random rng, out vec3 Wi, out float pdf)
{
    float alpha = p.roughness * p.roughness;

    vec3 Wh;

    vec3 randValue = nextVec3(rng);

    bool isSpecular = false;

    if (randValue.x < 0.5)
    {
        Wh = sampleGGX(p.vertex.normal, alpha, randValue.yz);
        Wi = reflect(-Wo, Wh);
        
        float NdotL = max(dot(p.vertex.normal, Wi), 0.0);
        float NdotV = max(dot(p.vertex.normal, Wo), 0.0);

        if (NdotL > 0.0f && NdotV > 0.0f)
            isSpecular = true;
    }
    
    if (!isSpecular)
    {
        Wi = sampleLambert(p.vertex.normal, randValue.yz);
        Wh = normalize(Wo + Wi);
    }

    pdf = pdfBRDF(p, Wo, Wh, Wi);

    return BRDF(p, Wo, Wh, Wi);
}
#endif