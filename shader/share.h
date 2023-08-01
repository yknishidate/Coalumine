
#ifdef __cplusplus
#pragma once
struct PushConstants {
    // Camera
    glm::mat4 invView;
    glm::mat4 invProj;

    int frame = 0;
    int sampleCount = 10;
    float bloomThreshold = 0.5f;
    float domeLightTheta = 0.0f;

    float domeLightPhi = 300.0f;

    // NEE & Infinite light
    int enableNEE = 1;
    int _dummy[2];

    glm::vec4 infiniteLightDirection = glm::vec4{glm::normalize(glm::vec3{-1.0, -1.0, 0.5}), 1.0};
    glm::vec4 infiniteLightIntensity = glm::vec4{1.0};
};
#else
layout(push_constant) uniform PushConstants {
    mat4 invView;
    mat4 invProj;

    int frame;
    int sampleCount;
    float bloomThreshold;
    float domeLightTheta;
    float domeLightPhi;

    // NEE & Infinite light
    int enableNEE;
    int _dummy[2];
    vec4 infiniteLightDirection;
    vec4 infiniteLightIntensity;
};
#endif

#ifndef __cplusplus
const float PI = 3.1415926535;

struct HitPayload {
    vec3 radiance;
    int depth;
    uint seed;
    // vec3 position;
    // vec3 normal;
    // vec3 emission;
    // vec3 brdf;
    // bool done;
};
#endif
