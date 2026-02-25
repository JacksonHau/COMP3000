#pragma once

#include <glm/glm.hpp>
#include <GL/glew.h>
#include <vector>
#include <string>
#include "UI.h"

struct AssimpModel;

struct Guard {
    glm::vec3 pos;
    float yaw = 0.0f;
    float speed = 2.0f;
    float detectionRange = 15.0f;
    bool active = true;
    AssimpModel* model = nullptr;
    GLuint texture = 0;

    Guard();
    ~Guard();

    void update(float dt, const glm::vec3& playerPos, const std::vector<AABB>& colliders);
    void render(const glm::mat4& view, const glm::mat4& proj, GLuint shader, GLint mvpLoc, GLint texLoc);
    bool loadModel(const std::string& objPath, const std::string& texPath);
    void cleanup();

    float getGroundOffset() const;
};

extern Guard gGuard;