#pragma once

#include <unordered_map>
#include <string>
#include <fstream>
#include <iostream>

inline void saveMapToFile(const std::unordered_map<std::string, int>& mTopicId, const std::string& filename) {
    std::ofstream ofs(filename);
    if (!ofs.is_open()) {
        std::cerr << "save open file failed: " << filename << std::endl;
        return;
    }

    for (const auto& [key, value] : mTopicId) {
        ofs << key << "," << value << "\n";
    }
    ofs.close();
}

inline void loadMapFromFile(std::unordered_map<std::string, int>& mTopicId, const std::string& filename) {
    std::ifstream ifs(filename);
    if (!ifs.is_open()) {
        std::cerr << "read open file failed: " << filename << std::endl;
        return;
    }

    mTopicId.clear();

    std::string line = "";
    while (std::getline(ifs, line)) {
        if (line.empty()) {
            continue;
        }

        auto pos = line.find(",");
        if (pos == std::string::npos) {
            continue;
        }

        std::string key = line.substr(0, pos);
        int value = std::stoi(line.substr(pos + 1));

        mTopicId.emplace(std::move(key), value);
    }

    ifs.close();
}