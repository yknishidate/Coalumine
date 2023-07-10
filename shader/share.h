
const int SHAPE_CUBE = 0;
const int SHAPE_SPHERE = 1;

#ifdef __cplusplus
struct PushConstants {
    // 128
    glm::mat4 invView;
    glm::mat4 invProj;

    // 32
    float remapValue[4] = {0.0f, 1.0f, 0.0f, 1.0f};

    // 32
    int frame = 0;
    int enableNoise = 1;
    int octave = 4;
    int shape = SHAPE_CUBE;

    // 32
    float noiseFreq = 1.0f;
    float lightIntensity = 1.0f;
    float gain = 0.5f;
    float _dummy2;

    // 32
    float absorption[4] = {0.9f, 0.9f, 0.9f, 0.9f};
};
#else
layout(push_constant) uniform PushConstants {
    mat4 invView;
    mat4 invProj;
    float remapValue[4];

    int frame;
    int enableNoise;
    int octave;
    int shape;

    float noiseFreq;
    float lightIntensity;
    float gain;
    float _dummy2;

    vec4 absorption;
};
#endif
