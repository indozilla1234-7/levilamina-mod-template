#pragma once 

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "jni.h"

namespace mc {
namespace world {
namespace actor {
    class Actor;
    namespace player {
        class Player;
    }
}
namespace level {
    class Level;
    namespace block {
        class Block;
    }
}
}
}

namespace modmorpher {

// ============================================================================
// Basic helper structs
// ============================================================================
struct Vec3 {
    float x, y, z;
};

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

    static JavaVM*              jvm;
    static thread_local JNIEnv* threadEnv;
};

// ============================================================================
// Bedrock Symbol Resolver
// ============================================================================
class BedrockSymbolResolver {
public:
    using ActorSetPos       = void(*)(void*, Vec3 const*);
    using ActorGetPos       = Vec3 const&(*)(void*, Vec3*);
    using BlockSetType      = bool(*)(void*, int, int, int, void*);
    using ActorAddTag       = void(*)(void*, std::string const&);
    using ActorSetAttribute = void(*)(void*, void const&, float);
    using BlockCreate       = void*(*)(char const*);

    static bool initialize();
    static void* resolveSymbol(char const* name);

    static ActorSetPos       getActorSetPos();
    static ActorGetPos       getActorGetPos();
    static BlockSetType      getBlockSetType();
    static ActorAddTag       getActorAddTag();
    static ActorSetAttribute getActorSetAttribute();
    static BlockCreate       getBlockCreate();

private:
    static ActorSetPos       actorSetPos;
    static ActorGetPos       actorGetPos;
    static BlockSetType      blockSetType;
    static ActorAddTag       actorAddTag;
    static ActorSetAttribute actorSetAttribute;
    static BlockCreate       blockCreate;
    static bool              resolved;
};

// ============================================================================
// Block State Mapper
// ============================================================================
class BlockStateMapper {
public:
    struct ForgeBlockState {
        std::string name;
        std::map<std::string, std::string> properties;
    };

    struct BedrockBlockState {
        std::string name;
        std::map<std::string, std::string> properties;
    };

    static bool loadMappings(std::string const& path);
    static std::string mapBlockName(std::string const& forgeName);
    static BedrockBlockState forgeToBedrockBlockState(ForgeBlockState const& fs);
    static ForgeBlockState   bedrockToForgeBlockState(BedrockBlockState const& bs);

private:
    static std::map<std::string, std::string> blockNameMappings;
    static std::map<std::string, std::map<std::string, std::string>> propertyMappings;
};

// ============================================================================
// Entity Tracker
// ============================================================================
class EntityTracker {
public:
    static void   registerEntity(JNIEnv* env, jobject javaEntity, void* bedrockActor);
    static void*  getBedrockActor(JNIEnv* env, jobject javaEntity);
    static jobject getJavaEntity(void* bedrockActor);
    static void   unregisterEntity(JNIEnv* env, jobject javaEntity);
    static bool   hasEntity(JNIEnv* env, jobject javaEntity);

private:
    static jlong getEntityId(JNIEnv* env, jobject javaEntity);

    static std::map<jlong, void*>   entityIdToBedrockMap;
    static std::map<void*, jlong>   bedrockToEntityIdMap;
    static std::map<jlong, jobject> entityIdToJavaRefMap;
};

// ============================================================================
// Native Shadow Adapter
// ============================================================================
class NativeShadowAdapter {
public:
    static bool registerNativeMethods(JNIEnv* env, std::string const& pkg);

private:
    static void JNICALL nativeEntitySetPos(JNIEnv* env, jobject entity, jdouble x, jdouble y, jdouble z);
    static jobject JNICALL nativeEntityGetPos(JNIEnv* env, jobject entity);
    static void JNICALL nativeEntityAddTag(JNIEnv* env, jobject entity, jstring tag);
    static jboolean JNICALL nativeBlockSetBlock(JNIEnv* env, jobject, jobject blockPos, jobject blockState, jint flags);
    static jobject JNICALL nativeBlockGetBlock(JNIEnv* env, jobject, jobject blockPos);
    static jobject JNICALL nativeItemUse(JNIEnv* env, jobject, jobject, jobject, jint);

    static JNINativeMethod nativeMethods[];
    static int             nativeMethodCount;
};

// ============================================================================
// Bedrock Pointer Helper
// ============================================================================
class BedrockPointerHelper {
public:
    static Vec3         makeVec3(double x, double y, double z);
    static jdoubleArray vec3ToJDoubleArray(JNIEnv* env, Vec3 const& v);

    static BlockPos extractBlockPos(JNIEnv* env, jobject obj);
    static jobject  createBlockPos(JNIEnv* env, int x, int y, int z);
};

// ============================================================================
// Forge Event Forwarder
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

    static void clearListeners(JNIEnv* env);

private:
    static std::vector<jobject> registeredForgeListeners;
};

// ============================================================================
// JNI Queue Helpers
// ============================================================================
void enqueueJNICall(std::string desc, std::function<void(JNIEnv*)> fn);
void startJNIWorker();
void stopJNIWorker();

// ============================================================================
// Public Mod Entry Points (used by MyMod.cpp)
// ============================================================================
class ModMorpher {
public:
    static void initialize();
    static void shutdown();
};

} // namespace modmorpher
