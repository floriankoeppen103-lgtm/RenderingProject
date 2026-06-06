#ifndef WORLD_LOAD_H
#define WORLD_LOAD_H

#include <cstdio>
#include <cstring>
#include "settings.h"
#include "types.h"
#include "build_world.h"

// Loads a world binary from binPath into B, rebuilds triangle[], recounts blocks.
// Returns the new populatedTriangleCount, or -1 if the file couldn't be opened.
inline int loadWorld(
    const char* binPath,
    int B[][worldDepth][worldWidth],
    struct face* triangle,
    const struct block* mat,
    int matCount,
    int& blockCount
) {
    FILE* f = fopen(binPath, "rb");
    if(!f) {
        printf("Could not open world file: %s\n", binPath);
        return -1;
    }
    int storedWidth, storedDepth, storedHeight;
    fread(&storedWidth,  sizeof(int), 1, f);
    fread(&storedDepth,  sizeof(int), 1, f);
    fread(&storedHeight, sizeof(int), 1, f);
    if(storedWidth != worldWidth || storedDepth != worldDepth || storedHeight != worldHeight) {
        printf("World '%s': stored dimensions %dx%dx%d don't match current %dx%dx%d -- skipping.\n",
               binPath, storedWidth, storedDepth, storedHeight, worldWidth, worldDepth, worldHeight);
        fclose(f);
        return -1;
    }
    fread(B, sizeof(int), worldWidth * worldDepth * worldHeight, f);
    fclose(f);

    memset(triangle, 0,
           sizeof(struct face) * worldWidth * worldDepth * worldHeight
                               * 12 * targetResolution * targetResolution);
    int count = buildTrianglesFromWorld(
        triangle, B, mat, matCount,
        worldWidth, worldDepth, worldHeight, targetResolution
    );

    blockCount = 0;
    for(int i = 0; i < worldHeight; i++)
        for(int j = 0; j < worldDepth; j++)
            for(int k = 0; k < worldWidth; k++)
                if(B[i][j][k] != 0) blockCount++;

    return count;
}

// Advances worldSaveCycleIndex to the next existing save in src/worlds/saves/,
// calls loadWorld(), and updates loadedWorldName.
// Wraps back to 1 if the next index has no file.
// Returns the new populatedTriangleCount, or -1 if no saves exist at all.
inline int loadNextWorldSave(
    int B[][worldDepth][worldWidth],
    char* loadedWorldName,
    int nameBufferSize,
    int& worldSaveCycleIndex,
    struct face* triangle,
    const struct block* mat,
    int matCount,
    int& blockCount
) {
    char saveBinPath[512];

    // Scan forward from (current+1), then wrap from 1, tolerating gaps in numbering.
    auto tryIndex = [&](int idx) -> bool {
        snprintf(saveBinPath, sizeof(saveBinPath),
                 "%sworlds/saves/world_data%d.bin", GetApplicationDirectory(), idx);
        FILE* f = fopen(saveBinPath, "rb");
        if(!f) return false;
        fclose(f);
        return true;
    };

    int found = -1;
    for(int i = worldSaveCycleIndex + 1; i <= 9999 && found < 0; i++)
        if(tryIndex(i)) found = i;
    for(int i = 1; i <= worldSaveCycleIndex && found < 0; i++)
        if(tryIndex(i)) found = i;

    if(found < 0) {
        printf("No save files found in worlds/saves/ -- press F1 to create one.\n");
        return -1;
    }

    worldSaveCycleIndex = found;
    snprintf(saveBinPath, sizeof(saveBinPath),
             "%sworlds/saves/world_data%d.bin", GetApplicationDirectory(), found);
    snprintf(loadedWorldName, nameBufferSize, "world_data%d", found);

    int newCount = loadWorld(saveBinPath, B, triangle, mat, matCount, blockCount);
    if(newCount >= 0)
        printf("Loaded world_data%d -- %d blocks, %d triangles.\n",
               found, blockCount, newCount);
    return newCount;
}

#endif // WORLD_LOAD_H
