#include "modmorpher.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/entity/ActorHurtEvent.h"
#include "ll/api/event/entity/MobDieEvent.h"
#include "ll/api/event/player/PlayerDieEvent.h"
#include "ll/api/event/player/PlayerDestroyBlockEvent.h"
#include "ll/api/event/player/PlayerChatEvent.h"
#include "ll/api/event/player/PlayerJoinEvent.h"

using namespace modmorpher;

// ============================================================================
// JNI Thread Manager
// ============================================================================
JavaVM* JNIThreadManager::jvm = nullptr;
thread_local JNIEnv* JNIThreadManager::threadEnv = nullptr;

void JNIThreadManager::setJVM(JavaVM* vm) {
    jvm = vm;
}

bool JNIThreadManager::ensureAttached() {
    if (!jvm) return false;

    JNIEnv* env = nullptr;
    jint res = jvm->GetEnv((void**)&env, JNI_VERSION_1_8);

    if (res == JNI_OK) {
        threadEnv = env;
        return true;
    }

    if (res == JNI_EDETACHED) {
        if (jvm->AttachCurrentThread((void**)&env, nullptr) == JNI_OK) {
            threadEnv = env;
            return true;
        }
    }

    return false;
}

void JNIThreadManager::detachCurrentThread() {
    if (jvm) jvm->DetachCurrentThread();
}

JNIEnv* JNIThreadManager::getEnv() {
    if (!ensureAttached()) return nullptr;
    return threadEnv;
}

JNIThreadManager::ThreadGuard::ThreadGuard() {
    wasAttached = JNIThreadManager::ensureAttached();
}

JNIThreadManager::ThreadGuard::~ThreadGuard() {
    if (wasAttached) JNIThreadManager::detachCurrentThread();
}

JNIEnv* JNIThreadManager::ThreadGuard::getEnv() {
    return JNIThreadManager::getEnv();
}

// ============================================================================
// JNI Worker Queue
// ============================================================================
static std::queue<std::function<void(JNIEnv*)>> gQueue;
static std::mutex gQueueMutex;
static std::atomic<bool> gWorkerRunning{false};
static std::thread gWorkerThread;

void modmorpher::enqueueJNICall(std::string, std::function<void(JNIEnv*)> fn) {
    std::lock_guard<std::mutex> lock(gQueueMutex);
    gQueue.push(fn);
}

void modmorpher::startJNIWorker() {
    if (gWorkerRunning) return;
    gWorkerRunning = true;

    gWorkerThread = std::thread([] {
        while (gWorkerRunning) {
            std::function<void(JNIEnv*)> fn;

            {
                std::lock_guard<std::mutex> lock(gQueueMutex);
                if (!gQueue.empty()) {
                    fn = gQueue.front();
                    gQueue.pop();
                }
            }

            if (fn) {
                JNIThreadManager::ThreadGuard guard;
                if (JNIEnv* env = guard.getEnv()) {
                    fn(env);
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
}

void modmorpher::stopJNIWorker() {
    gWorkerRunning = false;
    if (gWorkerThread.joinable()) gWorkerThread.join();
}

// ============================================================================
// ForgeEventForwarder
// ============================================================================
std::vector<jobject> ForgeEventForwarder::registeredListeners;

void ForgeEventForwarder::init() {
    // No Java listeners yet
}

void ForgeEventForwarder::shutdown() {
    JNIThreadManager::ThreadGuard guard;
    JNIEnv* env = guard.getEnv();
    if (!env) return;

    for (auto obj : registeredListeners) {
        env->DeleteGlobalRef(obj);
    }
    registeredListeners.clear();
}

void ForgeEventForwarder::forwardActorHurt(mc::world::actor::Actor& actor, int damage) {
    // Stub for Java bridge
}

void ForgeEventForwarder::forwardPlayerDeath(mc::world::actor::player::Player& player) {
}

void ForgeEventForwarder::forwardMobDeath(mc::world::actor::Actor& mob) {
}

void ForgeEventForwarder::forwardBlockBreak(mc::world::actor::player::Player& player, BlockPos const& pos) {
}

void ForgeEventForwarder::forwardPlayerChat(mc::world::actor::player::Player& player, std::string const& msg) {
}

void ForgeEventForwarder::forwardPlayerJoin(mc::world::actor::player::Player& player) {
}

// ============================================================================
// ModMorpher Entry
// ============================================================================
bool ModMorpher::initialize(JavaVM* vm, JNIEnv* env) {
    JNIThreadManager::setJVM(vm);
    startJNIWorker();

    auto& bus = ll::event::EventBus::getInstance();

    bus.emplaceListener<ll::event::entity::ActorHurtEvent>(
        [](ll::event::entity::ActorHurtEvent& ev) {
            if (auto* actor = ev.getActor()) {
                ForgeEventForwarder::forwardActorHurt(*actor, (int)ev.getDamage());
            }
        }
    );

    bus.emplaceListener<ll::event::player::PlayerDieEvent>(
        [](ll::event::player::PlayerDieEvent& ev) {
            if (auto* p = ev.getPlayer()) {
                ForgeEventForwarder::forwardPlayerDeath(*p);
            }
        }
    );

    bus.emplaceListener<ll::event::entity::MobDieEvent>(
        [](ll::event::entity::MobDieEvent& ev) {
            if (auto* m = ev.getMob()) {
                ForgeEventForwarder::forwardMobDeath(*m);
            }
        }
    );

    bus.emplaceListener<ll::event::player::PlayerDestroyBlockEvent>(
        [](ll::event::player::PlayerDestroyBlockEvent& ev) {
            if (auto* p = ev.getPlayer()) {
                auto bp = ev.getBlockPos();
                BlockPos pos{bp.x, bp.y, bp.z};
                ForgeEventForwarder::forwardBlockBreak(*p, pos);
            }
        }
    );

    bus.emplaceListener<ll::event::player::PlayerChatEvent>(
        [](ll::event::player::PlayerChatEvent& ev) {
            if (auto* p = ev.getPlayer()) {
                ForgeEventForwarder::forwardPlayerChat(*p, ev.getMessage());
            }
        }
    );

    bus.emplaceListener<ll::event::player::PlayerJoinEvent>(
        [](ll::event::player::PlayerJoinEvent& ev) {
            if (auto* p = ev.getPlayer()) {
                ForgeEventForwarder::forwardPlayerJoin(*p);
            }
        }
    );

    return true;
}

void ModMorpher::shutdown() {
    stopJNIWorker();
    ForgeEventForwarder::shutdown();
}
