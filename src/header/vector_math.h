#ifndef VECTOR_MATH_H
#define VECTOR_MATH_H

#include <cmath>
#include <cstdio>
#include "types.h"

// Vector Operation declarations
struct vector faceCenter(struct face face1);
void printVector(struct vector A);
void printVectorFormated(struct vector A);
double length(struct vector VECTOR);
struct vector crossProduct(struct vector V1, struct vector V2);
struct vector normalizedVector(struct vector V);
struct vector matrixMultiply(struct Matrix3x3 m, struct vector v);

// Array Operations
void printArray(double A[3][4]);
struct vector solveLinEqu(double A[3][4]);
double determinant(double A[3][4]);

// Vector Operation implementations
inline struct vector faceCenter(struct face face1) {
    struct vector solution;
    solution.x = (face1.P1.x + face1.P2.x + face1.P3.x) / 3;
    solution.y = (face1.P1.y + face1.P2.y + face1.P3.y) / 3;
    solution.z = (face1.P1.z + face1.P2.z + face1.P3.z) / 3;
    return solution;
}

inline double length(struct vector VECTOR) {
    return sqrt(VECTOR.x * VECTOR.x + VECTOR.y * VECTOR.y + VECTOR.z * VECTOR.z);
}

inline void printVector(struct vector A) {
    printf("X: %.2f   ", A.x);
    printf("Y: %.2f   ", A.y);
    printf("Z: %.2f   ", A.z);
    printf("Length: %.2f", length(A));
    printf("\n");
}

inline void printVectorFormated(struct vector A) {
    printf("(%.2f,", A.x);
    printf("%.2f,", A.y);
    printf("%.2f)", A.z);
    printf("\n");
}

inline double determinant(double A[3][4]) {
    return A[0][0] * A[1][1] * A[2][2] + 
           A[0][1] * A[1][2] * A[2][0] + 
           A[0][2] * A[1][0] * A[2][1] - 
           A[2][0] * A[1][1] * A[0][2] - 
           A[2][1] * A[1][2] * A[0][0] - 
           A[2][2] * A[1][0] * A[0][1];
}

inline struct vector crossProduct(struct vector V1, struct vector V2) {
    struct vector solution;
    solution.x = V1.y * V2.z - V1.z * V2.y;
    solution.y = V1.z * V2.x - V1.x * V2.z;
    solution.z = V1.x * V2.y - V1.y * V2.x;
    return solution;
}

inline void printArray(double A[3][4]) {
    printf("(%.2fx) + ", A[0][0]);
    printf("(%.2fy) + ", A[0][1]);
    printf("(%.2fz) = ", A[0][2]);
    printf("(%.2fd)", A[0][3]);
    printf("\n");

    printf("(%.2fx) + ", A[1][0]);
    printf("(%.2fy) + ", A[1][1]);
    printf("(%.2fz) = ", A[1][2]);
    printf("(%.2fd)", A[1][3]);
    printf("\n");

    printf("(%.2fx) + ", A[2][0]);
    printf("(%.2fy) + ", A[2][1]);
    printf("(%.2fz) = ", A[2][2]);
    printf("(%.2fd)", A[2][3]);
    printf("\n\n\n");
}

inline struct vector solveLinEqu(double A[3][4]) {
    double factor;
    double temp;
    struct vector res = {0, 0, 0};

    int p = 0;
    if (fabs(A[1][0]) > fabs(A[p][0])) {p = 1;}
    if (fabs(A[2][0]) > fabs(A[p][0])) {p = 2;}

    for (int i = 0; i < 4; i++) {
        temp = A[0][i];
        A[0][i] = A[p][i];
        A[p][i] = temp;
    }

    factor = A[1][0] / A[0][0];
    A[1][0] = 0;
    A[1][1] -= factor * A[0][1];
    A[1][2] -= factor * A[0][2];
    A[1][3] -= factor * A[0][3];

    factor = A[2][0] / A[0][0];
    A[2][0] = 0;
    A[2][1] -= factor * A[0][1];
    A[2][2] -= factor * A[0][2];
    A[2][3] -= factor * A[0][3];

    if (fabs(A[2][1]) > fabs(A[1][1])) {
        for (int i = 1; i < 4; i++) {
            temp = A[1][i];
            A[1][i] = A[2][i];
            A[2][i] = temp;
        }
    }

    factor = A[2][1] / A[1][1];
    A[2][1] = 0;
    A[2][2] -= factor * A[1][2];
    A[2][3] -= factor * A[1][3];

    res.z = A[2][3] / A[2][2];
    res.y = (A[1][3] - (A[1][2] * res.z)) / A[1][1];
    res.x = (A[0][3] - (A[0][1] * res.y) - (A[0][2] * res.z)) / A[0][0];

    return res;
}

inline struct vector normalizedVector(struct vector V) {
    struct vector solution;
    double l = length(V);

    if (l <= 1e-12) {
        return {1e-9, 1e-9, 1e-9};
    }

    solution.x = V.x / l;
    solution.y = V.y / l;
    solution.z = V.z / l;

    return solution;
}

inline struct vector matrixMultiply(struct Matrix3x3 m, struct vector v) {
    vector solution;
    solution.x = m.row1.x * v.x + m.row1.y * v.y + m.row1.z * v.z;
    solution.y = m.row2.x * v.x + m.row2.y * v.y + m.row2.z * v.z;
    solution.z = m.row3.x * v.x + m.row3.y * v.y + m.row3.z * v.z;
    return solution;
}

#endif // VECTOR_MATH_H
