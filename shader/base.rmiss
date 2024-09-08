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

void main()
{
    if (pc.useEnvLightTexture == 1) {
        vec2 uv = sampleSphericalMap(gl_WorldRayDirectionEXT.xyz);
        uv.x = mod(uv.x + radians(pc.envLightPhi) / (2 * PI), 1.0); // rotate phi
        payload.radiance = clamp(texture(envLightTexture, uv).rgb, 0.0, 100.0) * pc.envLightIntensity;
    } else {
        payload.radiance = pc.envLightColor.xyz;
    }
}
