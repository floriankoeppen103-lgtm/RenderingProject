#ifndef SETTINGS_H
#define SETTINGS_H

#include "types.h"

// GAMEPLAY SETTINGS
bool allowCommands            = true;         // allow commands to be run through the terminal
constexpr bool echoCommands             = true;         // echos back to you what command was executed
constexpr double sensMultiplier         = 0.50f;        // multiplies sensitivity by this factor. individual sens can be adjusted in mouse settings 
constexpr bool Cheats                   = true;         // Enables F3 (spectator) and F4 (survival) toggles at runtime
constexpr int startupGamemode          = 1;             // 0 for survival, 1 for creative, 2 for adventure, 3 for spectator
constexpr bool allowWorldSaveLoad       = true;         // Set allowWorldSaveLoad to false to disable F1/F2 entirely and hide all related UI.
constexpr bool showCrosshair            = true;
constexpr double startPos[3]               = {8.0f, 8.0f, 3.5f};       // pk start
//constexpr vector startPos               = {-5.0f, 5.0f, 6.0f};       // creative floating start

// MINECRAFT TYPE SETTINGS
constexpr int    targetResolution       = 2;            // Block texture resolution (1 or 2)
constexpr int    worldWidth             = 200;           // Max rendered world width  (X)
constexpr int    worldDepth             = 100;           // Max rendered world depth  (Y)
constexpr int    worldHeight            = 50;           // Max rendered world height (Z)
constexpr double playerEyeHeight        = 1.6f;         // Camera eye height
constexpr double PlayerHitboxBaseLength = 0.3f;         // Half-side of the square player base
constexpr double playerHitboxHeight     = 1.8f;         // Top of the player's head
constexpr double blockReach             = 12.0f;        // Max distance for block break/place
constexpr bool skyBackground            = true;         // Light blue sky background
constexpr bool drawWireframe            = false;        // Draw triangle wireframe
constexpr bool drawSurfaces             = true;         // Fill triangles with color
constexpr bool drawWorldBorder          = true;         // Draws a thin worldborder
          bool doCulling                = true;         // doesnt draw hidden faces
constexpr bool doTestColors             = false;         // draws each face in a clear color

// OVERLAY SETTINGS
constexpr bool renderOverlay       = true;   // Toggle the whole screen overlay
constexpr bool showBlockCount      = true;   // (o) Show current block count
constexpr bool showTriangleCount   = true;   // (o) Shows you how many triangles are being displayed  
constexpr bool showCoordinates     = true;   // (o) Show player coordinates
constexpr bool showCamDirection    = true;   // (o) Show camera direction vector
constexpr bool showSpeedVectors    = true;   // (o) Show forward/right/up speed
constexpr bool showFPS             = true;   // (o) Show FPS counter
constexpr bool showLoadedWorldName = true;   // (o) Show name of the currently loaded world
constexpr bool showGameMode        = true;   // (o) Show current gamemode (Survival / Creative)
constexpr bool showBenchmark       = true;   // (o) Show per-frame section timings in the overlay

// SETTINGS FOR WINDOW
constexpr bool   getScreenDimensions = true;    // Auto-maximize window on start
constexpr int    FPS                 = 60;      // Target FPS
constexpr int    manualWindowHeight  = 1200;    // Window height when auto-sizing is off
constexpr int    manualWindowWidth   = 1800;    // Window width when auto-sizing is off

// TURNING SETTINGS
constexpr bool   turnByMouse              = true;  // Use mouse to look (locks cursor); false = arrow keys
static    double mouseSensitivity         = 0.00300f * sensMultiplier; // Horizontal mouse sensitivity (radians per pixel)
static    double mouseVerticalSensitivity = 0.00200f * sensMultiplier; // Vertical mouse sensitivity (radians per pixel)
constexpr double Theta = -1.7f;  // Camera turn speed left/right (keyboard)
constexpr double upTa  =  1.0f;  // Camera look speed up/down (keyboard)

// MOVEMENT SETTINGS
constexpr double SprintBoost            =  5.0f;   // Speed boost while sprinting (additive)
constexpr double generalSpeedMultiplier =  0.5f;   // Global speed scale
constexpr double speedupFactor          =  0.80f;  // How quickly you accelerate
constexpr double slowdownFactor         =  0.50f;  // How slippery the ground is
constexpr double initWSpeedMax          =  8.0f;  // Max speed forwards
constexpr double SSpeedMax              = -7.0f;  // Max speed backwards
constexpr double DSpeedMax              =  8.0f;  // Max speed right
constexpr double ASpeedMax              = -8.0f;  // Max speed left
constexpr double SpaceSpeedMax          =  12.0f;  // Max speed upward
constexpr double ShiftSpeedMax          = -10.0f;  // Max speed downward

// DEBUGGING MODE - LOGGING
constexpr bool generalDebugMode      = false;  // Enable debug mode
constexpr bool logCamVectors         = false;  // Log camera vectors each frame
constexpr bool logTriangleDistances  = false;  // Log triangle distances each frame

#endif