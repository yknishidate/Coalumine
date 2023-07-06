
#ifdef __cplusplus
struct PushConstants {
    glm::mat4 invView;
    glm::mat4 invProj;
    float remapValue[4] = {0.0f, 1.0f, 0.0f, 1.0f};

    int frame = 0;
    int enableNoise = 1;
    float noiseFreq0 = 1.0f;
    float noiseFreq1 = 2.0f;

    float lightIntensity = 1.0f;
    float _dummy0;
    float _dummy1;
    float _dummy2;

    float absorption[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};
#else
layout(push_constant) uniform PushConstants {
    mat4 invView;
    mat4 invProj;
    float remapValue[4];

    int frame;
    int enableNoise;
    float noiseFreq0;
    float noiseFreq1;

    float lightIntensity;
    float _dummy0;
    float _dummy1;
    float _dummy2;

    vec4 absorption;
};
#endif
