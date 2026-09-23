#include <atomic>
#include <csignal>
#include <iostream>
#include <unistd.h>

#include "app.h"
#include "runtime.h"
#include "pq.hpp"

// ★ 全局唯一定义
std::atomic<bool> g_running(true);

// 用于二次 Ctrl+C 强制退出
static std::atomic<int> g_signalCnt(0);

static void signalHandler(int sig)
{
    // 第二次收到信号，恢复默认行为，直接退出
    if (g_signalCnt.fetch_add(1) >= 1) {
        signal(sig, SIG_DFL);
        raise(sig);
        return;
    }

    // ★ 只设标志，不做任何其它操作
    g_running = false;
}

int main(int argc, char** argv)
{
    signal(SIGINT,  signalHandler);
    signal(SIGTERM, signalHandler);

    app::Application application;
    if (!application.init()) {
        std::cerr << "Application init failed" << std::endl;
        return -1;
    }

    application.run(argc, argv);
    return 0;
}