#pragma once

#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <GL/glew.h>

// Shared GL helper
GLuint linkProgram(const char* vs, const char* fs);

// UI lifecycle
void UI_Shutdown();

struct AABB { glm::vec3 min, max; };

extern bool  gMapOpen;
extern float gMapZoom;
extern const float gMapZoomMin;
extern const float gMapZoomMax;

extern glm::vec2 gMapCenter;

extern bool   gMapDragging;
extern double gMapDragLastX;
extern double gMapDragLastY;

extern int gFBWidth;
extern int gFBHeight;

// Helper: world x,z -> screen x,y (pixels) for fullscreen map
inline glm::vec2 MapWorldToScreen(float wx, float wz,
    float x0, float y0,
    float scale, float worldHalf,
    const glm::vec2& center)
{
    float sx = x0 + ((wx - center.x) + worldHalf) * scale;
    float sy = y0 + ((wz - center.y) + worldHalf) * scale;
    return glm::vec2(sx, sy);
}

void initCrosshair();
void drawCrosshairNDC(int fbw, int fbh);

void initHudText();
void drawTextScreen(const std::string& text,
    float x, float y,
    int fbw, int fbh,
    const glm::vec3& color,
    float scale = 1.0f);

void initMinimap();
void drawMinimap(
    const std::vector<AABB>& boxes,
    size_t boxCount,
    int fbw, int fbh,
    const glm::vec3& playerPos,
    float playerYawDeg,
    const glm::vec3& spawnPos,
    const glm::vec3& exitKeyPos,
    const glm::vec3& powerCellPos,
    const glm::vec3& exitGatePos,
    const std::vector<glm::vec3>& guardPositions
);

void drawFullscreenMap(
    const std::vector<AABB>& colliders, size_t colliderCount,
    int fbw, int fbh,
    const glm::vec3& playerSpawn,
    const glm::vec3& exitKeyPos,
    const glm::vec3& powerCellPos,
    const glm::vec3& exitGatePos,
    const glm::vec3& playerPos,
    float playerYawDeg,
    const std::vector<glm::vec3>& guardPositions
);