#include <iostream>
#include <vector>
#include <cmath>
#include <sstream>
#include <unordered_map>
#include <string>
#include <fstream>
#include <cstddef>
#include <algorithm>
#include <cstdlib>
#include <ctime>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Map.h"
#include "UI.h"
#include "Guard.h"

#include "stb_easy_font.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <irrKlang.h>
using namespace irrklang;

ISoundEngine* gSoundEngine = nullptr;
bool gChaseSoundPlayed = false;

irrklang::ISound* gChaseSound = nullptr;
irrklang::ISound* gWalkSound = nullptr;
irrklang::ISound* gRunSound = nullptr;

static const int DEFAULT_W = 1080;
static const int DEFAULT_H = 1920;

std::string loadFile(const std::string& path)
{
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// ---------- Camera ----------
struct Camera {
    glm::vec3 pos{ 0.0f, 1.8f, 10.0f };
    float yaw = -90.0f;
    float pitch = 0.0f;
    float fov = 60.0f;

    float moveSpeed = 4.0f;
    float sprintMult = 1.8f;
    float mouseSens = 0.12f;

    glm::mat4 getView() const {
        glm::vec3 f;
        f.x = cosf(glm::radians(yaw)) * cosf(glm::radians(pitch));
        f.y = sinf(glm::radians(pitch));
        f.z = sinf(glm::radians(yaw)) * cosf(glm::radians(pitch));
        return glm::lookAt(pos, pos + glm::normalize(f), glm::vec3(0, 1, 0));
    }
};
Camera gCam;

// Single guard helper (the first guard is mirrored into this for some HUD checks)
Guard gGuard;

GLFWwindow* gWindow = nullptr;
bool gFirstMouse = true;
double gLastX = DEFAULT_W * 0.5, gLastY = DEFAULT_H * 0.5;
bool gMouseLocked = true;

// Jump + gravity state
float gVelY = 0.0f;
bool  gGrounded = true;
const float kEyeHeight = 1.8f;
const float kGravity = -18.0f;
const float kJumpSpeed = 6.5f;

// Fullscreen toggle state
bool gFullscreen = false;
int  gWindowedX = 100, gWindowedY = 100;
int  gWindowedW = DEFAULT_W, gWindowedH = DEFAULT_H;

// HUD text strings
std::string gHudPrompt;

bool gHasPowerCell = false;
bool gPowerCellUsed = false;

// Intro state
bool gShowIntro = true;

std::string gIntroTitle = "EXIT STRATEGY";
std::string gIntroLine1 = "You are trapped in the Maze Grid.";
std::string gIntroLine2 = "Find the key. Avoid the guard. Escape.";
std::string gIntroStartText = "Press ENTER to begin";

std::string gWinStoryText = "Maze Grid sector cleared.";

// Game over state
bool gGameOver = false;
std::string gGameOverText = "YOU WERE CAUGHT";
std::string gRestartText = "Press R to restart";

// Win state
bool gGameWon = false;
std::string gWinText = "SECTOR CLEARED";
std::string gWinRestartText = "Press R to play again";

struct LevelConfig {
    int mazeWidth;
    int mazeHeight;
    int guardCount;
};

std::vector<LevelConfig> gLevels = {
    {21, 21, 2},
    {27, 27, 4},
    {33, 33, 6},
    {39, 39, 8}
};

int  gCurrentLevel = 0;
bool gShowLevelComplete = false;
bool gShowFinalComic = false;
GLuint gFinalComicTex = 0;

std::string gLevelCompleteTitle = "LEVEL CLEARED";
std::string gLevelCompletePrompt = "Press ENTER for next level";

// Simple toast message
std::string gHudToast;
float gHudToastTimer = 0.0f;

// Collision
inline AABB boxFromTS(const glm::vec3& t, const glm::vec3& s) { return AABB{ t - s, t + s }; }

// Input edge helper
bool pressed(GLFWwindow* w, int key) {
    static std::unordered_map<int, int> last;
    int s = glfwGetKey(w, key);
    bool p = (s == GLFW_PRESS) && (last[key] != GLFW_PRESS);
    last[key] = s;
    return p;
}

// Similar for mouse buttons
bool mousePressed(GLFWwindow* w, int button) {
    static std::unordered_map<int, int> lastMouse;
    int s = glfwGetMouseButton(w, button);
    bool p = (s == GLFW_PRESS) && (lastMouse[button] != GLFW_PRESS);
    lastMouse[button] = s;
    return p;
}

// Callbacks
void framebuffer_size_callback(GLFWwindow*, int w, int h) {
    gFBWidth = w;
    gFBHeight = h;
    glViewport(0, 0, w, h);
}

void cursor_pos_callback(GLFWwindow*, double x, double y) {
    if (!gMouseLocked) return;
    if (gFirstMouse) { gLastX = x; gLastY = y; gFirstMouse = false; }

    double xoff = x - gLastX;
    double yoff = gLastY - y;
    gLastX = x; gLastY = y;

    // If map is open and we are dragging with RMB, pan the map instead of rotating camera
    if (gMapOpen && gMapDragging) {
        const float BASE_WORLD_HALF = 30.0f;
        float worldHalf = BASE_WORLD_HALF / gMapZoom;

        float mapSize = std::min((float)gFBWidth, (float)gFBHeight) * 0.95f;
        float scale = mapSize / (worldHalf * 2.0f);

        // Screen drag -> world offset
        float dxWorld = (float)(-xoff) / scale; // drag right, world moves right visually
        float dzWorld = (float)(yoff) / scale; // drag up, world moves up visually

        gMapCenter.x += dxWorld;
        gMapCenter.y += dzWorld;

        // Clamp centre so you cannot drag way off map
        float maxCenter = 25.0f;
        gMapCenter.x = glm::clamp(gMapCenter.x, -maxCenter, maxCenter);
        gMapCenter.y = glm::clamp(gMapCenter.y, -maxCenter, maxCenter);

        return;
    }

    // Normal camera look
    gCam.yaw += (float)xoff * gCam.mouseSens;
    gCam.pitch += (float)yoff * gCam.mouseSens;
    gCam.pitch = glm::clamp(gCam.pitch, -89.0f, 89.0f);
}

void toggleFullscreen() {
    gFullscreen = !gFullscreen;
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);

    if (gFullscreen) {
        glfwGetWindowPos(gWindow, &gWindowedX, &gWindowedY);
        glfwGetWindowSize(gWindow, &gWindowedW, &gWindowedH);

        glfwSetWindowMonitor(gWindow, monitor, 0, 0,
            mode->width, mode->height, mode->refreshRate);
        glfwSwapInterval(1);
    }
    else {
        glfwSetWindowMonitor(gWindow, nullptr, gWindowedX, gWindowedY,
            gWindowedW, gWindowedH, 0);
        glfwSwapInterval(1);
    }
}

void key_callback(GLFWwindow* w, int key, int, int action, int) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {

        // If fullscreen map is open → close map instead of quitting
        if (gMapOpen) {
            gMapOpen = false;
            return;
        }

        if (gMouseLocked) {
            gMouseLocked = false;
            glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
        else {
            glfwSetWindowShouldClose(w, GLFW_TRUE);
        }
    }
}

void mouse_button_callback(GLFWwindow* w, int button, int action, int) {
    // Left click to relock camera
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        if (!gMouseLocked) {
            gMouseLocked = true;
            gFirstMouse = true;
            double cx, cy; glfwGetCursorPos(w, &cx, &cy);
            gLastX = cx; gLastY = cy;
            glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
    }

    // Right click drag on fullscreen map
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS && gMapOpen) {
            gMapDragging = true;
            glfwGetCursorPos(w, &gMapDragLastX, &gMapDragLastY);
        }
        else if (action == GLFW_RELEASE) {
            gMapDragging = false;
        }
    }
}

void window_focus_callback(GLFWwindow* w, int focused) {
    if (focused && gMouseLocked) {
        gFirstMouse = true;
        glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }
}

void scroll_callback(GLFWwindow*, double, double yoffset) {
    if (!gMapOpen) return;

    float zoomFactor = 1.0f + (float)yoffset * 0.1f; // wheel up = zoom in, down = out
    gMapZoom *= zoomFactor;

    if (gMapZoom < gMapZoomMin) gMapZoom = gMapZoomMin;
    if (gMapZoom > gMapZoomMax) gMapZoom = gMapZoomMax;
}

// ---------- GL helpers ----------
GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0; glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::vector<GLchar> log(len);
        glGetShaderInfoLog(s, len, nullptr, log.data());
        std::cerr << "Shader compile error:\n" << log.data() << "\n";
    }
    return s;
}

GLuint linkProgram(const char* vs, const char* fs) {
    GLuint v = compile(GL_VERTEX_SHADER, vs);
    GLuint f = compile(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0; glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
        std::vector<GLchar> log(len);
        glGetProgramInfoLog(p, len, nullptr, log.data());
        std::cerr << "Program link error:\n" << log.data() << "\n";
    }
    return p;
}

// ---------- Texture loader ----------
GLuint loadTexture2D(const std::string& path) {
    int w, h, channels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!data) {
        std::cerr << "Failed to load texture: " << path << "\n";
        return 0;
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    stbi_image_free(data);
    return tex;
}

struct ModelMesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLsizei indexCount = 0;
};

ModelMesh loadModel(const std::string& path)
{
    ModelMesh mesh;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        path,
        aiProcess_Triangulate |
        aiProcess_GenNormals |
        aiProcess_FlipUVs
    );

    if (!scene || !scene->HasMeshes()) {
        std::cout << "Failed to load model: " << path << std::endl;
        return mesh;
    }

    aiMesh* ai_mesh = scene->mMeshes[0];

    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    for (unsigned int i = 0; i < ai_mesh->mNumVertices; i++) {
        vertices.push_back(ai_mesh->mVertices[i].x);
        vertices.push_back(ai_mesh->mVertices[i].y);
        vertices.push_back(ai_mesh->mVertices[i].z);

        vertices.push_back(ai_mesh->mNormals[i].x);
        vertices.push_back(ai_mesh->mNormals[i].y);
        vertices.push_back(ai_mesh->mNormals[i].z);
    }

    for (unsigned int i = 0; i < ai_mesh->mNumFaces; i++) {
        aiFace face = ai_mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; j++)
            indices.push_back(face.mIndices[j]);
    }

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glGenBuffers(1, &mesh.ebo);

    glBindVertexArray(mesh.vao);

    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    mesh.indexCount = indices.size();
    return mesh;
}

// ---------- Meshes ----------
struct Mesh { GLuint vao = 0, vbo = 0; GLsizei count = 0; };

Mesh makeGroundPlane(float half = 50.f) {
    float y = 0.0f;
    float s = half;

    // position            // UVs
    float v[] = {
        -s, y, -s,   0.0f, 0.0f,
        s, y, -s,   64.0f, 0.0f,
        s, y,  s,   64.0f, 64.0f,

        -s, y, -s,   0.0f, 0.0f,
        s, y,  s,   64.0f, 64.0f,
        -s, y,  s,   0.0f, 64.0f
    };

    Mesh m;
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);

    // position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);

    // UV
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

    glBindVertexArray(0);
    m.count = 6;
    return m;
}

// Textured cube (for walls with brick texture)
Mesh makeTexturedBox() {
    float v[] = {
        // positions          // UVs
        // front
        -1,-1, 1,  0,0,   1,-1, 1,  3,0,   1, 1, 1,  3,3,
        -1,-1, 1,  0,0,   1, 1, 1,  3,3,  -1, 1, 1,  0,3,
        // back
        -1,-1,-1,  3,0,   1,-1,-1,  0,0,   1, 1,-1,  0,3,
        -1,-1,-1,  3,0,   1, 1,-1,  0,3,  -1, 1,-1,  3,3,
        // left
        -1,-1,-1,  0,0,  -1,-1, 1,  3,0,  -1, 1, 1,  3,3,
        -1,-1,-1,  0,0,  -1, 1, 1,  3,3,  -1, 1,-1,  0,3,
        // right
         1,-1,-1,  3,0,   1,-1, 1,  0,0,   1, 1, 1,  0,3,
         1,-1,-1,  3,0,   1, 1, 1,  0,3,   1, 1,-1,  3,3,
         // top
         -1, 1, 1,  0,3,   1, 1, 1,  3,3,   1, 1,-1,  3,0,
         -1, 1, 1,  0,3,   1, 1,-1,  3,0,  -1, 1,-1,  0,0,
         // bottom
         -1,-1, 1,  0,0,   1,-1,-1,  3,3,   1,-1, 1,  3,0,
         -1,-1, 1,  0,0,  -1,-1,-1,  0,3,   1,-1,-1,  3,3
    };

    Mesh m;
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);

    // Position attribute (location = 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);

    // UV attribute (location = 1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

    glBindVertexArray(0);
    m.count = 36; // 12 triangles * 3 vertices
    return m;
}

// Simple coloured cube (for walls / buildings)
Mesh makeBox() {
    float v[] = {
        // pos            // col
        // front
        -1,-1, 1,  .8,.3,.3,  1,-1, 1,  .8,.3,.3,  1, 1, 1,  .8,.3,.3,
        -1,-1, 1,  .8,.3,.3,  1, 1, 1,  .8,.3,.3, -1, 1, 1,  .8,.3,.3,
        // back
        -1,-1,-1,  .3,.3,.8,  1, 1,-1,  .3,.3,.8,  1,-1,-1,  .3,.3,.8,
        -1,-1,-1,  .3,.3,.8, -1, 1,-1,  .3,.3,.8,  1, 1,-1,  .3,.3,.8,
        // left
        -1,-1,-1,  .3,.8,.3, -1,-1, 1,  .3,.8,.3, -1, 1, 1,  .3,.8,.3,
        -1,-1,-1,  .3,.8,.3, -1, 1, 1,  .3,.8,.3, -1, 1,-1,  .3,.8,.3,
        // right
         1,-1,-1,  .8,.8,.3,  1, 1, 1,  .8,.8,.3,  1,-1, 1,  .8,.8,.3,
         1,-1,-1,  .8,.8,.3,  1, 1,-1,  .8,.8,.3,  1, 1, 1,  .8,.8,.3,
         // top
         -1, 1, 1,  .6,.4,.9,  1, 1, 1,  .6,.4,.9,  1, 1,-1,  .6,.4,.9,
         -1, 1, 1,  .6,.4,.9,  1, 1,-1,  .6,.4,.9, -1, 1,-1,  .6,.4,.9,
         // bottom
         -1,-1, 1,  .4,.9,.9,  1,-1,-1,  .4,.9,.9,  1,-1, 1,  .4,.9,.9,
         -1,-1, 1,  .4,.9,.9, -1,-1,-1,  .4,.9,.9,  1,-1,-1,  .4,.9,.9
    };
    Mesh m;
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);
    m.count = 36;
    return m;
}

Mesh makeCeilingPlane(float half = 50.0f, float y = 5.0f) {
    float s = half;
    float tile = 64.0f;

    float v[] = {
        // positions           // UVs
        -s, y, -s,   0.0f, 0.0f,
        -s, y,  s,   0.0f, tile,
         s, y,  s,   tile, tile,

        -s, y, -s,   0.0f, 0.0f,
         s, y,  s,   tile, tile,
         s, y, -s,   tile, 0.0f
    };

    Mesh m;
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

    glBindVertexArray(0);
    m.count = 6;
    return m;
}

// ---------- Ray vs AABB (for NPC look-at) ----------
float rayAABB(const glm::vec3& ro, const glm::vec3& rd, const AABB& b) {
    glm::vec3 t1 = (b.min - ro) / rd;
    glm::vec3 t2 = (b.max - ro) / rd;
    glm::vec3 tmin = glm::min(t1, t2);
    glm::vec3 tmax = glm::max(t1, t2);

    float tN = std::max(std::max(tmin.x, tmin.y), tmin.z);
    float tF = std::min(std::min(tmax.x, tmax.y), tmax.z);
    if (tF < 0.0f || tN > tF) return -1.0f;
    return tN;
}

GLuint gObjProg = 0;
GLint  gObjMVP = -1;
GLint  gObjTex = -1;

// ---------- Collision resolution in XZ ----------
void resolveXZ(const glm::vec3& oldPos, glm::vec3& newPos,
    const std::vector<AABB>& boxes, float radius)
{
    glm::vec3 tmp = newPos;

    float dx = newPos.x - oldPos.x;
    for (const auto& b : boxes) {
        float minX = b.min.x - radius, maxX = b.max.x + radius;
        float minZ = b.min.z - radius, maxZ = b.max.z + radius;

        if (tmp.z > minZ && tmp.z < maxZ) {
            if (dx > 0 && oldPos.x <= minX && tmp.x > minX) tmp.x = minX;
            if (dx < 0 && oldPos.x >= maxX && tmp.x < maxX) tmp.x = maxX;
        }
    }

    float dz = newPos.z - oldPos.z;
    glm::vec3 tmp2 = tmp;
    for (const auto& b : boxes) {
        float minX = b.min.x - radius, maxX = b.max.x + radius;
        float minZ = b.min.z - radius, maxZ = b.max.z + radius;

        if (tmp2.x > minX && tmp2.x < maxX) {
            if (dz > 0 && oldPos.z <= minZ && tmp2.z > minZ) tmp2.z = minZ;
            if (dz < 0 && oldPos.z >= maxZ && tmp2.z < maxZ) tmp2.z = maxZ;
        }
    }

    newPos = tmp2;
}

void stopFootstepSounds() {
    if (gWalkSound) {
        gWalkSound->stop();
        gWalkSound->drop();
        gWalkSound = nullptr;
    }

    if (gRunSound) {
        gRunSound->stop();
        gRunSound->drop();
        gRunSound = nullptr;
    }
}

// ---------- Movement with jump + gravity + collision ----------
void processMovement(float dt, const std::vector<AABB>& boxes) {
    bool sprinting =
        glfwGetKey(gWindow, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
        glfwGetKey(gWindow, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

    float speed = gCam.moveSpeed;
    if (sprinting) {
        speed *= gCam.sprintMult;
    }

    glm::vec3 f{ cosf(glm::radians(gCam.yaw)), 0.0f, sinf(glm::radians(gCam.yaw)) };
    f = glm::normalize(f);
    glm::vec3 r = glm::normalize(glm::cross(f, glm::vec3(0, 1, 0)));

    glm::vec3 vel(0.0f);
    if (glfwGetKey(gWindow, GLFW_KEY_W) == GLFW_PRESS) vel += f;
    if (glfwGetKey(gWindow, GLFW_KEY_S) == GLFW_PRESS) vel -= f;
    if (glfwGetKey(gWindow, GLFW_KEY_A) == GLFW_PRESS) vel -= r;
    if (glfwGetKey(gWindow, GLFW_KEY_D) == GLFW_PRESS) vel += r;

    bool isMoving = glm::length(vel) > 0.0f;

    if (isMoving) {
        vel = glm::normalize(vel) * speed;
    }

    glm::vec3 oldPos = gCam.pos;
    glm::vec3 newPos = oldPos + vel * dt;

    if (gGrounded && glfwGetKey(gWindow, GLFW_KEY_SPACE) == GLFW_PRESS) {
        gVelY = kJumpSpeed;
        gGrounded = false;
    }

    gVelY += kGravity * dt;
    newPos.y += gVelY * dt;

    float feetY = newPos.y - kEyeHeight;
    if (feetY < 0.0f) {
        newPos.y = kEyeHeight;
        gVelY = 0.0f;
        gGrounded = true;
    }
    else {
        gGrounded = false;
    }

    const float playerRadius = 0.4f;
    resolveXZ(oldPos, newPos, boxes, playerRadius);

    gCam.pos = newPos;

    // Footstep audio
    bool shouldPlayFootsteps = isMoving && gGrounded && !gGameOver && !gGameWon && !gMapOpen;

    if (!shouldPlayFootsteps) {
        stopFootstepSounds();
        return;
    }

    if (sprinting) {
        if (gWalkSound) {
            gWalkSound->stop();
            gWalkSound->drop();
            gWalkSound = nullptr;
        }

        if (!gRunSound && gSoundEngine) {
            gRunSound = gSoundEngine->play2D("assets/audio/run.wav", true, false, true);
            if (gRunSound) gRunSound->setVolume(0.45f);
        }
    }
    else {
        if (gRunSound) {
            gRunSound->stop();
            gRunSound->drop();
            gRunSound = nullptr;
        }

        if (!gWalkSound && gSoundEngine) {
            gWalkSound = gSoundEngine->play2D("assets/audio/walk.wav", true, false, true);
            if (gWalkSound) gWalkSound->setVolume(0.35f);
        }
    }
}

void resetLevel(const glm::vec3& playerSpawn) {
    gCam.pos = glm::vec3(playerSpawn.x, kEyeHeight, playerSpawn.z);
    gCam.yaw = -90.0f;
    gCam.pitch = 0.0f;

    gVelY = 0.0f;
    gGrounded = true;

    gMapOpen = false;
    gMap.hasExitKey = false;

    gHudPrompt.clear();
    gHudToast.clear();
    gHudToastTimer = 0.0f;

    stopFootstepSounds();

    gHasPowerCell = false;
    gPowerCellUsed = false;

    gChaseSoundPlayed = false;

    if (gChaseSound) {
        gChaseSound->stop();
        gChaseSound->drop();
        gChaseSound = nullptr;
    }

    gGameOver = false;
    gGameWon = false;
    gMap.hasExitKey = false;

    // Guard states
    for (auto& guard : gGuards) {
        guard.alerted = false;
        guard.state = GuardState::Patrol;
        guard.stunned = false;
        guard.stunTimer = 0.0f;
        guard.currentPath.clear();
        guard.repathTimer = 0.0f;

        if (!guard.patrolPoints.empty()) {
            guard.currentPatrolIndex = 0;
            guard.patrolWaitTimer = 0.0f;
            guard.pos = guard.patrolPoints[0];
        }
    }
}

int Game_Run() {
    srand((unsigned int)time(0));

    if (!glfwInit()) { std::cerr << "GLFW init failed\n"; return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

    gWindow = glfwCreateWindow(DEFAULT_W, DEFAULT_H, "Exit Strategy", nullptr, nullptr);
    if (!gWindow) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(gWindow);
    glfwSwapInterval(1);

    glfwSetFramebufferSizeCallback(gWindow, framebuffer_size_callback);
    glfwSetCursorPosCallback(gWindow, cursor_pos_callback);
    glfwSetKeyCallback(gWindow, key_callback);
    glfwSetMouseButtonCallback(gWindow, mouse_button_callback);
    glfwSetWindowFocusCallback(gWindow, window_focus_callback);
    glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetScrollCallback(gWindow, scroll_callback);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) { glfwDestroyWindow(gWindow); glfwTerminate(); return 1; }

    gSoundEngine = createIrrKlangDevice();
    if (!gSoundEngine) {
        std::cerr << "Failed to initialise irrKlang\n";
    }

    int fbw, fbh; glfwGetFramebufferSize(gWindow, &fbw, &fbh);
    glViewport(0, 0, fbw, fbh);
    glEnable(GL_DEPTH_TEST);

    Mesh ground = makeGroundPlane(60.0f);
    Mesh ceiling = makeCeilingPlane(60.0f, 5.0f);
    Mesh box = makeBox(); // for walls

    std::string vs = loadFile("shaders/basic.vert");
    std::string fs = loadFile("shaders/basic.frag");

    GLuint prog = linkProgram(vs.c_str(), fs.c_str());

    GLint uMVP = glGetUniformLocation(prog, "uMVP");

    initCrosshair();
    initHudText();
    initMinimap();

    std::string objVS = loadFile("shaders/obj.vert");
    std::string objFS = loadFile("shaders/obj.frag");

    gObjProg = linkProgram(objVS.c_str(), objFS.c_str());

    gObjMVP = glGetUniformLocation(gObjProg, "uMVP");
    gObjTex = glGetUniformLocation(gObjProg, "uTex");

    // Map layout: Data-driven XML
    std::vector<glm::mat4> mapMats;
    std::vector<AABB> colliders;

    auto addMapBox = [&](const glm::vec3& center, const glm::vec3& half) {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), center)
            * glm::scale(glm::mat4(1.0f), half);
        mapMats.push_back(model);
        colliders.push_back(AABB{ center - half, center + half });
        };

    // Always generate a random maze for the current level
    const LevelConfig& cfg = gLevels[gCurrentLevel];
    generateRandomMaze(cfg.mazeWidth, cfg.mazeHeight);

    Mesh texturedBox = makeTexturedBox();

    GLuint brickTexture = loadTexture2D("assets/brick.png");
    if (!brickTexture) {
        std::cerr << "Warning: Could not load brick texture, walls will be invisible?\n";
    }

    GLuint floorTexture = loadTexture2D("assets/floor.png");
    if (!floorTexture) {
        std::cerr << "Warning: Could not load floor texture, walls will be invisible?\n";
    }

    GLuint ceilingTexture = loadTexture2D("assets/ceiling.png");
    if (!ceilingTexture) {
        std::cerr << "Warning: Could not load ceiling texture, ceiling will be invisible?\n";
    }

    gFinalComicTex = loadTexture2D("assets/final_comic.png");

    // World mapping: keep the same overall scale as before so minimap feels consistent.
    const float WORLD_SIZE = 90.0f;
    const float ORIGIN_X = -WORLD_SIZE * 0.5f;
    const float ORIGIN_Z = -WORLD_SIZE * 0.5f;

    const int MAP_W = std::max(1, gMap.width);
    const int MAP_H = std::max(1, gMap.height);

    const float CELL_X = WORLD_SIZE / (float)MAP_W;
    const float CELL_Z = WORLD_SIZE / (float)MAP_H;

    const float WALL_HALF_Y = 2.5f;
    const float WALL_HALF_X = CELL_X * 0.5f;
    const float WALL_HALF_Z = CELL_Z * 0.5f;

    auto worldFromCell = [&](int cx, int cz) -> glm::vec3 {
        float wx = ORIGIN_X + (cx + 0.5f) * CELL_X;
        float wz = ORIGIN_Z + (cz + 0.5f) * CELL_Z;
        return glm::vec3(wx, 0.0f, wz);
        };

    // Build maze walls from XML / generated map
    for (const auto& w : gMap.walls) {
        glm::vec3 p = worldFromCell(w.x, w.y);
        addMapBox(glm::vec3(p.x, WALL_HALF_Y, p.z), glm::vec3(WALL_HALF_X, WALL_HALF_Y, WALL_HALF_Z));
    }
    // Spawn / markers
    glm::vec3 playerSpawn = worldFromCell(gMap.spawn.x, gMap.spawn.y);

    glm::vec3 exitKeyPos(0.0f);
    if (gMap.exitKey.x > -1000) {
        exitKeyPos = worldFromCell(gMap.exitKey.x, gMap.exitKey.y);
    }

    glm::vec3 powerCellPos = worldFromCell(gMap.powerCell.x, gMap.powerCell.y);
    glm::vec3 exitGatePos = worldFromCell(gMap.exit.x, gMap.exit.y);

    // Load exit key model
    Guard exitKeyModel;
    if (!exitKeyModel.loadModel("assets/Key.obj", "")) {
        std::cerr << "Failed to load key model\n";
    }
    else {
        exitKeyModel.pos = exitKeyPos;
        exitKeyModel.modelScale = 0.50f;
    }

    // Load power cell model
    Guard powerCellModel;
    if (!powerCellModel.loadModel("assets/Power_Cell.obj", "assets/textures/EC_Power_Cell_Color_Blue.jpg")) {
        std::cerr << "Failed to load power cell model\n";
    }
    else {
        powerCellModel.pos = powerCellPos;
        powerCellModel.modelScale = 0.35f;
    }

    // Load guards
    gGuards.clear();
    gGuards.reserve(cfg.guardCount);

    for (int i = 0; i < cfg.guardCount; i++) {
        gGuards.emplace_back();
        Guard& guard = gGuards.back();

        if (!guard.loadModel("assets/Guard.obj", "")) {
            std::cerr << "Failed to load guard model\n";
            continue;
        }

        glm::vec3 guardSpawnPos;

        for (int tries = 0; tries < 5000; tries++) {
            int rx = rand() % gMap.width;
            int rz = rand() % gMap.height;

            bool isWall = false;
            for (const auto& w : gMap.walls) {
                if (w.x == rx && w.y == rz) {
                    isWall = true;
                    break;
                }
            }

            if (isWall) continue;

            glm::vec3 candidate = worldFromCell(rx, rz);

            float distFromPlayer = glm::length(candidate - playerSpawn);
            if (distFromPlayer < 15.0f) continue;

            bool tooCloseToOtherGuard = false;
            for (const auto& other : gGuards) {
                if (&other == &guard) continue;

                if (glm::length(candidate - other.pos) < 8.0f) {
                    tooCloseToOtherGuard = true;
                    break;
                }
            }

            if (tooCloseToOtherGuard) continue;

            guardSpawnPos = candidate;
            break;
        }

        guard.pos = guardSpawnPos;
        guard.modelScale = 1.1f;
        guard.detectionRange = 7.0f;
        guard.loseRange = 9999.0f;
        guard.searchDuration = 5.0f;
        guard.chaseSpeed = 4.0f;
        guard.patrolSpeed = 1.0f;

        guard.patrolPoints.clear();
        guard.patrolPoints.push_back(guard.pos);
        guard.patrolPoints.push_back(guard.pos + glm::vec3(6.0f, 0.0f, 0.0f));
        guard.patrolPoints.push_back(guard.pos + glm::vec3(6.0f, 0.0f, 6.0f));
        guard.patrolPoints.push_back(guard.pos + glm::vec3(0.0f, 0.0f, 6.0f));

        std::cout << "Guard " << i + 1 << " spawned at ("
            << guard.pos.x << ", " << guard.pos.y << ", " << guard.pos.z << ")\n";
    }

    // Start player at the spawn tile (eye height)
    gCam.pos = glm::vec3(playerSpawn.x, kEyeHeight, playerSpawn.z);

    // Start on intro screen
    gShowIntro = true;
    gMouseLocked = false;
    glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    gHudToast.clear();
    gHudToastTimer = 0.0f;

    // Remember how many colliders are "map"
    size_t mapColliderCount = colliders.size();

    auto loadCurrentGeneratedLevel = [&]() {
        // Clean old guards before creating new ones
        for (auto& guard : gGuards) {
            guard.cleanup();
        }
        gGuards.clear();

        // Clear old maze geometry
        mapMats.clear();
        colliders.clear();

        const LevelConfig& level = gLevels[gCurrentLevel];

        // Generate bigger random maze based on current level
        generateRandomMaze(level.mazeWidth, level.mazeHeight);

        // Recalculate world scale for the new maze size
        int mapW = std::max(1, gMap.width);
        int mapH = std::max(1, gMap.height);

        float cellX = WORLD_SIZE / (float)mapW;
        float cellZ = WORLD_SIZE / (float)mapH;

        float wallHalfX = cellX * 0.5f;
        float wallHalfZ = cellZ * 0.5f;

        auto levelWorldFromCell = [&](int cx, int cz) -> glm::vec3 {
            float wx = ORIGIN_X + (cx + 0.5f) * cellX;
            float wz = ORIGIN_Z + (cz + 0.5f) * cellZ;
            return glm::vec3(wx, 0.0f, wz);
            };

        // Rebuild walls and colliders
        for (const auto& w : gMap.walls) {
            glm::vec3 p = levelWorldFromCell(w.x, w.y);
            addMapBox(
                glm::vec3(p.x, WALL_HALF_Y, p.z),
                glm::vec3(wallHalfX, WALL_HALF_Y, wallHalfZ)
            );
        }

        // Refresh important positions
        playerSpawn = levelWorldFromCell(gMap.spawn.x, gMap.spawn.y);

        if (gMap.exitKey.x > -1000) {
            exitKeyPos = levelWorldFromCell(gMap.exitKey.x, gMap.exitKey.y);
        }

        powerCellPos = levelWorldFromCell(gMap.powerCell.x, gMap.powerCell.y);
        exitGatePos = levelWorldFromCell(gMap.exit.x, gMap.exit.y);

        // Move models to the new generated positions
        exitKeyModel.pos = exitKeyPos;
        powerCellModel.pos = powerCellPos;

        // Spawn guards for this level
        gGuards.reserve(level.guardCount);

        for (int i = 0; i < level.guardCount; i++) {
            gGuards.emplace_back();
            Guard& guard = gGuards.back();

            if (!guard.loadModel("assets/Guard.obj", "")) {
                std::cerr << "Failed to load guard model\n";
                continue;
            }

            glm::vec3 guardSpawnPos = playerSpawn + glm::vec3(8.0f, 0.0f, 8.0f);

            for (int tries = 0; tries < 5000; tries++) {
                int rx = rand() % gMap.width;
                int rz = rand() % gMap.height;

                bool isWall = false;
                for (const auto& w : gMap.walls) {
                    if (w.x == rx && w.y == rz) {
                        isWall = true;
                        break;
                    }
                }

                if (isWall) continue;

                glm::vec3 candidate = levelWorldFromCell(rx, rz);

                float distFromPlayer = glm::length(candidate - playerSpawn);
                if (distFromPlayer < 12.0f) continue;

                bool tooCloseToOtherGuard = false;
                for (const auto& other : gGuards) {
                    if (&other == &guard) continue;

                    if (glm::length(candidate - other.pos) < 6.0f) {
                        tooCloseToOtherGuard = true;
                        break;
                    }
                }

                if (tooCloseToOtherGuard) continue;

                guardSpawnPos = candidate;
                break;
            }

            guard.pos = guardSpawnPos;
            guard.modelScale = 1.1f;
            guard.detectionRange = 7.0f;
            guard.loseRange = 9999.0f;
            guard.searchDuration = 5.0f;
            guard.chaseSpeed = 4.0f;
            guard.patrolSpeed = 1.0f;

            guard.patrolPoints.clear();
            guard.patrolPoints.push_back(guard.pos);
            guard.patrolPoints.push_back(guard.pos + glm::vec3(6.0f, 0.0f, 0.0f));
            guard.patrolPoints.push_back(guard.pos + glm::vec3(6.0f, 0.0f, 6.0f));
            guard.patrolPoints.push_back(guard.pos + glm::vec3(0.0f, 0.0f, 6.0f));
        }

        mapColliderCount = colliders.size();

        resetLevel(playerSpawn);

        gShowLevelComplete = false;
        gShowFinalComic = false;
        gGameWon = false;
        gGameOver = false;
        gMapOpen = false;

        gMouseLocked = true;
        gFirstMouse = true;
        glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

        gHudToast = "Level " + std::to_string(gCurrentLevel + 1) + " started.";
        gHudToastTimer = 2.0f;
        };

    double last = glfwGetTime();
    double fpsTimer = last;
    int frames = 0;
    float fps = 0.0f;

    while (!glfwWindowShouldClose(gWindow)) {
        double now = glfwGetTime();
        float dt = float(now - last); last = now;

        glfwPollEvents();

        // Add intro
        if (gShowIntro) {
            if (pressed(gWindow, GLFW_KEY_ENTER)) {
                gShowIntro = false;
                gMouseLocked = true;
                gFirstMouse = true;
                glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

                gHudToast = "Find the key. Avoid the guard.";
                gHudToastTimer = 3.0f;
            }

            else {
                glfwGetFramebufferSize(gWindow, &fbw, &fbh);

                glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

                float titleY = fbh * 0.28f;
                float titleX = fbw * 0.5f - (gIntroTitle.size() * 10.0f);
                drawTextScreen(gIntroTitle, titleX, titleY, fbw, fbh,
                    glm::vec3(0.9f, 1.0f, 0.9f), 4.0f);

                float line1Y = fbh * 0.40f;
                float line1X = fbw * 0.5f - (gIntroLine1.size() * 5.0f);
                drawTextScreen(gIntroLine1, line1X, line1Y, fbw, fbh,
                    glm::vec3(1.0f, 1.0f, 1.0f), 2.2f);

                float line2Y = fbh * 0.46f;
                float line2X = fbw * 0.5f - (gIntroLine2.size() * 5.0f);
                drawTextScreen(gIntroLine2, line2X, line2Y, fbw, fbh,
                    glm::vec3(0.8f, 0.95f, 1.0f), 2.2f);

                float startY = fbh * 0.60f;
                float startX = fbw * 0.5f - (gIntroStartText.size() * 5.0f);
                drawTextScreen(gIntroStartText, startX, startY, fbw, fbh,
                    glm::vec3(0.7f, 1.0f, 0.7f), 2.0f);

                glfwSwapBuffers(gWindow);
                continue;
            }
        }

		// Level complete screen
        if (gShowLevelComplete) {
            if (pressed(gWindow, GLFW_KEY_ENTER)) {
                gCurrentLevel++;
                loadCurrentGeneratedLevel();
            }
            else {
                glfwGetFramebufferSize(gWindow, &fbw, &fbh);

                glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

                std::string levelText = "LEVEL " + std::to_string(gCurrentLevel + 1) + " CLEARED";

                float titleY = fbh * 0.38f;
                float titleX = fbw * 0.5f - (levelText.size() * 10.0f);

                drawTextScreen(levelText, titleX, titleY, fbw, fbh,
                    glm::vec3(0.2f, 1.0f, 0.3f), 4.0f);

                float promptY = fbh * 0.50f;
                float promptX = fbw * 0.5f - (gLevelCompletePrompt.size() * 5.0f);

                drawTextScreen(gLevelCompletePrompt, promptX, promptY, fbw, fbh,
                    glm::vec3(1.0f, 1.0f, 1.0f), 2.5f);

                glfwSwapBuffers(gWindow);
                continue;
            }
        }

        // R key to reset level on game over OR win
        if ((gGameOver || gGameWon) && pressed(gWindow, GLFW_KEY_R)) {
            if (gGameWon) {
                gCurrentLevel = 0;
            }

            loadCurrentGeneratedLevel();
        }

        // Toggle map
        if (pressed(gWindow, GLFW_KEY_M)) {
            gMapOpen = !gMapOpen;

            // Fullscreen map should show the whole maze first
            if (gMapOpen) {
                gMapCenter = glm::vec2(0.0f, 0.0f);
                gMapZoom = 1.0f;
            }
        }

        // Disable movement when map is open
        if (!gMapOpen && !gGameOver && !gGameWon && !gShowLevelComplete && !gShowFinalComic) {
            processMovement(dt, colliders);
        }

        if (!gGameWon && !gShowLevelComplete && !gShowFinalComic) {
            for (auto& guard : gGuards) {
                guard.update(dt, gCam.pos, colliders);
            }
        }

        bool anyGuardChasing = false;
        for (const auto& guard : gGuards) {
            if (guard.state == GuardState::Chase) {
                anyGuardChasing = true;
                break;
            }
        }

        if (anyGuardChasing && !gChaseSoundPlayed) {
            gChaseSoundPlayed = true;

            if (gSoundEngine) {
                gChaseSound = gSoundEngine->play2D(
                    "assets/audio/chase.wav",
                    true,
                    false,
                    true
                );
            }
        }

        // Guard catch check
        if (!gGameOver && !gGameWon) {
            for (auto& guard : gGuards) {
                glm::vec3 diff = guard.pos - gCam.pos;
                float distXZ2 = diff.x * diff.x + diff.z * diff.z;
                if (distXZ2 < 1.2f * 1.2f) {
                    gGameOver = true;
                    gMouseLocked = false;
                    glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                    gHudToast = "You were caught!";
                    gHudToastTimer = 2.0f;
                    stopFootstepSounds();
                    break;
                }
            }
        }

        glClearColor(0.10f, 0.12f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 V = gCam.getView();
        glfwGetFramebufferSize(gWindow, &fbw, &fbh);
        float aspect = fbh > 0 ? float(fbw) / float(fbh) : 16.f / 9.f;
        glm::mat4 P = glm::perspective(glm::radians(gCam.fov), aspect, 0.1f, 200.0f);

        gHudPrompt.clear();

        if (gHasPowerCell && !gGameOver && !gGameWon) {
            gHudPrompt = "Aim at guard and shoot";
        }

        if (anyGuardChasing) {
            gHudPrompt = "RUN!";
        }

        // toast countdown
        if (gHudToastTimer > 0.0f) {
            gHudToastTimer -= dt;
            if (gHudToastTimer <= 0.0f) {
                gHudToastTimer = 0.0f;
                gHudToast.clear();
            }
        }

        // Exit key pickup
        if (!gMap.hasExitKey && gMap.exitKey.x > -1000) {
            float dx = gCam.pos.x - exitKeyPos.x;
            float dz = gCam.pos.z - exitKeyPos.z;
            float dist2 = dx * dx + dz * dz;
            if (dist2 < 0.85f * 0.85f) {
                gMap.hasExitKey = true;
                gHudToast = "Exit Key collected!";
                gHudToastTimer = 2.0f;
                if (gSoundEngine) {
                    gSoundEngine->play2D("assets/audio/pick_up.wav", false);
                }
            }
        }

        // Power cell pickup
        if (!gHasPowerCell && !gPowerCellUsed) {
            float dx = gCam.pos.x - powerCellPos.x;
            float dz = gCam.pos.z - powerCellPos.z;
            float dist2 = dx * dx + dz * dz;

            if (dist2 < 1.0f * 1.0f) {
                gHasPowerCell = true;

                gHudToast = "Power Cell collected!";
                gHudToastTimer = 2.0f;

                if (gSoundEngine) {
                    gSoundEngine->play2D("assets/audio/pick_up.wav", false);
                }
            }
        }

        // Use power cell
        if (gHasPowerCell && !gPowerCellUsed && mousePressed(gWindow, GLFW_MOUSE_BUTTON_LEFT)) {
            gHasPowerCell = false;
            gPowerCellUsed = true;

            glm::vec3 camForward{
                cosf(glm::radians(gCam.yaw)) * cosf(glm::radians(gCam.pitch)),
                sinf(glm::radians(gCam.pitch)),
                sinf(glm::radians(gCam.yaw)) * cosf(glm::radians(gCam.pitch))
            };
            camForward = glm::normalize(camForward);

            int bestGuardIndex = -1;
            float bestAimDot = 0.90f;

            for (int i = 0; i < (int)gGuards.size(); i++) {
                Guard& guard = gGuards[i];

                if (guard.stunned) continue;

                glm::vec3 guardTarget = guard.pos + glm::vec3(0.0f, 1.0f, 0.0f);
                glm::vec3 toGuard = guardTarget - gCam.pos;
                float distance = glm::length(toGuard);

                if (distance <= 0.001f) continue;

                toGuard = glm::normalize(toGuard);
                float aimDot = glm::dot(camForward, toGuard);

                if (distance < 10.0f && aimDot > bestAimDot) {
                    bestAimDot = aimDot;
                    bestGuardIndex = i;
                }
            }

            if (bestGuardIndex != -1) {
                Guard& targetGuard = gGuards[bestGuardIndex];

                targetGuard.stunned = true;
                targetGuard.stunTimer = 5.0f;
                targetGuard.state = GuardState::Stunned;

                gHudToast = "Guard stunned.";
                gHudToastTimer = 2.0f;
            }
            else {
                gHudToast = "Power Cell wasted.";
                gHudToastTimer = 1.5f;
            }

            if (gSoundEngine) {
                gSoundEngine->play2D("assets/audio/zap.wav", false);
            }
        }

        // Escape / win check
        if (!gGameWon && !gShowLevelComplete && gMap.hasExitKey) {
            float dx = gCam.pos.x - exitGatePos.x;
            float dz = gCam.pos.z - exitGatePos.z;
            float dist2 = dx * dx + dz * dz;

            if (dist2 < 1.2f * 1.2f) {
                stopFootstepSounds();

                if (gChaseSound) {
                    gChaseSound->stop();
                    gChaseSound->drop();
                    gChaseSound = nullptr;
                }

                gChaseSoundPlayed = false;

                for (auto& guard : gGuards) {
                    guard.state = GuardState::Patrol;
                    guard.alerted = false;
                    guard.stunned = false;
                    guard.currentPath.clear();
                    guard.repathTimer = 0.0f;
                }

                gMouseLocked = false;
                glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

                if (gCurrentLevel < (int)gLevels.size() - 1) {
                    gShowLevelComplete = true;
                }
                else {
                    gGameWon = true;
                    gHudToast = "All sectors cleared.";
                    gHudToastTimer = 2.0f;
                }
            }
        }

        // Render ground with texture
        glUseProgram(gObjProg);

        glUniform1i(gObjTex, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, floorTexture);

        glm::mat4 MVP = P * V * glm::mat4(1.0f);
        glUniformMatrix4fv(gObjMVP, 1, GL_FALSE, glm::value_ptr(MVP));

        glBindVertexArray(ground.vao);
        glDrawArrays(GL_TRIANGLES, 0, ground.count);
        glBindVertexArray(0);

        // Render ceiling
        glUseProgram(gObjProg);
        glUniform1i(gObjTex, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ceilingTexture);

        glm::mat4 ceilingMVP = P * V * glm::mat4(1.0f);
        glUniformMatrix4fv(gObjMVP, 1, GL_FALSE, glm::value_ptr(ceilingMVP));

        glBindVertexArray(ceiling.vao);
        glDrawArrays(GL_TRIANGLES, 0, ceiling.count);
        glBindVertexArray(0);

        // Render textured walls (using brick texture)
        if (brickTexture) {
            glUseProgram(gObjProg);
            glUniform1i(gObjTex, 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, brickTexture);

            glBindVertexArray(texturedBox.vao);
            for (const auto& M : mapMats) {
                glm::mat4 MVP = P * V * M;
                glUniformMatrix4fv(gObjMVP, 1, GL_FALSE, glm::value_ptr(MVP));
                glDrawArrays(GL_TRIANGLES, 0, texturedBox.count);
            }
            glBindVertexArray(0);
        }
        else {
            // Fallback: render with colored shader if texture failed
            glUseProgram(prog);
            glBindVertexArray(box.vao);
            for (const auto& M : mapMats) {
                glm::mat4 MVP = P * V * M;
                glUniformMatrix4fv(uMVP, 1, GL_FALSE, glm::value_ptr(MVP));
                glDrawArrays(GL_TRIANGLES, 0, box.count);
            }
            glBindVertexArray(0);
        }

        // Render exit key if not collected yet
        if (!gMap.hasExitKey && gMap.exitKey.x > -1000) {
            exitKeyModel.pos = glm::vec3(exitKeyPos.x, 0.3f, exitKeyPos.z); // little lift off the floor
            exitKeyModel.yaw = (float)(glfwGetTime() * 90.0f); // spin
            exitKeyModel.render(V, P, gObjProg, gObjMVP, gObjTex);
        }

        // Render power cell if not collected yet
        if (!gHasPowerCell && !gPowerCellUsed) {
            powerCellModel.pos = glm::vec3(powerCellPos.x, 0.5f, powerCellPos.z);
            powerCellModel.yaw = (float)(glfwGetTime() * 70.0f);
            powerCellModel.render(V, P, gObjProg, gObjMVP, gObjTex);
        }

        for (auto& guard : gGuards) {
            guard.render(V, P, gObjProg, gObjMVP, gObjTex);
        }

        // Crosshair
        drawCrosshairNDC(fbw, fbh);

        glm::vec3 displayExitKeyPos = gMap.hasExitKey
            ? glm::vec3(9999.0f, 0.0f, 9999.0f)
            : exitKeyPos;

        glm::vec3 displayPowerCellPos = (gHasPowerCell || gPowerCellUsed)
            ? glm::vec3(9999.0f, 0.0f, 9999.0f)
            : powerCellPos;

        std::vector<glm::vec3> mapGuardPositions;
        mapGuardPositions.reserve(gGuards.size());

        for (const auto& guard : gGuards) {
            mapGuardPositions.push_back(guard.pos);
        }

        // Minimap or fullscreen map
        if (!gMapOpen) {
            drawMinimap(
                colliders, mapColliderCount,
                fbw, fbh,
                gCam.pos, gCam.yaw,
                playerSpawn,
                displayExitKeyPos,
                displayPowerCellPos,
                exitGatePos,
                mapGuardPositions
            );
        }
        else {
            drawFullscreenMap(
                colliders, mapColliderCount, fbw, fbh,
                playerSpawn,
                displayExitKeyPos,
                displayPowerCellPos,
                exitGatePos,
                gCam.pos, gCam.yaw,
                mapGuardPositions
            );
        }

        // HUD text
        if (!gHudPrompt.empty()) {
            float promptY = fbh * 0.28f;
            float promptX = fbw * 0.5f - (gHudPrompt.size() * 4.0f);

            glm::vec3 promptColor = glm::vec3(1.0f, 1.0f, 0.7f);
            if (gHudPrompt == "RUN!") {
                promptColor = glm::vec3(1.0f, 0.2f, 0.2f);
            }

            drawTextScreen(gHudPrompt, promptX, promptY, fbw, fbh,
                promptColor, 2.5f);
        }

        if (!gHudToast.empty()) {
            float toastY = fbh * 0.18f;
            float toastX = fbw * 0.5f - (gHudToast.size() * 4.0f);
            drawTextScreen(gHudToast, toastX, toastY, fbw, fbh,
                glm::vec3(0.7f, 1.0f, 0.7f), 2.5f);
        }

        if (gGameOver) {
            float titleY = fbh * 0.40f;
            float titleX = fbw * 0.5f - (gGameOverText.size() * 10.0f);

            drawTextScreen(gGameOverText, titleX, titleY, fbw, fbh,
                glm::vec3(1.0f, 0.2f, 0.2f), 4.0f);

            float subY = fbh * 0.48f;
            float subX = fbw * 0.5f - (gRestartText.size() * 5.0f);

            drawTextScreen(gRestartText, subX, subY, fbw, fbh,
                glm::vec3(1.0f, 1.0f, 1.0f), 2.5f);
        }

        if (gGameWon) {
            float titleY = fbh * 0.38f;
            float titleX = fbw * 0.5f - (gWinText.size() * 10.0f);

            drawTextScreen(gWinText, titleX, titleY, fbw, fbh,
                glm::vec3(0.2f, 1.0f, 0.3f), 4.0f);

            float storyY = fbh * 0.47f;
            float storyX = fbw * 0.5f - (gWinStoryText.size() * 5.0f);

            drawTextScreen(gWinStoryText, storyX, storyY, fbw, fbh,
                glm::vec3(0.8f, 1.0f, 0.85f), 2.2f);

            float subY = fbh * 0.55f;
            float subX = fbw * 0.5f - (gWinRestartText.size() * 5.0f);

            drawTextScreen(gWinRestartText, subX, subY, fbw, fbh,
                glm::vec3(1.0f, 1.0f, 1.0f), 2.5f);
        }

        glfwSwapBuffers(gWindow);

        // FPS counter in title
        frames++;
        double current = glfwGetTime();
        if (current - fpsTimer >= 0.5) {
            fps = frames / float(current - fpsTimer);
            fpsTimer = current;
            frames = 0;

            {
                std::ostringstream oss; oss << int(fps);
                std::string title = "Exit Strategy - FPS: " + oss.str();
                glfwSetWindowTitle(gWindow, title.c_str());
            }
        }
    }

    glDeleteVertexArrays(1, &ground.vao); glDeleteBuffers(1, &ground.vbo);
    glDeleteVertexArrays(1, &box.vao);    glDeleteBuffers(1, &box.vbo);
    glDeleteProgram(prog);

    if (gSoundEngine) {
        gSoundEngine->drop();
    }

    exitKeyModel.cleanup();
    powerCellModel.cleanup();

    for (auto& guard : gGuards) {
        guard.cleanup();
    }
    gGuards.clear();

    stopFootstepSounds();

    UI_Shutdown();

    glDeleteProgram(gObjProg);

    glfwDestroyWindow(gWindow);
    glfwTerminate();
    return 0;
}