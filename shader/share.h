
#ifdef __cplusplus
struct PushConstants {
    // 128
    glm::mat4 invView;
    glm::mat4 invProj;

    // 32
    float absorptionIntensity = 0.0f;
    float emissionIntensity = 1.0f;
    int enableToneMapping = 0;
    int enableGammaCorrection = 0;

    // 32
    int frame = 0;
    int enableNoise = 1;
    int enableBloom = 1;
    int enableFlowNoise = 1;

    // 32
    float bloomIntensity = 10.0f;
    float bloomThreshold = 0.4f;
    float lightIntensity = 1.0f;
    float flowSpeed = 0.025f;

    // 32
    float absorption[4] = {0.9f, 0.9f, 0.9f, 0.9f};

    float volumeSize[4];
    int blurSize = 32;
    float scatterIntensity = 1.0;
};
#else
layout(push_constant) uniform PushConstants {
    mat4 invView;
    mat4 invProj;

    float absorptionIntensity;
    float emissionIntensity;
    int enableToneMapping;
    int enableGammaCorrection;

    int frame;
    int enableNoise;
    int enableBloom;
    int enableFlowNoise;

    float bloomIntensity;
    float bloomThreshold;
    float lightIntensity;
    float flowSpeed;

    vec4 absorption;
    vec4 volumeSize;
    int blurSize;
    float scatterIntensity;
};
#endif
