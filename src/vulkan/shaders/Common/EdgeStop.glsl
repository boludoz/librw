//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#ifndef EDGE_STOPPING_GLSL
#define EDGE_STOPPING_GLSL

#define DEPTH_FACTOR 0.5


float normalEdgeStoppingWeight(vec3 centerNormal, vec3 sampleNormal, float power)
{
    return pow(clamp(dot(centerNormal, sampleNormal), 0.0f, 1.0f), power);
}


float depthEdgeStoppingWeight(float centerDepth, float sampleDepth, float phi)
{
   return exp(-abs(centerDepth - sampleDepth) / phi);
   //return abs(centerDepth - sampleDepth) / phi;
}


float lumaEdgeStoppingWeight(float centerLuma, float sampleLuma, float phi)
{
    return abs(centerLuma - sampleLuma) / phi;
}

float computeEdgeStoppingWeight(
                      float centerDepth,
                      float sampleDepth,
                      float phiZ,
                      vec3  centerNormal,
                      vec3  sampleNormal,
                      float phiNormal,
                      float centerLuma,
                      float sampleLuma,
                      float phiLuma)
{
    const float wZ      = depthEdgeStoppingWeight(centerDepth, sampleDepth, phiZ);
    const float wNormal = normalEdgeStoppingWeight(centerNormal, sampleNormal, phiNormal);
    const float wL      = lumaEdgeStoppingWeight(centerLuma, sampleLuma, phiLuma);
    const float w       = exp(0.0 - max(wL, 0.0) - max(wZ, 0.0)) * wNormal;
    return w;
}


float computeEdgeStoppingWeight(
                      vec3 centerWorldPos,
                      vec3 sampleWorldPos,
                      float phiZ,
                      vec3  centerNormal,
                      vec3  sampleNormal,
                      float phiNormal,
                      float centerLuma,
                      float sampleLuma,
                      float phiLuma)
{
    vec3 dir = normalize(sampleWorldPos -centerWorldPos);
    const float wZ      = abs(dot(centerNormal, dir)) / phiZ;
    //depthEdgeStoppingWeight(centerDepth, sampleDepth, phiZ);
    const float wNormal = normalEdgeStoppingWeight(centerNormal, sampleNormal, phiNormal);
    const float wL      = lumaEdgeStoppingWeight(centerLuma, sampleLuma, phiLuma);
    const float w       = exp(0.0 - max(wL, 0.0) - max(wZ, 0.0)) * wNormal;
    return w;
}


float computeEdgeStoppingWeight(float centerDepth,
                      float sampleDepth,
                      float phiZ, vec3 centerNormal,vec3  sampleNormal, float phiNormal)
{
    const float wZ      = depthEdgeStoppingWeight(centerDepth, sampleDepth, phiZ);
    const float wNormal = normalEdgeStoppingWeight(centerNormal, sampleNormal, phiNormal);
    const float w       = exp(- 1 - max(wZ, 0.0)) * wNormal;
    return  w;
}

#endif