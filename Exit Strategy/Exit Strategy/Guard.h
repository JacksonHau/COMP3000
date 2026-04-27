#pragma once

#include <glm/glm.hpp>
#include <GL/glew.h>
#include <vector>
#include <string>
#include "UI.h"

struct AssimpModel;

enum class GuardState {
    Patrol,
    Chase,
    Search,
    Stunned
};

struct Guard {
    glm::vec3 pos{ 0.0f };
    float yaw = 0.0f;

    float speed = 2.0f;
    float patrolSpeed = 1.2f;
    float chaseSpeed = 2.2f;
    float detectionRange = 10.0f;
    float loseRange = 14.0f;
    float modelScale = 0.6f;

    bool active = true;
    AssimpModel* model = nullptr;
    GLuint texture = 0;

    GuardState state = GuardState::Patrol;
    bool alerted = false;

    std::vector<glm::vec3> patrolPoints;
    int currentPatrolIndex = 0;
    float patrolWaitTimer = 0.0f;

    bool stunned = false;
    float stunTimer = 0.0f;

    glm::vec3 lastKnownPlayerPos{ 0.0f };
    float searchTimer = 0.0f;
    float searchDuration = 2.5f;

    // Pathfinding
    std::vector<glm::ivec2> currentPath;
    float repathTimer = 0.0f;

    Guard();
    ~Guard();

    void update(float dt, const glm::vec3& playerPos, const std::vector<AABB>& colliders);
    void render(const glm::mat4& view, const glm::mat4& proj, GLuint shader, GLint mvpLoc, GLint texLoc);
    bool loadModel(const std::string& objPath, const std::string& texPath);
    void cleanup();

    float getGroundOffset() const;

private:
    void moveTowards(const glm::vec3& target, float moveSpeed, float dt, const std::vector<AABB>& colliders);
    bool canSeePlayer(const glm::vec3& playerPos, const std::vector<AABB>& colliders) const;
};

extern std::vector<Guard> gGuards;