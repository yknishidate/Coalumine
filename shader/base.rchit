#version 460
#extension GL_EXT_ray_tracing : enable
#include "./share.h"

layout(binding = 8) uniform accelerationStructureEXT topLevelAS;

layout(binding = 16, set = 0) buffer Vertices{float vertices[];};
layout(binding = 17, set = 0) buffer Indices{uint indices[];};

layout(location = 0) rayPayloadInEXT HitPayload payload;

hitAttributeEXT vec3 attribs;

struct Vertex
{
    vec3 pos;
    vec3 normal;
    vec2 texCoord;
};

Vertex unpackVertex(uint index)
{
    uint stride = 8;
    uint offset = index * stride;
    Vertex v;
    v.pos = vec3(vertices[offset +  0], vertices[offset +  1], vertices[offset + 2]);
    v.normal = vec3(vertices[offset +  3], vertices[offset +  4], vertices[offset + 5]);
    v.texCoord = vec2(vertices[offset +  6], vertices[offset +  7]);
    return v;
}

void main()
{
    Vertex v0 = unpackVertex(indices[3 * gl_PrimitiveID + 0]);
    Vertex v1 = unpackVertex(indices[3 * gl_PrimitiveID + 1]);
    Vertex v2 = unpackVertex(indices[3 * gl_PrimitiveID + 2]);
    
    const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
    vec3 pos = v0.pos * barycentricCoords.x + v1.pos * barycentricCoords.y + v2.pos * barycentricCoords.z;
    vec3 normal = v0.normal * barycentricCoords.x + v1.normal * barycentricCoords.y + v2.normal * barycentricCoords.z;
    vec2 texCoord = v0.texCoord * barycentricCoords.x + v1.texCoord * barycentricCoords.y + v2.texCoord * barycentricCoords.z;
    
    payload.depth += 1;

    vec3 origin = gl_WorldRayOriginEXT.xyz;
    vec3 direction = gl_WorldRayDirectionEXT.xyz;
    direction = reflect(direction, normal);

    //traceRayEXT(
    //    topLevelAS,
    //    gl_RayFlagsOpaqueEXT,
    //    0xff, // cullMask
    //    0,    // sbtRecordOffset
    //    0,    // sbtRecordStride
    //    0,    // missIndex
    //    origin,
    //    gl_RayTminEXT,
    //    direction,
    //    gl_RayTmaxEXT,
    //    0     // payloadLocation
    //);

    //payload.radiance = payload.radiance * 0.5;
    payload.radiance = normal * 0.5 + 0.5;
    //payload.radiance = vec3(texCoord, 0.0);
    //payload.radiance = vec3(attribs.xy, 1);
}
