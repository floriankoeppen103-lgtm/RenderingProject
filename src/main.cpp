#include <raylib.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <vector>
#include <chrono>
#include <string>
#include <iostream>
#include "header/settings.h"
#include "header/types.h"
#include "header/thread_pool.h"
#include "header/vector_math.h"
#include "header/ray_intersection.h"
#include "header/rendering.h"
#include "header/build_world.h"
#include "header/world_save.h"
#include "header/world_load.h"
#include "header/block_data.h"

// All constexpr settings are in header/settings.h
// All block/material definitions are in header/block_data.h

enum GAMEMODE { SURVIVAL=0, CREATIVE=1, ADVENTURE=2, SPECTATOR=3 };

static void applyGamemodeFlags(int gm, bool& exertGravity, bool& noClip, bool& blockPlacementRights, bool& blockBreakingRights) {
    exertGravity         = (gm == SURVIVAL || gm == ADVENTURE);
    noClip               = (gm == SPECTATOR);
    blockPlacementRights = (gm != ADVENTURE);
    blockBreakingRights  = (gm != ADVENTURE);
}

int main(){
    bool grounded = false;
    bool forceDistanceAssignment = false;
    bool startCounting = true;
    double time = 0.0f;
    double deltaTime    = 1.0f / (double)FPS;
    int    WindowHeight = manualWindowHeight;
    int    WindowWidth  = manualWindowWidth;

    // FOV settings -- mutable because sprint changes them at runtime
    double FOVDepth            = 1.0f;
    double FOVWidth            = 1.5f;
    double FOVHeight           = 1.0f;
    double sprintFOVMultiplier = 1.3f;

    bool blockAllMovementInputs = false;
    int gamemode = startupGamemode;
    bool changeMadeToGamemode = false;
    bool noClip, exertGravity, blockPlacementRights, blockBreakingRights;
    applyGamemodeFlags(gamemode, exertGravity, noClip, blockPlacementRights, blockBreakingRights);

    char loadedWorldName[64] = "world_data";
    int worldSaveCycleIndex = 0;
    double forwardSpeed = 0.0f;
    double rightSpeed   = 0.0f;
    double upSpeed      = 0.0f;

    static struct face* triangle = new face[worldWidth*worldHeight*worldDepth*12*targetResolution*targetResolution]();
    static int B[worldHeight][worldDepth][worldWidth] = {};

    // Per-triangle projection results, filled in parallel before drawing.
    struct RenderResult {
        struct sextupleVector sv;
        bool visible1;
        bool visible2;
    };
    static struct RenderResult* renderResults = new RenderResult[worldWidth*worldHeight*worldDepth*12*targetResolution*targetResolution]();

    // Persistent worker pool for the per-frame projection/culling pass.
    int projectionThreadCount = (int)std::thread::hardware_concurrency();
    if(projectionThreadCount <= 0) projectionThreadCount = 1;
    if(projectionThreadCount > 8) projectionThreadCount = 8;
    static ThreadPool renderPool(projectionThreadCount);

    int blockCount = 0;
    int populatedTriangleCount = 0;
    {
        char prebuiltPath[512];
        snprintf(prebuiltPath, sizeof(prebuiltPath), "%sworlds/prebuilt/world_data.bin", GetApplicationDirectory());
        int result = loadWorld(prebuiltPath, B, triangle, mat, matCount, blockCount);
        if(result >= 0) { populatedTriangleCount = result; forceDistanceAssignment = true; }
    }

    if(getScreenDimensions) SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_MAXIMIZED);

    InitWindow(WindowWidth, WindowHeight, "basic window");
    if(getScreenDimensions) {
        MaximizeWindow();
        WindowWidth  = GetScreenWidth();
        WindowHeight = GetScreenHeight();
        FOVWidth = float(WindowWidth)/float(WindowHeight);
    }

    SetTargetFPS(FPS);
    if(turnByMouse) DisableCursor();

    double spawnPoint[3] = {startPos[0], startPos[1], startPos[2]};
    struct vector C   = {spawnPoint[0], spawnPoint[1], spawnPoint[2]};
    struct vector Cf  = {1.0f, 0.0f, -0.5f};
    double trueWSpeedMax = forwardSpeed;
    double realFOVHeight = FOVDepth;
    double realFOVWidth  = FOVHeight;
    double frameTheta;
    int deathBuffer = 0;

    struct block hotbar[] = {bedrock02, dirt02, grass02, oak02, leaves02, chest02, cobble02, dirt02};
    int hotbarSize = sizeof(hotbar) / sizeof(hotbar[0]);
    int selectedHotbarIndex = 0;
    struct block selectedMaterial = hotbar[selectedHotbarIndex];

    // per-frame section timings in milliseconds — displayed when showBenchmark is true
    double timeInput = 0.0, timePhysics = 0.0, timeDistances = 0.0, timeSort = 0.0, timeProjection = 0.0, timeRender = 0.0, timeWait = 0.0;
    double _bt = 0.0;


    auto handleBlockBreaking = [&]() {
        intVector blockToDestroy = {0};
        double upTilNowClosestT = 1000.0;
        for(int j = 0; j < populatedTriangleCount; j++) {
            double t = mollerTromboree(C, Cf, triangle[j]);
            if(t < 0.01) continue;
            if(t < upTilNowClosestT) {
                upTilNowClosestT = t;
                blockToDestroy = triangle[j].associatedBlockCoordinates;
            }
        }
        // found what block to destroy, coords stored in blockTorDestroy
        if(upTilNowClosestT < blockReach) {
            // now destroy it!
            // set B to 0
            B[blockToDestroy.z][blockToDestroy.y][blockToDestroy.x] = 0;
            // compact out the triangles belonging to the destroyed block and its 6 neighbors,
            // keeping the active range [0, populatedTriangleCount) contiguous
            int writeIndex = 0;
            for(int i = 0; i < populatedTriangleCount; i++) {
                struct intVector c = triangle[i].associatedBlockCoordinates;
                bool belongsToAffectedBlock =
                    (c.x == blockToDestroy.x     && c.y == blockToDestroy.y     && c.z == blockToDestroy.z)     ||
                    (c.x == blockToDestroy.x + 1 && c.y == blockToDestroy.y     && c.z == blockToDestroy.z)     ||
                    (c.x == blockToDestroy.x - 1 && c.y == blockToDestroy.y     && c.z == blockToDestroy.z)     ||
                    (c.x == blockToDestroy.x     && c.y == blockToDestroy.y + 1 && c.z == blockToDestroy.z)     ||
                    (c.x == blockToDestroy.x     && c.y == blockToDestroy.y - 1 && c.z == blockToDestroy.z)     ||
                    (c.x == blockToDestroy.x     && c.y == blockToDestroy.y     && c.z == blockToDestroy.z + 1) ||
                    (c.x == blockToDestroy.x     && c.y == blockToDestroy.y     && c.z == blockToDestroy.z - 1);

                if(belongsToAffectedBlock) continue;

                if(writeIndex != i) triangle[writeIndex] = triangle[i];
                writeIndex++;
            }
            for(int i = writeIndex; i < populatedTriangleCount; i++) triangle[i] = {0};
            populatedTriangleCount = writeIndex;

            blockCount--;
            // regenerate the triangles for neighboring blocks, now that B[blockToDestroy] is transparent
            if((blockToDestroy.x + 1 < worldWidth) && (B[blockToDestroy.z][blockToDestroy.y][blockToDestroy.x + 1] != 0)){
                populatedTriangleCount += buildTrianglesForBlock(triangle, B, populatedTriangleCount,
                    blockToDestroy.x + 1, blockToDestroy.y, blockToDestroy.z, getMaterial(mat, matCount, B[blockToDestroy.z][blockToDestroy.y][blockToDestroy.x + 1]),
                    mat, matCount, targetResolution);
            }

            if((blockToDestroy.x - 1 >= 0) && (B[blockToDestroy.z][blockToDestroy.y][blockToDestroy.x - 1] != 0)){
                populatedTriangleCount += buildTrianglesForBlock(triangle, B, populatedTriangleCount,
                    blockToDestroy.x - 1, blockToDestroy.y, blockToDestroy.z, getMaterial(mat, matCount, B[blockToDestroy.z][blockToDestroy.y][blockToDestroy.x - 1]),
                    mat, matCount, targetResolution);
            }

            if((blockToDestroy.y + 1 < worldDepth) && (B[blockToDestroy.z][blockToDestroy.y + 1][blockToDestroy.x] != 0)){
                populatedTriangleCount += buildTrianglesForBlock(triangle, B, populatedTriangleCount,
                    blockToDestroy.x, blockToDestroy.y + 1, blockToDestroy.z, getMaterial(mat, matCount, B[blockToDestroy.z][blockToDestroy.y + 1][blockToDestroy.x]),
                    mat, matCount, targetResolution);
            }

            if((blockToDestroy.y - 1 >= 0) && (B[blockToDestroy.z][blockToDestroy.y - 1][blockToDestroy.x] != 0)){
                populatedTriangleCount += buildTrianglesForBlock(triangle, B, populatedTriangleCount,
                    blockToDestroy.x, blockToDestroy.y - 1, blockToDestroy.z, getMaterial(mat, matCount, B[blockToDestroy.z][blockToDestroy.y - 1][blockToDestroy.x]),
                    mat, matCount, targetResolution);
            }

            if((blockToDestroy.z + 1 < worldHeight) && (B[blockToDestroy.z + 1][blockToDestroy.y][blockToDestroy.x] != 0)){
                populatedTriangleCount += buildTrianglesForBlock(triangle, B, populatedTriangleCount,
                    blockToDestroy.x, blockToDestroy.y, blockToDestroy.z + 1, getMaterial(mat, matCount, B[blockToDestroy.z + 1][blockToDestroy.y][blockToDestroy.x]),
                    mat, matCount, targetResolution);
            }

            if((blockToDestroy.z - 1 >= 0) && (B[blockToDestroy.z - 1][blockToDestroy.y][blockToDestroy.x] != 0)){
                populatedTriangleCount += buildTrianglesForBlock(triangle, B, populatedTriangleCount,
                    blockToDestroy.x, blockToDestroy.y, blockToDestroy.z - 1, getMaterial(mat, matCount, B[blockToDestroy.z - 1][blockToDestroy.y][blockToDestroy.x]),
                    mat, matCount, targetResolution);
            }

            for(int i = 0; i < populatedTriangleCount; i++) {
                double dx = C.x - triangle[i].Center.x, dy = C.y - triangle[i].Center.y, dz = C.z - triangle[i].Center.z;
                triangle[i].distance = dx*dx + dy*dy + dz*dz;
            }
            sortTrianglesByDistance(triangle, populatedTriangleCount, renderPool);

            forceDistanceAssignment = true;

        }
    };

    auto handleBlockPlacing = [&]() {
        // finding the right triangle
        struct vector triangleCenterLocation = {-1.0f, -1.0f, -1.0f};
        struct intVector blockLocation = {-1, -1, -1};
        double upTilNowClosestT = 1000.0f;
        for(int j = 0; j < populatedTriangleCount; j++) {
            double t = mollerTromboree(C, Cf, triangle[j]);
            if(t < 0.01f) continue;
            if(t < upTilNowClosestT) {
                upTilNowClosestT = t;
                blockLocation = triangle[j].associatedBlockCoordinates;
                triangleCenterLocation = triangle[j].Center;
            }
        }
        if(blockLocation.x == -1 || upTilNowClosestT >= blockReach) return;
        // finding the right block
        struct intVector opposingBlock = {-1, -1, -1};
        double faceX = triangleCenterLocation.x - double(blockLocation.x);
        double faceY = triangleCenterLocation.y - double(blockLocation.y);
        double faceZ = triangleCenterLocation.z - double(blockLocation.z);
        if(faceX == 0.0f) opposingBlock = {blockLocation.x - 1, blockLocation.y,     blockLocation.z    };
        if(faceX == 1.0f) opposingBlock = {blockLocation.x + 1, blockLocation.y,     blockLocation.z    };
        if(faceY == 1.0f) opposingBlock = {blockLocation.x,     blockLocation.y + 1, blockLocation.z    };
        if(faceY == 0.0f) opposingBlock = {blockLocation.x,     blockLocation.y - 1, blockLocation.z    };
        if(faceZ == 0.0f) opposingBlock = {blockLocation.x,     blockLocation.y,     blockLocation.z - 1};
        if(faceZ == 1.0f) opposingBlock = {blockLocation.x,     blockLocation.y,     blockLocation.z + 1};

        if(opposingBlock.x < 0 || opposingBlock.x >= worldWidth  ||
           opposingBlock.y < 0 || opposingBlock.y >= worldDepth  ||
           opposingBlock.z < 0 || opposingBlock.z >= worldHeight) {
            printf("Block placement out of bounds: %d, %d, %d\n", opposingBlock.x, opposingBlock.y, opposingBlock.z);
            return;
        }

        // placing the block
        B[opposingBlock.z][opposingBlock.y][opposingBlock.x] = selectedMaterial.ID;
        struct block placeMat = selectedMaterial;
        {int highestRes = 0;
        for(int mi = 0; mi < (int)(matCount); mi++) {
            if(mat[mi].ID == selectedMaterial.ID && mat[mi].PixelResolution > highestRes && mat[mi].PixelResolution <= targetResolution)
                highestRes = mat[mi].PixelResolution;
        }
        for(int mi = 0; mi < (int)(matCount); mi++) {
            if(mat[mi].ID == selectedMaterial.ID && mat[mi].PixelResolution == highestRes)
                placeMat = mat[mi];
        }}
        int added = buildTrianglesForBlock(triangle, B, populatedTriangleCount, opposingBlock.x, opposingBlock.y, opposingBlock.z, placeMat, mat, matCount, targetResolution);
        populatedTriangleCount += added;
        
        for(int i = 0; i < populatedTriangleCount; i++) {
                double dx = C.x - triangle[i].Center.x, dy = C.y - triangle[i].Center.y, dz = C.z - triangle[i].Center.z;
                triangle[i].distance = dx*dx + dy*dy + dz*dz;
            }
            sortTrianglesByDistance(triangle, populatedTriangleCount, renderPool);

        blockCount++;
        forceDistanceAssignment = true;

        // destroy the stuff
        int writeIndex = 0;
        for(int i = 0; i < populatedTriangleCount; i++) {
            struct intVector c = triangle[i].associatedBlockCoordinates;
            bool belongsToAffectedBlock =
                (c.x == blockLocation.x     && c.y == blockLocation.y     && c.z == blockLocation.z);

            if(belongsToAffectedBlock) continue;

            if(writeIndex != i) triangle[writeIndex] = triangle[i];
            writeIndex++;
        }

        for(int i = writeIndex; i < populatedTriangleCount; i++) triangle[i] = {0};
        
        populatedTriangleCount = writeIndex;    
        
        populatedTriangleCount += buildTrianglesForBlock(triangle, B, populatedTriangleCount,
                    blockLocation.x, blockLocation.y, blockLocation.z, getMaterial(mat, matCount, B[blockLocation.z][blockLocation.y][blockLocation.x]),
                    mat, matCount, targetResolution);
    };

    auto handleCameraRotation = [&]() {
        // Horizontal (keyboard)
        bool turnRight = IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_L);
        bool turnLeft  = IsKeyDown(KEY_LEFT)  || IsKeyDown(KEY_J);
        if(turnRight && !turnLeft) {
            Cf = normalizedVector(matrixMultiply({{cos(frameTheta),-sin(frameTheta),0},{sin(frameTheta),cos(frameTheta),0},{0,0,1}}, Cf));
        } else if(turnLeft && !turnRight) {
            Cf = normalizedVector(matrixMultiply({{cos(-frameTheta),-sin(-frameTheta),0},{sin(-frameTheta),cos(-frameTheta),0},{0,0,1}}, Cf));
        }
        // Vertical (keyboard)
        bool lookUp   = IsKeyDown(KEY_UP)   || IsKeyDown(KEY_I);
        bool lookDown = IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_K);
        if(lookUp && !lookDown) {
            Cf = normalizedVector({Cf.x, Cf.y, Cf.z + upTa * deltaTime});
        } else if(lookDown && !lookUp) {
            Cf = normalizedVector({Cf.x, Cf.y, Cf.z - upTa * deltaTime});
        }
        // Mouse look
        if(turnByMouse) {
            Vector2 md = GetMouseDelta();
            if(md.x != 0.0f) {
                double angle = (double)md.x * mouseSensitivity;
                Cf = normalizedVector(matrixMultiply({{cos(angle),sin(angle),0},{-sin(angle),cos(angle),0},{0,0,1}}, Cf));
            }
            if(md.y != 0.0f) {
                double angle = -(double)md.y * mouseVerticalSensitivity;
                struct vector Cr_m = normalizedVector({Cf.y, -Cf.x, 0.0});
                double ux = Cr_m.x, uy = Cr_m.y;
                double c = cos(angle), s = sin(angle), ic = 1.0 - c;
                Cf = normalizedVector(matrixMultiply({{c+ux*ux*ic, ux*uy*ic, uy*s},{uy*ux*ic, c+uy*uy*ic, -ux*s},{-uy*s, ux*s, c}}, Cf));
            }
        }
    };

    auto drawHUD = [&]() {
        if(showCrosshair) {
            DrawRectangle(WindowWidth/2 - 4,  WindowHeight/2 - 25, 8,  50, {50,50,50,125});
            DrawRectangle(WindowWidth/2 - 25, WindowHeight/2 - 4,  50, 8,  {50,50,50,125});
        }
        if(showCoordinates && renderOverlay)
            DrawText(TextFormat("POS    X : %.2f   Y : %.2f Z :   %.2f", C.x, C.y, C.z), 10, 10, 30, BLACK);
        if(showCamDirection && renderOverlay)
            DrawText(TextFormat("DIR     X : %.2f   Y : %.2f Z :   %.2f", Cf.x, Cf.y, Cf.z), 10, 40, 30, BLACK);
        if(showSpeedVectors && renderOverlay)
            DrawText(TextFormat("Forward Speed: %.2f   Right Speed: %.2f    Upward Speed:   %.2f", forwardSpeed, rightSpeed, upSpeed), 10, 70, 30, BLACK);
        if(showTriangleCount && renderOverlay)
            DrawText(TextFormat("Triangles: %d", populatedTriangleCount), GetScreenWidth()-300, 40, 30, BLACK);
        if(showFPS && renderOverlay)
            DrawText(TextFormat("FPS: %.2f", 1/deltaTime), GetScreenWidth()-300, 70, 30, BLACK);
        if(showBlockCount && renderOverlay)
            DrawText(TextFormat("The Current amound of Blocks is: %d", blockCount), 10, 100, 30, BLACK);
        if(showLoadedWorldName && renderOverlay && allowWorldSaveLoad)
            DrawText(TextFormat("World: %s", loadedWorldName), 10, 130, 30, BLACK);
        if(showGameMode && renderOverlay) {
            if(gamemode == SURVIVAL)  DrawText("Gamemode: Survival",  10, 160, 30, BLACK);
            if(gamemode == CREATIVE)  DrawText("Gamemode: Creative",  10, 160, 30, BLACK);
            if(gamemode == ADVENTURE) DrawText("Gamemode: Adventure", 10, 160, 30, BLACK);
            if(gamemode == SPECTATOR) DrawText("Gamemode: Spectator", 10, 160, 30, BLACK);
        }
        if(showBenchmark && renderOverlay)
            DrawText(TextFormat("Input: %.2fms   Physics: %.2fms   Dist: %.2fms   Sort: %.2fms   Proj: %.2fms   Render: %.2fms   Wait: %.2fms",
                                timeInput, timePhysics, timeDistances, timeSort, timeProjection, timeRender, timeWait),
                     10, 192, 20, BLACK);
        if(!renderOverlay) {
            DrawText("2x2 Parkour", 30, 30, 60, WHITE);
            DrawText(TextFormat("Time: %.2fs", time), 20, 130, 60, WHITE);
        }
        if(deathBuffer > 0) { DrawText("YOU DIED", WindowWidth/2-200, WindowHeight/2, 100, RED); deathBuffer--; }
        if(startCounting) time += GetFrameTime();

        // Hotbar
        //const char* hotbarNames[] = {"bedrock", "dirt", "grass", "oak", "leaves", "chest", "cobblestone"};
        int hotbarX = WindowWidth/2 - (hotbarSize*70)/2;
        int hotbarY = WindowHeight - 90;
        for(int i = 0; i < hotbarSize; i++) {
            Color borderColor = (i == selectedHotbarIndex) ? YELLOW : DARKGRAY;
            int x = hotbarX + i*70;
            DrawRectangle(x, hotbarY, 60, 60, {50,50,50,200});
            for(int px = 0; px < 2; px++) {
                for(int py = 0; py < 2; py++) {
                    int ci = py*2 + px;
                    Color col = hotbar[i].topColor[ci];
                    if(col.a < 10) col = hotbar[i].frontColor[ci];
                    DrawRectangle(x + px*30, hotbarY + py*30, 30, 30, col);
                }
            }
            DrawRectangleLines(x, hotbarY, 60, 60, borderColor);
            DrawText(hotbar[i].displayName, x+2, hotbarY+44, 11, WHITE);
        }
        if(allowWorldSaveLoad)
            DrawText("Scroll to cycle  |  F1 save  |  F2 load next", WindowWidth/2 - 195, WindowHeight - 30, 18, WHITE);
        else
            DrawText("Scroll to cycle blocks", WindowWidth/2 - 90, WindowHeight - 30, 18, WHITE);
    };

    auto applyCollisionAndMove = [&]() {
        struct vector CmoveStraight = normalizedVector({Cf.x, Cf.y, 0});
        struct vector Cr_           = {Cf.y, -1 * Cf.x, 0};
        struct vector CmoveRight    = normalizedVector(Cr_);

        double dx = generalSpeedMultiplier * deltaTime * (forwardSpeed * CmoveStraight.x + rightSpeed * CmoveRight.x);
        double dy = generalSpeedMultiplier * deltaTime * (forwardSpeed * CmoveStraight.y + rightSpeed * CmoveRight.y);
        double dz = generalSpeedMultiplier * upSpeed * deltaTime;

        bool XDirectionBlocked = false, YDirectionBlocked = false, upwardsBlocked = false;

        // Hitbox corners — world-space coordinates at feet/torso/head levels
        double ZFeet  = C.z - playerEyeHeight;
        double ZTorso = C.z - playerEyeHeight + (playerHitboxHeight/2);
        double ZHead  = C.z - playerEyeHeight + (playerHitboxHeight/1);
        double ZFeetAfterMove = ZFeet + dz, ZHeadAfterMove = ZHead + dz;
        double XRight = C.x + PlayerHitboxBaseLength, XLeft = C.x - PlayerHitboxBaseLength;
        double XRightAfterMove = XRight + dx, XLeftAfterMove = XLeft + dx;
        double YFront = C.y + PlayerHitboxBaseLength, YBack = C.y - PlayerHitboxBaseLength;
        double YFrontAfterMove = YFront + dy, YBackAfterMove = YBack + dy;

        int blockZFeet  = (int)floor(ZFeet),  blockZTorso = (int)floor(ZTorso), blockZHead  = (int)floor(ZHead);
        int blockZFeetAfterMove = (int)floor(ZFeetAfterMove), blockZHeadAfterMove = (int)floor(ZHeadAfterMove);
        int blockXRight = (int)floor(XRight), blockXLeft = (int)floor(XLeft);
        int blockXRightAfterMove = (int)floor(XRightAfterMove), blockXLeftAfterMove = (int)floor(XLeftAfterMove);
        int blockYFront = (int)floor(YFront), blockYBack = (int)floor(YBack);
        int blockYFrontAfterMove = (int)floor(YFrontAfterMove), blockYBackAfterMove = (int)floor(YBackAfterMove);

        bool xCurrentInBounds   = blockXRight >= 0 && blockXRight < worldWidth  && blockXLeft  >= 0 && blockXLeft  < worldWidth;
        bool yCurrentInBounds   = blockYFront >= 0 && blockYFront < worldDepth  && blockYBack  >= 0 && blockYBack  < worldDepth;
        bool xAfterMoveInBounds = blockXRightAfterMove >= 0 && blockXRightAfterMove < worldWidth  && blockXLeftAfterMove >= 0 && blockXLeftAfterMove < worldWidth;
        bool yAfterMoveInBounds = blockYFrontAfterMove >= 0 && blockYFrontAfterMove < worldDepth && blockYBackAfterMove  >= 0 && blockYBackAfterMove  < worldDepth;
        bool zHitboxInBounds    = blockZFeet >= 0 && blockZFeet < worldHeight && blockZTorso >= 0 && blockZTorso < worldHeight && blockZHead >= 0 && blockZHead < worldHeight;

        if(gamemode != SPECTATOR) {
            bool stuck = (xCurrentInBounds && yCurrentInBounds && zHitboxInBounds) && (
                B[blockZFeet ][blockYFront][blockXRight] || B[blockZFeet ][blockYFront][blockXLeft] ||
                B[blockZFeet ][blockYBack ][blockXRight] || B[blockZFeet ][blockYBack ][blockXLeft] ||
                B[blockZTorso][blockYFront][blockXRight] || B[blockZTorso][blockYFront][blockXLeft] ||
                B[blockZTorso][blockYBack ][blockXRight] || B[blockZTorso][blockYBack ][blockXLeft] ||
                B[blockZHead ][blockYFront][blockXRight] || B[blockZHead ][blockYFront][blockXLeft] ||
                B[blockZHead ][blockYBack ][blockXRight] || B[blockZHead ][blockYBack ][blockXLeft]);
            if(gamemode != CREATIVE) exertGravity = !stuck;
            noClip = stuck;
        }

        if(!noClip && zHitboxInBounds && yCurrentInBounds && xAfterMoveInBounds)
            XDirectionBlocked =
                B[blockZFeet ][blockYFront][blockXRightAfterMove] || B[blockZFeet ][blockYBack][blockXRightAfterMove] ||
                B[blockZFeet ][blockYFront][blockXLeftAfterMove]  || B[blockZFeet ][blockYBack][blockXLeftAfterMove]  ||
                B[blockZTorso][blockYFront][blockXRightAfterMove] || B[blockZTorso][blockYBack][blockXRightAfterMove] ||
                B[blockZTorso][blockYFront][blockXLeftAfterMove]  || B[blockZTorso][blockYBack][blockXLeftAfterMove]  ||
                B[blockZHead ][blockYFront][blockXRightAfterMove] || B[blockZHead ][blockYBack][blockXRightAfterMove] ||
                B[blockZHead ][blockYFront][blockXLeftAfterMove]  || B[blockZHead ][blockYBack][blockXLeftAfterMove];

        if(!noClip && zHitboxInBounds && xCurrentInBounds && yAfterMoveInBounds)
            YDirectionBlocked =
                B[blockZFeet ][blockYFrontAfterMove][blockXRight] || B[blockZFeet ][blockYFrontAfterMove][blockXLeft] ||
                B[blockZFeet ][blockYBackAfterMove ][blockXRight] || B[blockZFeet ][blockYBackAfterMove ][blockXLeft] ||
                B[blockZTorso][blockYFrontAfterMove][blockXRight] || B[blockZTorso][blockYFrontAfterMove][blockXLeft] ||
                B[blockZTorso][blockYBackAfterMove ][blockXRight] || B[blockZTorso][blockYBackAfterMove ][blockXLeft] ||
                B[blockZHead ][blockYFrontAfterMove][blockXRight] || B[blockZHead ][blockYFrontAfterMove][blockXLeft] ||
                B[blockZHead ][blockYBackAfterMove ][blockXRight] || B[blockZHead ][blockYBackAfterMove ][blockXLeft];

        // additional check diagonally :) no more bugging in da wall
        if(!noClip && dx != 0.0 && dy != 0.0 && zHitboxInBounds && xAfterMoveInBounds && yAfterMoveInBounds) {
            bool DiagonalBlocked =
                B[blockZFeet ][blockYFrontAfterMove][blockXRightAfterMove] || B[blockZFeet ][blockYFrontAfterMove][blockXLeftAfterMove] ||
                B[blockZFeet ][blockYBackAfterMove ][blockXRightAfterMove] || B[blockZFeet ][blockYBackAfterMove ][blockXLeftAfterMove] ||
                B[blockZTorso][blockYFrontAfterMove][blockXRightAfterMove] || B[blockZTorso][blockYFrontAfterMove][blockXLeftAfterMove] ||
                B[blockZTorso][blockYBackAfterMove ][blockXRightAfterMove] || B[blockZTorso][blockYBackAfterMove ][blockXLeftAfterMove] ||
                B[blockZHead ][blockYFrontAfterMove][blockXRightAfterMove] || B[blockZHead ][blockYFrontAfterMove][blockXLeftAfterMove] ||
                B[blockZHead ][blockYBackAfterMove ][blockXRightAfterMove] || B[blockZHead ][blockYBackAfterMove ][blockXLeftAfterMove];
            if(DiagonalBlocked) {
                // Can't go diagonally — let the player slide along whichever single
                // axis is actually open, and block the other one outright (even if
                // it would also be individually fine on its own).
                if(!XDirectionBlocked) {
                    YDirectionBlocked = true;
                } else if(!YDirectionBlocked) {
                    XDirectionBlocked = true;
                }
            }
        }


        if(!noClip && xCurrentInBounds && yCurrentInBounds) {
            if(blockZFeetAfterMove >= 0 && blockZFeetAfterMove < worldHeight)
                upwardsBlocked =
                    B[blockZFeetAfterMove][blockYFront][blockXRight] || B[blockZFeetAfterMove][blockYFront][blockXLeft] ||
                    B[blockZFeetAfterMove][blockYBack ][blockXRight] || B[blockZFeetAfterMove][blockYBack ][blockXLeft];
            if(blockZHeadAfterMove >= 0 && blockZHeadAfterMove < worldHeight)
                upwardsBlocked |=
                    B[blockZHeadAfterMove][blockYFront][blockXRight] || B[blockZHeadAfterMove][blockYFront][blockXLeft] ||
                    B[blockZHeadAfterMove][blockYBack ][blockXRight] || B[blockZHeadAfterMove][blockYBack ][blockXLeft];
        }

        if(XDirectionBlocked) dx = 0;
        if(YDirectionBlocked) dy = 0;
        if(upwardsBlocked && dz <= 0.0f) {
            dz = (blockZFeetAfterMove + 1) + playerEyeHeight + 0.001 - C.z;
            upSpeed = 0.0f;
            grounded = true;
        } else { grounded = false; }
        if(upwardsBlocked && dz > 0.0f) { dz = 0.0f; upSpeed = 0.0f; }

        C.x += dx; C.y += dy; C.z += dz;
    };


    // Command thread — reads lines from stdin, hands them to the main thread
    std::mutex        commandMutex;
    std::string       commandInput;
    std::atomic<bool> freshCommandSent{false};
    std::atomic<bool> commandReceived{false};

    std::thread commandThread([&]() {
        std::string line;
        while(std::getline(std::cin, line)) {
            {
                std::lock_guard<std::mutex> lock(commandMutex);
                commandInput = line;
            }
            freshCommandSent = true;
            while(!commandReceived.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            commandReceived  = false;
            freshCommandSent = false;
        }
    });
    commandThread.detach();


    while(!WindowShouldClose()){
        deltaTime  = GetFrameTime();
        frameTheta = Theta * deltaTime;
        _bt = GetTime();

        // Gamemode flag sync
        if(changeMadeToGamemode) {
            applyGamemodeFlags(gamemode, exertGravity, noClip, blockPlacementRights, blockBreakingRights);
            changeMadeToGamemode = false;
        }

        // Command thread handshake
        std::string commandToBeExecuted;
        if(freshCommandSent.load()) {
            {
                std::lock_guard<std::mutex> lock(commandMutex);
                commandToBeExecuted = commandInput;
            }
            commandReceived = true;
        }
        
        // COMMAND IMPLEMENTATIONS
        bool blockCmdOut = false;
        if(commandToBeExecuted == "67"){allowCommands = !allowCommands;printf("%s", allowCommands ? "Commands activated\n" : "Commands deactivated\n");blockCmdOut=true;}
        if(allowCommands&&(!commandToBeExecuted.empty())&&!blockCmdOut){
            int cx, cy, cz, cid, fx, fy, fz;
            double Px, Py, Pz;
            if(sscanf(commandToBeExecuted.c_str(), "setblock %d %d %d %d", &cx, &cy, &cz, &cid) == 4 && !blockCmdOut) {
                if(cx >= 0 && cx < worldWidth && cy >= 0 && cy < worldDepth && cz >= 0 && cz < worldHeight) {
                    B[cz][cy][cx] = cid;
                    struct block placeMat = mat[0];
                    int highestRes = 0;
                    for(int mi = 0; mi < (int)(matCount); mi++) {
                        if(mat[mi].ID == cid && mat[mi].PixelResolution > highestRes && mat[mi].PixelResolution <= targetResolution)
                            highestRes = mat[mi].PixelResolution;
                    }
                    for(int mi = 0; mi < (int)(matCount); mi++) {
                        if(mat[mi].ID == cid && mat[mi].PixelResolution == highestRes)
                            placeMat = mat[mi];
                    }
                    int added = buildTrianglesForBlock(triangle, B, populatedTriangleCount, cx, cy, cz, placeMat, mat, matCount, targetResolution);
                    populatedTriangleCount += added;
                    blockCount++;
                    forceDistanceAssignment = true;
                    if(echoCommands)printf("The block at %d %d %d was set to %s\n", cx, cy, cz, placeMat.displayName);
                } else {
                    printf("setblock: coordinates out of bounds (%d %d %d)\n", cx, cy, cz);
                }blockCmdOut=true;
            }
            else if(sscanf(commandToBeExecuted.c_str(), "fill %d %d %d %d %d %d %d", &cx, &cy, &cz, &fx, &fy, &fz, &cid) == 7 && !blockCmdOut) {
                struct block placeMat = mat[0];
                int highestRes = 0;
                for(int mi = 0; mi < (int)(matCount); mi++) {
                    if(mat[mi].ID == cid && mat[mi].PixelResolution > highestRes && mat[mi].PixelResolution <= targetResolution)
                        highestRes = mat[mi].PixelResolution;
                }
                for(int mi = 0; mi < (int)(matCount); mi++) {
                    if(mat[mi].ID == cid && mat[mi].PixelResolution == highestRes)
                        placeMat = mat[mi];
                }
                for(int dz = 0; dz < fz; dz++) {
                    for(int dy = 0; dy < fy; dy++) {
                        for(int dx = 0; dx < fx; dx++) {
                            int bx = cx+dx, by = cy+dy, bz = cz+dz;
                            if(bx < 0 || bx >= worldWidth || by < 0 || by >= worldDepth || bz < 0 || bz >= worldHeight) continue;
                            if(B[bz][by][bx] != 0) {
                                B[bz][by][bx] = 0;
                                for(int i = 0; i < populatedTriangleCount; i++) {
                                    if(triangle[i].associatedBlockCoordinates.x == bx &&
                                       triangle[i].associatedBlockCoordinates.y == by &&
                                       triangle[i].associatedBlockCoordinates.z == bz) {
                                        populatedTriangleCount--;
                                        triangle[i] = triangle[populatedTriangleCount];
                                        i--;
                                    }
                                }
                                blockCount--;
                            }
                            B[bz][by][bx] = cid;
                            int added = buildTrianglesForBlock(triangle, B, populatedTriangleCount, bx, by, bz, placeMat, mat, matCount, targetResolution);
                            populatedTriangleCount += added;
                            blockCount++;
                        }
                    }
                }
                if(echoCommands)printf("Successfully filled area with %s\n", placeMat.displayName);
                forceDistanceAssignment = true;
                blockCmdOut=true;
            }
            else if(sscanf(commandToBeExecuted.c_str(), "tp %lf %lf %lf", &Px, &Py, &Pz) == 3 && !blockCmdOut) {
                if(true) {
                    C.x = Px;
                    C.y = Py;
                    C.z = Pz;
                    if(echoCommands)printf("Teleported Player to (%lf %lf %lf)\n", Px, Py, Pz);
                }
                blockCmdOut=true;
            }
            else if(commandToBeExecuted.rfind("loadworld", 0) == 0 && !blockCmdOut) {
                int worldIndex;
                char worldName[512];
                if(sscanf(commandToBeExecuted.c_str(), "loadworld %d", &worldIndex) == 1) {
                    char prebuiltDir[512];
                    snprintf(prebuiltDir, sizeof(prebuiltDir), "%sworlds/prebuilt", GetApplicationDirectory());
                    FilePathList prebuiltFiles = LoadDirectoryFiles(prebuiltDir);
                    if(worldIndex >= 0 && worldIndex < (int)prebuiltFiles.count) {
                        int result = loadWorld(prebuiltFiles.paths[worldIndex], B, triangle, mat, matCount, blockCount);
                        if(result >= 0) { populatedTriangleCount = result; forceDistanceAssignment = true; }
                        if(echoCommands)printf("Successfully loaded the %d-th world from \\worlds\\prebuilt\\:\n%s\n", worldIndex, prebuiltFiles.paths[worldIndex]);
                    } else {
                        printf("loadworld: index %d out of range (found %u worlds in prebuilt/)\n", worldIndex, prebuiltFiles.count);
                    }
                    UnloadDirectoryFiles(prebuiltFiles);
                    // holy damn, no way this works
                }
                else if(sscanf(commandToBeExecuted.c_str(), "loadworld \"%511[^\"]\"", worldName) == 1) {
                    char prebuiltPath[1024];
                    snprintf(prebuiltPath, sizeof(prebuiltPath), "%sworlds/prebuilt/%s", GetApplicationDirectory(), worldName);
                    int result = loadWorld(prebuiltPath, B, triangle, mat, matCount, blockCount);
                    if(result >= 0){
                        populatedTriangleCount = result; forceDistanceAssignment = true;
                        if(echoCommands)printf("Successfully loaded world: %s\n", worldName);
                    } else{
                        if(echoCommands)printf("Couldnt find world \"%s\"\n", worldName);
                    }
                }
                blockCmdOut=true;
            }
            else{
                if(!blockCmdOut)printf("Command couldn't be resolved. Take a look at https://github.com/floriankoeppen103-lgtm/RenderingProject/blob/main/COMMANDOVERVIEW.md\n");
            }
        }
        if(!allowCommands&&!commandToBeExecuted.empty()&&!blockCmdOut) printf("No Commands allowed\n");


        // INPUT
        if(//IsKeyPressed(KEY_Q) || 
        GetMouseWheelMove()>0)
            selectedHotbarIndex = (selectedHotbarIndex - 1 + hotbarSize) % hotbarSize;
        if(//IsKeyPressed(KEY_E) || 
        GetMouseWheelMove()<0)
            selectedHotbarIndex = (selectedHotbarIndex + 1) % hotbarSize;
        selectedMaterial = hotbar[selectedHotbarIndex];

        if(allowWorldSaveLoad && IsKeyPressed(KEY_F1)) saveWorld(B);
        if(Cheats && IsKeyPressed(KEY_F3)) { gamemode = (gamemode+1)%4; changeMadeToGamemode = true; }
        if(allowWorldSaveLoad && IsKeyPressed(KEY_F2)) {
            int newCount = loadNextWorldSave(B, loadedWorldName, sizeof(loadedWorldName), worldSaveCycleIndex, triangle, mat, (int)(matCount), blockCount);
            if(newCount >= 0) { populatedTriangleCount = newCount; forceDistanceAssignment = true; }
        }
        if(blockBreakingRights && (IsKeyPressed(KEY_BACKSPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)))
            handleBlockBreaking();
        if(blockPlacementRights && (IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)))
            handleBlockPlacing();

        timeInput = (GetTime() - _bt) * 1000.0;

        // MOVEMENT & PHYSICS
        _bt = GetTime();

        // Sprinting — FOV multiplier
        bool unlimitedSpeedMode = (gamemode == CREATIVE || gamemode == SPECTATOR);
        if(IsKeyDown(KEY_LEFT_CONTROL) && (!blockAllMovementInputs) && IsKeyDown(KEY_W)) {
            trueWSpeedMax = unlimitedSpeedMode ? 100.0 : (initWSpeedMax + SprintBoost);
            realFOVHeight *= 1.05f; realFOVWidth *= 1.05f;
            if(realFOVHeight > FOVHeight*sprintFOVMultiplier) realFOVHeight = FOVHeight*sprintFOVMultiplier;
            if(realFOVWidth  > FOVWidth *sprintFOVMultiplier) realFOVWidth  = FOVWidth *sprintFOVMultiplier;
        } else {
            trueWSpeedMax = initWSpeedMax;
            realFOVHeight /= 1.1f; realFOVWidth /= 1.1f;
            if(realFOVHeight < FOVHeight) realFOVHeight = FOVHeight;
            if(realFOVWidth  < FOVWidth ) realFOVWidth  = FOVWidth;
        }

        double scaledSlowdownFactor = slowdownFactor * deltaTime * 60.0;
        double scaledSpeedupFactor  = speedupFactor  * deltaTime * 60.0;

        // Forwards & Backwards
        if((IsKeyDown(KEY_W) && IsKeyDown(KEY_S)) || (!IsKeyDown(KEY_W) && !IsKeyDown(KEY_S))) {
            double fwdSlowdown = (fabs(forwardSpeed) > 30.0) ? scaledSlowdownFactor * 2.0 : scaledSlowdownFactor;
            if(forwardSpeed > 0) forwardSpeed -= fwdSlowdown;
            else if(forwardSpeed < 0) forwardSpeed += fwdSlowdown;
            if(fabs(forwardSpeed) <= fwdSlowdown) forwardSpeed = 0.0f;
        } else if(IsKeyDown(KEY_W) && !blockAllMovementInputs) {
            forwardSpeed += scaledSpeedupFactor;
            if(forwardSpeed > trueWSpeedMax) forwardSpeed = trueWSpeedMax;
        } else if(IsKeyDown(KEY_S) && !blockAllMovementInputs) {
            forwardSpeed -= scaledSpeedupFactor;
            if(forwardSpeed < SSpeedMax) forwardSpeed = SSpeedMax;
        }

        // Right & Left
        if((IsKeyDown(KEY_A) && IsKeyDown(KEY_D)) || (!IsKeyDown(KEY_A) && !IsKeyDown(KEY_D))) {
            double rightSlowdown = (fabs(rightSpeed) > 30.0) ? scaledSlowdownFactor * 2.0 : scaledSlowdownFactor;
            if(rightSpeed > 0) rightSpeed -= rightSlowdown;
            else if(rightSpeed < 0) rightSpeed += rightSlowdown;
            if(fabs(rightSpeed) <= rightSlowdown) rightSpeed = 0.0f;
        } else if(IsKeyDown(KEY_D) && !blockAllMovementInputs) {
            rightSpeed += scaledSpeedupFactor;
            if(rightSpeed > DSpeedMax) rightSpeed = DSpeedMax;
        } else if(IsKeyDown(KEY_A) && !blockAllMovementInputs) {
            rightSpeed -= scaledSpeedupFactor;
            if(rightSpeed < ASpeedMax) rightSpeed = ASpeedMax;
        }

        // Up / Down  (flight or gravity + jump)
        if(!exertGravity) {
            if((IsKeyDown(KEY_SPACE) && IsKeyDown(KEY_LEFT_SHIFT)) || (!IsKeyDown(KEY_SPACE) && !IsKeyDown(KEY_LEFT_SHIFT))) {
                double upSlowdown = (fabs(upSpeed) > 30.0) ? scaledSlowdownFactor * 2.0 : scaledSlowdownFactor;
                if(upSpeed > 0.0f) upSpeed -= upSlowdown;
                else if(upSpeed < 0.0f) upSpeed += upSlowdown;
                if(fabs(upSpeed) <= upSlowdown) upSpeed = 0.0f;
            } else if(IsKeyDown(KEY_SPACE) && !blockAllMovementInputs) {
                upSpeed += scaledSpeedupFactor;
                if(upSpeed > SpaceSpeedMax) upSpeed = SpaceSpeedMax;
            } else if(IsKeyDown(KEY_LEFT_SHIFT) && !blockAllMovementInputs) {
                upSpeed -= scaledSpeedupFactor;
                if(upSpeed < ShiftSpeedMax) upSpeed = ShiftSpeedMax;
            }
        } else {
            if(IsKeyDown(KEY_SPACE) && !blockAllMovementInputs && upSpeed == 0.0f && grounded)
                upSpeed = SpaceSpeedMax;
            upSpeed -= scaledSlowdownFactor;
        }

        applyCollisionAndMove();

        // Respawn / win
        if(C.z < -10.0f || IsKeyDown(KEY_R)) {
            C = {spawnPoint[0], spawnPoint[1], spawnPoint[2]};
            Cf = {1.0f, 0.0f, -0.5f};
            upSpeed = 0.0f; deathBuffer = 240; time = 0.0f; startCounting = true;
        }
        
        handleCameraRotation();

        timePhysics = (GetTime() - _bt) * 1000.0;

        // Assign distances — recalculate when moving or when a forced sort was requested.
        _bt = GetTime();
        if(forwardSpeed != 0.0 || rightSpeed != 0.0 || upSpeed != 0.0 || forceDistanceAssignment) {
            for(int i = 0; i < populatedTriangleCount; i++) {
                double dx = C.x - triangle[i].Center.x, dy = C.y - triangle[i].Center.y, dz = C.z - triangle[i].Center.z;
                triangle[i].distance = dx*dx + dy*dy + dz*dz;
            }
        }
        timeDistances = (GetTime() - _bt) * 1000.0;

        // Sort triangles farthest-first for painter's algorithm
        _bt = GetTime();
        sortTrianglesByDistance(triangle, populatedTriangleCount, renderPool);
        forceDistanceAssignment = false;
        timeSort = (GetTime() - _bt) * 1000.0;

        if(generalDebugMode && logTriangleDistances) {
            printf("\n\n\n\n");
            for(int i = 0; i < populatedTriangleCount; i++) printf("Distance: %.2f\n", triangle[i].distance);
        }

        // Project & backface-cull all triangles in parallel, before any GL calls.
        _bt = GetTime();
        renderPool.runParallel(populatedTriangleCount, [&](int start, int end) {
            for(int i = start; i < end; i++) {
                struct sextupleVector sv = GetSolutionVector(triangle[i], WindowWidth, WindowHeight, C, Cf, FOVDepth, realFOVWidth, realFOVHeight);

                struct vector a1 = {sv.x1, sv.y1, 0}, a2 = {sv.x2, sv.y2, 0}, a3 = {sv.x3, sv.y3, 0};
                struct vector edge1a = {a2.x-a1.x, a2.y-a1.y, a2.z-a1.z};
                struct vector edge2a = {a3.x-a1.x, a3.y-a1.y, a3.z-a1.z};
                bool visible1 = crossProduct(edge1a, edge2a).z < 0;

                bool visible2 = false;
                if(sv.x4 != 0.0) {
                    struct vector b1 = {sv.x4, sv.y4, 0}, b2 = {sv.x5, sv.y5, 0}, b3 = {sv.x6, sv.y6, 0};
                    struct vector edge1b = {b2.x-b1.x, b2.y-b1.y, b2.z-b1.z};
                    struct vector edge2b = {b3.x-b1.x, b3.y-b1.y, b3.z-b1.z};
                    visible2 = crossProduct(edge1b, edge2b).z < 0;
                }

                renderResults[i] = {sv, visible1, visible2};
            }
        });
        timeProjection = (GetTime() - _bt) * 1000.0;

        // DRAW
        _bt = GetTime();
        BeginDrawing();
        ClearBackground(skyBackground ? Color{0x77,0xA8,0xFF,0xFF} : WHITE);

        if(drawWorldBorder) {
            const double bW = worldWidth, bD = worldDepth, bH = worldHeight;
            const Color  bcol = {0, 0, 0, 255/8};
            auto drawBorderTri = [&](struct vector p1, struct vector p2, struct vector p3) {
                struct face bf = {}; bf.P1 = p1; bf.P2 = p2; bf.P3 = p3;
                struct sextupleVector sv = GetSolutionVector(bf, WindowWidth, WindowHeight, C, Cf, FOVDepth, realFOVWidth, realFOVHeight);
                DrawTriangleLines({float(sv.x1),float(sv.y1)},{float(sv.x2),float(sv.y2)},{float(sv.x3),float(sv.y3)}, bcol);
                DrawTriangleLines({float(sv.x3),float(sv.y3)},{float(sv.x2),float(sv.y2)},{float(sv.x1),float(sv.y1)}, bcol);
                DrawTriangleLines({float(sv.x4),float(sv.y4)},{float(sv.x5),float(sv.y5)},{float(sv.x6),float(sv.y6)}, bcol);
                DrawTriangleLines({float(sv.x6),float(sv.y6)},{float(sv.x5),float(sv.y5)},{float(sv.x4),float(sv.y4)}, bcol);
            };

            drawBorderTri({0,  0,  0 }, {bW, 0,  0 }, {bW, 0,  0 });  // bottom 1
            drawBorderTri({0,  0,  0 }, {0 , bD, 0 }, {0 , bD, 0 });  // bottom 2
            drawBorderTri({0,  bD, 0 }, {bW, bD, 0 }, {bW, bD, 0 });  // top 1
            drawBorderTri({bW,  0, 0 }, {bW, bD, 0 }, {bW, bD, 0 });  // top 2

            drawBorderTri({0,   0,  0 }, {0 ,  0,   bH}, {0 ,  0,   bH});  // back  (y=0)  1
            drawBorderTri({bW,  0,  0 }, {bW,  0,   bH}, {bW,  0,   bH});  // back  (y=0)  2
            drawBorderTri({0,  bD,  0 }, {0 ,  bD,  bH}, {0 ,  bD,  bH});  // front (y=bD) 1
            drawBorderTri({bW, bD,  0 }, {bW,  bD,  bH}, {bW,  bD,  bH});  // front (y=bD) 2
            
            drawBorderTri({0,  0,  bH }, {bW, 0,  bH}, {bW, 0,  bH});  // bottom 1
            drawBorderTri({0,  0,  bH }, {0 , bD, bH}, {0 , bD, bH});  // bottom 2
            drawBorderTri({0,  bD, bH }, {bW, bD, bH}, {bW, bD, bH});  // top 1
            drawBorderTri({bW,  0, bH }, {bW, bD, bH}, {bW, bD, bH});  // top 2
        }

        for(int i = 0; i < populatedTriangleCount; i++) {
            struct RenderResult& rr = renderResults[i];
            struct Vector2 v1={float(rr.sv.x1),float(rr.sv.y1)}, v2={float(rr.sv.x2),float(rr.sv.y2)}, v3={float(rr.sv.x3),float(rr.sv.y3)};
            struct Vector2 v4={float(rr.sv.x4),float(rr.sv.y4)}, v5={float(rr.sv.x5),float(rr.sv.y5)}, v6={float(rr.sv.x6),float(rr.sv.y6)};

            if(drawSurfaces) {
                if(rr.visible1) DrawTriangle(v1,v2,v3,triangle[i].Colour);
                if(rr.visible2) DrawTriangle(v4,v5,v6,triangle[i].Colour);
            }
            if(drawWireframe) {
                if(rr.visible1) { DrawTriangleLines(v1,v2,v3,BLACK); DrawTriangleLines(v3,v2,v1,BLACK); }
                if(rr.visible2) { DrawTriangleLines(v4,v5,v6,BLACK); DrawTriangleLines(v6,v5,v4,BLACK); }
            }
        }

        drawHUD();

        timeRender = (GetTime() - _bt) * 1000.0;
        _bt = GetTime();
        EndDrawing();
        timeWait = (GetTime() - _bt) * 1000.0;
    }

    CloseWindow();
    return 0;
}