#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#include "./share.h"
#include "./random.glsl"

layout(binding = 8) uniform accelerationStructureEXT topLevelAS;

layout(binding = 16) buffer VertexBuffers{float vertices[];} vertexBuffers[];
layout(binding = 17) buffer IndexBuffers{uint indices[];} indexBuffers[];
layout(binding = 19) buffer TransformMatrixBuffer{mat4 transformMatrices[];};
layout(binding = 20) buffer NormalMatrixBuffer{mat3 normalMatrices[];};

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

void main()
{
    uint meshIndex = gl_InstanceID;
    Vertex v0 = unpackVertex(meshIndex, indexBuffers[meshIndex].indices[3 * gl_PrimitiveID + 0]);
    Vertex v1 = unpackVertex(meshIndex, indexBuffers[meshIndex].indices[3 * gl_PrimitiveID + 1]);
    Vertex v2 = unpackVertex(meshIndex, indexBuffers[meshIndex].indices[3 * gl_PrimitiveID + 2]);
    
    const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
    vec3 pos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    vec3 normal = v0.normal * barycentricCoords.x + v1.normal * barycentricCoords.y + v2.normal * barycentricCoords.z;
    vec2 texCoord = v0.texCoord * barycentricCoords.x + v1.texCoord * barycentricCoords.y + v2.texCoord * barycentricCoords.z;
    
    payload.depth += 1;
    if(payload.depth >= 8){
        return;
    }

    vec3 origin = gl_WorldRayOriginEXT.xyz;

    // uniform
    vec3 direction = sampleHemisphereUniform(normal, payload.seed);
    float cosTheta = dot(normal, direction);
    float pdf = 1.0 / (2.0 * PI);

    // importance
    //vec3 direction = sampleHemisphereUniform(normal, payload.seed);
    //float cosTheta = dot(normal, direction);
    //float pdf = cosTheta / PI; // importance

    traceRay(origin, direction);

    // fr = ƒÏ / pi
    vec3 color = vec3(0.9);
    vec3 brdf = color / PI;

    // Lo = fr * Li * cosƒÆ / pdf
    payload.radiance = brdf * payload.radiance * cosTheta / pdf;
    //payload.radiance = pos * 0.5 + 0.5;
    //payload.radiance = normal * 0.5 + 0.5;
}
