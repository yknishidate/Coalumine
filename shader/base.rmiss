#version 460
#extension GL_EXT_ray_tracing : enable
#include "./share.h"
#include "./color.glsl"

layout(location = 0) rayPayloadInEXT HitPayload payload;

const vec2 invAtan = vec2(0.1591, 0.3183);

vec2 sampleSphericalMap(vec3 v)
{
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv = uv * invAtan + 0.5; // radian -> uv
    return uv;
}

vec3 rotateDirection(vec3 v, float theta, float phi) {
    theta = radians(theta);
    phi = radians(phi);
    { // rotate by phi
        float r = sqrt(v.x * v.x + v.z * v.z);
        float cosA = v.x;
        float sinA = -v.z;
        float cosB = cos(phi);
        float sinB = sin(phi);
        v.x = r * (cosA * cosB - sinA * sinB);
        v.z = r * (-(sinA * cosB + cosA * sinB));
    }
    { // rotate by theta
        float r = sqrt(v.x * v.x + v.y * v.y);
        float cosA = v.x;
        float sinA = -v.y;
        float cosB = cos(theta);
        float sinB = sin(theta);
        v.x = r * (cosA * cosB - sinA * sinB);
        v.y = r * (-(sinA * cosB + cosA * sinB));
    }
    return v;
}

void main()
{
    vec3 v = rotateDirection(gl_WorldRayDirectionEXT.xyz, domeLightTheta, domeLightPhi);
    vec2 uv = sampleSphericalMap(v);
    //payload.radiance = texture(domeLightTexture, uv).rgb;
    //payload.radiance = texture(lowDomeLightTexture, uv).rgb;

    // template0
    //payload.radiance = colorRamp5(uv.x,
    //    vec3(225, 245, 253) / 255.0,
    //    vec3(  1, 115, 233) / 255.0,
    //    vec3(  2,  37, 131) / 255.0,
    //    vec3(  0,   3,  49) / 255.0,
    //    vec3(  0,   0,   3) / 255.0);

    // template1
    payload.radiance = colorRamp5(uv.x,
        vec3(225, 245, 253) / 255.0,
        vec3(  0,   3,  49) / 255.0,
        vec3(  2,  37, 131) / 255.0,
        vec3(  1, 115, 233) / 255.0,
        vec3(  0,   0,   3) / 255.0);

    // black-white
    //payload.radiance = colorRamp2(uv.x,
    //    vec3(0.0, 0.0, 0.0),
    //    vec3(1.0, 1.0, 1.0));
}
