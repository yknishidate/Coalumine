
#ifdef __cplusplus
struct PushConstants {
    glm::mat4 invView;
    glm::mat4 invProj;
    int frame = 0;
};
#else
layout(push_constant) uniform PushConstants {
    mat4 invView;
    mat4 invProj;
    int frame;
};
#endif
