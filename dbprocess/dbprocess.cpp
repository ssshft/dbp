#include <iostream>
#include <csignal>
#include <cstdio>
#include "log_engine.h"
#include "program_util.h"
#include "config.h"
#include "crypto_exception.h"
#include "dbprocess/dbpserver.h"
#include "hft_metrics.h"


static void signal_handler(int signum) {
    LOG_INFO("handler interrupt signal {} received, dbprocess will exit now.", signum);
    fprintf(stdout, "handler interrupt signal (%d) received.\n", signum);
    fmtlog::poll();
    sleep(0.5);//wait logger write data to disk
    exit(signum);
}

static void usage(void){
    fprintf(stderr, "\nusage:\n");
    fprintf(stderr, "./application json_config_file \n");
    fprintf(stderr, "\n");
    exit(-1);
}

int main(int argc, char* argv[]) {
    if ( argc != 2 ){
        usage();
    }

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    Config* config = Config::instance();
    config->load(argv[1]);

    std::string tag = "";
    std::string logPath = "";
    std::string logLevel = "";

    if (config->get_string("tag", tag) && config->get_log_config("log_path", logPath) && config->get_log_config("level", logLevel)) {
        if (crypto::create_directory(logPath)) {
            log_maintain(tag, logPath, logLevel);
        }
        else {
            exit(-1);
        }
    }
    else {
        fprintf(stderr, "not found log config info in %s,\nneed to add log config\n", argv[1]);
        exit(-1);
    }

    std::string program = tag;
    long currentPid = getpid();
    long filePid = crypto::get_program_pid(program);
    if(!crypto::ensure_one_instance(program) && currentPid != filePid) {
        std::string errormsg = tag + " with pid=" + std::to_string(filePid) + " already exists, aborted";
        cryptothrow(errormsg.c_str(), -1);
    }
    crypto::write_program_pid(program);

    // 时钟校准 + 先把调用线程的分片/直方图分配掉（起工作线程之前）。
    // 不调也不会错，只是第一次埋点会多做一次 malloc。
#if HFT_METRICS_ENABLED
    hft::metrics::warmup();
#endif

    dbp::DbpServer* op = new dbp::DbpServer();
    if(op->PreStart(config)) {
        op->Run();
    }
    else {
        cryptothrow("dbprocess start failed!", -1);
    }

    // ------------------------------------------------------------------
    // 延时统计输出（默认关：要开就 cmake -DHFT_METRICS=ON）
    // ------------------------------------------------------------------
    // 文件名用 dbp_metrics_YYYYMMDD.log，**故意和 db 的 metrics_YYYYMMDD.log 分开**：
    // 两个进程的 log_path 很可能指向同一个目录，同名会互相覆盖成半截文件。
    // 周期 10 s，和 db 保持一致；超过 8 MB 就地滚成 .1，只留一份。
#if HFT_METRICS_ENABLED
    hft::metrics::TextFileSink metricsSink(logPath, "dbp_metrics", 8u << 20);
    hft::metrics::Poller metricsPoller(
        10000000000LL, [&metricsSink](const hft::metrics::Snapshot& s) {
            if (!metricsSink.write(s)) {
                std::fputs(s.to_text().c_str(), stderr);
            }
        });
#else
    LOG_WARN("latency metrics disabled at compile time (-DHFT_METRICS=OFF); no dbp_metrics file will be written. rebuild with -DHFT_METRICS=ON to enable");
#endif

    while(1) {
        log_maintain(tag, logPath, logLevel);
#if HFT_METRICS_ENABLED
        metricsPoller.poll();   // 0 参 = 自己读时钟
#endif
        usleep(1000);
    }

    return 0;
}