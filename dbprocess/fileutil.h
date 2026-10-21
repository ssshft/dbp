#pragma once

#include <unordered_map>
#include <string>
#include <fstream>
#include <iostream>
#include <cstdio>

// 写 dbptopicid：**先写 .tmp 再 rename**。
//
// 为什么不能直接 ofstream(filename)：ofstream 默认打开即截断，而 PreStart 现在会校验
// 这个文件里的 id 必须构成 0..snapscount-1 的排列。进程如果在"截断之后、写完之前"
// 被杀（热加路径每加一个新对子都会重写一次，窗口反复出现），下次启动看到的就是
// 空/半截文件 ⇒ snapscount=0 ⇒ 拒绝启动，只能人工删 CSV + shm 重建。
// rename(2) 在同一文件系统上是原子的 ⇒ 磁盘上永远是"完整的旧版"或"完整的新版"。
//
// 注意：写完必须检查流状态再 rename，否则磁盘满时会用半截文件覆盖掉好文件。
inline void saveMapToFile(const std::unordered_map<std::string, int>& mTopicId, const std::string& filename) {
    const std::string tmp = filename + ".tmp";
    {
        std::ofstream ofs(tmp, std::ios::trunc);
        if (!ofs.is_open()) {
            std::cerr << "save open file failed: " << tmp << std::endl;
            return;
        }

        for (const auto& [key, value] : mTopicId) {
            ofs << key << "," << value << "\n";
        }
        ofs.flush();

        // 写失败（磁盘满/IO 错）就丢弃 tmp，保住原来那份好的
        if (!ofs) {
            std::cerr << "save write failed, keep old file: " << filename << std::endl;
            ofs.close();
            std::remove(tmp.c_str());
            return;
        }
        ofs.close();
    }

    if (std::rename(tmp.c_str(), filename.c_str()) != 0) {
        std::cerr << "save rename failed: " << tmp << " -> " << filename << std::endl;
        std::remove(tmp.c_str());
    }
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