
#ifdef __cplusplus
#pragma once
struct PushConstants {
    // Camera
    glm::mat4 invView;
    glm::mat4 invProj;

    int frame = 0;
    int sampleCount = 10;
    float bloomThreshold = 0.5f;
    float domeLightTheta = 0.0f;

    float domeLightPhi = 32.0f;

    // NEE & Infinite light
    int enableNEE = 1;
    int _dummy[2];

    glm::vec4 infiniteLightDirection = glm::vec4{glm::normalize(glm::vec3{-1.0, -1.0, 0.5}), 1.0};
    glm::vec4 infiniteLightIntensity = glm::vec4{1.0};
};
#else
layout(push_constant) uniform PushConstants {
    mat4 invView;
    mat4 invProj;

    int frame;
    int sampleCount;
    float bloomThreshold;
    float domeLightTheta;
    float domeLightPhi;

    // NEE & Infinite light
    int enableNEE;
    int _dummy[2];
    vec4 infiniteLightDirection;
    vec4 infiniteLightIntensity;
};
#endif

#ifndef __cplusplus

#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require

const float PI = 3.1415926535;

struct HitPayload {
    vec3 radiance;
    int depth;
    uint seed;
    // vec3 position;
    // vec3 normal;
    // vec3 emission;
    // vec3 brdf;
    // bool done;
};

struct Address {
    // uint64_t vertices;
    // uint64_t indices;
    uint64_t materials;
};

struct Vertex {
    vec3 pos;
    vec3 normal;
    vec2 texCoord;
};

struct Material {
    int baseColorTextureIndex;
    int metallicRoughnessTextureIndex;
    int normalTextureIndex;
    int occlusionTextureIndex;
    int emissiveTextureIndex;

    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    vec3 emissiveFactor;
};

// Image
layout(binding = 0, rgba32f) uniform image2D baseImage;
layout(binding = 1, rgba32f) uniform image2D bloomImage;
layout(binding = 2) uniform sampler2D domeLightTexture;
layout(binding = 3) uniform sampler2D lowDomeLightTexture;

// Accel
layout(binding = 10) uniform accelerationStructureEXT topLevelAS;

// Buffer
layout(binding = 20) buffer VertexBuffers {
    float vertices[];
}
vertexBuffers[];

layout(binding = 21) buffer IndexBuffers {
    uint indices[];
}
indexBuffers[];

layout(binding = 22) buffer TransformMatrixBuffer {
    mat4 transformMatrices[];
};

layout(binding = 23) buffer NormalMatrixBuffer {
    mat4 normalMatrices[];
};

layout(binding = 24) buffer AddressBuffer {
    Address addresses;
};

layout(binding = 25) buffer MaterialIndexBuffer {
    int materialIndices[];
};

// Buffer reference
layout(buffer_reference, scalar) buffer Materials {
    Material materials[];
};

#endif
