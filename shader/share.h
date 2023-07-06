
#ifdef __cplusplus
struct PushConstants {
    int frame = 0;
};
#else
layout(push_constant) uniform PushConstants {
    int frame;
};
#endif
