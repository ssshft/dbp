// 验证 sp::Writer::AddNewColumn 在超过初始映射容量时的行为。
//
// 背景：Writer 构造时按 max(初始列数, 500) 映射列头段，按 max(初始列数*topiclen,
// 500*topiclen) 映射数据段。这两块**只映射一次**，之后 AddNewColumn 只往后推
// msize / __buffrom，**不再 mmap**。所以超过 500 列时就是往映射外写。
//
// 这个程序故意越界，让它崩，看崩在第几列、崩在哪一侧（列头段 / 数据段）。

#include <shmpool/shmpool.h>
#include <dbp/include.h>

#include <cstdio>
#include <cstring>
#include <vector>

int main() {
    const uint32_t topiclen = 100;
    const uint32_t colsize  = 500;
    const uint32_t P        = 1;

    const char* mpath = "/tmp/dbpbench/shm/CRASH_TOPIC";
    const char* dpath = "/tmp/dbpbench/shm/CRASH_DATAS";
    std::remove(mpath);
    std::remove(dpath);

    std::vector<dbp::DbpTopic> vc(P);
    std::snprintf(vc[0].__name, sizeof(vc[0].__name), "P0");
    vc[0].__bufsize = topiclen;

    {
        sp::Initialer<dbp::DbpTopic, dbp::DbpData> init(mpath, P, dpath, P * topiclen, &vc, 1, colsize, colsize * topiclen);
    }

    sp::Writer<dbp::DbpTopic, dbp::DbpData> w(mpath, dpath, colsize);

    std::printf("初始 msize=%llu  sizeof(DbpTopic)=%zu  sizeof(DbpData)=%zu\n",
                (unsigned long long)w.ColumnCount(), sizeof(dbp::DbpTopic), sizeof(dbp::DbpData));
    std::printf("列头段映射 ≈ %u B  数据段映射 ≈ %u B\n",
                colsize * (uint32_t)sizeof(dbp::DbpTopic),
                colsize * topiclen * (uint32_t)sizeof(dbp::DbpData));
    std::printf("→ 列头段约在 %u 列后越界；数据段约在 %u 列后越界\n",
                colsize + 4000 / (uint32_t)sizeof(dbp::DbpTopic),
                colsize + 3000 / ((uint32_t)sizeof(dbp::DbpData) * topiclen));
    std::printf("\n开始灌列，每 100 列打一行：\n");

    for (int i = 0; i < 2000; ++i) {
        dbp::DbpTopic t{};
        std::snprintf(t.__name, sizeof(t.__name), "PAD%04d", i);
        t.__bufsize = topiclen;
        w.AddNewColumn(t);

        // 写一条数据，让数据段的 __buffrom 也被真正踩到
        dbp::DbpData* p = w.Fetch(w.ColumnCount() - 1);
        std::memset(p, 0, sizeof(dbp::DbpData));
        w.Commit(w.ColumnCount() - 1);

        if ((i + 1) % 100 == 0 || i > 494) {
            std::printf("  已加到 %d 列，仍在映射内\n", (int)w.ColumnCount());
            std::fflush(stdout);
        }
    }
    std::printf("2000 列全部成功（说明映射足够大，与预期不符，需重新看 mmap 参数）\n");
    return 0;
}
