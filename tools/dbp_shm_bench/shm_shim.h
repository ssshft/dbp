// 只为在 macOS 上跑 ShmSpmcQueue 的 benchmark 用。
//
// 为什么要这个：ShmSpmcQueue 的 openOrCreate 用 flock 保护"创建者初始化"这段临界区，
// 而 macOS 对 POSIX shm fd 上的 flock 返回 ENOTSUP（Linux 支持）。生产跑在 Linux 上，
// 所以这不是代码问题，是本机跑不了。
//
// 办法：把 shm_open/shm_unlink 重定向到 /tmp 下的普通文件。普通文件上
// flock / ftruncate / mmap(MAP_SHARED) 全部可用，被测量的东西
// （seq_cst 原子写、seqlock 读、published_seq 跳转）与后备存储无关。
//
// 必须用 -include 放在最前面，并且本文件先 include 真实系统头、再定义宏，
// 这样系统头里的声明不会被宏改写。

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

namespace benchshim {
inline int open_(const char* name, int flags, mode_t mode) {
    std::string p = "/tmp/dbpbench/shm";
    p += name;                      // name 以 '/' 开头
    return ::open(p.c_str(), flags, mode);
}
inline int unlink_(const char* name) {
    std::string p = "/tmp/dbpbench/shm";
    p += name;
    return ::unlink(p.c_str());
}
} // namespace benchshim

#define shm_open   benchshim::open_
#define shm_unlink benchshim::unlink_
