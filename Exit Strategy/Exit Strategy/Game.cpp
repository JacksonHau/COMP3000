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

// Game over state
bool gGameOver = false;
std::string gGameOverText = "YOU WERE CAUGHT";
std::string gRestartText = "Press R to restart";

// Win state
bool gGameWon = false;
std::string gWinText = "YOU ESCAPED";
std::string gWinRestartText = "Press R to play again";

// Simple toast message
std::string gHudToast;
float gHudToastTimer = 0.0f;

// ---------- Collision ----------
inline AABB boxFromTS(const glm::vec3& t, const glm::vec3& s) { return AABB{ t - s, t + s }; }

// ---------- Input edge helper ----------
bool pressed(GLFWwindow* w, int key) {
    static std::unordered_map<int, int> last;
    int s = glfwGetKey(w, key);
    bool p = (s == GLFW_PRESS) && (last[key] != GLFW_PRESS);
    last[key] = s;
    return p;
}

// ---------- Callbacks ----------
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

    // Reset guard
    gGuard.alerted = false;
    gGuard.state = GuardState::Patrol;
    gGuard.stunned = false;
    gGuard.stunTimer = 0.0f;
    gGuard.currentPath.clear();
    gGuard.repathTimer = 0.0f;

    if (!gGuard.patrolPoints.empty()) {
        gGuard.currentPatrolIndex = 0;
        gGuard.patrolWaitTimer = 0.0f;
        gGuard.pos = gGuard.patrolPoints[0];
    }

    gHudToast = "Find the key. Avoid the guard.";
    gHudToastTimer = 3.0f;
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

    ModelMesh keyModel = loadModel("assets/Key.obj");

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

    // Endless mode via runtime maze generation
    const bool kUseRandomMaze = true;
    if (kUseRandomMaze) {
        generateRandomMaze(16, 16);
    }
    else {
        if (!loadMapXML("assets/map.xml")) {
            std::cerr << "FATAL: could not load assets/map.xml\n";
            return 1;
        }
    }

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

    // Build maze walls from XML
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

    // Load guard model
    if (!gGuard.loadModel("assets/Guard.obj", "")) {
        std::cerr << "Failed to load guard model\n";
    }
    else {
        // Random guard spawn
        glm::vec3 guardSpawnPos;
        while (true) {
            int rx = rand() % gMap.width;
            int rz = rand() % gMap.height;

            // skip walls
            bool isWall = false;
            for (const auto& w : gMap.walls) {
                if (w.x == rx && w.y == rz) {
                    isWall = true;
                    break;
                }
            }
            if (isWall) continue;

            glm::vec3 candidate = worldFromCell(rx, rz);

            // keep distance from player spawn
            float dist = glm::length(candidate - playerSpawn);
            if (dist < 15.0f) continue;

            guardSpawnPos = candidate;
            break;
        }

        gGuard.pos = guardSpawnPos;

        gGuard.modelScale = 1.1f;
        gGuard.detectionRange = 7.0f;
        gGuard.loseRange = 9999.0f;
        gGuard.searchDuration = 5.0f;
        gGuard.chaseSpeed = 4.0f;
        gGuard.patrolSpeed = 1.0f;

        gGuard.patrolPoints.clear();
        gGuard.patrolPoints.push_back(gGuard.pos);
        gGuard.patrolPoints.push_back(gGuard.pos + glm::vec3(6.0f, 0.0f, 0.0f));
        gGuard.patrolPoints.push_back(gGuard.pos + glm::vec3(6.0f, 0.0f, 6.0f));
        gGuard.patrolPoints.push_back(gGuard.pos + glm::vec3(0.0f, 0.0f, 6.0f));

        std::cout << "Guard spawned at (" << gGuard.pos.x << ", " << gGuard.pos.y << ", " << gGuard.pos.z << ")\n";
    }

    // Start player at the spawn tile (eye height)
    gCam.pos = glm::vec3(playerSpawn.x, kEyeHeight, playerSpawn.z);
    gHudToast = "Find the key. Avoid the guard.";
    gHudToastTimer = 3.0f;

    // Remember how many colliders are "map"
    size_t mapColliderCount = colliders.size();

    double last = glfwGetTime();
    double fpsTimer = last;
    int frames = 0;
    float fps = 0.0f;

    while (!glfwWindowShouldClose(gWindow)) {
        double now = glfwGetTime();
        float dt = float(now - last); last = now;

        glfwPollEvents();

        // R key to reset level on game over OR win
        if ((gGameOver || gGameWon) && pressed(gWindow, GLFW_KEY_R)) {
            resetLevel(playerSpawn);
        }

        // Toggle map
        if (pressed(gWindow, GLFW_KEY_M)) {
            gMapOpen = !gMapOpen;

            // When opening map, center view on player like GTA/RDR
            if (gMapOpen) {
                gMapCenter = glm::vec2(gCam.pos.x, gCam.pos.z);
            }
        }

        // Disable movement when map is open
        if (!gMapOpen && !gGameOver && !gGameWon) {
            processMovement(dt, colliders);
        }

        if (!gGameWon) {
            gGuard.update(dt, gCam.pos, colliders);

            if (gGuard.state == GuardState::Chase && !gChaseSoundPlayed) {
                gChaseSoundPlayed = true;

                if (gSoundEngine) {
                    gChaseSound = gSoundEngine->play2D(
                        "assets/audio/chase.wav",
                        false,
                        false,
                        true
                    );
                }
            }
        }

        // Guard catch check
        if (!gGameOver && !gGameWon) {
            glm::vec3 diff = gGuard.pos - gCam.pos;
            diff.y = 0.0f;
            float catchDist = glm::length(diff);

            if (catchDist < 1.2f) {
                stopFootstepSounds();
                gGameOver = true;
                if (gChaseSound) {
                    gChaseSound->stop();
                    gChaseSound->drop();   // free memory
                    gChaseSound = nullptr;
                }
                gMouseLocked = false;
                glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
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
            gHudPrompt = "Press E to use Power Cell";
        }

        if (gGuard.state == GuardState::Chase) {
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

                gHudToast = "Power Cell collected! Press E to use.";
                gHudToastTimer = 2.0f;

                if (gSoundEngine) {
                    gSoundEngine->play2D("assets/audio/pick_up.wav", false);
                }
            }
        }

        // Use power cell from inventory
        if (gHasPowerCell && !gPowerCellUsed && pressed(gWindow, GLFW_KEY_E)) {
            gHasPowerCell = false;
            gPowerCellUsed = true;

            gGuard.stunned = true;
            gGuard.stunTimer = 5.0f;
            gGuard.state = GuardState::Stunned;

            gHudToast = "Power Cell activated! Guard stunned.";
            gHudToastTimer = 2.0f;

            if (gSoundEngine) {
                gSoundEngine->play2D("assets/audio/zap.wav", false);
            }
        }

        // Escape / win check
        if (!gGameWon && gMap.hasExitKey) {
            float dx = gCam.pos.x - exitGatePos.x;
            float dz = gCam.pos.z - exitGatePos.z;
            float dist2 = dx * dx + dz * dz;

            if (dist2 < 1.2f * 1.2f) {
                gGameWon = true;

                stopFootstepSounds();

                gChaseSoundPlayed = false;
                if (gChaseSound) {
                    gChaseSound->stop();
                    gChaseSound->drop();
                    gChaseSound = nullptr;
                }

                // Stop the guard completely
                gGuard.state = GuardState::Patrol;
                gGuard.alerted = false;
                gGuard.stunned = false;
                gGuard.currentPath.clear();
                gGuard.repathTimer = 0.0f;

                gMouseLocked = false;
                glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                gHudToast = "Escape successful!";
                gHudToastTimer = 2.0f;
            }
        }

        glm::vec3 camForward{
            cosf(glm::radians(gCam.yaw)) * cosf(glm::radians(gCam.pitch)),
            sinf(glm::radians(gCam.pitch)),
            sinf(glm::radians(gCam.yaw)) * cosf(glm::radians(gCam.pitch))
        };
        camForward = glm::normalize(camForward);

        glBindVertexArray(keyModel.vao);
        glDrawElements(GL_TRIANGLES, keyModel.indexCount, GL_UNSIGNED_INT, 0);

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

        gGuard.render(V, P, gObjProg, gObjMVP, gObjTex);

        // Crosshair
        drawCrosshairNDC(fbw, fbh);

        glm::vec3 displayExitKeyPos = gMap.hasExitKey
            ? glm::vec3(9999.0f, 0.0f, 9999.0f)
            : exitKeyPos;

        glm::vec3 displayPowerCellPos = (gHasPowerCell || gPowerCellUsed)
            ? glm::vec3(9999.0f, 0.0f, 9999.0f)
            : powerCellPos;

        // Minimap or fullscreen map
        if (!gMapOpen) {
            drawMinimap(
                colliders,
                mapColliderCount,
                fbw, fbh,
                gCam.pos,
                gCam.yaw,
                playerSpawn,
                displayExitKeyPos,
                displayPowerCellPos,
                exitGatePos,
                gGuard.pos,
                gGuard.yaw
            );
        }
        else {
            drawFullscreenMap(
                colliders, mapColliderCount, fbw, fbh,
                playerSpawn, displayExitKeyPos, displayPowerCellPos, exitGatePos,
                gCam.pos, gCam.yaw,
                gGuard.pos, gGuard.yaw
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
            float titleY = fbh * 0.40f;
            float titleX = fbw * 0.5f - (gWinText.size() * 10.0f);

            drawTextScreen(gWinText, titleX, titleY, fbw, fbh,
                glm::vec3(0.2f, 1.0f, 0.3f), 4.0f);

            float subY = fbh * 0.48f;
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
    gGuard.cleanup();

    stopFootstepSounds();

    UI_Shutdown();

    glDeleteProgram(gObjProg);

    glfwDestroyWindow(gWindow);
    glfwTerminate();
    return 0;
}