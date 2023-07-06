
bool intersectAABB(in vec3 origin, in vec3 direction, in vec3 aabbMin, in vec3 aabbMax,
                   out float tMin, out float tMax)
{
    vec3 t1 = (aabbMin - origin) / direction;
    vec3 t2 = (aabbMax - origin) / direction;
    vec3 min1 = min(t1, t2);
    vec3 max1 = max(t1, t2);
    tMin = max(max(min1.x, min1.y), min1.z);
    tMax = min(min(max1.x, max1.y), max1.z);
    return 0 <= tMax && tMin <= tMax;
}

bool intersectSphere(in vec3 origin, in vec3 direction, in vec3 center, in float radius,
                     out float tMin, out float tMax)
{
    vec3 oc = center - origin;
    float b = dot(oc, direction);
    float det = b * b - dot(oc, oc) + radius * radius;
    if(det <= 0.0){
        return false;
    }
    float sqrtDet = sqrt(det);
    tMin = b - sqrtDet;
    tMax = b + sqrtDet;
    return true;
}
