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

void main()
{
    vec2 uv = sampleSphericalMap(gl_WorldRayDirectionEXT.xyz);
    uv.x = mod(uv.x + radians(domeLightPhi) / (2 * PI), 1.0); // rotate phi
    payload.radiance = clamp(texture(domeLightTexture, uv).rgb, 0.0, 10.0);

    // template0
    //payload.radiance = colorRamp5(uv.x,
    //    vec3(225, 245, 253) / 255.0,
    //    vec3(  1, 115, 233) / 255.0,
    //    vec3(  2,  37, 131) / 255.0,
    //    vec3(  0,   3,  49) / 255.0,
    //    vec3(  0,   0,   3) / 255.0);
}
