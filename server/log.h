#ifndef __LOG_H__
#define __LOG_H__

#include <iostream>
#include <fstream>
#include <string>
#include <mutex>
#include <time.h>
#include <cstring>
#include <sys/stat.h>
#include <chrono>

// 日志分级，由低到高
enum LogLevel {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
};

// 对齐项目单例风格：局部静态实例，无双重锁
class Log {
private:
    Log();
    ~Log();
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;

    std::mutex m_mtx;
    std::ofstream m_fout;
    std::string m_logFileName;
    size_t m_maxFileSize;
    LogLevel m_curLevel;

    std::string getCurrentTime();
    void rollLogFile();
    std::string getLevelStr(LogLevel level);

public:
    static Log* getInstance();
    void init(LogLevel level, const std::string& fileName, size_t maxSize = 10 * 1024 * 1024);
    void log(LogLevel level, const std::string& file, int line, const std::string& msg);
};

// 日志宏，自动携带文件名、行号
#define LOG_DEBUG(msg) Log::getInstance()->log(LOG_DEBUG, __FILE__, __LINE__, msg)
#define LOG_INFO(msg)  Log::getInstance()->log(LOG_INFO,  __FILE__, __LINE__, msg)
#define LOG_WARN(msg)  Log::getInstance()->log(LOG_WARN,  __FILE__, __LINE__, msg)
#define LOG_ERROR(msg) Log::getInstance()->log(LOG_ERROR, __FILE__, __LINE__, msg)

#endif