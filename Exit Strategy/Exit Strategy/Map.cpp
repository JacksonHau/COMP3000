#include "Map.h"

#include <iostream>
#include <sstream>
#include <string>

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

    // ExitKey (optional)
    if (auto* key = map->FirstChildElement("ExitKey")) {
        gMap.exitKey = { key->IntAttribute("x"), key->IntAttribute("z") };
    } else {
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
        if (gMap.width  <= 0) gMap.width  = (int)lines[0].size();

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
