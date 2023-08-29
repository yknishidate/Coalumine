#version 460
#extension GL_EXT_ray_tracing : enable

#include "./share.h"
#include "./random.glsl"
#include "./color.glsl"

layout(location = 0) rayPayloadInEXT HitPayload payload;
layout(location = 1) rayPayloadEXT bool shadowed;

hitAttributeEXT vec3 attribs;

Vertex unpackVertex(uint meshIndex,  uint vertexIndex) {
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

vec3 sampleSphereUniformLocal(inout uint seed) {
    float u = rand(seed);
    float v = rand(seed);

    float theta = 2.0 * PI * u;
    float phi = acos(2.0 * v - 1.0);

    vec3 localDir;
    localDir.x = sin(phi) * cos(theta);
    localDir.y = sin(phi) * sin(theta);
    localDir.z = cos(phi);

    return localDir;
}

vec3 localToWorld(in vec3 localDir, in vec3 normal) {
    vec3 up = abs(normal.z) < 0.999 ? vec3(0,0,1) : vec3(1,0,0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);
    return normalize(localDir.x * tangent + localDir.y * bitangent + localDir.z * normal);
}

vec3 worldToLocal(in vec3 worldDir, in vec3 normal) {
    vec3 up = abs(normal.z) < 0.999 ? vec3(0,0,1) : vec3(1,0,0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);

    vec3 localDir;
    localDir.x = dot(worldDir, tangent);
    localDir.y = dot(worldDir, bitangent);
    localDir.z = dot(worldDir, normal);
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

void traceRay(vec3 origin, vec3 direction) {
    traceRayEXT(
        topLevelAS,
        gl_RayFlagsOpaqueEXT,
        0xff, // cullMask
        0,    // sbtRecordOffset
        0,    // sbtRecordStride
        0,    // missIndex
        origin,
        0.001,
        direction,
        1000.0,
        0     // payloadLocation
    );
}

void traceShadowRay(vec3 origin, vec3 direction, float tmin){
    traceRayEXT(
        topLevelAS,
        gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT,
        0xff, // cullMask
        0,    // sbtRecordOffset
        0,    // sbtRecordStride
        1,    // missIndex
        origin,
        tmin,
        direction,
        1000.0,
        1     // payloadLocation
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
    if(roughness == 0.0) return 10000.0;
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float d = (NdotH * alpha2 - NdotH) * NdotH + 1.0;
    return alpha2 / (PI * d * d);
}

float ggxGeometry1(vec3 v, float a) {
    float t = tanTheta(v);
    return 2 / (1 + sqrt(1 + a * a * t * t));
}

float ggxGeometry(vec3 i, vec3 o, float roughness) {
    float a = roughness * roughness;
    return ggxGeometry1(i, a) * ggxGeometry1(o, a);
}

vec3 fresnelSchlick(float VdotH, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
}

float fresnelSchlick(float VdotH, float F0) {
    return F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
}

vec3 sampleGGX(float roughness, inout uint seed) {
    // NOTE: This function obeys D(m) * dot(m, n)
    // NOTE: if roughness == 0.0, return vec3(0, 0, 1)
    float u = rand(seed);
    float v = rand(seed);
    float alpha = roughness * roughness;
    float theta = atan(alpha * sqrt(v) / sqrt(1.0 - v));
    float phi = 2.0 * PI * u;
    return vec3(sin(phi) * sin(theta), cos(phi) * sin(theta), cos(theta));
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

    mat3 normalMatrix = mat3(normalMatrices[meshIndex]);
    normal = normalize(normalMatrix * normal);

    // Get material
    vec3 baseColor = vec3(1.0, 0.0, 1.0);
    float transmission = 0.0;
    float metallic = 0.0;
    float roughness = 0.0;
    vec3 emissive = vec3(0.0);

    int materialIndex = materialIndices[meshIndex];
    if(materialIndex != -1){
        Materials _materials = Materials(addresses.materials);
        Material material = _materials.materials[materialIndex];
        baseColor = material.baseColorFactor.rgb;
        transmission = 1.0 - material.baseColorFactor.a;
        metallic = material.metallicFactor;
        roughness = material.roughnessFactor;
        emissive = material.emissiveFactor.rgb;
    }

    payload.depth += 1;
    if(payload.depth >= 12){
        payload.radiance = emissive;
        return;
    }

    vec3 origin = pos;
    if(metallic > 0.0){
        // Based on Walter 2007
        // i: in
        // o: out
        // m: half

        // Sample direction
        vec3 i = worldToLocal(-gl_WorldRayDirectionEXT, normal);
        vec3 m = sampleGGX(roughness, payload.seed);
        vec3 o = reflect(-i, m);

        traceRay(origin, localToWorld(o, normal));

        // Compute the GGX BRDF
        float no = max(cosTheta(o), 0.0);
        float ni = max(cosTheta(i), 0.0);
        float nm = max(cosTheta(m), 0.0);
        float mo = max(dot(o, m), 0.0);

        // NOTE: If F0 = 1, the Fresnel value will always be 1, 
        // thus eliminating the Fresnel-like effect.
        vec3 F = fresnelSchlick(mo, baseColor);
        float G = ggxGeometry(i, o, roughness);

        // Importance sampling:
        // weight = (fr * cos) / pdf
        vec3 weight = vec3(F * G * mo) / max(ni * nm, 0.001);
        payload.radiance = emissive + weight * payload.radiance;
    }else if(transmission > 0.0){
        float ior = 1.51;
        bool into = dot(gl_WorldRayDirectionEXT, normal) < 0.0;
        float n1 = into ? 1.0 : ior;
        float n2 = into ? ior : 1.0;
        float eta = n1 / n2;
        vec3 n = into ? normal : -normal;
        
        vec3 i = worldToLocal(-gl_WorldRayDirectionEXT, n);
        vec3 m = sampleGGX(roughness, payload.seed);
        vec3 or = reflect(-i, m);      // out(reflect)
        vec3 ot = refract(-i, m, eta); // out(transmit)
        float mi = max(dot(i, m), 0.0);
        float ni = max(cosTheta(i), 0.0);
        float nm = max(cosTheta(m), 0.0);

        // total reflection
        if(or == vec3(0.0)){
            traceRay(origin, localToWorld(or, n));
            
            // Compute the GGX BRDF
            // NOTE: F = 1.0 in total reflection
            float no = max(cosTheta(or), 0.0);
            float G = ggxGeometry(i, or, roughness);
            vec3 weight = vec3(G * mi) / max(ni * nm, 0.001);
            payload.radiance = emissive + weight * payload.radiance;
        }
        
        // if n = 1.5, F0 = 0.04
        float F0 = ((n1 - n2) * (n1 - n2)) / ((n1 + n2) * (n1 + n2));
        float F = fresnelSchlick(mi, F0);
        if(rand(payload.seed) < F){
            traceRay(origin, localToWorld(or, n));
            
            float G = max(ggxGeometry(i, or, roughness), 0.0);
            vec3 weight = vec3(G * mi) / max(ni * nm, 0.001);
            payload.radiance = emissive + weight * payload.radiance;
        }else{
            // refraction
            traceRay(origin, localToWorld(ot, n));
            
            float G = max(ggxGeometry(i, ot, roughness), 0.0);
            vec3 weight = vec3(G * mi) / max(ni * nm, 0.001);
            payload.radiance = emissive + weight * payload.radiance;
        }
    }else{
        // Shadow ray
        shadowed = true;
        vec3 inifiniteLightTerm = vec3(0.0);
        traceShadowRay(origin, infiniteLightDirection.xyz, 0.1);
        if(!shadowed){
            float cosTheta = max(dot(normal, infiniteLightDirection.xyz), 0.0);
            inifiniteLightTerm = baseColor * infiniteLightIntensity * cosTheta;
        }

        // Diffuse IS
        vec3 direction = sampleHemisphereCosine(normal, payload.seed);
        traceRay(origin, direction);
        
        // Radiance (with Diffuse Importance sampling)
        // Lo = Le + brdf * Li * cos(theta) / pdf
        //    = Le + (color / PI) * Li * cos(theta) / (cos(theta) / PI)
        //    = Le + color * Li
        payload.radiance = emissive + (baseColor * payload.radiance + inifiniteLightTerm);
        //payload.radiance = emissive + (baseColor * payload.radiance);
    }
}
