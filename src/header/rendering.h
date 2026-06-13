#ifndef RENDERING_H
#define RENDERING_H

#include "settings.h"
#include "types.h"
#include "vector_math.h"
#include "thread_pool.h"

static inline void timInsertionSort(struct face* arr, int left, int right) {
    for(int i = left + 1; i <= right; i++) {
        struct face tmp = arr[i];
        int j = i - 1;
        while(j >= left && arr[j].distance < tmp.distance) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = tmp;
    }
}

static inline void timMerge(struct face* arr, int left, int mid, int right, struct face* buf) {
    int lenLeft = mid - left + 1;
    for(int i = 0; i < lenLeft; i++) buf[i] = arr[left + i];

    int i = 0, j = mid + 1, k = left;
    while(i < lenLeft && j <= right) {
        if(buf[i].distance >= arr[j].distance)
            arr[k++] = buf[i++];
        else
            arr[k++] = arr[j++];
    }
    while(i < lenLeft) arr[k++] = buf[i++];
}

inline void sortTrianglesByDistance(struct face triangle[], int populatedTriangleCount, ThreadPool& pool) {
    int n = populatedTriangleCount;
    if(n < 2) return;

    static const int MIN_RUN = 32;
    static struct face* timBuf = new face[worldWidth * worldDepth * worldHeight * 12 * targetResolution * targetResolution]();

    // Each run is an independent, disjoint slice of the array — safe to sort in parallel.
    int runCount = (n + MIN_RUN - 1) / MIN_RUN;
    pool.runParallel(runCount, [&](int start, int end) {
        for(int r = start; r < end; r++) {
            int left = r * MIN_RUN;
            int right = (left + MIN_RUN - 1 < n - 1) ? left + MIN_RUN - 1 : n - 1;
            timInsertionSort(triangle, left, right);
        }
    });

    // Each merge pair at a given level touches a disjoint [left, right] slice of the
    // array, and (by offsetting into timBuf by `left`) a disjoint scratch region too —
    // so all pairs at a level can merge in parallel. Levels themselves stay sequential.
    for(int size = MIN_RUN; size < n; size *= 2) {
        int pairCount = (n + 2*size - 1) / (2*size);
        pool.runParallel(pairCount, [&](int start, int end) {
            for(int p = start; p < end; p++) {
                int left  = p * 2 * size;
                int mid   = left + size - 1;
                int right = (left + 2*size - 1 < n - 1) ? left + 2*size - 1 : n - 1;
                if(mid < right)
                    timMerge(triangle, left, mid, right, timBuf + left);
            }
        });
    }
}

inline struct sextupleVector GetSolutionVector(struct face CurrentTriangle, int WindowWidth, int WindowHeight,
                        struct vector C, struct vector Cf,
                        double FOVDepth, double FOVWidth, double FOVHeight) {

    struct vector Cr = normalizedVector({Cf.y, -Cf.x, 0.0});
    struct vector Cd = normalizedVector({Cf.x*Cf.z, Cf.z*Cf.y, -Cf.x*Cf.x - Cf.y*Cf.y});

    // Perspective-project a world point to pixel coordinates.
    // Returns {pixel_x, pixel_y, 0}.
    auto project = [&](struct vector P) -> struct vector {
        double rx = P.x - C.x, ry = P.y - C.y, rz = P.z - C.z;
        double fwd = rx*Cf.x + ry*Cf.y + rz*Cf.z;
        double rt  = rx*Cr.x + ry*Cr.y + rz*Cr.z;
        double dn  = rx*Cd.x + ry*Cd.y + rz*Cd.z;
        double s = 0.5 + (FOVDepth * rt) / (2.0 * FOVWidth  * fwd);
        double r = 0.5 + (FOVDepth * dn) / (2.0 * FOVHeight * fwd);
        return {s * WindowWidth, r * WindowHeight, 0.0};
    };

    // Points at or behind this depth in front of the camera are clipped.
    // Must be > 0 so project() never divides by zero.
    const double nearPlane = 0.01;

    // A point is occluded when it is at or closer than the near plane.
    auto isOccluded = [&](struct vector P) -> bool {
        return (P.x-C.x)*Cf.x + (P.y-C.y)*Cf.y + (P.z-C.z)*Cf.z <= nearPlane;
    };

    // Find where the edge from a visible point Pv to an occluded point Po
    // crosses the near plane (fwd == nearPlane), return that intersection.
    // The result is guaranteed to have fwd == nearPlane, so project() is safe.
    auto clipEdge = [&](struct vector Pv, struct vector Po) -> struct vector {
        double dv = (Pv.x-C.x)*Cf.x + (Pv.y-C.y)*Cf.y + (Pv.z-C.z)*Cf.z;
        double dp = (Po.x-C.x)*Cf.x + (Po.y-C.y)*Cf.y + (Po.z-C.z)*Cf.z;
        double t  = (dv - nearPlane) / (dv - dp);
        return {Pv.x + t*(Po.x-Pv.x), Pv.y + t*(Po.y-Pv.y), Pv.z + t*(Po.z-Pv.z)};
    };

    bool occ1 = isOccluded(CurrentTriangle.P1);
    bool occ2 = isOccluded(CurrentTriangle.P2);
    bool occ3 = isOccluded(CurrentTriangle.P3);
    int occludedCount = (occ1 ? 1 : 0) + (occ2 ? 1 : 0) + (occ3 ? 1 : 0);

    enum TriangleScreenVisibility {
        ZERO_POINTS_OCCLUDED  = 0,
        ONE_POINT_OCCLUDED    = 1,
        TWO_POINTS_OCCLUDED   = 2,
        THREE_POINTS_OCCLUDED = 3
    };

    switch(occludedCount) {
        case THREE_POINTS_OCCLUDED:
            return {0,0,0,0,0,0,0,0,0,0,0,0};

        case ZERO_POINTS_OCCLUDED: {
            struct vector pA = project(CurrentTriangle.P1);
            struct vector pB = project(CurrentTriangle.P2);
            struct vector pC = project(CurrentTriangle.P3);
            return {pA.x, pA.y, pB.x, pB.y, pC.x, pC.y,
                    0.0,  0.0,  0.0,  0.0,  0.0,  0.0};
        }

        case ONE_POINT_OCCLUDED: {
            // 2 visible, 1 occluded → quad → 2 triangles
            struct vector Pv1, Pv2, Po;
            if     (occ1) { Po = CurrentTriangle.P1; Pv1 = CurrentTriangle.P2; Pv2 = CurrentTriangle.P3; }
            else if(occ2) { Po = CurrentTriangle.P2; Pv1 = CurrentTriangle.P3; Pv2 = CurrentTriangle.P1; }
            else          { Po = CurrentTriangle.P3; Pv1 = CurrentTriangle.P1; Pv2 = CurrentTriangle.P2; }

            struct vector Pc1 = clipEdge(Pv1, Po);
            struct vector Pc2 = clipEdge(Pv2, Po);

            struct vector pA = project(Pv1);
            struct vector pB = project(Pv2);
            struct vector pC = project(Pc1);
            struct vector pD = project(Pc2);

            return {pA.x, pA.y, pB.x, pB.y, pC.x, pC.y,
                    pB.x, pB.y, pD.x, pD.y, pC.x, pC.y};
        }

        default: { // TWO_POINTS_OCCLUDED: 1 visible, 2 occluded → clipped triangle
            struct vector Pv, Po1, Po2;
            if     (!occ1) { Pv = CurrentTriangle.P1; Po1 = CurrentTriangle.P2; Po2 = CurrentTriangle.P3; }
            else if(!occ2) { Pv = CurrentTriangle.P2; Po1 = CurrentTriangle.P3; Po2 = CurrentTriangle.P1; }
            else           { Pv = CurrentTriangle.P3; Po1 = CurrentTriangle.P1; Po2 = CurrentTriangle.P2; }

            struct vector Pc1 = clipEdge(Pv, Po1);
            struct vector Pc2 = clipEdge(Pv, Po2);

            struct vector pA = project(Pv);
            struct vector pB = project(Pc1);
            struct vector pC = project(Pc2);

            return {pA.x, pA.y, pB.x, pB.y, pC.x, pC.y,
                    0.0,  0.0,  0.0,  0.0,  0.0,  0.0};
        }
    }
}

#endif // RENDERING_H
