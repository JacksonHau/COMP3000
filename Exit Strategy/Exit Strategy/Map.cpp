#include "Map.h"

#include <iostream>
#include <sstream>
#include <string>

#include <random>
#include <chrono>
#include <stack>

#include "tinyxml2.h"
using namespace tinyxml2;

MapData gMap;

bool loadMapXML(const char* path) {
    XMLDocument doc;
    XMLError err = doc.LoadFile(path);
    if (err != XML_SUCCESS) {
        std::cerr << "TinyXML2: failed to load " << path << " error: " << doc.ErrorStr() << "\n";
        return false;
    }

    XMLElement* map = doc.FirstChildElement("Map");
    if (!map) {
        std::cerr << "TinyXML2: <Map> root not found\n";
        return false;
    }

    map->QueryIntAttribute("width", &gMap.width);
    map->QueryIntAttribute("height", &gMap.height);
    map->QueryFloatAttribute("tileSize", &gMap.tileSize);

    auto* spawn = map->FirstChildElement("Spawn");
    if (spawn) gMap.spawn = { spawn->IntAttribute("x"), spawn->IntAttribute("z") };

    auto* exit = map->FirstChildElement("Exit");
    if (exit) gMap.exit = { exit->IntAttribute("x"), exit->IntAttribute("z") };

    auto* power = map->FirstChildElement("PowerCell");
    if (power) gMap.powerCell = { power->IntAttribute("x"), power->IntAttribute("z") };

    // ExitKey 
    if (auto* key = map->FirstChildElement("ExitKey")) {
        gMap.exitKey = { key->IntAttribute("x"), key->IntAttribute("z") };
    }
    else {
        gMap.exitKey = { -9999, -9999 };
    }
    gMap.hasExitKey = false;

    gMap.npcSpawns.clear();
    if (auto* npcs = map->FirstChildElement("NPCs")) {
        for (auto* n = npcs->FirstChildElement("NPC"); n; n = n->NextSiblingElement("NPC")) {
            gMap.npcSpawns.emplace_back(n->IntAttribute("x"), n->IntAttribute("z"));
        }
    }

    gMap.walls.clear();
    auto* maze = map->FirstChildElement("Maze");
    if (!maze || !maze->GetText()) {
        std::cerr << "TinyXML2: <Maze> missing or empty\n";
        return false;
    }

    // Parse ASCII grid
    std::vector<std::string> lines;
    {
        std::istringstream iss(maze->GetText());
        std::string line;
        while (std::getline(iss, line)) {
            // Trim CR
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            lines.push_back(line);
        }
    }

    if (!lines.empty()) {
        // If width/height are not set, infer
        if (gMap.height <= 0) gMap.height = (int)lines.size();
        if (gMap.width <= 0) gMap.width = (int)lines[0].size();

        for (int z = 0; z < (int)lines.size(); ++z) {
            const std::string& row = lines[z];
            for (int x = 0; x < (int)row.size(); ++x) {
                if (row[x] == '#') {
                    gMap.walls.emplace_back(x, z);
                }
            }
        }
    }

    return true;
}

// Random Maze
static unsigned int timeSeed() {
    return (unsigned int)std::chrono::high_resolution_clock::now().time_since_epoch().count();
}

void generateRandomMaze(int width, int height, unsigned int seed) {
    // Clamp + force odd sizes
    if (width < 7) width = 7;
    if (height < 7) height = 7;
    if (width % 2 == 0) width += 1;
    if (height % 2 == 0) height += 1;

    if (seed == 0) seed = timeSeed();
    std::mt19937 rng(seed);

    // Grid: 1 = wall, 0 = passage
    std::vector<uint8_t> grid(width * height, 1);
    auto idx = [&](int x, int z) { return z * width + x; };
    auto inBounds = [&](int x, int z) { return x > 0 && z > 0 && x < width - 1 && z < height - 1; };

    // Start at (1,1)
    int sx = 1, sz = 1;
    grid[idx(sx, sz)] = 0;

    struct Cell { int x, z; };
    std::stack<Cell> st;
    st.push({ sx, sz });

    const int dirs[4][2] = { {2,0},{-2,0},{0,2},{0,-2} };

    while (!st.empty()) {
        Cell c = st.top();

        // collect unvisited neighbors 2 steps away
        std::vector<Cell> choices;
        choices.reserve(4);
        for (int i = 0; i < 4; ++i) {
            int nx = c.x + dirs[i][0];
            int nz = c.z + dirs[i][1];
            if (!inBounds(nx, nz)) continue;
            if (grid[idx(nx, nz)] == 1) choices.push_back({ nx, nz });
        }

        if (choices.empty()) {
            st.pop();
            continue;
        }

        std::uniform_int_distribution<int> pick(0, (int)choices.size() - 1);
        Cell n = choices[pick(rng)];

        // carve wall between c and n
        int wx = (c.x + n.x) / 2;
        int wz = (c.z + n.z) / 2;
        grid[idx(wx, wz)] = 0;
        grid[idx(n.x, n.z)] = 0;

        st.push(n);
    }

    // Build MapData
    gMap = MapData{};
    gMap.width = width;
    gMap.height = height;
    gMap.tileSize = 1.0f;

    gMap.spawn = { 1, 1 };
    gMap.exit = { width - 2, height - 2 };

    // Place key + power cell somewhere in passages
    auto randomPassage = [&]() -> glm::ivec2 {
        std::uniform_int_distribution<int> rx(1, width - 2);
        std::uniform_int_distribution<int> rz(1, height - 2);
        for (int tries = 0; tries < 5000; ++tries) {
            int x = rx(rng);
            int z = rz(rng);
            if ((x == gMap.spawn.x && z == gMap.spawn.y) || (x == gMap.exit.x && z == gMap.exit.y)) continue;
            if (grid[idx(x, z)] == 0) return { x, z };
        }
        return { 1, 1 };
        };

    gMap.exitKey = randomPassage();
    gMap.powerCell = randomPassage();

    // Convert all wall-cells into wall list
    gMap.walls.clear();
    gMap.walls.reserve((size_t)width * (size_t)height);
    for (int z = 0; z < height; ++z) {
        for (int x = 0; x < width; ++x) {
            if (grid[idx(x, z)] == 1) {
                gMap.walls.push_back({ x, z });
            }
        }
    }

    // No NPCs in endless mode for now
    gMap.npcSpawns.clear();
}