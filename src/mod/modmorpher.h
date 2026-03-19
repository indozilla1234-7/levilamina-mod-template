#pragma once

#include <jni.h>
#include <string>
#include <map>
#include <memory>
#include <functional>
#include <vector>

namespace modmorpher {

/**
 * ModMorpher: Forge to Bedrock Runtime Translator
 * 
 * Consolidates all functionality for running Forge mods in Bedrock
 * - Symbol resolution for Bedrock C++ functions
 * - JNI native method implementations
 * - Block state translation
 * - Entity tracker for Java↔Bedrock mapping
 * - Thread-safe JNI operations
 */

// ============================================================================
// BEDROCK SYMBOL RESOLVER
// ============================================================================

/**
 * Resolves Bedrock C++ function symbols using ll::memory
 * Maps mangled names from output.txt to function pointers
 */
class BedrockSymbolResolver {
public:
    // Function pointer types
    using ActorSetPos = void(*)(void* actor, const void* vec3);
    using ActorGetPos = void(*)(void* actor, void* outVec3);
    using BlockSetType = void(*)(void* blockSource, int x, int y, int z, const void* block);
    using DimensionGetBlock = void*(*)(void* dimension, int x, int y, int z);
    using ActorAddTag = void(*)(void* actor, const char* tag);
    using ActorSetAttribute = void(*)(void* actor, const char* attributeName, double value);
    using CommandExecute = int(*)(void* dimension, const char* command);
    using BlockCreate = void*(*)(const char* blockName);

    /**
     * Initialize symbol resolver - resolve all Bedrock function pointers
     */
    static bool initialize();

    /**
     * Get resolved function pointers
     */
    static ActorSetPos getActorSetPos();
    static ActorGetPos getActorGetPos();
    static BlockSetType getBlockSetType();
    static DimensionGetBlock getDimensionGetBlock();
    static ActorAddTag getActorAddTag();
    static ActorSetAttribute getActorSetAttribute();
    static CommandExecute getCommandExecute();
    static BlockCreate getBlockCreate();

    /**
     * Resolve a symbol by mangled name
     */
    static void* resolveSymbol(const char* mangledName);

private:
    static ActorSetPos actorSetPos;
    static ActorGetPos actorGetPos;
    static BlockSetType blockSetType;
    static DimensionGetBlock dimensionGetBlock;
    static ActorAddTag actorAddTag;
    static ActorSetAttribute actorSetAttribute;
    static CommandExecute commandExecute;
    static BlockCreate blockCreate;
    static bool resolved;
};

// ============================================================================
// JNI THREAD MANAGER
// ============================================================================

/**
 * Safely manages JNI calls from different threads
 * Handles AttachCurrentThread and DetachCurrentThread
 */
class JNIThreadManager {
public:
    /**
     * Set the cached JVM for thread management
     */
    static void setJVM(JavaVM* vm);

    /**
     * Ensure current thread is attached to JVM
     */
    static bool ensureAttached();

    /**
     * Detach current thread from JVM
     */
    static void detachCurrentThread();

    /**
     * Get JNIEnv for current thread
     */
    static JNIEnv* getEnv();

    /**
     * Scope guard for automatic thread attachment/detachment
     */
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

/**
 * Translates between Forge BlockState and Bedrock BlockState
 */
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

    /**
     * Load block mappings from JSON file
     */
    static bool loadMappings(const std::string& mappingsFile);

    /**
     * Convert Forge block state to Bedrock
     */
    static BedrockBlockState forgeToBedrockBlockState(const ForgeBlockState& forgeState);

    /**
     * Convert Bedrock block state to Forge
     */
    static ForgeBlockState bedrockToForgeBlockState(const BedrockBlockState& bedrockState);

    /**
     * Map block name
     */
    static std::string mapBlockName(const std::string& forgeName);

private:
    static std::map<std::string, std::string> blockNameMappings;
    static std::map<std::string, std::map<std::string, std::string>> propertyMappings;
};

// ============================================================================
// ENTITY TRACKER
// ============================================================================

/**
 * Maintains bidirectional mapping between Java Entity objects and Bedrock Actors
 * Uses unique Long IDs from Java objects instead of unreliable jobject pointers
 */
class EntityTracker {
public:
    /**
     * Map a Java entity jobject to a Bedrock actor pointer
     * Extracts unique ID from Java object for safe key storage
     */
    static void registerEntity(JNIEnv* env, jobject javaEntity, void* bedrockActor);

    /**
     * Get Bedrock actor pointer from Java entity using unique ID
     */
    static void* getBedrockActor(JNIEnv* env, jobject javaEntity);

    /**
     * Get Java entity jobject from Bedrock actor
     */
    static jobject getJavaEntity(void* bedrockActor);

    /**
     * Unregister entity mapping (on entity death)
     */
    static void unregisterEntity(JNIEnv* env, jobject javaEntity);

    /**
     * Check if entity exists in mapping
     */
    static bool hasEntity(JNIEnv* env, jobject javaEntity);

private:
    /**
     * Extract unique ID from Java object (use hashCode or object field)
     */
    static jlong getEntityId(JNIEnv* env, jobject javaEntity);
    static std::map<jlong, void*> entityIdToBedrockMap;          // Java entity ID → Bedrock actor
    static std::map<void*, jlong> bedrockToEntityIdMap;          // Bedrock actor → Java entity ID
    static std::map<jlong, jobject> entityIdToJavaRefMap;        // Java entity ID → global ref
};

// ============================================================================
// NATIVE SHADOW ADAPTER
// ============================================================================

/**
 * Creates native JNI stubs that Forge methods call into
 */
class NativeShadowAdapter {
public:
    /**
     * Register all native methods with JVM
     */
    static bool registerNativeMethods(JNIEnv* env, const std::string& modPackageName);

    /**
     * JNI native method implementations
     */
    static void JNICALL nativeEntitySetPos(JNIEnv* env, jobject entity, jdouble x, jdouble y, jdouble z);
    static jobject JNICALL nativeEntityGetPos(JNIEnv* env, jobject entity);
    static void JNICALL nativeEntityAddTag(JNIEnv* env, jobject entity, jstring tag);
    
    static jboolean JNICALL nativeBlockSetBlock(
        JNIEnv* env,
        jobject level,
        jobject blockPos,
        jobject blockState,
        jint flags
    );
    
    static jobject JNICALL nativeBlockGetBlock(JNIEnv* env, jobject level, jobject blockPos);
    
    static jobject JNICALL nativeItemUse(
        JNIEnv* env,
        jobject item,
        jobject level,
        jobject player,
        jint hand
    );

private:
    static JNINativeMethod nativeMethods[];
    static int nativeMethodCount;
};

// ============================================================================
// BEDROCK POINTER HELPER
// ============================================================================

/**
 * Helper utilities for working with Bedrock C++ objects
 */
class BedrockPointerHelper {
public:
    /**
     * Vector3 type for Bedrock
     */
    struct Vec3 {
        float x, y, z;
    };

    /**
     * BlockPos type
     */
    struct BlockPos {
        int x, y, z;
    };

    /**
     * Pack Vec3 into Bedrock format
     */
    static Vec3 makeVec3(double x, double y, double z);

    /**
     * Convert Bedrock Vec3 to Java double array
     */
    static jdoubleArray vec3ToJDoubleArray(JNIEnv* env, const Vec3& vec);

    /**
     * Extract coordinates from Java BlockPos
     */
    static BlockPos extractBlockPos(JNIEnv* env, jobject blockPosObj);

    /**
     * Create Java BlockPos from coordinates
     */
    static jobject createBlockPos(JNIEnv* env, int x, int y, int z);
};

// ============================================================================
// EVENT FORWARDER
// ============================================================================

/**
 * Forwards Bedrock events to Forge mods
 */
class ForgeEventForwarder {
public:
    using PlayerEventHandler = std::function<void(const std::string&)>;
    using BlockEventHandler = std::function<void(int, int, int, const std::string&)>;

    /**
     * Register event handlers
     */
    static void on PlayerJoin(PlayerEventHandler handler);
    static void onPlayerLeave(PlayerEventHandler handler);
    static void onBlockBreak(BlockEventHandler handler);
    static void onBlockPlace(BlockEventHandler handler);

    /**
     * Forward events to registered handlers
     */
    static void forwardPlayerJoinEvent(const std::string& playerId);
    static void forwardPlayerLeaveEvent(const std::string& playerId);
    static void forwardBlockBreakEvent(int x, int y, int z, const std::string& playerId);
    static void forwardBlockPlaceEvent(int x, int y, int z, const std::string& blockName, const std::string& playerId);

    /**
     * Register Forge event listener
     */
    static void registerForgeEventListener(JNIEnv* env, const std::string& eventClass, jobject listener);

private:
    static std::vector<PlayerEventHandler> playerJoinHandlers;
    static std::vector<PlayerEventHandler> playerLeaveHandlers;
    static std::vector<BlockEventHandler> blockBreakHandlers;
    static std::vector<BlockEventHandler> blockPlaceHandlers;
    static std::vector<jobject> registeredForgeListeners;
};

// ============================================================================
// MODMORPHER MANAGER
// ============================================================================

/**
 * Main coordinator for Forge mod translation
 */
class ModMorpher {
public:
    /**
     * Initialize ModMorpher with JVM
     * Call this from MyMod::load() after JVM is created
     */
    static bool initialize(JavaVM* jvm, JNIEnv* env);

    /**
     * Shutdown ModMorpher
     * Call this from MyMod::disable()
     */
    static void shutdown();

    /**
     * Load a Forge mod JAR
     */
    static bool loadForgeMod(const std::string& jarPath);

    /**
     * Unload a Forge mod
     */
    static bool unloadForgeMod(const std::string& modId);

    /**
     * Get list of loaded mods
     */
    static std::vector<std::string> getLoadedMods();

    /**
     * Check if initialized
     */
    static bool isInitialized();

private:
    static bool initialized;
    static JavaVM* cachedJVM;
    static JNIEnv* cachedEnv;
    static std::vector<std::string> loadedMods;
};

