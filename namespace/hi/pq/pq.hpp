#ifndef HISI_PQ_HPP
#define HISI_PQ_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <thread>
#include <atomic>
#include <unistd.h>

namespace hisi {
namespace pq {

class PQ {
public:
    // ---------- 基础 PQ 参数导入/导出 ----------
    static bool setCurrentPipe(int pipeId);
    bool loadFromFile(const std::string& binFilePath);
    bool exportToFile(const std::string& binFilePath);
    bool importFromMemory(const uint8_t* data, size_t size);
    std::vector<uint8_t> exportToMemory();
    static size_t getBinTotalLen();

    // ---------- 模式1：直接调用 pq_control_main（阻塞）----------
    static int runControlMain(int argc, char** argv);

    // ---------- 模式2：后台线程运行 pq_control_main ----------
    static bool startControlThread(int argc, char** argv);
    static void stopControlThread();
    static bool isControlThreadRunning();

    // ---------- 模式3：独立进程运行 pq_control_main ----------
    static bool startControlProcess(int argc, char** argv);
    static void stopControlProcess();      // 发送 SIGTERM
    static void forceKillControlProcess(); // 发送 SIGKILL
    static bool isControlProcessRunning();

    // 设置可执行文件路径（用于独立进程模式）
    static void setControlPath(const std::string& path);
    // 获取当前设置的可执行文件路径
    static std::string getControlPath();

private:
    static void printError(int ret);
    static void controlThreadEntry(int argc, char** argv); // 线程入口
    
    // 线程模式
    static std::thread s_controlThread;
    static std::atomic<bool> s_controlThreadRunning;
    
    // 进程模式
    static pid_t s_controlPid;
    static std::string s_controlPath;   // 存储pq_control_main路径
    static void ensureExecutable();     // 辅助函数，使用s_controlPath
};

} // namespace pq
} // namespace hisi

#endif