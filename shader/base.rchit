#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_debug_printf : enable

#include "./share.h"
#include "./random.glsl"
#include "./color.glsl"

layout(location = 0) rayPayloadInEXT HitPayload payload;
layout(location = 1) rayPayloadEXT bool shadowed;

hitAttributeEXT vec3 attribs;

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

void traceRay(vec3 pos, vec3 direction) {
    traceRayEXT(
        topLevelAS,
        gl_RayFlagsOpaqueEXT,
        0xff, // cullMask
        0,    // sbtRecordOffset
        0,    // sbtRecordStride
        0,    // missIndex
        pos,
        0.001,
        direction,
        1000.0,
        0     // payloadLocation
    );
}

void traceShadowRay(vec3 pos, vec3 direction, float tmin){
    traceRayEXT(
        topLevelAS,
        gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT,
        0xff, // cullMask
        0,    // sbtRecordOffset
        0,    // sbtRecordStride
        1,    // missIndex
        pos,
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
    return sinTheta(w) / (cosTheta(w) + 0.001);
}

float tan2Theta(vec3 w) {
    return sin2Theta(w) / cos2Theta(w);
}

float ggxGeometry1(vec3 v, vec3 m, float a) {
    float t = tanTheta(v);
    float x = step(0.0, dot(v, m) / cosTheta(v));
    return x * 2.0 / (1.0 + sqrt(1 + a * a * t * t));
}

float ggxGeometry(vec3 i, vec3 o, vec3 m, float roughness) {
    float a = roughness * roughness;
    return ggxGeometry1(i, m, a) * ggxGeometry1(o, m, a);
}

float fresnel(float mi, float eta) {
    float g = sqrt(eta * eta - 1.0 + mi * mi);
    return (1.0 / 2.0) * (((g - mi) * (g - mi)) / ((g + mi) * (g + mi)));
}

float fresnelSchlick(float mi, float F0) {
    return F0 + (1.0 - F0) * pow(1.0 - mi, 5.0);
}

vec3 getInfiniteLightRadiance(vec3 pos) {
    if (pc.infiniteLightIntensity == 0.0) {
        return vec3(0.0);
    }

    shadowed = true;
    traceShadowRay(pos, pc.infiniteLightDirection.xyz, 0.001);
    if(shadowed){
        return vec3(0.0);
    }

    // return Li
    return pc.infiniteLightColor.xyz * pc.infiniteLightIntensity;
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

vec3 LambertBRDF(vec3 baseColor) {
    return baseColor / PI;
}

vec3 GlassRadiance(vec3 pos, vec3 normal, float roughness, float ior) {
    bool into = dot(gl_WorldRayDirectionEXT, normal) < 0.0;
    float n1 = into ? 1.0 : ior;
    float n2 = into ? ior : 1.0;
    float eta = n1 / n2;
    vec3 n = into ? normal : -normal;

    vec3 i = worldToLocal(-gl_WorldRayDirectionEXT, n);
    vec3 m = sampleGGX(roughness, payload.seed);
    vec3 or = reflect(-i, m);      // out(reflect)
    vec3 ot = refract(-i, m, eta); // out(transmit)
    float ni = abs(cosTheta(i));
    float nm = abs(cosTheta(m));
    float mi = abs(dot(i, m));

    // total reflection
    if(or == vec3(0.0)){
        traceRay(pos, localToWorld(or, n));

        // Compute the GGX BRDF
        // NOTE: F = 1.0 in total reflection
        float G = ggxGeometry(i, or, m, roughness);
        vec3 weight = vec3(G * mi) / max(ni * nm, 0.1);
        return weight * payload.radiance;
    }

    // if n = 1.5, F0 = 0.04
    float F0 = ((n1 - n2) * (n1 - n2)) / ((n1 + n2) * (n1 + n2));
    float F = fresnelSchlick(mi, F0);
    if(rand(payload.seed) < F){
        traceRay(pos, localToWorld(or, n));
            
        float G = ggxGeometry(i, or, m, roughness);
        vec3 weight = vec3(G * mi) / max(ni * nm, 0.1);
        return weight * payload.radiance;
    }else{
        // refraction
        traceRay(pos, localToWorld(ot, n));
            
        float G = ggxGeometry(i, ot, m, roughness);
        vec3 weight = vec3(G * mi) / max(ni * nm, 0.1);
        return weight * payload.radiance;
    }
}

// Cauchy's equation
vec3 ComputeIOR(float a, float b, vec3 wavelength) {
    return a + b / (wavelength * wavelength);
}

void main()
{
    NodeData data = nodeData[gl_InstanceCustomIndexEXT];

    VertexBuffer vertexBuffer = VertexBuffer(data.vertexBufferAddress);
    IndexBuffer indexBuffer = IndexBuffer(data.indexBufferAddress);

    uvec3 index = indexBuffer.indices[gl_PrimitiveID];
    Vertex v0 = vertexBuffer.vertices[index[0]];
    Vertex v1 = vertexBuffer.vertices[index[1]];
    Vertex v2 = vertexBuffer.vertices[index[2]];
    
    const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
    float t = gl_HitTEXT;
    vec3 pos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    vec3 normal = normalize(v0.normal * barycentricCoords.x + v1.normal * barycentricCoords.y + v2.normal * barycentricCoords.z);
    vec2 texCoord = v0.texCoord * barycentricCoords.x + v1.texCoord * barycentricCoords.y + v2.texCoord * barycentricCoords.z;

    mat3 normalMatrix = mat3(data.normalMatrix);
    normal = normalize(normalMatrix * normal);

    // Get material
    vec3 baseColor = vec3(1.0, 0.0, 1.0);
    float transmission = 0.0;
    float metallic = 0.0;
    float roughness = 0.0;
    vec3 emissive = vec3(0.0);
    float ior = 1.51;
    float dispersion = 0.0;

    int materialIndex = data.materialIndex;
    if(materialIndex != -1){
        Material material = materials[materialIndex];
        baseColor = material.baseColorFactor.rgb;
        transmission = 1.0 - material.baseColorFactor.a;
        metallic = material.metallicFactor;
        roughness = material.roughnessFactor;
        emissive = material.emissiveFactor.rgb;
        ior = material.ior;
        dispersion = material.dispersion;
    }
    payload.depth += 1;
    if(payload.depth >= 24){
        payload.radiance = emissive;
        return;
    }

    if(metallic > 0.0){
        // metallicは基本的に2値であると考えていい
        // Based on Walter 2007
        // i: in
        // o: out
        // m: half

        // Sample direction
        vec3 i = worldToLocal(-gl_WorldRayDirectionEXT, normal);
        vec3 m = sampleGGX(roughness, payload.seed);
        vec3 o = reflect(-i, m);

        traceRay(pos, localToWorld(o, normal));

        // Compute the GGX BRDF
        float no = abs(cosTheta(o));
        float ni = abs(cosTheta(i));
        float nm = abs(cosTheta(m));
        float mi = abs(dot(i, m));

        vec3 F = baseColor;
        float G = ggxGeometry(i, o, m, roughness);

        // Importance sampling:
        // NOTE: max(x, 0.1) is greatly affects the appearance. 
        // Smaller values make it too bright.
        vec3 weight = vec3(F * G * mi) / max(ni * nm, 0.1);
        payload.radiance = emissive + weight * payload.radiance;
    }else if(transmission > 0.0){
        // 非metallicの場合にのみtransmissionは有効となる
        // emissiveは無視
        if (dispersion == 0.0) {
            vec3 radiance = GlassRadiance(pos, normal, roughness, ior);
            vec3 transmittance = exp(-payload.t * (vec3(1.0) - baseColor)); // payload.tはGlassRadiance()内で更新される
            payload.radiance = transmittance * radiance;
        } else {
            // λ (μm)
            const vec3 wavelength = vec3(0.7, 0.5461, 0.4358);
            const vec3 n_rgb = ComputeIOR(ior, dispersion, wavelength);

            int index = payload.component;
            float pdf = 1.0;
            if (payload.component == -1) {
                // if comp is unselected, select it randomly
                float rand_val = rand(payload.seed);
                index = int(rand_val * 3.0) % 3;
                pdf = 0.333333333;
            }
            float radiance_index = GlassRadiance(pos, normal, roughness, n_rgb[index])[index];
            float transmittance_index = exp(-payload.t * (1.0 - baseColor[index])); // payload.tはGlassRadiance()内で更新される
            payload.radiance = vec3(0.0);
            payload.radiance[index] = transmittance_index * radiance_index / pdf;
        }

        payload.t = t;
    }else{
        // Infinite light NEE
        vec3 infLightTerm = vec3(0.0);
        {
            float cosTheta = max(dot(normal, pc.infiniteLightDirection.xyz), 0.0);
            if (cosTheta > 0.0) {
                float pdf = 1.0;
                vec3 incoming = getInfiniteLightRadiance(pos);
                vec3 brdf = LambertBRDF(baseColor);
                infLightTerm = brdf * incoming * cosTheta / pdf;
            }
        }

        // Diffuse IS
        vec3 ptLightTerm = vec3(0.0);
        {
            vec3 direction = sampleHemisphereCosine(normal, payload.seed);
            traceRay(pos, direction);

            // Radiance (with Diffuse Importance sampling)
            // Lo = Le + brdf         * Li * cos(theta) / pdf
            //    = Le + (color / PI) * Li * cos(theta) / (cos(theta) / PI)
            //    = Le + color * Li
            ptLightTerm = baseColor * payload.radiance;
        }

        payload.radiance = emissive + ptLightTerm + infLightTerm;
    }
}
