
#ifdef __cplusplus
struct PushConstants {
    glm::mat4 invView;
    glm::mat4 invProj;
    float remapValue[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    int frame = 0;
    float noiseFreq0 = 1.0;
    float noiseFreq1 = 2.0;
};
#else
layout(push_constant) uniform PushConstants {
    mat4 invView;
    mat4 invProj;
    float remapValue[4];
    int frame;
    float noiseFreq0;
    float noiseFreq1;
};
#endif
