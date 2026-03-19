#pragma once

#include <jni.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace mc {
namespace world {
namespace actor {
    class Actor;
    namespace player {
        class Player;
    }
}
}
}

namespace modmorpher {

// ============================================================================
// Simple BlockPos
// ============================================================================
struct BlockPos {
    int x, y, z;
};

// ============================================================================
// JNI Thread Manager
// ============================================================================
class JNIThreadManager {
public:
    static void setJVM(JavaVM* vm);
    static JNIEnv* getEnv();

    class ThreadGuard {
    public:
        ThreadGuard();
        ~ThreadGuard();
        JNIEnv* getEnv();
    private:
        bool wasAttached{false};
    };

private:
    static bool ensureAttached();
    static void detachCurrentThread();

    static JavaVM* jvm;
    static thread_local JNIEnv* threadEnv;
};

// ============================================================================
// Event Forwarder (Java bridge only)
// ============================================================================
class ForgeEventForwarder {
public:
    static void init();
    static void shutdown();

    static void forwardActorHurt(mc::world::actor::Actor& actor, int damage);
    static void forwardPlayerDeath(mc::world::actor::player::Player& player);
    static void forwardMobDeath(mc::world::actor::Actor& mob);
    static void forwardBlockBreak(mc::world::actor::player::Player& player, BlockPos const& pos);
    static void forwardPlayerChat(mc::world::actor::player::Player& player, std::string const& msg);
    static void forwardPlayerJoin(mc::world::actor::player::Player& player);

private:
    static std::vector<jobject> registeredListeners;
};

// ============================================================================
// JNI Worker Queue
// ============================================================================
void enqueueJNICall(std::string desc, std::function<void(JNIEnv*)> fn);
void startJNIWorker();
void stopJNIWorker();

// ============================================================================
// Public Mod Entry Points
// ============================================================================
class ModMorpher {
public:
    static bool initialize(JavaVM* vm, JNIEnv* env);
    static void shutdown();
};

} // namespace modmorpher
