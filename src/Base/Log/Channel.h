#pragma once
#include <vector>
#include <memory>
#include <string>
#include <Base/Thread/DispatchQueue.h>
namespace hrtc {
    class Channel {
    public:
        virtual void Write(const std::string& data) {
            for (auto& c : m_channel) {
                c->Write(data);
            }
        }
        virtual ~Channel() { ClearChannel(); };
        virtual bool Open(const std::string& arg) {
            for (auto& c : m_channel) {
                c->Open(arg);
            }
            return true;
        }
        virtual bool Close() {
            for (auto& c : m_channel) {
                c->Close();
            }
            return true;
        }
        virtual void AddChannel(std::shared_ptr<Channel> channel) {
            if (channel.get() == dynamic_cast<Channel*>(this)) {
                //not support add self.
                return;
            }
            auto it = std::find(m_channel.begin(), m_channel.end(), channel);
            if (it != m_channel.end()) {
                return;
            }
            m_channel.push_back(channel);
        }
        virtual void DeleteChannel(std::shared_ptr<Channel> channel) {
            auto it = std::find(m_channel.begin(), m_channel.end(), channel);
            if (it != m_channel.end()) {
                m_channel.erase(it);
            }
        }
        virtual void ClearChannel() {
            for (auto& c : m_channel) {
                c->Close();
                c->ClearChannel();
            }
            m_channel.clear();
        }
    protected:
        std::vector<std::shared_ptr<Channel>> m_channel;
    };
    class FileChannel : public Channel {
    public:
        ~FileChannel() { Close(); }
        bool Open(const std::string& path)override {
            if (m_fileHandle) {
                return false;
            }
            m_fileHandle = fopen(path.c_str(), "a");
            return m_fileHandle != NULL;
        }
        bool Close()override {
            if (m_fileHandle)
            {
                fflush(m_fileHandle);
                fclose(m_fileHandle);
                m_fileHandle = nullptr;
            }
            return true;
        }

        void Write(const std::string& data) override {
            if (m_fileHandle) {
                fwrite(data.c_str(), sizeof(char), data.size(), m_fileHandle);
                fflush(m_fileHandle);
            }
        }
    private:
        std::string m_filePath;
        FILE* m_fileHandle;
    };

    class ConsoleChannel : public Channel {
    public:
        void Write(const std::string& data)override {
            printf(data.c_str());
        }
    };

    class AsyncChannel : public Channel {
    public:
        void Write(const std::string& data)override {
            if (auto task = m_taskQueue) {
                task->async([data, this]() {
                    Channel::Write(data);
                    });
            }
        }
        bool Open(const std::string& arg) override {
            Channel::Open(arg);
            m_taskQueue = dispatch_task_queue::create("Log");
            return m_taskQueue != nullptr;
        }
        bool Close() override {
            if (m_taskQueue) {
                m_taskQueue.reset();
                Channel::Close();
                return true;
            }
        }
    private:
        std::shared_ptr<dispatch_task_queue::task_queue> m_taskQueue;
    };
}