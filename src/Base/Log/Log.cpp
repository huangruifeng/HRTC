#include "Log.h"
#include <sstream>
namespace hrtc {

#define BUFFER_SIZE (1024)

    bool LogChannel::InitLog(const std::string& path, bool enableConsole, bool async)
    {
        UnInitLog();
        Channel* channel = this;
        if (async) {
            auto asyncChannel = std::make_shared<AsyncChannel>();
            AddChannel(asyncChannel);
            channel = asyncChannel.get();
        }
        channel->AddChannel(std::make_shared<FileChannel>());
        if (enableConsole) {
            channel->AddChannel(std::make_shared<ConsoleChannel>());
        }
        Open(path);
        return true;
    }

    void LogChannel::UnInitLog()
    {
        ClearChannel();
    }

    void LogChannel::SetLogLevel(LOG_LEVEL level)
    {
        m_logLevel = level;
    }

    std::string LogChannel::ToString(LOG_LEVEL level)
    {
        switch (level)
        {
        case LOG_LEVEL::N_DEBUG:
            return "D";
        case LOG_LEVEL::N_INFO:
            return "I";
        case LOG_LEVEL::N_WARNING:
            return "W";
        case LOG_LEVEL::N_ERRORR:
            return "E";
        default:
            break;
        }
        return "";
    }

    std::string extractFileName(const std::string& fullPath) {
        // 定义可能的目录分隔符
#ifdef _WIN32
        const char* separators = "\\";
#else
        const char* separators = "/"'
#endif

            // 找到最后一个目录分隔符的位置
            size_t pos = fullPath.find_last_of(separators);

        // 如果找到了分隔符，则返回分隔符之后的部分；否则返回整个字符串
        if (pos != std::string::npos) {
            return fullPath.substr(pos + 1);
        }
        else {
            return fullPath;
        }
    }


    void LogChannel::Log(LOG_LEVEL level, const std::string& file, const std::string& function, int line, const std::string& log)
    {
        if (level < m_logLevel) {
            return;
        }
        std::ostringstream oss;
        oss << std::this_thread::get_id();
        std::string stid = oss.str();
        std::string info = "[";
        info.reserve(BUFFER_SIZE);
        //info.append(GetDetailTimeStr());
        info.append(" ");
        info.append(std::move(ToString(level)));
        info.append(" ");
        info.append(stid);
        info.append("] ");
        info.append(function);
        info.append(" ");
        info.append(log);
        info.append(" ");
        info.append(extractFileName(file));
        info.append(":");
        info.append(std::to_string(line));
        info.append("\n");
        Write(info);
    }


    static thread_local std::shared_ptr<LogChannel> currentChannel = nullptr;
    static thread_local int refCount = 0;

    void BindCurrentLog(std::shared_ptr<LogChannel> channel)
    {
        refCount++;
        currentChannel = channel;
    }

    std::shared_ptr<LogChannel>& GetCurrentLog()
    {
        return currentChannel;
    }

    void UnBindCurrentLog()
    {
        if (--refCount <= 0) {
            currentChannel = nullptr;
            refCount = 0;
        }
    }
}