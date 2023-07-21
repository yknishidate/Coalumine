
#ifdef __cplusplus
struct PushConstants {
    // Camera
    glm::mat4 invView;
    glm::mat4 invProj;

    int frame = 0;
    int enableToneMapping;
    int enableGammaCorrection;

    // Bloom
    int enableBloom = 0;
    int blurSize = 16;
    float bloomIntensity = 1.0f;
    float bloomThreshold = 0.5f;
};
#else
layout(push_constant) uniform PushConstants {
    mat4 invView;
    mat4 invProj;

    int frame;
    int enableToneMapping;
    int enableGammaCorrection;

    int enableBloom;
    int blurSize;
    float bloomIntensity;
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
