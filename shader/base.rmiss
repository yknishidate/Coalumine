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
    uv.y = 1.0 - uv.y;
    return uv;
}

vec3 sampleEnvLightTexture() {
    vec2 uv = sampleSphericalMap(gl_WorldRayDirectionEXT.xyz);
    uv.x = mod(uv.x + radians(pc.envLightPhi) / (2 * PI), 1.0); // rotate phi
    return clamp(texture(envLightTexture, uv).rgb, 0.0, 100.0) * pc.envLightIntensity;
    //return texture(envLightTexture, uv).rgb * pc.envLightIntensity;
}

void main()
{
    if (pc.useEnvLightTexture == 1) {
        if (pc.visibleEnvLightTexture == 1) {
            payload.radiance = sampleEnvLightTexture();
        } else {
            // invisible from primary ray
            if (payload.depth == 0) {
                payload.radiance = pc.envLightColor.xyz;
            } else {
                payload.radiance = sampleEnvLightTexture();
            }
        }
    } else {
        payload.radiance = pc.envLightColor.xyz;
    }
}
