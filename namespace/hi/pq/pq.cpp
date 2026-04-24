#include "pq.hpp"
#include "hi_pq_bin.h"
#include "hi_common.h"

#include <fstream>
#include <iostream>
#include <cstring>
#include <cerrno>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <vector>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>



extern "C"{
// 声明海思提供的函数（通常在某个头文件中，这里显式声明）
extern int pq_control_main(int argc, char **argv);
}


namespace hisi {
namespace pq {





// 静态成员初始化
std::thread PQ::s_controlThread;
std::atomic<bool> PQ::s_controlThreadRunning(false);
pid_t PQ::s_controlPid = -1;
std::string PQ::s_controlPath = "./pq_control_main";   // 默认路径




void PQ::setControlPath(const std::string& path) {
    s_controlPath = path;
}

std::string PQ::getControlPath() {
    return s_controlPath;
}

void PQ::ensureExecutable() {
    if (access(s_controlPath.c_str(), F_OK) != 0) {
        std::cerr << "Error: " << s_controlPath << " does not exist." << std::endl;
        return;
    }
    if (access(s_controlPath.c_str(), X_OK) != 0) {
        std::cout << "Warning: " << s_controlPath << " is not executable. Attempting to add execute permission..." << std::endl;
        if (chmod(s_controlPath.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0) {
            std::cerr << "Failed to add execute permission to " << s_controlPath << std::endl;
        } else {
            std::cout << "Execute permission added successfully." << std::endl;
        }
    }
}


// ---------------------------------------------------------------------
// 基础 PQ 函数
// ---------------------------------------------------------------------
bool PQ::setCurrentPipe(int pipeId) {
    std::cout << "[PQ] setCurrentPipe(" << pipeId << ") - assuming success.\n";
    return true;
}

size_t PQ::getBinTotalLen() {
    return HI_PQ_GetBinTotalLen();
}

void PQ::printError(int ret) {
    switch (ret) {
        case HI_BIN_NULL_POINT: std::cerr << "PQ error: NULL pointer" << std::endl; break;
        case HI_BIN_REG_ATTR_ERR: std::cerr << "PQ error: register attribute error" << std::endl; break;
        case HI_BIN_MALLOC_ERR: std::cerr << "PQ error: memory allocation failed" << std::endl; break;
        case HI_BIN_CHIP_ERR: std::cerr << "PQ error: chip mismatch" << std::endl; break;
        case HI_BIN_CRC_ERR: std::cerr << "PQ error: CRC failed" << std::endl; break;
        case HI_BIN_SIZE_ERR: std::cerr << "PQ error: bin size error" << std::endl; break;
        case HI_BIN_LEBLE_ERR: std::cerr << "PQ error: label error" << std::endl; break;
        case HI_BIN_DATA_ERR: std::cerr << "PQ error: data error" << std::endl; break;
        case HI_BIN_SECURITY_SOLUTION_FAILED: std::cerr << "PQ error: security failed" << std::endl; break;
        default: std::cerr << "PQ error: unknown 0x" << std::hex << ret << std::endl; break;
    }
}

bool PQ::importFromMemory(const uint8_t* data, size_t size) {
    if (!data || size == 0) return false;
    int ret = HI_PQ_BIN_ImportBinData(const_cast<uint8_t*>(data), static_cast<unsigned int>(size));
    if (ret != HI_SUCCESS) { printError(ret); return false; }
    return true;
}

bool PQ::loadFromFile(const std::string& binFilePath) {
    std::ifstream file(binFilePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) { std::cerr << "Cannot open: " << binFilePath << std::endl; return false; }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (size <= 0) { std::cerr << "Empty file" << std::endl; return false; }
    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) { std::cerr << "Read failed" << std::endl; return false; }
    return importFromMemory(buffer.data(), buffer.size());
}

std::vector<uint8_t> PQ::exportToMemory() {
    unsigned int len = HI_PQ_GetBinTotalLen();
    if (len == 0) { std::cerr << "GetBinTotalLen failed" << std::endl; return {}; }
    std::vector<uint8_t> buffer(len);
    int ret = HI_PQ_BIN_ExportBinData(buffer.data(), len);
    if (ret != HI_SUCCESS) { printError(ret); return {}; }
    return buffer;
}

bool PQ::exportToFile(const std::string& binFilePath) {
    auto data = exportToMemory();
    if (data.empty()) return false;
    std::ofstream file(binFilePath, std::ios::binary);
    if (!file.is_open()) { std::cerr << "Cannot create: " << binFilePath << std::endl; return false; }
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    return true;
}

// ---------------------------------------------------------------------
// 模式1：直接调用（阻塞）
// ---------------------------------------------------------------------
int PQ::runControlMain(int argc, char** argv) {
    return pq_control_main(argc, argv);
}

// ---------------------------------------------------------------------
// 模式2：后台线程（私有静态成员函数实现线程入口）
// ---------------------------------------------------------------------
void PQ::controlThreadEntry(int argc, char** argv) {
    // 使线程可被取消
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, nullptr);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, nullptr);
    
    int ret = runControlMain(argc, argv);
    std::cout << "pq_control_main thread exited with code " << ret << std::endl;
    s_controlThreadRunning = false;
}

bool PQ::startControlThread(int argc, char** argv) {
    if (s_controlThreadRunning.load()) {
        std::cerr << "Control thread already running" << std::endl;
        return false;
    }
    
    // 复制参数（因为外部 argv 可能被修改）
    char** args = new char*[argc + 1];
    for (int i = 0; i < argc; ++i) {
        args[i] = new char[strlen(argv[i]) + 1];
        strcpy(args[i], argv[i]);
    }
    args[argc] = nullptr;
    
    // 启动线程，并传递参数副本
    s_controlThread = std::thread([argc, args]() {
        controlThreadEntry(argc, args);
        for (int i = 0; i < argc; ++i) delete[] args[i];
        delete[] args;
    });
    s_controlThreadRunning = true;
    return true;
}

void PQ::stopControlThread() {
    if (!s_controlThreadRunning.load()) return;
    pthread_cancel(s_controlThread.native_handle());
    if (s_controlThread.joinable()) s_controlThread.join();
    s_controlThreadRunning = false;
    std::cout << "Control thread stopped." << std::endl;
}

bool PQ::isControlThreadRunning() {
    return s_controlThreadRunning.load();
}

// ---------------------------------------------------------------------
// 模式3：独立进程
// ---------------------------------------------------------------------
bool PQ::startControlProcess(int argc, char** argv) {
    if (s_controlPid > 0 && kill(s_controlPid, 0) == 0) {
        std::cerr << "Control process already running, PID=" << s_controlPid << std::endl;
        return false;
    }
    
    ensureExecutable();
    // 再次检查是否可执行（如果 ensure 失败，可能仍不可执行）
    if (access(s_controlPath.c_str(), X_OK) != 0) {
        std::cerr << "Control process executable not usable: " << s_controlPath << std::endl;
        return false;
    }
    
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return false;
    }
    
    if (pid == 0) {
        // 子进程：可以在此 chdir 到工作目录（如果需要）
        // chdir("/some/working/dir");
        
        std::vector<char*> args;
        args.push_back(const_cast<char*>(s_controlPath.c_str()));
        for (int i = 0; i < argc; ++i) {
            args.push_back(argv[i]);
        }
        args.push_back(nullptr);
        execvp(s_controlPath.c_str(), args.data());
        perror("execvp");
        _exit(1);
    } else {
        s_controlPid = pid;
        std::cout << "Started control process, PID=" << s_controlPid << std::endl;
        return true;
    }
}




void PQ::stopControlProcess() {
    if (s_controlPid > 0) {
        if (kill(s_controlPid, SIGTERM) == 0) {
            // 非阻塞等待，避免长时间阻塞
            waitpid(s_controlPid, nullptr, WNOHANG);
            std::cout << "Sent SIGTERM to process " << s_controlPid << std::endl;
        } else {
            if (errno != ESRCH) perror("kill");
        }
        s_controlPid = -1;
    }
}

void PQ::forceKillControlProcess() {
    if (s_controlPid > 0) {
        kill(s_controlPid, SIGKILL);
        waitpid(s_controlPid, nullptr, 0);
        std::cout << "Force killed process " << s_controlPid << std::endl;
        s_controlPid = -1;
    }
}

bool PQ::isControlProcessRunning() {
    if (s_controlPid <= 0) return false;
    return kill(s_controlPid, 0) == 0;
}

} // namespace pq
} // namespace hisi