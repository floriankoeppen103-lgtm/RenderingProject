#ifndef TYPES_H
#define TYPES_H

#include <raylib.h>

// Vector Operations
struct sextupleVector {
    double x1, y1, x2, y2, x3, y3;
    double x4, y4, x5, y5, x6, y6;
};

struct vector {
    double x, y, z;
};

struct intVector {
    int x, y, z;
};

struct Matrix3x3 {
    struct vector row1, row2, row3;
};

struct face {
    struct vector P1;
    struct vector P2;
    struct vector P3;
    struct vector Center;
    struct Color Colour;
    double distance;
    intVector associatedBlockCoordinates;
};

struct block {
    int ID;
    int PixelResolution;
    Color topColor[4];
    Color bottomColor[4];
    Color frontColor[4];
    Color backColor[4];
    Color leftColor[4];
    Color rightColor[4];
};

#endif // TYPES_H
