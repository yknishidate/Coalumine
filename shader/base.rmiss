#version 460
#extension GL_EXT_ray_tracing : enable
#include "./share.h"

layout(binding = 18) uniform sampler2D domeLightTexture;

layout(location = 0) rayPayloadInEXT HitPayload payload;

const vec2 invAtan = vec2(0.1591, 0.3183);

vec2 sampleSphericalMap(vec3 v)
{
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv = uv * invAtan + 0.5; // radian -> uv
    return uv;
}

void main()
{
    vec2 uv = sampleSphericalMap(gl_WorldRayDirectionEXT.xyz);
    //vec2 value = mod(uv, vec2(0.1));
    //payload.radiance = vec3(value * 10.0, 0.0);
    payload.radiance = texture(domeLightTexture, uv).rgb;
}
