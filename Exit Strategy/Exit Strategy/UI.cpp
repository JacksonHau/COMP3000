#include "UI.h"
#include "Map.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "stb_easy_font.h"

bool gMapOpen = false;
float gMapZoom = 1.0f;
const float gMapZoomMin = 0.75f;
const float gMapZoomMax = 4.0f;

// Fullscreen map pan state (world x,z)
glm::vec2 gMapCenter(0.0f, 0.0f);

// Right-mouse drag for map
bool   gMapDragging = false;
double gMapDragLastX = 0.0;
double gMapDragLastY = 0.0;

// Cached framebuffer size for map maths
int gFBWidth = 1280;
int gFBHeight = 720;

// ---------- Crosshair ----------
GLuint gCrossVAO = 0, gCrossVBO = 0, gCrossProg = 0;
GLint  gCrossColorLoc = -1;

const char* kCrossVS = R"(#version 330 core
layout (location=0) in vec2 aPos;
void main(){
    gl_Position = vec4(aPos, 0.0, 1.0);
})";

const char* kCrossFS = R"(#version 330 core
uniform vec3 uColor;
out vec4 FragColor;
void main(){ FragColor = vec4(uColor, 1.0); }
)";

void initCrosshair() {
    gCrossProg = linkProgram(kCrossVS, kCrossFS);
    gCrossColorLoc = glGetUniformLocation(gCrossProg, "uColor");

    glGenVertexArrays(1, &gCrossVAO);
    glGenBuffers(1, &gCrossVBO);
    glBindVertexArray(gCrossVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gCrossVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 8, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);
}

void drawCrosshairNDC(int fbw, int fbh) {
    if (!gCrossProg) return;
    const int sizePx = 8;
    float sx = sizePx / (fbw * 0.5f);
    float sy = sizePx / (fbh * 0.5f);

    float verts[8] = {
        -sx, 0.0f,   sx, 0.0f,
         0.0f,-sy,   0.0f, sy
    };

    glBindBuffer(GL_ARRAY_BUFFER, gCrossVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

    glUseProgram(gCrossProg);
    glUniform3f(gCrossColorLoc, 0.95f, 0.95f, 0.95f);
    glBindVertexArray(gCrossVAO);

    glDisable(GL_DEPTH_TEST);
    glLineWidth(2.0f);
    glDrawArrays(GL_LINES, 0, 4);
    glEnable(GL_DEPTH_TEST);

    glBindVertexArray(0);
}

// ================= HUD TEXT (stb_easy_font) =================

GLuint gHudVAO = 0, gHudVBO = 0, gHudProg = 0;
GLint  gHudColorLoc = -1;
GLint  gHudScreenSizeLoc = -1;

const char* kHudVS = R"(#version 330 core
layout (location=0) in vec2 aPos;
uniform vec2 uScreenSize;
void main(){
    vec2 ndc;
    ndc.x = (aPos.x / (uScreenSize.x * 0.5)) - 1.0;
    ndc.y = 1.0 - (aPos.y / (uScreenSize.y * 0.5));
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

const char* kHudFS = R"(#version 330 core
uniform vec3 uColor;
out vec4 FragColor;
void main(){
    FragColor = vec4(uColor, 1.0);
}
)";

void initHudText() {
    gHudProg = linkProgram(kHudVS, kHudFS);
    gHudColorLoc = glGetUniformLocation(gHudProg, "uColor");
    gHudScreenSizeLoc = glGetUniformLocation(gHudProg, "uScreenSize");

    glGenVertexArrays(1, &gHudVAO);
    glGenBuffers(1, &gHudVBO);

    glBindVertexArray(gHudVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gHudVBO);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);
}

void drawTextScreen(const std::string& text,
    float x, float y,
    int fbw, int fbh,
    const glm::vec3& color,
    float scale)
{
    if (text.empty()) return;

    static char rawBuffer[20000];

    int num_quads = stb_easy_font_print(
        0.0f, 0.0f,
        (char*)text.c_str(),
        nullptr,
        rawBuffer,
        sizeof(rawBuffer)
    );
    if (num_quads <= 0) return;

    int num_verts = num_quads * 4;
    std::vector<float> verts;
    verts.resize(num_verts * 2);

    float* src = (float*)rawBuffer;
    for (int i = 0; i < num_verts; ++i) {
        float vx = src[i * 4 + 0];
        float vy = src[i * 4 + 1];

        verts[i * 2 + 0] = x + vx * scale;
        verts[i * 2 + 1] = y + vy * scale;
    }

    std::vector<unsigned int> indices;
    indices.reserve(num_quads * 6);
    for (int q = 0; q < num_quads; ++q) {
        unsigned int base = q * 4;
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }

    GLuint ebo = 0;
    glGenBuffers(1, &ebo);

    glUseProgram(gHudProg);
    glUniform3f(gHudColorLoc, color.r, color.g, color.b);
    glUniform2f(gHudScreenSizeLoc, (float)fbw, (float)fbh);

    glBindVertexArray(gHudVAO);

    glBindBuffer(GL_ARRAY_BUFFER, gHudVBO);
    glBufferData(GL_ARRAY_BUFFER,
        verts.size() * sizeof(float),
        verts.data(),
        GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        indices.size() * sizeof(unsigned int),
        indices.data(),
        GL_DYNAMIC_DRAW);

    glDisable(GL_DEPTH_TEST);
    glDrawElements(GL_TRIANGLES,
        (GLsizei)indices.size(),
        GL_UNSIGNED_INT,
        nullptr);
    glEnable(GL_DEPTH_TEST);

    glBindVertexArray(0);
    glDeleteBuffers(1, &ebo);
}

GLuint gMinimapVAO = 0, gMinimapVBO = 0;

void initMinimap() {
    glGenVertexArrays(1, &gMinimapVAO);
    glGenBuffers(1, &gMinimapVBO);

    glBindVertexArray(gMinimapVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gMinimapVBO);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);
}

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
)
{
    if (!gHudProg || !gMinimapVAO || boxCount == 0) return;

    // Keep the minimap pixel size the same.
    const float size = 220.0f;
    const float margin = 20.0f;
    const float x0 = fbw - size - margin;
    const float y0 = margin;
    const float x1 = fbw - margin;
    const float y1 = margin + size;
    const float cxScreen = (x0 + x1) * 0.5f;
    const float cyScreen = (y0 + y1) * 0.5f;

	// World coords are in a -18 to +18 range, so scale that to fit the minimap size.
    const float worldHalf = 18.0f;
    const float scale = size / (worldHalf * 2.0f);

	// Precompute sin/cos for rotating world opposite to player yaw, so the arrow can stay pointing "up".
    const float yawRad = glm::radians(playerYawDeg);
    const float a = -(yawRad + glm::half_pi<float>());
    const float c = cosf(a);
    const float s = sinf(a);

    auto toMini = [&](float wx, float wz) -> glm::vec2 {
        // Center on player, rotate world opposite to player yaw so the arrow can stay "up".
        float rx = wx - playerPos.x;
        float rz = wz - playerPos.z;
        float mx = rx * c - rz * s;
        float mz = rx * s + rz * c;
        return { cxScreen + mx * scale, cyScreen + mz * scale };
        };

    glUseProgram(gHudProg);
    glUniform2f(gHudScreenSizeLoc, (float)fbw, (float)fbh);
    glBindVertexArray(gMinimapVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gMinimapVBO);

    glDisable(GL_DEPTH_TEST);

    // Background + border
    {
        float bgVerts[8] = { x0, y0,  x1, y0,  x1, y1,  x0, y1 };
        glBufferData(GL_ARRAY_BUFFER, sizeof(bgVerts), bgVerts, GL_DYNAMIC_DRAW);
        glUniform3f(gHudColorLoc, 0.03f, 0.03f, 0.08f);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

        glLineWidth(2.0f);
        glUniform3f(gHudColorLoc, 1.0f, 1.0f, 1.0f);
        glDrawArrays(GL_LINE_LOOP, 0, 4);
    }

    // Clip everything to the minimap square (prevents rotated walls drawing outside)
    glEnable(GL_SCISSOR_TEST);
    glScissor((int)x0, (int)(fbh - y1), (int)size, (int)size);

    // World blocks (draw each AABB as a rotated quad in minimap space)
    for (size_t i = 0; i < boxCount; ++i) {
        const AABB& b = boxes[i];

        glm::vec2 p0 = toMini(b.min.x, b.min.z);
        glm::vec2 p1 = toMini(b.max.x, b.min.z);
        glm::vec2 p2 = toMini(b.max.x, b.max.z);
        glm::vec2 p3 = toMini(b.min.x, b.max.z);

        float quad[8] = {
            p0.x, p0.y,
            p1.x, p1.y,
            p2.x, p2.y,
            p3.x, p3.y
        };
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_DYNAMIC_DRAW);
        glUniform3f(gHudColorLoc, 0.18f, 0.55f, 0.65f);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }

    // Player arrow stays centered and points UP
    {
        const float radius = 8.0f;
        glm::vec2 dir(0.0f, -1.0f); 
        glm::vec2 right(1.0f, 0.0f);

        glm::vec2 p0 = glm::vec2(cxScreen, cyScreen) + dir * (radius * 1.3f);
        glm::vec2 p1 = glm::vec2(cxScreen, cyScreen) - dir * (radius * 0.8f) + right * (radius * 0.7f);
        glm::vec2 p2 = glm::vec2(cxScreen, cyScreen) - dir * (radius * 0.8f) - right * (radius * 0.7f);

        float tri[6] = { p0.x, p0.y,  p1.x, p1.y,  p2.x, p2.y };
        glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_DYNAMIC_DRAW);
        glUniform3f(gHudColorLoc, 1.0f, 0.95f, 0.3f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    // Guard markers on minimap
    for (const glm::vec3& guardPos : guardPositions) {
        glm::vec2 gp = toMini(guardPos.x, guardPos.z);

        float guardSize = 6.0f;
        float enemyVerts[8] = {
            gp.x - guardSize, gp.y - guardSize,
            gp.x + guardSize, gp.y - guardSize,
            gp.x + guardSize, gp.y + guardSize,
            gp.x - guardSize, gp.y + guardSize
        };

        glBufferData(GL_ARRAY_BUFFER, sizeof(enemyVerts), enemyVerts, GL_DYNAMIC_DRAW);
        glUniform3f(gHudColorLoc, 1.0f, 0.15f, 0.15f);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }

    // ----------- MINIMAP LABELS -----------
    glm::vec3 labelColor(1.0f, 1.0f, 1.0f);
    float labelScale = 1.2f;

    // Spawn
    {
        glm::vec2 p = toMini(spawnPos.x, spawnPos.z);
        drawTextScreen("Spawn",
            p.x - 18, p.y - 10,
            fbw, fbh,
            labelColor, labelScale);
    }

    // Exit Key
    {
        glm::vec2 p = toMini(exitKeyPos.x, exitKeyPos.z);
        drawTextScreen("Exit Key",
            p.x - 28, p.y - 10,
            fbw, fbh,
            labelColor, labelScale);
    }

    // Power Cell
    {
        glm::vec2 p = toMini(powerCellPos.x, powerCellPos.z);
        drawTextScreen("Power Cell",
            p.x - 38, p.y - 10,
            fbw, fbh,
            labelColor, labelScale);
    }

    // Exit Gate
    {
        glm::vec2 p = toMini(exitGatePos.x, exitGatePos.z);
        drawTextScreen("Exit Gate",
            p.x - 35, p.y - 10,
            fbw, fbh,
            labelColor, labelScale);
    }

    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
}

void drawFullscreenMap(const std::vector<AABB>& colliders, size_t mapColliderCount,
    int fbw, int fbh,
    const glm::vec3& playerSpawn,
    const glm::vec3& exitKeyPos,
    const glm::vec3& powerCellPos,
    const glm::vec3& exitGatePos,
    const glm::vec3& playerPos,
    float playerYawDeg,
    const std::vector<glm::vec3>& guardPositions)
{
    glDisable(GL_DEPTH_TEST);

    glUseProgram(gHudProg);
    glUniform2f(gHudScreenSizeLoc, (float)fbw, (float)fbh);

    glBindVertexArray(gMinimapVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gMinimapVBO);

    // Dark background covering whole screen
    {
        float verts[8] = {
            0,          0,
            (float)fbw, 0,
            (float)fbw, (float)fbh,
            0,          (float)fbh
        };
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);

        glUniform3f(gHudColorLoc, 0.05f, 0.05f, 0.10f);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }

    // ----- Centered square map with zoom -----
    const float BASE_WORLD_HALF = 50.0f;

    // Zoom in = see smaller chunk of world, so effective half range shrinks
    float worldHalf = BASE_WORLD_HALF / gMapZoom;

    // Size of the map square in pixels (big AAA style)
    float mapSize = std::min((float)fbw, (float)fbh) * 0.95f;

    // Top-left corner of the map square (centered)
    float x0 = (fbw - mapSize) * 0.5f;
    float y0 = (fbh - mapSize) * 0.5f;

    float scale = mapSize / (worldHalf * 2.0f);   // world -> screen

    // Draw world blocks
    for (size_t i = 0; i < mapColliderCount; ++i) {
        const AABB& b = colliders[i];

        float cx = (b.min.x + b.max.x) * 0.5f;
        float cz = (b.min.z + b.max.z) * 0.5f;
        float hx = (b.max.x - b.min.x) * 0.5f;
        float hz = (b.max.z - b.min.z) * 0.5f;

        float sx = x0 + ((cx - gMapCenter.x) + worldHalf) * scale;
        float sy = y0 + ((cz - gMapCenter.y) + worldHalf) * scale;

        float hxPix = hx * scale;
        float hzPix = hz * scale;

        float bx0 = sx - hxPix;
        float bx1 = sx + hxPix;
        float by0 = sy - hzPix;
        float by1 = sy + hzPix;

        float boxVerts[8] = {
            bx0, by0,
            bx1, by0,
            bx1, by1,
            bx0, by1
        };
        glBufferData(GL_ARRAY_BUFFER, sizeof(boxVerts), boxVerts, GL_DYNAMIC_DRAW);

        glUniform3f(gHudColorLoc, 0.25f, 0.8f, 0.9f);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }

    // ----------- LABELS ON MAP -----------

    glm::vec3 labelColor(1.0f, 1.0f, 1.0f);
    float labelScale = 1.3f;

    // Player spawn
    {
        glm::vec2 p = MapWorldToScreen(playerSpawn.x, playerSpawn.z, x0, y0, scale, worldHalf, gMapCenter);
        drawTextScreen("Spawn",
            p.x - 24, p.y - 18,
            fbw, fbh,
            labelColor, labelScale);
    }
    // Exit Key
    if (!gMap.hasExitKey && gMap.exitKey.x > -1000) {
        glm::vec2 p = MapWorldToScreen(exitKeyPos.x, exitKeyPos.z, x0, y0, scale, worldHalf, gMapCenter);
        drawTextScreen("Exit Key",
            p.x - 40, p.y - 18,
            fbw, fbh,
            labelColor, labelScale);
    }
    // Power Cell
    {
        glm::vec2 p = MapWorldToScreen(powerCellPos.x, powerCellPos.z, x0, y0, scale, worldHalf, gMapCenter);
        drawTextScreen("Power Cell",
            p.x - 55, p.y - 18,
            fbw, fbh,
            labelColor, labelScale);
    }

    // Exit Gate
    {
        glm::vec2 p = MapWorldToScreen(exitGatePos.x, exitGatePos.z, x0, y0, scale, worldHalf, gMapCenter);
        drawTextScreen("Exit Gate",
            p.x - 50, p.y - 18,
            fbw, fbh,
            labelColor, labelScale);
    }

    // Rebind minimap geometry buffers after text drawing
    glUseProgram(gHudProg);
    glUniform2f(gHudScreenSizeLoc, (float)fbw, (float)fbh);
    glBindVertexArray(gMinimapVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gMinimapVBO);

    // Player arrow on map
    {
        float px = playerPos.x;
        float pz = playerPos.z;

        float sx = x0 + ((px - gMapCenter.x) + worldHalf) * scale;
        float sy = y0 + ((pz - gMapCenter.y) + worldHalf) * scale;

        float yawRad = glm::radians(playerYawDeg);
        glm::vec2 dir(cosf(yawRad), sinf(yawRad));
        dir = glm::normalize(dir);
        glm::vec2 right(-dir.y, dir.x);

        float r = 15.0f;

        glm::vec2 p0 = glm::vec2(sx, sy) + dir * (r * 1.4f);
        glm::vec2 p1 = glm::vec2(sx, sy) - dir * (r * 0.8f) + right * (r * 0.7f);
        glm::vec2 p2 = glm::vec2(sx, sy) - dir * (r * 0.8f) - right * (r * 0.7f);

        float triVerts[6] = {
            p0.x, p0.y,
            p1.x, p1.y,
            p2.x, p2.y
        };
        glBufferData(GL_ARRAY_BUFFER, sizeof(triVerts), triVerts, GL_DYNAMIC_DRAW);

        glUniform3f(gHudColorLoc, 1.0f, 0.95f, 0.3f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    // Guard markers on fullscreen map
    for (const glm::vec3& guardPos : guardPositions) {
        glm::vec2 gp = MapWorldToScreen(
            guardPos.x, guardPos.z,
            x0, y0, scale, worldHalf, gMapCenter
        );

        float size = 10.0f;

        float verts[8] = {
            gp.x - size, gp.y - size,
            gp.x + size, gp.y - size,
            gp.x + size, gp.y + size,
            gp.x - size, gp.y + size
        };

        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
        glUniform3f(gHudColorLoc, 1.0f, 0.0f, 0.0f);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }

    // Hint text (can stay top-left)
    drawTextScreen("Scroll to zoom  |  Press M to close map",
        40, 40, fbw, fbh,
        glm::vec3(1.0f, 1.0f, 1.0f), 2.0f);

    glEnable(GL_DEPTH_TEST);
}


void UI_Shutdown() {
    // Crosshair
    if (gCrossVAO) glDeleteVertexArrays(1, &gCrossVAO); gCrossVAO = 0;
    if (gCrossVBO) glDeleteBuffers(1, &gCrossVBO); gCrossVBO = 0;
    if (gCrossProg) glDeleteProgram(gCrossProg); gCrossProg = 0;

    // HUD text
    if (gHudVAO) glDeleteVertexArrays(1, &gHudVAO); gHudVAO = 0;
    if (gHudVBO) glDeleteBuffers(1, &gHudVBO); gHudVBO = 0;
    if (gHudProg) glDeleteProgram(gHudProg); gHudProg = 0;

    // Minimap
    if (gMinimapVAO) glDeleteVertexArrays(1, &gMinimapVAO); gMinimapVAO = 0;
    if (gMinimapVBO) glDeleteBuffers(1, &gMinimapVBO); gMinimapVBO = 0;
}
