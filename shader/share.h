
// ------------------------------
// Macro
// ------------------------------

// For C++
#ifdef __cplusplus
#pragma once

#include <glm/glm.hpp>

#define USING_GLM           \
    using vec3 = glm::vec3; \
    using vec4 = glm::vec4; \
    using mat3 = glm::mat3; \
    using mat4 = glm::mat4;

#define FIELD(type, name, default_value) type name = default_value

// For GLSL
#else

// To use uint64_t, this needs to be written here
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require

#define USING_GLM /* nothing */

#define FIELD(type, name, default_value) type name

#endif

#define TEXTURE_TYPE_OFFSET 1024

// ------------------------------
// Struct
// ------------------------------

struct PushConstants {
    USING_GLM

    FIELD(mat4, invView, mat4(1.0f));
    FIELD(mat4, invProj, mat4(1.0f));

    FIELD(int, frame, 0);
    FIELD(int, sampleCount, 10);
    FIELD(float, bloomThreshold, 0.5f);
    FIELD(float, envLightIntensity, 1.0f);

    FIELD(float, envLightPhi, 0.0f);
    FIELD(int, enableNEE, 1);
    FIELD(int, enableAccum, 1);
    FIELD(int, useEnvLightTexture, 0);

    FIELD(vec4, infiniteLightColor, vec4(0.0f, 0.0f, 0.0f, 0.0f));
    FIELD(vec3, infiniteLightDirection, vec3(0.0f, 1.0f, 0.0f));
    FIELD(float, infiniteLightIntensity, 0.0f);

    FIELD(vec4, envLightColor, vec4(0.0f, 0.0f, 0.0f, 0.0f));

    FIELD(int, enableAdaptiveSampling, 0);
};

struct Material {
    USING_GLM

    // テクスチャインデックスが1024以上なら3Dテクスチャとみなす
    // 1024は TEXTURE_TYPE_OFFSET として定義されている
    // i: [0000~1023] => 2D[i]
    // i: [1024~2047] => 3D[i]
    FIELD(int, baseColorTextureIndex, -1);
    FIELD(int, metallicRoughnessTextureIndex, -1);
    FIELD(int, normalTextureIndex, -1);
    FIELD(int, occlusionTextureIndex, -1);

    FIELD(int, emissiveTextureIndex, -1);
    FIELD(float, metallicFactor, 0.0f);
    FIELD(float, roughnessFactor, 1.0f);
    FIELD(float, ior, 1.51f);

    FIELD(vec4, baseColorFactor, vec4(1.0f));
    FIELD(vec3, emissiveFactor, vec4(0.0f));
    FIELD(float, dispersion, 0.0f);
};

struct NodeData {
    USING_GLM

    FIELD(mat4, normalMatrix, mat4(1.0f));
    FIELD(uint64_t, vertexBufferAddress, 0);
    FIELD(uint64_t, indexBufferAddress, 0);
    FIELD(int, materialIndex, 0);
    FIELD(int, _dummy0, 0);
    FIELD(int, _dummy1, 0);
    FIELD(int, _dummy2, 0);
    FIELD(vec3, meshAabbMin, vec3(0.0f));
    FIELD(int, _dummy3, 0);
    FIELD(vec3, meshAabbMax, vec3(0.0f));
    FIELD(int, _dummy4, 0);
};

#ifndef __cplusplus

// TODO: move to other file

const float PI = 3.1415926535;

struct HitPayload {
    vec3 radiance;
    int depth;
    uint seed;
    int component;  // selected RGB (-1 means unselected)
    float t;
    // vec3 position;
    // vec3 normal;
    // vec3 emission;
    // vec3 brdf;
    // bool done;
};

struct Vertex {
    vec3 pos;
    vec3 normal;
    vec2 texCoord;
};

layout(push_constant) uniform PushConstantsBuffer {
    PushConstants pc;
};

// Image
layout(binding = 0, rgba32f) uniform image2D baseImage;
layout(binding = 1, rgba32f) uniform image2D bloomImage;
layout(binding = 2) uniform sampler2D envLightTexture;
layout(binding = 3) uniform sampler2D textures2d[];
layout(binding = 4) uniform sampler3D textures3d[];

// Accel
layout(binding = 10) uniform accelerationStructureEXT topLevelAS;

// Buffer
layout(binding = 20) buffer NodeDataBuffer {
    NodeData nodeData[];
};

layout(binding = 21) buffer MaterialBuffer {
    Material materials[];
};

// Buffer reference
layout(buffer_reference, scalar) buffer VertexBuffer {
    Vertex vertices[];
};

layout(buffer_reference, scalar) buffer IndexBuffer {
    uvec3 indices[];
};

#endif
