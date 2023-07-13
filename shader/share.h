
#ifdef __cplusplus
struct PushConstants {
    // 128
    glm::mat4 invView;
    glm::mat4 invProj;

    // 32
    float remapValue[4] = {0.0f, 1.0f, 0.0f, 1.5f};

    // 32
    int frame = 0;
    int enableNoise = 1;
    int enableBloom = 1;
    int enableFlowNoise = 1;

    // 32
    float bloomIntensity = 10.0f;
    float bloomThreshold = 0.4f;
    float lightIntensity = 1.0f;
    float flowSpeed = 0.05f;

    // 32
    float absorption[4] = {0.9f, 0.9f, 0.9f, 0.9f};

    float volumeSize[4];
    int blurSize = 32;
};
#else
layout(push_constant) uniform PushConstants {
    mat4 invView;
    mat4 invProj;
    float remapValue[4];

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
};
#endif
