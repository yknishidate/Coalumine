#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require

#include "./share.h"
#include "./random.glsl"


struct Address
{
    //uint64_t vertices;
    //uint64_t indices;
    uint64_t materials;
};

struct Vertex
{
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

layout(binding = 8) uniform accelerationStructureEXT topLevelAS;

layout(binding = 16) buffer VertexBuffers{float vertices[];} vertexBuffers[];
layout(binding = 17) buffer IndexBuffers{uint indices[];} indexBuffers[];
layout(binding = 19) buffer TransformMatrixBuffer{mat4 transformMatrices[];};
layout(binding = 20) buffer NormalMatrixBuffer{mat3 normalMatrices[];};
//layout(binding = 21) buffer MaterialBuffer{float materials[];};
//layout(binding = 22) buffer MaterialIndexBuffer{int materialIndices[];};

layout(buffer_reference, scalar) buffer Materials { Material materials[]; };
layout(binding = 23) buffer AddressBuffer { Address addresses; };

layout(location = 0) rayPayloadInEXT HitPayload payload;

hitAttributeEXT vec3 attribs;

Vertex unpackVertex(uint meshIndex,  uint vertexIndex)
{
    uint stride = 8;
    uint offset = vertexIndex * stride;
    Vertex v;
    v.pos = vec3(
        vertexBuffers[meshIndex].vertices[offset +  0], 
        vertexBuffers[meshIndex].vertices[offset +  1], 
        vertexBuffers[meshIndex].vertices[offset + 2]);
    v.normal = vec3(
        vertexBuffers[meshIndex].vertices[offset +  3], 
        vertexBuffers[meshIndex].vertices[offset +  4], 
        vertexBuffers[meshIndex].vertices[offset + 5]);
    v.texCoord = vec2(
        vertexBuffers[meshIndex].vertices[offset +  6], 
        vertexBuffers[meshIndex].vertices[offset +  7]);
    return v;
}

// Global space
vec3 sampleHemisphereUniform(in vec3 normal, inout uint seed) {
    float u = rand(seed);
    float v = rand(seed);

    float r = sqrt(1.0 - u * u);
    float phi = 2.0 * PI * v;
    
    vec3 localDir;
    localDir.x = cos(phi) * r;
    localDir.y = sin(phi) * r;
    localDir.z = u;

    vec3 up = abs(normal.z) < 0.999 ? vec3(0,0,1) : vec3(1,0,0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);

    vec3 sampledDir = localDir.x * tangent + localDir.y * bitangent + localDir.z * normal;
    return normalize(sampledDir);
}

// Tangent space (Z-up)
vec3 sampleHemisphereUniformLocal(inout uint seed) {
    float u = rand(seed);
    float v = rand(seed);

    float r = sqrt(1.0 - u * u);
    float phi = 2.0 * PI * v;
    
    vec3 localDir;
    localDir.x = cos(phi) * r;
    localDir.y = sin(phi) * r;
    localDir.z = u;

    return localDir;
}

vec3 localToGlobal(in vec3 localDir, in vec3 normal) {
    vec3 up = abs(normal.z) < 0.999 ? vec3(0,0,1) : vec3(1,0,0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);

    vec3 sampledDir = localDir.x * tangent + localDir.y * bitangent + localDir.z * normal;
    return normalize(sampledDir);
}

vec3 globalToLocal(in vec3 globalDir, in vec3 normal) {
    vec3 up = abs(normal.z) < 0.999 ? vec3(0,0,1) : vec3(1,0,0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);

    vec3 localDir;
    localDir.x = dot(globalDir, tangent);
    localDir.y = dot(globalDir, bitangent);
    localDir.z = dot(globalDir, normal);

    return localDir;
}

// Global space
vec3 sampleHemisphereCosine(in vec3 normal, inout uint seed) {
    float u = rand(seed);  // Get a random number between 0 and 1
    float v = rand(seed);

    float phi = 2.0 * PI * u;  // Azimuthal angle
    float cosTheta = sqrt(1.0 - v);  // Polar angle (use sqrt to distribute points evenly)
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    vec3 direction;
    direction.x = cos(phi) * sinTheta;
    direction.y = sin(phi) * sinTheta;
    direction.z = cosTheta;

    // Create an orthonormal basis with 'normal' as one of the vectors
    vec3 up = abs(normal.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);

    // Convert direction from local coordinates to world coordinates
    vec3 sampledDirection = tangent * direction.x + bitangent * direction.y + normal * direction.z;
    return normalize(sampledDirection);
}

void traceRay(vec3 origin, vec3 direction)
{
    traceRayEXT(
        topLevelAS,
        gl_RayFlagsOpaqueEXT,
        0xff, // cullMask
        0,    // sbtRecordOffset
        0,    // sbtRecordStride
        0,    // missIndex
        origin,
        gl_RayTminEXT,
        direction,
        gl_RayTmaxEXT,
        0     // payloadLocation
    );
}

float cosTheta(vec3 w) {
    return w.z;
}

float cos2Theta(vec3 w) {
    return w.z * w.z;
}

float absCosTheta(vec3 w) {
    return abs(w.z);
}

float sin2Theta(vec3 w) {
    return max(0.0, 1.0 - cos2Theta(w));
}

float sinTheta(vec3 w) {
    return sqrt(sin2Theta(w));
}

float tanTheta(vec3 w) {
    return sinTheta(w) / cosTheta(w);
}

float tan2Theta(vec3 w) {
    return sin2Theta(w) / cos2Theta(w);
}

float ggxDistribution(float NdotH, float roughness) {
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float d = (NdotH * alpha2 - NdotH) * NdotH + 1.0;
    return alpha2 / (PI * d * d);
}

float ggxGeometry(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}

vec3 fresnelSchlick(float VdotH, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
}

vec3 sampleGGX(float roughness, uint seed) {
    float u = rand(seed);
    float v = rand(seed);
    float alpha = roughness * roughness;
    float theta = atan(alpha * sqrt(v) / sqrt(max(1.0 - v, 0.0)));
    float phi = 2.0f * PI * u;
    return vec3(cos(phi) * sin(theta), cos(theta), sin(phi) * sin(theta));
}

void main()
{
    uint meshIndex = gl_InstanceID;
    Vertex v0 = unpackVertex(meshIndex, indexBuffers[meshIndex].indices[3 * gl_PrimitiveID + 0]);
    Vertex v1 = unpackVertex(meshIndex, indexBuffers[meshIndex].indices[3 * gl_PrimitiveID + 1]);
    Vertex v2 = unpackVertex(meshIndex, indexBuffers[meshIndex].indices[3 * gl_PrimitiveID + 2]);
    
    const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
    vec3 pos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    vec3 normal = normalize(v0.normal * barycentricCoords.x + v1.normal * barycentricCoords.y + v2.normal * barycentricCoords.z);
    vec2 texCoord = v0.texCoord * barycentricCoords.x + v1.texCoord * barycentricCoords.y + v2.texCoord * barycentricCoords.z;
    
    // Get material
    Materials _materials = Materials(addresses.materials);
    Material material = _materials.materials[meshIndex];
    vec3 baseColor = material.baseColorFactor.rgb;
    float metallic = material.metallicFactor;
    float roughness = material.roughnessFactor;

    payload.depth += 1;
    if(payload.depth >= 8){
        return;
    }

    // Importance sampling
    vec3 origin = pos;

    if(metallic > 0.0){
        vec3 direction = sampleHemisphereUniform(normal, payload.seed);

        traceRay(origin, direction);

        vec3 V = -gl_WorldRayDirectionEXT;
        vec3 L = direction;
        vec3 H = normalize(L + V);
        float NdotL = max(dot(normal, L), 0.0);
        float NdotV = max(dot(normal, V), 0.0);
        float NdotH = max(dot(normal, H), 0.0);
        float VdotH = max(dot(V, H), 0.0);

        // Compute the GGX BRDF
        const vec3 dielectricF0 = vec3(0.04);
        vec3 F0 = mix(dielectricF0, baseColor, metallic);
        vec3 F = fresnelSchlick(VdotH, F0);
        float D = ggxDistribution(NdotH, roughness);
        float G = ggxGeometry(NdotV, NdotL, roughness);

        vec3 numerator = D * G * F;
        float denominator = 4 * max(NdotL, 0.0) * max(NdotV, 0.0) + 0.001; // prevent division by zero
        vec3 specular = numerator / denominator;

        float pdf = 1.0 / (2.0 * PI);

        vec3 radiance = specular * payload.radiance * NdotL / pdf;
        payload.radiance = radiance;
    }else{
        // Diffuse IS
        vec3 direction = sampleHemisphereCosine(normal, payload.seed);
        traceRay(origin, direction);
        
        // Radiance (with Diffuse Importance sampling)
        // Lo = brdf * Li * cos(theta) / pdf
        //    = (color / PI) * Li * cos(theta) / (cos(theta) / PI)
        //    = color * Li
        payload.radiance = baseColor * payload.radiance;
    }
}
