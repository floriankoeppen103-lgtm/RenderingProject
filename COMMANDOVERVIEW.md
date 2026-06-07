# QUICK OVERVIEW OF CURRENTLY SUPPORTED COMMANDS - 
## - WILL REMAIN UPDATED THROUGH TIME, CONTAINING THE TEMPLATES, AS WELL AS SIMPLE EXAMPLES FOR EACH COMMAND

## [GENERAL INFO]
### To execute a command, go to the console (command window), type or paste your command, and hit enter. 
### In between commands, you need to tab back into the game!
### Commands cannot be bunched, and only run with the allowCommands setting set to true.

## [TELEPORTING]
### tmp.:   tp [doubleX][doubleY][doubleZ]
### ex.:    tp 10 8 4.5
teleports your Camera to the coordinates X = 10.0f, Y = 8.0f, Z = 4.5f.
if you want to land at a position with your feet like in minecraft, add on another 1.60 to your Z.

## [SETTING SINGLE BLOCKS]
### tmp.:   setblock [int X] [int Y] [int Z] [BlockID]
### ex.:    setblock 0 0 0 1
sets the block at 0 0 0 to Bedrock, since Bedrock has the blockID 1. For finding BlockIDs of certain blocks, check src\header\block_data.h.

## [FILLING A REGION WITH BLOCKS]
### tmp.:   fill [int OriginBlockX][int OriginBlockY][int OriginBlockZ] [int Depth][int Width][int Height] [BlockID]
### ex.1:   fill 50 50 50 1 2 3 0
sets the blocks with 50<=X<=51, 50<=Y<=52, 50<=Z<=53 to air blocks
aka, it goes to block {50, 50, 50}, goes 1 forward, 2 to the side, 3 up, and fills the cubic region in between with air (BlockID 0).
### ex.2:   fill 0 0 0 200 100 0 1
sets the bottom layer of the world to bedrock (BlockID 1).
### ex.3:   fill 0 0 0 200 100 0 3
or to grass (BlockID 3).

## [LOADING A SPECIFIC WORLD FROM THE PREBUILT SECTION]
### tmp.:   loadworld [int] || "[WorldNameString]"
### ex.1:   loadworld 0
loads the first world inside worlds\prebuilt\
### ex.2:   loadworld "myHouse.wad"
loads the world \worlds\prebuilt\myHouse.wad
the worldNameString needs to match a world present inside the prebuilt folder, else nothing is loaded.







