
float computeLuminance(vec3 color) {
    // RGB to YUV weights for luminance calculation
    const vec3 W = vec3(0.2126, 0.7152, 0.0722);
    return dot(color, W);
}

vec3 colorRamp5(float value, vec3 color0, vec3 color1, vec3 color2, vec3 color3, vec3 color4) {
    if (value == 0.0)
        return vec3(0.0);
    float knot0 = 0.0;
    float knot1 = 0.1;   // violet
    float knot2 = 0.25;  // blue
    float knot3 = 0.6;   // red
    float knot4 = 0.8;   // yellow
    if (value < knot0) {
        return color0;
    } else if (value < knot1) {
        float t = (value - knot0) * (knot1 - knot0);
        return mix(color0, color1, t);
    } else if (value < knot2) {
        float t = (value - knot1) * (knot2 - knot1);
        return mix(color1, color2, t);
    } else if (value < knot3) {
        float t = (value - knot2) * (knot3 - knot2);
        return mix(color2, color3, t);
    } else if (value < knot4) {
        float t = (value - knot3) * (knot4 - knot3);
        return mix(color3, color4, t);
    } else {
        return color4;
    }
}

vec3 colorRamp6(float value,
                vec3 color0,
                vec3 color1,
                vec3 color2,
                vec3 color3,
                vec3 color4,
                vec3 color5) {
    if (value == 0.0)
        return vec3(0.0);
    float knot0 = 0.0;
    float knot1 = 0.2;
    float knot2 = 0.4;
    float knot3 = 0.5;
    float knot4 = 0.8;
    float knot5 = 1.0;
    if (value < knot0) {
        return color0;
    } else if (value < knot1) {
        float t = (value - knot0) * (knot1 - knot0);
        return mix(color0, color1, t);
    } else if (value < knot2) {
        float t = (value - knot1) * (knot2 - knot1);
        return mix(color1, color2, t);
    } else if (value < knot3) {
        float t = (value - knot2) * (knot3 - knot2);
        return mix(color2, color3, t);
    } else if (value < knot4) {
        float t = (value - knot3) * (knot4 - knot3);
        return mix(color3, color4, t);
    } else if (value < knot5) {
        float t = (value - knot4) * (knot5 - knot4);
        return mix(color4, color5, t);
    } else {
        return color5;
    }
}

vec3 toneMapping(vec3 color, float exposure) {
    color = vec3(1.0) - exp(-color * exposure);
    return color;
}

vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

vec3 saturate(vec3 color, float saturation) {
    vec3 hsv = rgb2hsv(color);
    hsv.y *= saturation;  // saturate
    return hsv2rgb(hsv);
}
