#pragma once

#include <vector>
#include <glm/glm.hpp>

struct MapData {
    int width = 0;
    int height = 0;
    float tileSize = 1.0f;

    glm::ivec2 spawn{ 1,1 };
    glm::ivec2 exit{ 22,22 };
    glm::ivec2 powerCell{ 12,20 };
    glm::ivec2 exitKey{ -9999, -9999 };

    bool hasExitKey = false;

    std::vector<glm::ivec2> npcSpawns;
    std::vector<glm::ivec2> walls;
};

extern MapData gMap;

bool loadMapXML(const char* path);

// Build a fresh maze at runtime 
void generateRandomMaze(int width, int height, unsigned int seed = 0);