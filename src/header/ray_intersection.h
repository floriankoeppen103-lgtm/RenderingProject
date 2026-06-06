#ifndef RAY_INTERSECTION_H
#define RAY_INTERSECTION_H

#include "types.h"
#include "vector_math.h"

inline double mollerTromboree(struct vector C, struct vector Cf, struct face &triangle) {
    double orig[3] = {C.x, C.y, C.z};
    double dir[3]  = {Cf.x, Cf.y, Cf.z};
    double vert0[3] = {triangle.P1.x, triangle.P1.y, triangle.P1.z};
    double vert1[3] = {triangle.P2.x, triangle.P2.y, triangle.P2.z};
    double vert2[3] = {triangle.P3.x, triangle.P3.y, triangle.P3.z};

    double edge1[3], edge2[3], tvec[3], pvec[3], qvec[3];

    // edge1 = vert1 - vert0
    edge1[0] = vert1[0] - vert0[0];
    edge1[1] = vert1[1] - vert0[1];
    edge1[2] = vert1[2] - vert0[2];

    // edge2 = vert2 - vert0
    edge2[0] = vert2[0] - vert0[0];
    edge2[1] = vert2[1] - vert0[1];
    edge2[2] = vert2[2] - vert0[2];

    // pvec = dir x edge2
    pvec[0] = dir[1]*edge2[2] - dir[2]*edge2[1];
    pvec[1] = dir[2]*edge2[0] - dir[0]*edge2[2];
    pvec[2] = dir[0]*edge2[1] - dir[1]*edge2[0];

    double det = edge1[0]*pvec[0] + edge1[1]*pvec[1] + edge1[2]*pvec[2];

    if(det > -0.000001 && det < 0.000001)
        return -1.0;

    double inv_det = 1.0 / det;

    // tvec = orig - vert0
    tvec[0] = orig[0] - vert0[0];
    tvec[1] = orig[1] - vert0[1];
    tvec[2] = orig[2] - vert0[2];

    double u = (tvec[0]*pvec[0] + tvec[1]*pvec[1] + tvec[2]*pvec[2]) * inv_det;
    if(u < 0.0 || u > 1.0)
        return -1.0;

    // qvec = tvec x edge1
    qvec[0] = tvec[1]*edge1[2] - tvec[2]*edge1[1];
    qvec[1] = tvec[2]*edge1[0] - tvec[0]*edge1[2];
    qvec[2] = tvec[0]*edge1[1] - tvec[1]*edge1[0];

    double v = (dir[0]*qvec[0] + dir[1]*qvec[1] + dir[2]*qvec[2]) * inv_det;
    if(v < 0.0 || u + v > 1.0)
        return -1.0;

    return (edge2[0]*qvec[0] + edge2[1]*qvec[1] + edge2[2]*qvec[2]) * inv_det;
}

#endif // RAY_INTERSECTION_H
