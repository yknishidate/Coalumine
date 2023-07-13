
int perm[] = int[](151,
                   160,
                   137,
                   91,
                   90,
                   15,
                   131,
                   13,
                   201,
                   95,
                   96,
                   53,
                   194,
                   233,
                   7,
                   225,
                   140,
                   36,
                   103,
                   30,
                   69,
                   142,
                   8,
                   99,
                   37,
                   240,
                   21,
                   10,
                   23,
                   190,
                   6,
                   148,
                   247,
                   120,
                   234,
                   75,
                   0,
                   26,
                   197,
                   62,
                   94,
                   252,
                   219,
                   203,
                   117,
                   35,
                   11,
                   32,
                   57,
                   177,
                   33,
                   88,
                   237,
                   149,
                   56,
                   87,
                   174,
                   20,
                   125,
                   136,
                   171,
                   168,
                   68,
                   175,
                   74,
                   165,
                   71,
                   134,
                   139,
                   48,
                   27,
                   166,
                   77,
                   146,
                   158,
                   231,
                   83,
                   111,
                   229,
                   122,
                   60,
                   211,
                   133,
                   230,
                   220,
                   105,
                   92,
                   41,
                   55,
                   46,
                   245,
                   40,
                   244,
                   102,
                   143,
                   54,
                   65,
                   25,
                   63,
                   161,
                   1,
                   216,
                   80,
                   73,
                   209,
                   76,
                   132,
                   187,
                   208,
                   89,
                   18,
                   169,
                   200,
                   196,
                   135,
                   130,
                   116,
                   188,
                   159,
                   86,
                   164,
                   100,
                   109,
                   198,
                   173,
                   186,
                   3,
                   64,
                   52,
                   217,
                   226,
                   250,
                   124,
                   123,
                   5,
                   202,
                   38,
                   147,
                   118,
                   126,
                   255,
                   82,
                   85,
                   212,
                   207,
                   206,
                   59,
                   227,
                   47,
                   16,
                   58,
                   17,
                   182,
                   189,
                   28,
                   42,
                   223,
                   183,
                   170,
                   213,
                   119,
                   248,
                   152,
                   2,
                   44,
                   154,
                   163,
                   70,
                   221,
                   153,
                   101,
                   155,
                   167,
                   43,
                   172,
                   9,
                   129,
                   22,
                   39,
                   253,
                   19,
                   98,
                   108,
                   110,
                   79,
                   113,
                   224,
                   232,
                   178,
                   185,
                   112,
                   104,
                   218,
                   246,
                   97,
                   228,
                   251,
                   34,
                   242,
                   193,
                   238,
                   210,
                   144,
                   12,
                   191,
                   179,
                   162,
                   241,
                   81,
                   51,
                   145,
                   235,
                   249,
                   14,
                   239,
                   107,
                   49,
                   192,
                   214,
                   31,
                   181,
                   199,
                   106,
                   157,
                   184,
                   84,
                   204,
                   176,
                   115,
                   121,
                   50,
                   45,
                   127,
                   4,
                   150,
                   254,
                   138,
                   236,
                   205,
                   93,
                   222,
                   114,
                   67,
                   29,
                   24,
                   72,
                   243,
                   141,
                   128,
                   195,
                   78,
                   66,
                   215,
                   61,
                   156,
                   180,
                   151);

float fade(float t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float grad(int hash, float x, float y, float z) {
    int h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

float noise3D(float x, float y, float z) {
    int X = int(floor(x)) & 0xff;
    int Y = int(floor(y)) & 0xff;
    int Z = int(floor(z)) & 0xff;
    x -= floor(x);
    y -= floor(y);
    z -= floor(z);
    float u = fade(x);
    float v = fade(y);
    float w = fade(z);
    int A = (perm[X] + Y) & 0xff;
    int B = (perm[X + 1] + Y) & 0xff;
    int AA = (perm[A] + Z) & 0xff;
    int BA = (perm[B] + Z) & 0xff;
    int AB = (perm[A + 1] + Z) & 0xff;
    int BB = (perm[B + 1] + Z) & 0xff;
    return mix(
        mix(mix(grad(perm[AA], x, y, z), grad(perm[BA], x - 1, y, z), u),
            mix(grad(perm[AB], x, y - 1, z), grad(perm[BB], x - 1, y - 1, z), u), v),
        mix(mix(grad(perm[AA + 1], x, y, z - 1), grad(perm[BA + 1], x - 1, y, z - 1), u),
            mix(grad(perm[AB + 1], x, y - 1, z - 1), grad(perm[BB + 1], x - 1, y - 1, z - 1), u),
            v),
        w);
}

float noise3D(vec3 coord) {
    return noise3D(coord.x, coord.y, coord.z);
}

float fbm(vec3 coord, int octave, float gain) {
    float f = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < octave; i++) {
        f += amplitude * noise3D(coord);
        coord *= 2.0;
        amplitude *= gain;
    }
    return f;
}

// return: [-1.0,  1.0]
vec3 flowNoise(vec3 uvw, float t, vec3 scale, vec3 shift, float rotationRadius) {
    mat3 rot = mat3(cos(rotationRadius), sin(rotationRadius), 0, -sin(rotationRadius),
                    cos(rotationRadius), 0, 0, 0, 1);
    uvw = rot * uvw;
    uvw *= scale;
    uvw += shift;

    float n1 = noise3D(vec3(uvw.x, uvw.y, uvw.z + t));
    float n2 = noise3D(vec3(uvw.x, uvw.y, uvw.z + t + 5.0));
    float n3 = noise3D(vec3(uvw.x, uvw.y, uvw.z + t + 10.0));
    float n = (n1 + n2 + n3) / 3.0;
    return vec3(n);
}
