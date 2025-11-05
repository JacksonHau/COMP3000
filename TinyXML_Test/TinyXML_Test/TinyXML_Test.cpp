#include <iostream>
#include "tinyxml2.h"
using namespace tinyxml2;

int main() {
    XMLDocument doc;

    // Create a new XML structure
    XMLElement* root = doc.NewElement("Player");
    root->SetAttribute("name", "Alex");
    root->SetAttribute("health", 100);
    doc.InsertFirstChild(root);

    XMLElement* inventory = doc.NewElement("Inventory");
    inventory->SetText("Pistol, Medkit, Ammo");
    root->InsertEndChild(inventory);

    // Save to file
    doc.SaveFile("player.xml");
    std::cout << "XML file created: player.xml" << std::endl;

    // Now read it back
    XMLDocument readDoc;
    readDoc.LoadFile("player.xml");
    XMLElement* readRoot = readDoc.FirstChildElement("Player");
    std::cout << "Name: " << readRoot->Attribute("name") << std::endl;
    std::cout << "Health: " << readRoot->IntAttribute("health") << std::endl;
    std::cout << "Inventory: "
        << readRoot->FirstChildElement("Inventory")->GetText() << std::endl;

    return 0;
}
