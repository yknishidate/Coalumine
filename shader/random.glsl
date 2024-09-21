
uint pcg(inout uint state)
{
    uint prev = state * 747796405u + 2891336453u;
    uint word = ((prev >> ((prev >> 28u) + 4u)) ^ prev) * 277803737u;
    state     = prev;
    return (word >> 22u) ^ word;
}

uvec2 pcg2d(uvec2 v)
{
    v = v * 1664525u + 1013904223u;

    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;

    v = v ^ (v >> 16u);

    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;

    v = v ^ (v >> 16u);

    return v;
}

float rand(inout uint seed)
{
    uint val = pcg(seed);
    return (float(val) * (1.0 / float(0xffffffffu)));
}

vec2 sampleDisk(inout uint seed) {
    const float PI = 3.1415926535;
    float u = rand(seed);
    float v = rand(seed);
    vec2 pos;
    pos.x = sqrt(u) * cos(2.0 * PI * v);
    pos.y = sqrt(u) * sin(2.0 * PI * v);
    return pos;
}
