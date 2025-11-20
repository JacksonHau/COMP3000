#include <iostream>
#include <vector>
#include <cmath>
#include <sstream>
#include <unordered_map>
#include <string>
#include <fstream>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "stb_easy_font.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

static const int WIDTH = 1280;
static const int HEIGHT = 720;

// ---------- Camera ----------
struct Camera {
    glm::vec3 pos{ 0.0f, 1.8f, 5.0f }; // eye at 1.8m above ground
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
double gLastX = WIDTH * 0.5, gLastY = HEIGHT * 0.5;
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
int  gWindowedW = WIDTH, gWindowedH = HEIGHT;

// NPC UI vs FPS title override
bool gNpcUIActive = false;

// HUD text strings
std::string gHudPrompt;
std::string gHudNpcLine;

// ---------- Collision ----------
struct AABB { glm::vec3 min, max; };
inline AABB boxFromTS(const glm::vec3& t, const glm::vec3& s) { return AABB{ t - s, t + s }; }

// ---------- NPC ----------
struct NPC {
    glm::vec3 pos{ 3.0f, 1.0f, -6.0f };
    glm::vec3 half{ 0.7f, 1.2f, 0.7f };
    bool talking = false;
    int  line = 0;
    std::vector<const char*> dialog{
        "Courier, you made it. Supplies are thin in this block.",
        "I need a crate recovered from the old warehouse near the wall.",
        "Watch for patrols. They do not miss twice.",
        "Come back alive. We still need you."
    };
};
NPC gNPC;

// ---------- Input edge helper ----------
bool pressed(GLFWwindow* w, int key) {
    static std::unordered_map<int, int> last;
    int s = glfwGetKey(w, key);
    bool p = (s == GLFW_PRESS) && (last[key] != GLFW_PRESS);
    last[key] = s;
    return p;
}

// ---------- Callbacks ----------
void framebuffer_size_callback(GLFWwindow*, int w, int h) { glViewport(0, 0, w, h); }

void cursor_pos_callback(GLFWwindow*, double x, double y) {
    if (!gMouseLocked) return;
    if (gFirstMouse) { gLastX = x; gLastY = y; gFirstMouse = false; }
    double xoff = x - gLastX;
    double yoff = gLastY - y;
    gLastX = x; gLastY = y;
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
        if (gMouseLocked) {
            gMouseLocked = false;
            glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
        else {
            glfwSetWindowShouldClose(w, GLFW_TRUE);
        }
    }
    if (key == GLFW_KEY_F11 && action == GLFW_PRESS) {
        toggleFullscreen();
    }
}

void mouse_button_callback(GLFWwindow* w, int button, int action, int) {
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        if (!gMouseLocked) {
            gMouseLocked = true;
            gFirstMouse = true;
            double cx, cy; glfwGetCursorPos(w, &cx, &cy);
            gLastX = cx; gLastY = cy;
            glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
    }
}

void window_focus_callback(GLFWwindow* w, int focused) {
    if (focused && gMouseLocked) {
        gFirstMouse = true;
        glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }
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
GLuint gNPCTexture = 0;

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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    stbi_image_free(data);
    return tex;
}

// ---------- Meshes ----------
struct Mesh { GLuint vao = 0, vbo = 0; GLsizei count = 0; };

Mesh makeGroundPlane(float half = 50.f) {
    float y = 0, s = half;
    float v[] = {
        -s,y,-s, .35,.38,.40,  s,y,-s, .35,.38,.40,  s,y, s, .35,.38,.40,
        -s,y,-s, .35,.38,.40,  s,y, s, .35,.38,.40, -s,y, s, .35,.38,.40
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
    m.count = 6;
    return m;
}

// ---------- World-space shader ----------
const char* kVS = R"(#version 330 core
layout (location=0) in vec3 aPos;
layout (location=1) in vec3 aCol;
uniform mat4 uMVP;
out vec3 vCol;
void main(){
    vCol = aCol;
    gl_Position = uMVP * vec4(aPos,1.0);
})";

const char* kFS = R"(#version 330 core
in vec3 vCol;
out vec4 FragColor;
void main(){ FragColor = vec4(vCol,1.0); })";

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
    float scale = 1.0f)
{
    if (text.empty()) return;

    static char rawBuffer[20000];

    // Generate the geometry at origin (0,0)
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

        // scale around (0,0) then offset to (x,y)
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

// ============ Dialog box (Pokemon style) using same HUD shader ============

GLuint gDialogVAO = 0, gDialogVBO = 0;

void initDialogBox() {
    glGenVertexArrays(1, &gDialogVAO);
    glGenBuffers(1, &gDialogVBO);

    glBindVertexArray(gDialogVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gDialogVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 8, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);
}

void drawDialogBoxWithText(const std::string& text, int fbw, int fbh) {
    if (text.empty() || !gHudProg || !gDialogVAO) return;

    float marginX = fbw * 0.05f;
    float marginY = fbh * 0.05f;
    float boxHeight = fbh * 0.22f;

    float x0 = marginX;
    float x1 = fbw - marginX;
    float y1 = fbh - marginY;
    float y0 = y1 - boxHeight;

    float verts[8] = {
        x0, y0,
        x1, y0,
        x1, y1,
        x0, y1
    };

    glUseProgram(gHudProg);
    glUniform2f(gHudScreenSizeLoc, (float)fbw, (float)fbh);

    glBindVertexArray(gDialogVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gDialogVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);

    glDisable(GL_DEPTH_TEST);

    // fill
    glUniform3f(gHudColorLoc, 0.03f, 0.03f, 0.08f);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // border
    glLineWidth(3.0f);
    glUniform3f(gHudColorLoc, 1.0f, 1.0f, 1.0f);
    glDrawArrays(GL_LINE_LOOP, 0, 4);

    glEnable(GL_DEPTH_TEST);

    float textX = x0 + 20.0f;
    float textY = y0 + 24.0f;
    drawTextScreen(text, textX, textY, fbw, fbh, glm::vec3(1.0f, 1.0f, 1.0f),2.5f);
}

// ================= ASSIMP TEXTURED MODEL =================

struct SimpleVertex {
    glm::vec3 pos;
    glm::vec2 uv;
};

struct AssimpModel {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLsizei vertexCount = 0;

    bool load(const std::string& path) {
        Assimp::Importer importer;

        const aiScene* scene = importer.ReadFile(
            path,
            aiProcess_Triangulate |
            aiProcess_JoinIdenticalVertices
        );

        if (!scene || !scene->HasMeshes()) {
            std::cerr << "Assimp failed: " << importer.GetErrorString() << "\n";
            return false;
        }

        aiMesh* mesh = scene->mMeshes[0];

        std::vector<SimpleVertex> verts;
        verts.reserve(mesh->mNumFaces * 3);

        for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices != 3) continue;

            for (unsigned int i = 0; i < 3; ++i) {
                unsigned int idx = face.mIndices[i];

                SimpleVertex v{};
                const aiVector3D& p = mesh->mVertices[idx];
                v.pos = glm::vec3(p.x, p.y, p.z);

                if (mesh->mTextureCoords[0]) {
                    const aiVector3D& t = mesh->mTextureCoords[0][idx];
                    v.uv = glm::vec2(t.x, t.y);
                }
                else {
                    v.uv = glm::vec2(0.0f);
                }

                verts.push_back(v);
            }
        }

        if (verts.empty()) {
            std::cerr << "Assimp: no vertices generated from mesh\n";
            return false;
        }

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER,
            verts.size() * sizeof(SimpleVertex),
            verts.data(),
            GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
            sizeof(SimpleVertex), (void*)0);

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
            sizeof(SimpleVertex),
            (void*)offsetof(SimpleVertex, uv));

        glBindVertexArray(0);

        vertexCount = (GLsizei)verts.size();
        std::cout << "Assimp loaded: " << path
            << " vertices: " << vertexCount << "\n";
        return true;
    }

    void draw() const {
        if (!vao || !vertexCount) return;
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, vertexCount);
        glBindVertexArray(0);
    }
};

AssimpModel gNPCModel;

const char* kObjVS = R"(#version 330 core
layout (location=0) in vec3 aPos;
layout (location=1) in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main(){
    vUV = aUV;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* kObjFS = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
out vec4 FragColor;
void main(){
    FragColor = texture(uTex, vUV);
}
)";

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

// ---------- Movement with jump + gravity + NPC collision ----------
void processMovement(float dt, const std::vector<AABB>& boxes) {
    float speed = gCam.moveSpeed;
    if (glfwGetKey(gWindow, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
        glfwGetKey(gWindow, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
        speed *= gCam.sprintMult;
    }

    glm::vec3 f{ cosf(glm::radians(gCam.yaw)), 0.0f, sinf(glm::radians(gCam.yaw)) };
    f = glm::normalize(f);
    glm::vec3 r = glm::normalize(glm::cross(f, glm::vec3(0, 1, 0)));

    glm::vec3 vel(0);
    if (glfwGetKey(gWindow, GLFW_KEY_W) == GLFW_PRESS) vel += f;
    if (glfwGetKey(gWindow, GLFW_KEY_S) == GLFW_PRESS) vel -= f;
    if (glfwGetKey(gWindow, GLFW_KEY_A) == GLFW_PRESS) vel -= r;
    if (glfwGetKey(gWindow, GLFW_KEY_D) == GLFW_PRESS) vel += r;
    if (glm::length(vel) > 0) vel = glm::normalize(vel) * speed;

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
}

// ---------- Main ----------
int main() {
    if (!glfwInit()) { std::cerr << "GLFW init failed\n"; return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

    gWindow = glfwCreateWindow(WIDTH, HEIGHT, "Exit Strategy", nullptr, nullptr);
    if (!gWindow) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(gWindow);
    glfwSwapInterval(1);

    glfwSetFramebufferSizeCallback(gWindow, framebuffer_size_callback);
    glfwSetCursorPosCallback(gWindow, cursor_pos_callback);
    glfwSetKeyCallback(gWindow, key_callback);
    glfwSetMouseButtonCallback(gWindow, mouse_button_callback);
    glfwSetWindowFocusCallback(gWindow, window_focus_callback);
    glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) { glfwDestroyWindow(gWindow); glfwTerminate(); return 1; }

    int fbw, fbh; glfwGetFramebufferSize(gWindow, &fbw, &fbh);
    glViewport(0, 0, fbw, fbh);
    glEnable(GL_DEPTH_TEST);

    Mesh ground = makeGroundPlane(60.0f);
    GLuint prog = linkProgram(kVS, kFS);
    GLint uMVP = glGetUniformLocation(prog, "uMVP");

    initCrosshair();
    initHudText();
    initDialogBox();

    gObjProg = linkProgram(kObjVS, kObjFS);
    gObjMVP = glGetUniformLocation(gObjProg, "uMVP");
    gObjTex = glGetUniformLocation(gObjProg, "uTex");

    gNPCModel.load("assets/npc.obj");
    gNPCTexture = loadTexture2D("assets/man_t256.png");

    // Colliders: only NPC for now
    std::vector<AABB> colliders;
    colliders.push_back(AABB{ gNPC.pos - gNPC.half, gNPC.pos + gNPC.half });

    double last = glfwGetTime();
    double fpsTimer = last;
    int frames = 0;
    float fps = 0.0f;

    while (!glfwWindowShouldClose(gWindow)) {
        double now = glfwGetTime();
        float dt = float(now - last); last = now;

        glfwPollEvents();
        processMovement(dt, colliders);

        glClearColor(0.10f, 0.12f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 V = gCam.getView();
        glfwGetFramebufferSize(gWindow, &fbw, &fbh);
        float aspect = fbh > 0 ? float(fbw) / float(fbh) : 16.f / 9.f;
        glm::mat4 P = glm::perspective(glm::radians(gCam.fov), aspect, 0.1f, 200.0f);

        gNpcUIActive = false;
        gHudPrompt.clear();
        gHudNpcLine.clear();

        glm::vec3 camForward{
            cosf(glm::radians(gCam.yaw)) * cosf(glm::radians(gCam.pitch)),
            sinf(glm::radians(gCam.pitch)),
            sinf(glm::radians(gCam.yaw)) * cosf(glm::radians(gCam.pitch))
        };
        camForward = glm::normalize(camForward);

        AABB npcBox{ gNPC.pos - gNPC.half, gNPC.pos + gNPC.half };
        float tHit = rayAABB(gCam.pos, camForward, npcBox);
        bool lookingAt = tHit > 0.0f && tHit < 3.0f;

        if (!gNPC.talking) {
            if (lookingAt) {
                gNpcUIActive = true;
                gHudPrompt = "Press E to talk";
                if (pressed(gWindow, GLFW_KEY_E)) {
                    gNPC.talking = true;
                    gNPC.line = 0;
                    gHudNpcLine = gNPC.dialog[gNPC.line];
                }
            }
        }
        else {
            gNpcUIActive = true;
            gHudNpcLine = gNPC.dialog[gNPC.line];

            if (pressed(gWindow, GLFW_KEY_ENTER)) {
                gNPC.line++;
                if (gNPC.line >= (int)gNPC.dialog.size()) {
                    gNPC.talking = false;
                    gNpcUIActive = false;
                    gHudNpcLine.clear();
                }
                else {
                    gHudNpcLine = gNPC.dialog[gNPC.line];
                }
            }
            if (pressed(gWindow, GLFW_KEY_E)) {
                gNPC.talking = false;
                gNpcUIActive = false;
                gHudNpcLine.clear();
            }
        }

        // Render ground
        glUseProgram(prog);
        {
            glm::mat4 MVP = P * V * glm::mat4(1.0f);
            glUniformMatrix4fv(uMVP, 1, GL_FALSE, glm::value_ptr(MVP));
            glBindVertexArray(ground.vao);
            glDrawArrays(GL_TRIANGLES, 0, ground.count);
        }

        // Render NPC model
        {
            glm::vec3 npcWorldPos(gNPC.pos.x, 0.0f, gNPC.pos.z);
            glm::mat4 model = glm::translate(glm::mat4(1.0f), npcWorldPos) *
                glm::scale(glm::mat4(1.0f), glm::vec3(1.0f));
            glm::mat4 MVP = P * V * model;

            glUseProgram(gObjProg);
            glUniformMatrix4fv(gObjMVP, 1, GL_FALSE, glm::value_ptr(MVP));

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, gNPCTexture);
            glUniform1i(gObjTex, 0);

            gNPCModel.draw();
        }

        // Crosshair
        drawCrosshairNDC(fbw, fbh);

        // HUD text
        if (!gHudPrompt.empty()) {
            float promptY = fbh * 0.28f;
            float promptX = fbw * 0.5f - (gHudPrompt.size() * 4.0f);
            drawTextScreen(gHudPrompt, promptX, promptY, fbw, fbh, glm::vec3(1.0f, 1.0f, 0.7f),2.5f);
        }

        if (!gHudNpcLine.empty()) {
            drawDialogBoxWithText(gHudNpcLine, fbw, fbh);
        }

        glfwSwapBuffers(gWindow);

        // FPS counter
        frames++;
        double current = glfwGetTime();
        if (current - fpsTimer >= 0.5) {
            fps = frames / float(current - fpsTimer);
            fpsTimer = current;
            frames = 0;

            if (!gNpcUIActive && !gNPC.talking) {
                std::ostringstream oss; oss << int(fps);
                std::string title = "Exit Strategy - FPS: " + oss.str();
                glfwSetWindowTitle(gWindow, title.c_str());
            }
        }
    }

    glDeleteVertexArrays(1, &ground.vao); glDeleteBuffers(1, &ground.vbo);
    glDeleteProgram(prog);

    glDeleteVertexArrays(1, &gCrossVAO);
    glDeleteBuffers(1, &gCrossVBO);
    glDeleteProgram(gCrossProg);

    glDeleteVertexArrays(1, &gHudVAO);
    glDeleteBuffers(1, &gHudVBO);
    glDeleteProgram(gHudProg);

    glDeleteVertexArrays(1, &gDialogVAO);
    glDeleteBuffers(1, &gDialogVBO);

    glDeleteProgram(gObjProg);
    glDeleteTextures(1, &gNPCTexture);

    glfwDestroyWindow(gWindow);
    glfwTerminate();
    return 0;
}
