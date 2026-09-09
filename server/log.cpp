#include "log.h"

Log::Log() : m_maxFileSize(10 * 1024 * 1024), m_curLevel(LOG_INFO)
{
}

Log::~Log()
{
    if (m_fout.is_open())
    {
        m_fout.close();
    }
}

Log* Log::getInstance()
{
    static Log ins;
    return &ins;
}

void Log::init(LogLevel level, const std::string& fileName, size_t maxSize)
{
    std::lock_guard<std::mutex> lock(m_mtx);
    m_curLevel = level;
    m_maxFileSize = maxSize;
    m_logFileName = fileName;

    if (m_fout.is_open())
        m_fout.close();
    m_fout.open(m_logFileName, std::ios::app);
    if (!m_fout.is_open())
    {
        std::cerr << "[FATAL] open log file " << m_logFileName << " failed" << std::endl;
    }
}

std::string Log::getCurrentTime()
{
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char buf[64] = {0};
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);
    return std::string(buf);
}

std::string Log::getLevelStr(LogLevel level)
{
    switch (level)
    {
    case LOG_DEBUG: return "DEBUG";
    case LOG_INFO:  return "INFO";
    case LOG_WARN:  return "WARN";
    case LOG_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}

void Log::rollLogFile()
{
    struct stat st;
    if (stat(m_logFileName.c_str(), &st) == 0)
    {
        if ((size_t)st.st_size >= m_maxFileSize)
        {
            m_fout.close();
            std::string newName = m_logFileName + "." + getCurrentTime();
            rename(m_logFileName.c_str(), newName.c_str());
            m_fout.open(m_logFileName, std::ios::app);
        }
    }
}

void Log::log(LogLevel level, const std::string& file, int line, const std::string& msg)
{
    if (level < m_curLevel)
        return;

    std::lock_guard<std::mutex> lock(m_mtx);
    rollLogFile();

    std::string timeStr = getCurrentTime();
    std::string lvlStr = getLevelStr(level);

    // 控制台输出
    std::cout << "[" << timeStr << "][" << lvlStr << "][" << file << ":" << line << "] " << msg << "\n";
    // 文件输出
    if (m_fout.is_open())
    {
        m_fout << "[" << timeStr << "][" << lvlStr << "][" << file << ":" << line << "] " << msg << "\n";
    }
}