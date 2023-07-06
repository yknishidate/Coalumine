
const int SHAPE_CUBE = 0;
const int SHAPE_SPHERE = 1;

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
    int shape = SHAPE_CUBE;
    float stepEdge = 0.25f;
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
    float noiseFreq0;
    float noiseFreq1;

    float lightIntensity;
    int shape;
    float stepEdge;
    float _dummy2;

    vec4 absorption;
};
#endif
