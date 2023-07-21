#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#include "./share.h"
#include "./random.glsl"

layout(binding = 8) uniform accelerationStructureEXT topLevelAS;

layout(binding = 16) buffer VertexBuffers{float vertices[];} vertexBuffers[];
layout(binding = 17) buffer IndexBuffers{uint indices[];} indexBuffers[];

layout(location = 0) rayPayloadInEXT HitPayload payload;

hitAttributeEXT vec3 attribs;

struct Vertex
{
    vec3 pos;
    vec3 normal;
    vec2 texCoord;
};

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

vec3 sampleHemisphereUniform(in vec3 normal, inout uint seed) {
    float u = rand(seed);
    float v = rand(seed);

    float r = sqrt(1.0 - u * u);
    float phi = 2.0 * 3.14159265 * v;
    
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

void main()
{
    uint meshIndex = gl_InstanceID;
    Vertex v0 = unpackVertex(meshIndex, indexBuffers[meshIndex].indices[3 * gl_PrimitiveID + 0]);
    Vertex v1 = unpackVertex(meshIndex, indexBuffers[meshIndex].indices[3 * gl_PrimitiveID + 1]);
    Vertex v2 = unpackVertex(meshIndex, indexBuffers[meshIndex].indices[3 * gl_PrimitiveID + 2]);
    
    const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
    vec3 pos = v0.pos * barycentricCoords.x + v1.pos * barycentricCoords.y + v2.pos * barycentricCoords.z;
    vec3 normal = v0.normal * barycentricCoords.x + v1.normal * barycentricCoords.y + v2.normal * barycentricCoords.z;
    vec2 texCoord = v0.texCoord * barycentricCoords.x + v1.texCoord * barycentricCoords.y + v2.texCoord * barycentricCoords.z;
    
    payload.depth += 1;
    if(payload.depth >= 31){
        return;
    }

    vec3 origin = gl_WorldRayOriginEXT.xyz;
    vec3 direction = sampleHemisphereUniform(normal, payload.seed);

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

    //float brdf  = 1.0 / PI;
    //vec3 color = normal * 0.5 + 0.5;
    vec3 color = vec3(0.9);
    //payload.radiance = payload.radiance * 0.5;
    payload.radiance = color * payload.radiance;
    //payload.radiance = vec3(texCoord, 0.0);
    //payload.radiance = vec3(attribs.xy, 1);
}
