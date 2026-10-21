// 空 pairlist（启动时 pairlist.xml 为空或全部 enable=0）之后的**热加**行为。
//
// 这是 crash_test.cpp 的姊妹场景，但更阴：
//   InitShm 建 0 列 ⇒ Writer 里 `bufsize` 保持 0（那个 for 循环没执行）
//   ⇒ 数据段 datasize = 500 × 0 = 0 ⇒ 只映射了 header 那几十字节（实际占 1 个页）。
//   第 1 个热加对子写的是"页内 EOF 之后的零填充区" ⇒ **看起来成功、进程内也读得到**；
//   第 2 个的 __buffrom = 100（偏移 60000 B）直接飞出那 1 个页 ⇒ SIGSEGV。
//
// 现场表现：运维先禁掉所有对子启动 dbp，再逐个启用 → 启用第 2 个时 dbprocess 消失。
//
// 用法：./run.sh empty   （预期 exit 139）
#include <shmpool/shmpool.h>
#include <dbp/include.h>

#include <cstdio>
#include <cstring>
#include <vector>

int main() {
    const uint32_t topiclen = 100;   // dbp/etc/dbprocess.xml
    const uint32_t colsize  = 500;   // dbp::DBP_COL_SIZE
    const uint32_t P        = 0;     // ← 关键：启动时一个对子都没有

    const char* mpath = "/tmp/dbpbench/shm/EMPTY_TOPIC";
    const char* dpath = "/tmp/dbpbench/shm/EMPTY_DATAS";
    std::remove(mpath);
    std::remove(dpath);

    std::vector<dbp::DbpTopic> vc;   // 空

    {
        sp::Initialer<dbp::DbpTopic, dbp::DbpData> init(
            mpath, P, dpath, P * topiclen, &vc, 1, colsize, colsize * topiclen);
        std::printf("Initialer ok, ColumnCount=%u\n", init.ColumnCount());
    }

    sp::Writer<dbp::DbpTopic, dbp::DbpData> w(mpath, dpath, colsize);
    std::printf("Writer ok, ColumnCount=%u（数据段按 bufsize=0 映射 ⇒ 只有一个页）\n",
                w.ColumnCount());

    for (int i = 0; i < 3; ++i) {
        dbp::DbpTopic t{};
        std::snprintf(t.__name, sizeof(t.__name), "NEW%02d", i);
        t.__bufsize = topiclen;
        w.AddNewColumn(t);

        auto id = w.ColumnCount() - 1;
        dbp::DbpData* p = w.Fetch(id);
        std::memset(p, 0, sizeof(dbp::DbpData));
        p->spreadBidAsk = 1.5 + i;
        w.Commit(id);

        std::printf("  加第 %d 列 ok, id=%u, 写入 spreadBidAsk=%f\n", i + 1, id, p->spreadBidAsk);
        std::fflush(stdout);
    }

    std::printf("没崩（与预期不符，需重新看 mmap 参数）\n");
    return 0;
}
