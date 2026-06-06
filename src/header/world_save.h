#ifndef WORLD_SAVE_H
#define WORLD_SAVE_H

#include <cstdio>
#include <raylib.h>
#include "settings.h"

// Saves the current world to worlds/saves/world_dataN.h and world_dataN.bin
// (relative to the executable directory),
// picking the next N not yet present in that folder.
// The .h matches the world_data.h format exactly and can be #included.
// The .bin is a flat int dump used by loadNextWorldSave for fast runtime reloading.
inline void saveWorld(int B[][worldDepth][worldWidth]) {
    // scan for the first index whose .h file does not yet exist
    int nextSaveIndex = 1;
    char headerSavePath[512];
    char binarySavePath[512];
    while(true) {
        snprintf(binarySavePath, sizeof(binarySavePath),
                 "%sworlds/saves/world_data%d.bin", GetApplicationDirectory(), nextSaveIndex);
        FILE* existenceCheck = fopen(binarySavePath, "rb");
        if(!existenceCheck) break;   // index is free
        fclose(existenceCheck);
        nextSaveIndex++;
    }
    snprintf(headerSavePath, sizeof(headerSavePath),
             "%sworlds/saves/world_data%d.h", GetApplicationDirectory(), nextSaveIndex);

    // write the human-readable header file
    FILE* headerFile = fopen(headerSavePath, "w");
    if(headerFile) {
        fprintf(headerFile, "    int B[%d][%d][%d] = {\n", worldHeight, worldDepth, worldWidth);
        for(int layer = 0; layer < worldHeight; layer++) {
            fprintf(headerFile, "        // layer %d\n        {\n", layer);
            for(int row = 0; row < worldDepth; row++) {
                fprintf(headerFile, "            /* ROW%-2d */ {", row);
                for(int col = 0; col < worldWidth; col++) {
                    fprintf(headerFile, "{%d}%s", B[layer][row][col], col < worldWidth-1 ? "," : "");
                }
                fprintf(headerFile, "}%s\n", row < worldDepth-1 ? "," : "");
            }
            fprintf(headerFile, "        }%s\n", layer < worldHeight-1 ? "," : "");
        }
        fprintf(headerFile, "    };\n");
        fclose(headerFile);
    }
    // write the compact binary for fast runtime loading via F2
    FILE* binaryFile = fopen(binarySavePath, "wb");
    if(binaryFile) {
        fwrite(&worldWidth,  sizeof(int), 1, binaryFile);
        fwrite(&worldDepth,  sizeof(int), 1, binaryFile);
        fwrite(&worldHeight, sizeof(int), 1, binaryFile);
        fwrite(B, sizeof(int), worldWidth * worldDepth * worldHeight, binaryFile);
        fclose(binaryFile);
        printf("World saved as world_data%d (.h + .bin)\n", nextSaveIndex);
    }
}

#endif // WORLD_SAVE_H
