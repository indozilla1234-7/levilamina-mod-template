#pragma once

#include "jni.h"
#include <string>
#include <map>
#include <functional>
#include <vector>

// Make Vec3 visible everywhere this header is included
#include "mc/math/vector/Vecs.h"

namespace modmorpher {

// ============================================================================
// BEDROCK SYMBOL RESOLVER
// ============================================================================

class BedrockSymbolResolver {
public:
    using ActorSetPos       = void(*)(void* actor, const void* vec3);
    using ActorGetPos       = void(*)(void* actor, void* outVec3);
    using BlockSetType      = void(*)(void* blockSource, int x, int y, int z, const void* block);
    using ActorAddTag       = void(*)(void* actor, const char* tag);
    using ActorSetAttribute = void(*)(void* actor, const void* attribute, float value);
    using CommandExecute    = int(*)(void* level, const char* command);
    using BlockCreate       = void*(*)(const char* blockName);

    static bool initialize();

    static ActorSetPos       getActorSetPos();
    static ActorGetPos       getActorGetPos();
    static BlockSetType      getBlockSetType();
    static ActorAddTag       getActorAddTag();
    static ActorSetAttribute getActorSetAttribute();
    static CommandExecute    getCommandExecute();
    static BlockCreate       getBlockCreate();

    static void* resolveSymbol(const char* name);

private:
    static ActorSetPos       actorSetPos;
    static ActorGetPos       actorGetPos;
    static BlockSetType      blockSetType;
    static ActorAddTag       actorAddTag;
    static ActorSetAttribute actorSetAttribute;
    static CommandExecute    commandExecute;
    static BlockCreate       blockCreate;
    static bool              resolved;
};

// ============================================================================
// JNI THREAD MANAGER
// ============================================================================

class JNIThreadManager {
public:
    static void setJVM(JavaVM* vm);
    static bool ensureAttached();
    static void detachCurrentThread();
    static JNIEnv* getEnv();

    class ThreadGuard {
    public:
        ThreadGuard();
        ~ThreadGuard();
        JNIEnv* getEnv();
    private:
        bool wasAttached;
    };

private:
    static JavaVM* jvm;
    thread_local static JNIEnv* threadEnv;
};

// ============================================================================
// BLOCK STATE MAPPER
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

    static bool loadMappings(const std::string& file);
    static BedrockBlockState forgeToBedrockBlockState(const ForgeBlockState&);
    static ForgeBlockState  bedrockToForgeBlockState(const BedrockBlockState&);
    static std::string      mapBlockName(const std::string& forgeName);

private:
    static std::map<std::string, std::string> blockNameMappings;
    static std::map<std::string, std::map<std::string, std::string>> propertyMappings;
};

// ============================================================================
// ENTITY TRACKER
// ============================================================================

class EntityTracker {
public:
    static void    registerEntity(JNIEnv* env, jobject javaEntity, void* bedrockActor);
    static void*   getBedrockActor(JNIEnv* env, jobject javaEntity);
    static jobject getJavaEntity(void* bedrockActor);
    static void    unregisterEntity(JNIEnv* env, jobject javaEntity);
    static bool    hasEntity(JNIEnv* env, jobject javaEntity);

private:
    static jlong getEntityId(JNIEnv* env, jobject javaEntity);

    static std::map<jlong, void*>   entityIdToBedrockMap;
    static std::map<void*, jlong>   bedrockToEntityIdMap;
    static std::map<jlong, jobject> entityIdToJavaRefMap;
};

// ============================================================================
// NATIVE SHADOW ADAPTER
// ============================================================================

class NativeShadowAdapter {
public:
    static bool registerNativeMethods(JNIEnv* env, const std::string& pkg);

    static void JNICALL nativeEntitySetPos(JNIEnv* env, jobject entity, jdouble x, jdouble y, jdouble z);
    static jobject JNICALL nativeEntityGetPos(JNIEnv* env, jobject entity);
    static void JNICALL nativeEntityAddTag(JNIEnv* env, jobject entity, jstring tag);

    static jboolean JNICALL nativeBlockSetBlock(JNIEnv* env, jobject, jobject blockPos, jobject blockState, jint flags);
    static jobject  JNICALL nativeBlockGetBlock(JNIEnv* env, jobject, jobject blockPos);

    static jobject JNICALL nativeItemUse(JNIEnv*, jobject, jobject, jobject, jint);

private:
    static JNINativeMethod nativeMethods[];
    static int nativeMethodCount;
};

// ============================================================================
// BEDROCK POINTER HELPER
// ============================================================================

class BedrockPointerHelper {
public:
    struct BlockPos { int x, y, z; };

    // Use Bedrock's Vec3
// FIXED:     static Vec3 makeVec3(double x, double y, double z);
    static jdoubleArray vec3ToJDoubleArray(JNIEnv* env, const Vec3& vec);

    static BlockPos extractBlockPos(JNIEnv* env, jobject blockPosObj);
    static jobject createBlockPos(JNIEnv* env, int x, int y, int z);
};

// ============================================================================
// EVENT FORWARDER
// ============================================================================

class ForgeEventForwarder {
public:
    using PlayerEventHandler = std::function<void(const std::string&)>;
    using BlockEventHandler  = std::function<void(int, int, int, const std::string&)>;

    static void registerLeviLaminaHooks();

    static void onPlayerJoin(PlayerEventHandler);
    static void onPlayerLeave(PlayerEventHandler);
    static void onBlockBreak(BlockEventHandler);
    static void onBlockPlace(BlockEventHandler);

    static void forwardPlayerJoinEvent(const std::string&);
    static void forwardPlayerLeaveEvent(const std::string&);
    static void forwardBlockBreakEvent(int x, int y, int z, const std::string&);
    static void forwardBlockPlaceEvent(int x, int y, int z, const std::string& block, const std::string& player);

    static void registerForgeEventListener(JNIEnv* env, const std::string& eventClass, jobject listener);

private:
    static std::vector<PlayerEventHandler> playerJoinHandlers;
    static std::vector<PlayerEventHandler> playerLeaveHandlers;
    static std::vector<BlockEventHandler>  blockBreakHandlers;
    static std::vector<BlockEventHandler>  blockPlaceHandlers;
};

// ============================================================================
// MODMORPHER MANAGER
// ============================================================================

class ModMorpher {
public:
    static bool initialize(JavaVM* jvm, JNIEnv* env);
    static void shutdown();

    static bool loadForgeMod(const std::string& jarPath);
    static bool unloadForgeMod(const std::string& modId);

    static std::vector<std::string> getLoadedMods();
    static bool isInitialized();

private:
    static bool initialized;
    static JavaVM* cachedJVM;
    static JNIEnv* cachedEnv;
    static std::vector<std::string> loadedMods;
};

} // namespace modmorpher
