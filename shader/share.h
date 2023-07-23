#pragma once

#ifdef __cplusplus
struct PushConstants {
    // Camera
    glm::mat4 invView;
    glm::mat4 invProj;

    int frame = 0;
    float bloomThreshold = 0.5;
};
#else
layout(push_constant) uniform PushConstants {
    mat4 invView;
    mat4 invProj;

    int frame;
    float bloomThreshold;
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
