#pragma once
#include <initializer_list>
#include <map>

template <class T>
void Enable(T* arg) {
}
template <class T>
bool Enabled(T* arg) {
    return false;
}

template <class T, class... U>
void Enable(T* arg, U*... args) {
    Enable(arg);
    Enable(args...);
}

template <class T>
void Disable(T* arg) {
}

template <class T, class... U>
void Disable(T* arg, U*... args) {
    Disable(arg);
    Disable(args...);
}

template <class T>
class GuardHelper {
public:
    GuardHelper(T* args...) : GuardHelper({args}) {}
    GuardHelper(std::initializer_list<T*> const& args) { 
        for (auto&arg : args){
            if (arg == nullptr)
                continue;
            auto enable = Enabled(arg);
            if (enable){
                Disable(arg); 
            }
            m_args[arg] = enable;
        }
    }
    ~GuardHelper() { 
        for (auto& arg : m_args){
            if (arg.second){
                Enable(arg.first);
            }
        }
        m_args.clear();
    }

private:
    std::map<T*, bool> m_args;
};