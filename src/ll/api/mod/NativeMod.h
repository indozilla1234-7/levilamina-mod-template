#pragma once

#include <string>
#include <memory>

namespace ll {
namespace mod {

class Logger {
public:
    void info(const std::string& msg) {}
    void warn(const std::string& msg) {}
    void error(const std::string& msg) {}
    void debug(const std::string& msg) {}
};

class NativeMod {
    Logger mLogger;
    
public:
    static NativeMod* current() {
        static NativeMod instance;
        return &instance;
    }
    
    NativeMod() = default;
    
    Logger& getLogger() {
        return mLogger;
    }
};

} // namespace mod
} // namespace ll

// Forward declare MyMod for convenience
namespace my_mod {
class MyMod;
}
