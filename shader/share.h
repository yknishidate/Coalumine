
const int SHAPE_CUBE = 0;
const int SHAPE_SPHERE = 1;

#ifdef __cplusplus
struct PushConstants {
    glm::mat4 invView;
    glm::mat4 invProj;
    float remapValue[4] = {0.0f, 1.0f, 0.0f, 1.0f};

    int frame = 0;
    int enableNoise = 1;
    int octave = 4;
    int shape = SHAPE_CUBE;

    float noiseFreq = 1.0f;
    float lightIntensity = 1.0f;
    float stepEdge = 0.1f;
    float _dummy2;

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
    float stepEdge;
    float _dummy2;

    vec4 absorption;
};
#endif
