#include "Guard.h"
#include "UI.h"
#include "Map.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <iostream>
#include <vector>
#include <map>
#include <queue>
#include <algorithm>

#include <filesystem>
namespace fs = std::filesystem;

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "stb_image.h"

// ---------- Create texture from color ----------
static GLuint createColorTexture(unsigned char r, unsigned char g, unsigned char b) {
    unsigned char color[] = { r, g, b, 255 };
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, color);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return tex;
}

// ---------- Load texture from file ----------
static GLuint loadTextureFromFile(const std::string& path) {
    int w, h, channels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!data) {
        std::cerr << "Failed to load texture: " << path << "\n";
        return 0;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    stbi_image_free(data);
    return tex;
}

// ---------- Simple vertex ----------
struct SimpleVertex {
    glm::vec3 pos;
    glm::vec2 uv;
    glm::vec3 normal;
};

// ---------- Mesh with its own material ----------
struct MeshPart {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLsizei vertexCount = 0;
    GLuint texture = 0;
    float minY = 0.0f;
    float maxY = 0.0f;

    void cleanup() {
        if (vao) glDeleteVertexArrays(1, &vao);
        if (vbo) glDeleteBuffers(1, &vbo);
    }
};

// ---------- Model with multiple meshes ----------
struct AssimpModel {
    std::vector<MeshPart> meshes;
    float overallMinY = 0.0f;
    float overallMaxY = 0.0f;

    bool load(const std::string& path) {
        Assimp::Importer importer;

        const aiScene* scene = importer.ReadFile(
            path,
            aiProcess_Triangulate |
            aiProcess_JoinIdenticalVertices |
            aiProcess_GenSmoothNormals |
            aiProcess_FlipUVs
        );

        if (!scene || !scene->HasMeshes()) {
            std::cerr << "Assimp failed: " << importer.GetErrorString() << "\n";
            return false;
        }

        overallMinY = 999999.0f;
        overallMaxY = -999999.0f;

        for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
            aiMesh* mesh = scene->mMeshes[m];
            MeshPart part;

            std::vector<SimpleVertex> verts;
            verts.reserve(mesh->mNumFaces * 3);

            float meshMinY = 999999.0f;
            float meshMaxY = -999999.0f;

            for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
                const aiFace& face = mesh->mFaces[f];
                if (face.mNumIndices != 3) continue;

                for (unsigned int i = 0; i < 3; ++i) {
                    unsigned int idx = face.mIndices[i];
                    SimpleVertex v{};

                    const aiVector3D& p = mesh->mVertices[idx];
                    v.pos = glm::vec3(p.x, p.y, p.z);

                    if (p.y < meshMinY) meshMinY = p.y;
                    if (p.y > meshMaxY) meshMaxY = p.y;

                    if (mesh->mTextureCoords[0]) {
                        const aiVector3D& t = mesh->mTextureCoords[0][idx];
                        v.uv = glm::vec2(t.x, t.y);
                    }
                    else {
                        v.uv = glm::vec2(0.0f);
                    }

                    if (mesh->mNormals) {
                        const aiVector3D& n = mesh->mNormals[idx];
                        v.normal = glm::vec3(n.x, n.y, n.z);
                    }
                    else {
                        v.normal = glm::vec3(0, 1, 0);
                    }

                    verts.push_back(v);
                }
            }

            if (verts.empty()) continue;

            // Create VAO/VBO for this mesh part
            glGenVertexArrays(1, &part.vao);
            glGenBuffers(1, &part.vbo);
            glBindVertexArray(part.vao);
            glBindBuffer(GL_ARRAY_BUFFER, part.vbo);
            glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(SimpleVertex), verts.data(), GL_STATIC_DRAW);

            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SimpleVertex), (void*)0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SimpleVertex), (void*)offsetof(SimpleVertex, uv));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(SimpleVertex), (void*)offsetof(SimpleVertex, normal));

            glBindVertexArray(0);

            part.vertexCount = (GLsizei)verts.size();
            part.minY = meshMinY;
            part.maxY = meshMaxY;

            // Get material for this mesh
            if (scene->HasMaterials() && mesh->mMaterialIndex < scene->mNumMaterials) {
                aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];

                aiString texPath;
                if (material->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
                    std::filesystem::path objDir = std::filesystem::path(path).parent_path();
                    std::filesystem::path fullTexPath = objDir / texPath.C_Str();

                    part.texture = loadTextureFromFile(fullTexPath.string());

                    if (part.texture) {
                        std::cout << "Loaded material texture: " << fullTexPath.string() << "\n";
                    }
                }

                if (!part.texture) {
                    aiColor3D diffuse(0.8f, 0.8f, 0.8f);
                    if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse)) {
                        unsigned char r = (unsigned char)(diffuse.r * 255);
                        unsigned char g = (unsigned char)(diffuse.g * 255);
                        unsigned char b = (unsigned char)(diffuse.b * 255);
                        part.texture = createColorTexture(r, g, b);
                    }
                    else {
                        part.texture = createColorTexture(128, 128, 128);
                    }
                }
            }
            else {
                part.texture = createColorTexture(128, 128, 128);
            }

            if (meshMinY < overallMinY) overallMinY = meshMinY;
            if (meshMaxY > overallMaxY) overallMaxY = meshMaxY;

            meshes.push_back(part);
        }

        std::cout << "Loaded " << meshes.size() << " mesh parts\n";
        std::cout << "Overall bounds: minY=" << overallMinY << " maxY=" << overallMaxY << "\n";
        return !meshes.empty();
    }

    void draw() const {
        for (const auto& part : meshes) {
            if (!part.vao || !part.vertexCount) continue;
            glBindTexture(GL_TEXTURE_2D, part.texture);
            glBindVertexArray(part.vao);
            glDrawArrays(GL_TRIANGLES, 0, part.vertexCount);
        }
        glBindVertexArray(0);
    }

    void cleanup() {
        for (auto& part : meshes) {
            part.cleanup();
            if (part.texture) glDeleteTextures(1, &part.texture);
        }
        meshes.clear();
    }
};

// ---------- Maze helpers for pathfinding ----------
static bool isWallCell(int x, int z) {
    for (const auto& w : gMap.walls) {
        if (w.x == x && w.y == z) return true;
    }
    return false;
}

static glm::ivec2 worldToCell(const glm::vec3& p) {
    const float WORLD_SIZE = 90.0f;
    const float ORIGIN_X = -WORLD_SIZE * 0.5f;
    const float ORIGIN_Z = -WORLD_SIZE * 0.5f;

    float cellX = WORLD_SIZE / (float)gMap.width;
    float cellZ = WORLD_SIZE / (float)gMap.height;

    int cx = (int)((p.x - ORIGIN_X) / cellX);
    int cz = (int)((p.z - ORIGIN_Z) / cellZ);

    cx = glm::clamp(cx, 0, gMap.width - 1);
    cz = glm::clamp(cz, 0, gMap.height - 1);
    return glm::ivec2(cx, cz);
}

static glm::vec3 cellToWorld(const glm::ivec2& cell, float y) {
    const float WORLD_SIZE = 90.0f;
    const float ORIGIN_X = -WORLD_SIZE * 0.5f;
    const float ORIGIN_Z = -WORLD_SIZE * 0.5f;

    float cellX = WORLD_SIZE / (float)gMap.width;
    float cellZ = WORLD_SIZE / (float)gMap.height;

    float wx = ORIGIN_X + (cell.x + 0.5f) * cellX;
    float wz = ORIGIN_Z + (cell.y + 0.5f) * cellZ;
    return glm::vec3(wx, y, wz);
}

static std::vector<glm::ivec2> findPathBFS(glm::ivec2 start, glm::ivec2 goal) {
    if (start == goal) return {};

    std::queue<glm::ivec2> q;
    std::vector<std::vector<bool>> visited(gMap.height, std::vector<bool>(gMap.width, false));
    std::vector<std::vector<glm::ivec2>> parent(
        gMap.height,
        std::vector<glm::ivec2>(gMap.width, glm::ivec2(-1, -1))
    );

    q.push(start);
    visited[start.y][start.x] = true;

    const glm::ivec2 dirs[4] = {
        { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }
    };

    while (!q.empty()) {
        glm::ivec2 cur = q.front();
        q.pop();

        if (cur == goal) break;

        for (const auto& d : dirs) {
            glm::ivec2 nxt = cur + d;

            if (nxt.x < 0 || nxt.x >= gMap.width || nxt.y < 0 || nxt.y >= gMap.height) continue;
            if (visited[nxt.y][nxt.x]) continue;
            if (isWallCell(nxt.x, nxt.y)) continue;

            visited[nxt.y][nxt.x] = true;
            parent[nxt.y][nxt.x] = cur;
            q.push(nxt);
        }
    }

    if (!visited[goal.y][goal.x]) {
        return {};
    }

    std::vector<glm::ivec2> path;
    for (glm::ivec2 cur = goal; cur != start; cur = parent[cur.y][cur.x]) {
        path.push_back(cur);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

// ---------- Guard implementation ----------
Guard::Guard() : model(new AssimpModel()) {}
Guard::~Guard() { delete model; }

bool Guard::loadModel(const std::string& objPath, const std::string& texPath) {
    if (!model->load(objPath)) {
        return false;
    }

    if (!texPath.empty()) {
        GLuint forcedTex = loadTextureFromFile(texPath);
        if (forcedTex) {
            for (auto& mesh : model->meshes) {
                if (mesh.texture) {
                    glDeleteTextures(1, &mesh.texture);
                }
                mesh.texture = forcedTex;
            }
        }
    }

    return true;
}

void Guard::moveTowards(const glm::vec3& target, float moveSpeed, float dt, const std::vector<AABB>& colliders) {
    glm::vec3 toTarget = target - pos;
    toTarget.y = 0.0f;

    float dist = glm::length(toTarget);
    if (dist < 0.001f) return;

    glm::vec3 dir = glm::normalize(toTarget);
    glm::vec3 oldPos = pos;
    glm::vec3 newPos = oldPos + dir * moveSpeed * dt;

    const float radius = 0.6f;

    float dx = newPos.x - oldPos.x;
    for (const auto& b : colliders) {
        float minX = b.min.x - radius;
        float maxX = b.max.x + radius;
        float minZ = b.min.z - radius;
        float maxZ = b.max.z + radius;

        if (newPos.z > minZ && newPos.z < maxZ) {
            if (dx > 0 && oldPos.x <= minX && newPos.x > minX) newPos.x = minX;
            if (dx < 0 && oldPos.x >= maxX && newPos.x < maxX) newPos.x = maxX;
        }
    }

    float dz = newPos.z - oldPos.z;
    for (const auto& b : colliders) {
        float minX = b.min.x - radius;
        float maxX = b.max.x + radius;
        float minZ = b.min.z - radius;
        float maxZ = b.max.z + radius;

        if (newPos.x > minX && newPos.x < maxX) {
            if (dz > 0 && oldPos.z <= minZ && newPos.z > minZ) newPos.z = minZ;
            if (dz < 0 && oldPos.z >= maxZ && newPos.z < maxZ) newPos.z = maxZ;
        }
    }

    pos = newPos;
    yaw = glm::degrees(atan2(dir.x, dir.z));
}

bool Guard::canSeePlayer(const glm::vec3& playerPos, const std::vector<AABB>& colliders) const {
    glm::vec3 eyePos = pos + glm::vec3(0.0f, 1.2f, 0.0f);
    glm::vec3 targetPos = playerPos;
    targetPos.y = eyePos.y;

    glm::vec3 ray = targetPos - eyePos;
    float maxDist = glm::length(ray);

    if (maxDist <= 0.001f) return true;

    glm::vec3 dir = glm::normalize(ray);

    for (const auto& b : colliders) {
        glm::vec3 t1 = (b.min - eyePos) / dir;
        glm::vec3 t2 = (b.max - eyePos) / dir;

        glm::vec3 tmin = glm::min(t1, t2);
        glm::vec3 tmax = glm::max(t1, t2);

        float tNear = std::max(std::max(tmin.x, tmin.y), tmin.z);
        float tFar = std::min(std::min(tmax.x, tmax.y), tmax.z);

        if (tFar < 0.0f || tNear > tFar) continue;

        // Wall is between guard and player
        if (tNear > 0.0f && tNear < maxDist) {
            return false;
        }
    }

    return true;
}

void Guard::update(float dt, const glm::vec3& playerPos, const std::vector<AABB>& colliders) {
    if (!active || !model || model->meshes.empty()) return;

    glm::vec3 flatPlayer = playerPos;
    flatPlayer.y = pos.y;

    float distToPlayer = glm::length(glm::vec3(flatPlayer.x - pos.x, 0.0f, flatPlayer.z - pos.z));
    bool hasLOS = canSeePlayer(playerPos, colliders);

    // Handle stun first
    if (stunned) {
        stunTimer -= dt;
        if (stunTimer <= 0.0f) {
            stunned = false;
            state = alerted ? GuardState::Chase : GuardState::Patrol;
        }
        return;
    }

    // First sighting permanently alerts the guard
    if (distToPlayer <= detectionRange && hasLOS) {
        alerted = true;
    }

    // Full no-mercy mode
    if (alerted) {
        state = GuardState::Chase;
        lastKnownPlayerPos = flatPlayer;
    }

    switch (state) {
    case GuardState::Patrol: {
        if (patrolPoints.empty()) return;

        glm::vec3 target = patrolPoints[currentPatrolIndex];
        target.y = pos.y;

        glm::vec3 delta = target - pos;
        delta.y = 0.0f;
        float dist = glm::length(delta);

        if (dist < 0.3f) {
            patrolWaitTimer += dt;
            if (patrolWaitTimer >= 1.0f) {
                patrolWaitTimer = 0.0f;
                currentPatrolIndex = (currentPatrolIndex + 1) % patrolPoints.size();
            }
        }
        else {
            moveTowards(target, patrolSpeed, dt, colliders);
        }
        break;
    }

    case GuardState::Chase: {
        repathTimer -= dt;

        glm::ivec2 guardCell = worldToCell(pos);
        glm::ivec2 playerCell = worldToCell(lastKnownPlayerPos);

        if (repathTimer <= 0.0f || currentPath.empty()) {
            currentPath = findPathBFS(guardCell, playerCell);
            repathTimer = 0.20f;
        }

        if (!currentPath.empty()) {
            glm::vec3 nextTarget = cellToWorld(currentPath.front(), pos.y);

            glm::vec3 delta = nextTarget - pos;
            delta.y = 0.0f;

            if (glm::length(delta) < 0.45f) {
                currentPath.erase(currentPath.begin());
            }
            else {
                moveTowards(nextTarget, chaseSpeed, dt, colliders);
            }
        }
        else {
            // Fallback if path somehow fails
            moveTowards(lastKnownPlayerPos, chaseSpeed, dt, colliders);
        }
        break;
    }

    case GuardState::Search:
        state = GuardState::Chase;
        break;

    case GuardState::Stunned:
        break;
    }
}

void Guard::render(const glm::mat4& view, const glm::mat4& proj, GLuint shader, GLint mvpLoc, GLint texLoc) {
    if (!active || !model || model->meshes.empty()) return;

    glUseProgram(shader);

    glm::mat4 modelMat = glm::translate(glm::mat4(1.0f), pos);
    modelMat = glm::rotate(modelMat, glm::radians(yaw), glm::vec3(0, 1, 0));
    modelMat = glm::scale(modelMat, glm::vec3(modelScale));

    // Lift model so lowest point touches ground
    modelMat = modelMat * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, getGroundOffset(), 0.0f));

    glm::mat4 mvp = proj * view * modelMat;
    glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, glm::value_ptr(mvp));

    model->draw();
}

void Guard::cleanup() {
    if (model) model->cleanup();
}

float Guard::getGroundOffset() const {
    if (!model) return 0.0f;
    return -model->overallMinY;
}

Guard gGuard;