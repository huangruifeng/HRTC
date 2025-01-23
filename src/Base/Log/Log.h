#pragma once

#include <Base/Log/Channel.h>

namespace hrtc {
    enum class LOG_LEVEL : int
    {
        N_DEBUG = 0,
        N_INFO,
        N_WARNING,
        N_ERRORR
    };

    class LogChannel : public Channel {
    public:
        LogChannel() :m_logLevel(LOG_LEVEL::N_DEBUG) {}
        ~LogChannel() { UnInitLog(); };
        LOG_LEVEL GetLevel() { return m_logLevel; }
        bool InitLog(const std::string& path, bool enableConsole = false, bool async = false);
        void UnInitLog();
        void SetLogLevel(LOG_LEVEL level);
        static std::string ToString(LOG_LEVEL level);
        void Log(LOG_LEVEL level, const std::string& file, const std::string& function, int line, const std::string& log);
        static thread_local std::shared_ptr<LogChannel> currentLogChannel;
    private:
        LOG_LEVEL m_logLevel;
    };

    std::string GetDefaultAppLogPath();
    void BindCurrentLog(std::shared_ptr<LogChannel>);
    void UnBindCurrentLog();
    std::shared_ptr<LogChannel>& GetCurrentLog();

    class CurrentLogGuard {
    public:
        CurrentLogGuard(const std::shared_ptr<LogChannel>& channel) {
            BindCurrentLog(channel);
        }
        ~CurrentLogGuard() {
            UnBindCurrentLog();
        }
    };
}