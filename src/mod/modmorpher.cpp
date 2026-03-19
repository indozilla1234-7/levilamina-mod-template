#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "modmorpher.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <sstream>

#include "mod/MyMod.h"

// LeviLamina
#include "ll/api/command/Command.h"
#include "ll/api/command/CommandHandle.h"
#include "mc/server/commands/CommandOutput.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/entity/ActorHurtEvent.h"
#include "ll/api/event/entity/MobDieEvent.h"
#include "ll/api/event/player/PlayerAttackEvent.h"
#include "ll/api/event/player/PlayerChatEvent.h"
#include "ll/api/event/player/PlayerDieEvent.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/event/player/PlayerPickUpItemEvent.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "ll/api/event/player/PlayerRespawnEvent.h"
#include "ll/api/event/player/PlayerDestroyBlockEvent.h"
#include "ll/api/event/world/LevelTickEvent.h"
#include "ll/api/event/world/SpawnMobEvent.h"
#include "ll/api/event/server/ServerStartedEvent.h"
#include "ll/api/event/server/ServerStoppingEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/memory/Memory.h"
#include "ll/api/memory/Symbol.h"

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

using mc::world::actor::Actor;
using mc::world::actor::player::Player;
using mc::world::level::Level;
using mc::world::level::block::Block;

namespace modmorpher {

// ============================================================================
// JNI queue + worker
// ============================================================================
struct JNICall {
    std::function<void(JNIEnv*)> fn;
    std::string                  description;
};

static std::queue<JNICall>  gJNIQueue;
static std::mutex           gJNIQueueMutex;
static std::atomic<bool>    gJNIWorkerRunning{false};
static std::thread          gJNIWorkerThread;

static void jniWorkerLoop() {
    auto& logger = my_mod::MyMod::getInstance().getSelf().getLogger();
    logger.info("ModMorpher: JNI worker thread started");
    while (gJNIWorkerRunning.load(std::memory_order_relaxed)) {
        JNICall call;
        {
            std::unique_lock<std::mutex> lock(gJNIQueueMutex);
            if (gJNIQueue.empty()) {
                lock.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            call = std::move(gJNIQueue.front());
            gJNIQueue.pop();
        }
        JNIThreadManager::ThreadGuard guard;
        JNIEnv* env = guard.getEnv();
        if (!env) {
            logger.error("JNI worker: no env for call: " + call.description);
            continue;
        }
        call.fn(env);
    }
    logger.info("ModMorpher: JNI worker thread stopped");
}

void enqueueJNICall(std::string desc, std::function<void(JNIEnv*)> fn) {
    std::lock_guard<std::mutex> lock(gJNIQueueMutex);
    gJNIQueue.push(JNICall{std::move(fn), std::move(desc)});
}

void startJNIWorker() {
    if (gJNIWorkerRunning.exchange(true)) return;
    gJNIWorkerThread = std::thread(jniWorkerLoop);
}

void stopJNIWorker() {
    if (!gJNIWorkerRunning.exchange(false)) return;
    if (gJNIWorkerThread.joinable()) gJNIWorkerThread.join();
}

// ============================================================================
// JNIThreadManager
// ============================================================================
JavaVM*              JNIThreadManager::jvm       = nullptr;
thread_local JNIEnv* JNIThreadManager::threadEnv = nullptr;

void JNIThreadManager::setJVM(JavaVM* vm) {
    jvm = vm;
}

bool JNIThreadManager::ensureAttached() {
    if (threadEnv) return true;
    if (!jvm) return false;
    JNIEnv* env = nullptr;
    if (jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_8) == JNI_OK && env) {
        threadEnv = env;
        return true;
    }
    if (jvm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) != JNI_OK || !env) {
        return false;
    }
    threadEnv = env;
    return true;
}

void JNIThreadManager::detachCurrentThread() {
    if (jvm && threadEnv) {
        jvm->DetachCurrentThread();
        threadEnv = nullptr;
    }
}

JNIEnv* JNIThreadManager::getEnv() {
    return ensureAttached() ? threadEnv : nullptr;
}

JNIThreadManager::ThreadGuard::ThreadGuard() {
    wasAttached = (threadEnv != nullptr);
    if (!wasAttached) JNIThreadManager::ensureAttached();
}

JNIThreadManager::ThreadGuard::~ThreadGuard() {
    if (!wasAttached) JNIThreadManager::detachCurrentThread();
}

JNIEnv* JNIThreadManager::ThreadGuard::getEnv() {
    return threadEnv;
}

// ============================================================================
// BedrockSymbolResolver
// ============================================================================
BedrockSymbolResolver::ActorSetPos       BedrockSymbolResolver::actorSetPos       = nullptr;
BedrockSymbolResolver::ActorGetPos       BedrockSymbolResolver::actorGetPos       = nullptr;
BedrockSymbolResolver::BlockSetType      BedrockSymbolResolver::blockSetType      = nullptr;
BedrockSymbolResolver::ActorAddTag       BedrockSymbolResolver::actorAddTag       = nullptr;
BedrockSymbolResolver::ActorSetAttribute BedrockSymbolResolver::actorSetAttribute = nullptr;
BedrockSymbolResolver::CommandExecute    BedrockSymbolResolver::commandExecute    = nullptr;
BedrockSymbolResolver::BlockCreate       BedrockSymbolResolver::blockCreate       = nullptr;
bool                                     BedrockSymbolResolver::resolved          = false;

bool BedrockSymbolResolver::initialize() {
    if (resolved) return true;
    auto& logger = my_mod::MyMod::getInstance().getSelf().getLogger();
    logger.info("ModMorpher: resolving Bedrock symbols...");

    struct SymbolDef {
        char const* name;
        void**      target;
    };

    SymbolDef symbols[] = {
        {"?setPos@Actor@@UEAAXAEBVVec3@@@Z", reinterpret_cast<void**>(&actorSetPos)},
        {"?getPosition@Actor@@UEBAAEBVVec3@@@Z", reinterpret_cast<void**>(&actorGetPos)},
        {"?setBlock@BlockSource@@QEAA_NAEBVBlockPos@@AEBVBlock@@HPEAUActorBlockSyncMessage@@@Z",
            reinterpret_cast<void**>(&blockSetType)},
        {"?addTag@Actor@@QEAAXAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z",
            reinterpret_cast<void**>(&actorAddTag)},
        {"?setAttribute@Actor@@QEAAXAEBVAttribute@@M@Z",
            reinterpret_cast<void**>(&actorSetAttribute)},
    };

    int ok = 0;
    for (auto& s : symbols) {
        ll::memory::Symbol sym{std::string{s.name}};
        *s.target = sym.view().resolve(true);
        if (*s.target) {
            ++ok;
            logger.info(std::format("  [OK] {}", s.name));
        } else {
            logger.warn(std::format("  [!!] {}", s.name));
        }
    }

    logger.info(std::format("ModMorpher: {}/{} symbols resolved",
                            ok, int(sizeof(symbols) / sizeof(symbols[0]))));
    resolved = true;
    return true;
}

void* BedrockSymbolResolver::resolveSymbol(char const* n) {
    ll::memory::Symbol sym{std::string{n}};
    return sym.view().resolve(true);
}

BedrockSymbolResolver::ActorSetPos BedrockSymbolResolver::getActorSetPos()       { return actorSetPos; }
BedrockSymbolResolver::ActorGetPos BedrockSymbolResolver::getActorGetPos()       { return actorGetPos; }
BedrockSymbolResolver::BlockSetType BedrockSymbolResolver::getBlockSetType()     { return blockSetType; }
BedrockSymbolResolver::ActorAddTag BedrockSymbolResolver::getActorAddTag()       { return actorAddTag; }
BedrockSymbolResolver::ActorSetAttribute BedrockSymbolResolver::getActorSetAttribute() { return actorSetAttribute; }
BedrockSymbolResolver::CommandExecute BedrockSymbolResolver::getCommandExecute() { return commandExecute; }
BedrockSymbolResolver::BlockCreate BedrockSymbolResolver::getBlockCreate()       { return blockCreate; }

// ============================================================================
// BlockStateMapper
// ============================================================================
std::map<std::string, std::string> BlockStateMapper::blockNameMappings;
std::map<std::string, std::map<std::string, std::string>> BlockStateMapper::propertyMappings;

bool BlockStateMapper::loadMappings(std::string const&) {
    auto& logger = my_mod::MyMod::getInstance().getSelf().getLogger();

    blockNameMappings = {
        {"minecraft:stone", "minecraft:stone"},
        {"minecraft:cobblestone", "minecraft:cobblestone"},
        {"minecraft:dirt", "minecraft:dirt"},
        {"minecraft:grass_block", "minecraft:grass"},
        {"minecraft:water", "minecraft:water"},
        {"minecraft:lava", "minecraft:lava"},
        {"minecraft:air", "minecraft:air"},
    };

    propertyMappings.clear();
    propertyMappings["minecraft:stone"] = {{"stone_type", "stone_type"}};

    logger.info(std::format("BlockStateMapper: {} blocks, {} property remaps loaded",
                            blockNameMappings.size(), propertyMappings.size()));
    return true;
}

std::string BlockStateMapper::mapBlockName(std::string const& forgeName) {
    auto it = blockNameMappings.find(forgeName);
    if (it != blockNameMappings.end()) return it->second;
    return forgeName;
}

BlockStateMapper::BedrockBlockState BlockStateMapper::forgeToBedrockBlockState(ForgeBlockState const& fs) {
    BedrockBlockState out;
    out.name = mapBlockName(fs.name);
    auto remap = propertyMappings.find(fs.name);
    for (auto const& [k, v] : fs.properties) {
        if (remap != propertyMappings.end() && remap->second.count(k)) {
            out.properties[remap->second.at(k)] = v;
        } else {
            out.properties[k] = v;
        }
    }
    return out;
}

BlockStateMapper::ForgeBlockState BlockStateMapper::bedrockToForgeBlockState(BedrockBlockState const& bs) {
    ForgeBlockState out;
    for (auto const& [forge, bedrock] : blockNameMappings) {
        if (bedrock == bs.name) {
            out.name = forge;
            break;
        }
    }
    if (out.name.empty()) out.name = bs.name;
    out.properties = bs.properties;
    return out;
}

// ============================================================================
// EntityTracker
// ============================================================================
std::map<jlong, void*>   EntityTracker::entityIdToBedrockMap;
std::map<void*, jlong>   EntityTracker::bedrockToEntityIdMap;
std::map<jlong, jobject> EntityTracker::entityIdToJavaRefMap;

jlong EntityTracker::getEntityId(JNIEnv* env, jobject javaEntity) {
    if (!env || !javaEntity) return 0;
    jclass sys = env->FindClass("java/lang/System");
    if (!sys) {
        env->ExceptionClear();
        return reinterpret_cast<jlong>(javaEntity);
    }
    jmethodID m = env->GetStaticMethodID(sys, "identityHashCode", "(Ljava/lang/Object;)I");
    env->DeleteLocalRef(sys);
    if (!m) return reinterpret_cast<jlong>(javaEntity);
    jint id = env->CallStaticIntMethod(sys, m, javaEntity);
    return static_cast<jlong>(id);
}

void EntityTracker::registerEntity(JNIEnv* env, jobject javaEntity, void* bedrockActor) {
    if (!env || !javaEntity || !bedrockActor) return;
    jlong id = getEntityId(env, javaEntity);
    auto it = entityIdToJavaRefMap.find(id);
    if (it != entityIdToJavaRefMap.end()) {
        env->DeleteGlobalRef(it->second);
    }
    entityIdToBedrockMap[id]           = bedrockActor;
    bedrockToEntityIdMap[bedrockActor] = id;
    entityIdToJavaRefMap[id]           = env->NewGlobalRef(javaEntity);
}

void* EntityTracker::getBedrockActor(JNIEnv* env, jobject javaEntity) {
    if (!env || !javaEntity) return nullptr;
    jlong id = getEntityId(env, javaEntity);
    auto it  = entityIdToBedrockMap.find(id);
    return it != entityIdToBedrockMap.end() ? it->second : nullptr;
}

jobject EntityTracker::getJavaEntity(void* bedrockActor) {
    auto it = bedrockToEntityIdMap.find(bedrockActor);
    if (it == bedrockToEntityIdMap.end()) return nullptr;
    auto jt = entityIdToJavaRefMap.find(it->second);
    if (jt == entityIdToJavaRefMap.end()) return nullptr;
    return jt->second;
}

void EntityTracker::unregisterEntity(JNIEnv* env, jobject javaEntity) {
    if (!env || !javaEntity) return;
    jlong id = getEntityId(env, javaEntity);
    auto it  = entityIdToBedrockMap.find(id);
    if (it == entityIdToBedrockMap.end()) return;
    bedrockToEntityIdMap.erase(it->second);
    entityIdToBedrockMap.erase(it);
    auto jt = entityIdToJavaRefMap.find(id);
    if (jt != entityIdToJavaRefMap.end()) {
        env->DeleteGlobalRef(jt->second);
        entityIdToJavaRefMap.erase(jt);
    }
}

bool EntityTracker::hasEntity(JNIEnv* env, jobject javaEntity) {
    if (!env || !javaEntity) return false;
    jlong id = getEntityId(env, javaEntity);
    return entityIdToBedrockMap.count(id) > 0;
}

// ============================================================================
// NativeShadowAdapter
// ============================================================================
JNINativeMethod NativeShadowAdapter::nativeMethods[] = {
    {(char*)"setPos",   (char*)"(DDD)V",   (void*)&NativeShadowAdapter::nativeEntitySetPos},
    {(char*)"getPos",   (char*)"()[D",     (void*)&NativeShadowAdapter::nativeEntityGetPos},
    {(char*)"addTag",   (char*)"(Ljava/lang/String;)V", (void*)&NativeShadowAdapter::nativeEntityAddTag},
    {(char*)"setBlock", (char*)"(Lnet/minecraft/core/BlockPos;Lnet/minecraft/world/level/block/BlockState;I)Z",
                        (void*)&NativeShadowAdapter::nativeBlockSetBlock},
    {(char*)"getBlock", (char*)"(Lnet/minecraft/core/BlockPos;)Lnet/minecraft/world/level/block/BlockState;",
                        (void*)&NativeShadowAdapter::nativeBlockGetBlock},
};
int NativeShadowAdapter::nativeMethodCount = sizeof(NativeShadowAdapter::nativeMethods) / sizeof(JNINativeMethod);

bool NativeShadowAdapter::registerNativeMethods(JNIEnv* env, std::string const& pkg) {
    std::string path = pkg;
    std::replace(path.begin(), path.end(), '.', '/');
    path += "/ForgeEntityAdapter";
    jclass c = env->FindClass(path.c_str());
    if (!c) {
        env->ExceptionClear();
        return false;
    }
    bool ok = (env->RegisterNatives(c, nativeMethods, nativeMethodCount) == JNI_OK);
    env->DeleteLocalRef(c);
    return ok;
}

void JNICALL NativeShadowAdapter::nativeEntitySetPos(JNIEnv* env, jobject entity, jdouble x, jdouble y, jdouble z) {
    void* actor = EntityTracker::getBedrockActor(env, entity);
    if (!actor) return;
    auto fn = BedrockSymbolResolver::getActorSetPos();
    if (!fn) return;
    Vec3 v = BedrockPointerHelper::makeVec3(x, y, z);
    fn(actor, &v);
}

jobject JNICALL NativeShadowAdapter::nativeEntityGetPos(JNIEnv* env, jobject entity) {
    void* actor = EntityTracker::getBedrockActor(env, entity);
    if (!actor) return nullptr;
    auto fn = BedrockSymbolResolver::getActorGetPos();
    if (!fn) return nullptr;
    Vec3 v{0, 0, 0};
    fn(actor, &v);
    return BedrockPointerHelper::vec3ToJDoubleArray(env, v);
}

void JNICALL NativeShadowAdapter::nativeEntityAddTag(JNIEnv* env, jobject entity, jstring tag) {
    void* actor = EntityTracker::getBedrockActor(env, entity);
    if (!actor) return;
    auto fn = BedrockSymbolResolver::getActorAddTag();
    if (!fn) return;
    char const* s = env->GetStringUTFChars(tag, nullptr);
    fn(actor, std::string{s});
    env->ReleaseStringUTFChars(tag, s);
}

jboolean JNICALL NativeShadowAdapter::nativeBlockSetBlock(JNIEnv* env, jobject, jobject blockPos, jobject blockState, jint) {
    auto pos = BedrockPointerHelper::extractBlockPos(env, blockPos);
    auto bct = BedrockSymbolResolver::getBlockCreate();
    auto bst = BedrockSymbolResolver::getBlockSetType();
    if (!bct || !bst) return JNI_FALSE;

    jclass bsc   = env->GetObjectClass(blockState);
    jmethodID gt = env->GetMethodID(bsc, "getType", "()Lnet/minecraft/world/level/block/Block;");
    jobject bo   = env->CallObjectMethod(blockState, gt);
    jclass bc    = env->GetObjectClass(bo);
    jmethodID gn = env->GetMethodID(bc, "getName", "()Ljava/lang/String;");
    jstring jn   = (jstring)env->CallObjectMethod(bo, gn);

    char const* name = env->GetStringUTFChars(jn, nullptr);
    std::string bedrockName = BlockStateMapper::mapBlockName(name);
    env->ReleaseStringUTFChars(jn, name);

    env->DeleteLocalRef(bsc);
    env->DeleteLocalRef(bo);
    env->DeleteLocalRef(bc);
    env->DeleteLocalRef(jn);

    void* block = bct(bedrockName.c_str());
    if (!block) return JNI_FALSE;
    bst(nullptr, pos.x, pos.y, pos.z, block);
    return JNI_TRUE;
}

jobject JNICALL NativeShadowAdapter::nativeBlockGetBlock(JNIEnv*, jobject, jobject) {
    return nullptr;
}

jobject JNICALL NativeShadowAdapter::nativeItemUse(JNIEnv*, jobject, jobject, jobject, jint) {
    return nullptr;
}

// ============================================================================
// BedrockPointerHelper
// ============================================================================
Vec3 BedrockPointerHelper::makeVec3(double x, double y, double z) {
    return Vec3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
}

jdoubleArray BedrockPointerHelper::vec3ToJDoubleArray(JNIEnv* env, Vec3 const& v) {
    jdoubleArray arr = env->NewDoubleArray(3);
    jdouble data[3] = {v.x, v.y, v.z};
    env->SetDoubleArrayRegion(arr, 0, 3, data);
    return arr;
}

BlockPos BedrockPointerHelper::extractBlockPos(JNIEnv* env, jobject obj) {
    BlockPos p{0, 0, 0};
    if (!obj) return p;
    jclass c = env->GetObjectClass(obj);
    jfieldID fx = env->GetFieldID(c, "x", "I");
    jfieldID fy = env->GetFieldID(c, "y", "I");
    jfieldID fz = env->GetFieldID(c, "z", "I");
    p.x = env->GetIntField(obj, fx);
    p.y = env->GetIntField(obj, fy);
    p.z = env->GetIntField(obj, fz);
    env->DeleteLocalRef(c);
    return p;
}

jobject BedrockPointerHelper::createBlockPos(JNIEnv* env, int x, int y, int z) {
    jclass c = env->FindClass("net/minecraft/core/BlockPos");
    if (!c) {
        env->ExceptionClear();
        return nullptr;
    }
    jmethodID ctor = env->GetMethodID(c, "<init>", "(III)V");
    if (!ctor) {
        env->DeleteLocalRef(c);
        return nullptr;
    }
    jobject obj = env->NewObject(c, ctor, x, y, z);
    env->DeleteLocalRef(c);
    return obj;
}

// ============================================================================
// ForgeEventForwarder
// ============================================================================
std::vector<jobject> ForgeEventForwarder::registeredForgeListeners;

static void fireJavaBridge(std::string const& eventName,
                           std::function<void(JNIEnv*, jclass)> fn) {
    enqueueJNICall(eventName, [fn = std::move(fn)](JNIEnv* env) {
        jclass bridge = env->FindClass("com/example/mod/ForgeEventBridge");
        if (!bridge) {
            env->ExceptionClear();
            return;
        }
        fn(env, bridge);
        env->DeleteLocalRef(bridge);
    });
}

void ForgeEventForwarder::init() {
    startJNIWorker();
}

void ForgeEventForwarder::shutdown() {
    JNIThreadManager::ThreadGuard guard;
    JNIEnv* env = guard.getEnv();
    if (env) clearListeners(env);
    stopJNIWorker();
}

void ForgeEventForwarder::forwardActorHurt(Actor& actor, int damage) {
    fireJavaBridge("ActorHurt", [&actor, damage](JNIEnv* env, jclass bridge) {
        jmethodID mid = env->GetStaticMethodID(
            bridge,
            "onActorHurt",
            "(J I)V"
        );
        if (!mid) {
            env->ExceptionClear();
            return;
        }
        jlong ptr = reinterpret_cast<jlong>(&actor);
        env->CallStaticVoidMethod(bridge, mid, ptr, jint(damage));
    });
}

void ForgeEventForwarder::forwardPlayerDeath(Player& player) {
    fireJavaBridge("PlayerDeath", [&player](JNIEnv* env, jclass bridge) {
        jmethodID mid = env->GetStaticMethodID(
            bridge,
            "onPlayerDeath",
            "(J)V"
        );
        if (!mid) {
            env->ExceptionClear();
            return;
        }
        jlong ptr = reinterpret_cast<jlong>(&player);
        env->CallStaticVoidMethod(bridge, mid, ptr);
    });
}

void ForgeEventForwarder::forwardMobDeath(Actor& mob) {
    fireJavaBridge("MobDeath", [&mob](JNIEnv* env, jclass bridge) {
        jmethodID mid = env->GetStaticMethodID(
            bridge,
            "onMobDeath",
            "(J)V"
        );
        if (!mid) {
            env->ExceptionClear();
            return;
        }
        jlong ptr = reinterpret_cast<jlong>(&mob);
        env->CallStaticVoidMethod(bridge, mid, ptr);
    });
}

void ForgeEventForwarder::forwardBlockBreak(Player& player, BlockPos const& pos) {
    fireJavaBridge("BlockBreak", [&player, pos](JNIEnv* env, jclass bridge) {
        jmethodID mid = env->GetStaticMethodID(
            bridge,
            "onBlockBreak",
            "(JIII)V"
        );
        if (!mid) {
            env->ExceptionClear();
            return;
        }
        jlong ptr = reinterpret_cast<jlong>(&player);
        env->CallStaticVoidMethod(bridge, mid, ptr, jint(pos.x), jint(pos.y), jint(pos.z));
    });
}

void ForgeEventForwarder::forwardPlayerChat(Player& player, std::string const& msg) {
    fireJavaBridge("PlayerChat", [&player, msg](JNIEnv* env, jclass bridge) {
        jmethodID mid = env->GetStaticMethodID(
            bridge,
            "onPlayerChat",
            "(JLjava/lang/String;)V"
        );
        if (!mid) {
            env->ExceptionClear();
            return;
        }
        jlong ptr = reinterpret_cast<jlong>(&player);
        jstring jmsg = env->NewStringUTF(msg.c_str());
        env->CallStaticVoidMethod(bridge, mid, ptr, jmsg);
        env->DeleteLocalRef(jmsg);
    });
}

void ForgeEventForwarder::forwardPlayerJoin(Player& player) {
    fireJavaBridge("PlayerJoin", [&player](JNIEnv* env, jclass bridge) {
        jmethodID mid = env->GetStaticMethodID(
            bridge,
            "onPlayerJoin",
            "(J)V"
        );
        if (!mid) {
            env->ExceptionClear();
            return;
        }
        jlong ptr = reinterpret_cast<jlong>(&player);
        env->CallStaticVoidMethod(bridge, mid, ptr);
    });
}

void ForgeEventForwarder::clearListeners(JNIEnv* env) {
    for (auto ref : registeredForgeListeners) {
        if (ref) env->DeleteGlobalRef(ref);
    }
    registeredForgeListeners.clear();
}

// ============================================================================
// Event registration (hook into LeviLamina bus)
// ============================================================================
static void registerEvents() {
    auto& bus    = ll::event::EventBus::getInstance();
    auto& logger = my_mod::MyMod::getInstance().getSelf().getLogger();

    bus.emplaceListener<ll::event::entity::ActorHurtEvent>([](ll::event::entity::ActorHurtEvent& ev) {
        auto* actor = ev.actor();
        if (!actor) return;
        int dmg = int(ev.damage());
        ForgeEventForwarder::forwardActorHurt(*actor, dmg);
    });

    bus.emplaceListener<ll::event::player::PlayerDieEvent>([](ll::event::player::PlayerDieEvent& ev) {
        auto* player = ev.player();
        if (!player) return;
        ForgeEventForwarder::forwardPlayerDeath(*player);
    });

    bus.emplaceListener<ll::event::entity::MobDieEvent>([](ll::event::entity::MobDieEvent& ev) {
        auto* mob = ev.mob();
        if (!mob) return;
        ForgeEventForwarder::forwardMobDeath(*mob);
    });

    bus.emplaceListener<ll::event::player::PlayerBreakBlockEvent>([](ll::event::player::PlayerBreakBlockEvent& ev) {
        auto* player = ev.player();
        if (!player) return;
        auto const& bp = ev.blockPos();
        BlockPos pos{bp.x, bp.y, bp.z};
        ForgeEventForwarder::forwardBlockBreak(*player, pos);
    });

    bus.emplaceListener<ll::event::player::PlayerChatEvent>([](ll::event::player::PlayerChatEvent& ev) {
        auto* player = ev.player();
        if (!player) return;
        ForgeEventForwarder::forwardPlayerChat(*player, ev.message());
    });

    bus.emplaceListener<ll::event::player::PlayerJoinEvent>([](ll::event::player::PlayerJoinEvent& ev) {
        auto* player = ev.player();
        if (!player) return;
        ForgeEventForwarder::forwardPlayerJoin(*player);
    });

    bus.emplaceListener<ll::event::server::ServerStartedEvent>([](ll::event::server::ServerStartedEvent&) {
        auto& logger = my_mod::MyMod::getInstance().getSelf().getLogger();
        logger.info("ModMorpher: ServerStartedEvent -> init");
        BedrockSymbolResolver::initialize();
        ForgeEventForwarder::init();
    });

    bus.emplaceListener<ll::event::server::ServerStoppingEvent>([](ll::event::server::ServerStoppingEvent&) {
        auto& logger = my_mod::MyMod::getInstance().getSelf().getLogger();
        logger.info("ModMorpher: ServerStoppingEvent -> shutdown");
        ForgeEventForwarder::shutdown();
    });

    logger.info("ModMorpher: events registered");
}

// ============================================================================
// Module entry (called from MyMod or JNI_OnLoad)
// ============================================================================
static bool gInitialized = false;

void initializeModMorpher(JavaVM* vm) {
    if (gInitialized) return;
    JNIThreadManager::setJVM(vm);
    BlockStateMapper::loadMappings("");
    registerEvents();
    gInitialized = true;
}

} // namespace modmorpher
