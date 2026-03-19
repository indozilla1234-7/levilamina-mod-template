#include "modmorpher.h"
#include "mod/MyMod.h"
#include <ll/api/memory/Memory.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>

namespace modmorpher {

using json = nlohmann::json;

// ============================================================================
// BEDROCK SYMBOL RESOLVER - STATIC INITIALIZATION
// ============================================================================

BedrockSymbolResolver::ActorSetPos BedrockSymbolResolver::actorSetPos = nullptr;
BedrockSymbolResolver::ActorGetPos BedrockSymbolResolver::actorGetPos = nullptr;
BedrockSymbolResolver::BlockSetType BedrockSymbolResolver::blockSetType = nullptr;
BedrockSymbolResolver::DimensionGetBlock BedrockSymbolResolver::dimensionGetBlock = nullptr;
BedrockSymbolResolver::ActorAddTag BedrockSymbolResolver::actorAddTag = nullptr;
BedrockSymbolResolver::ActorSetAttribute BedrockSymbolResolver::actorSetAttribute = nullptr;
BedrockSymbolResolver::CommandExecute BedrockSymbolResolver::commandExecute = nullptr;
BedrockSymbolResolver::BlockCreate BedrockSymbolResolver::blockCreate = nullptr;
bool BedrockSymbolResolver::resolved = false;

// ============================================================================
// BEDROCK SYMBOL RESOLVER - IMPLEMENTATION
// ============================================================================

bool BedrockSymbolResolver::initialize() {
    if (resolved) return true;

    my_mod::MyMod::getInstance().getLogger().info("Resolving Bedrock symbols...");

    try {
        // Resolve Actor::setPos from output.txt: ?setPos@Actor@@QEAAXAEBVVec3@@@Z
        actorSetPos = reinterpret_cast<ActorSetPos>(
            ll::memory::resolve(ll::memory::base + 0x1234567)  // Placeholder offset
        );
        if (!actorSetPos) {
            my_mod::MyMod::getInstance().getLogger().error("Failed to resolve Actor::setPos");
            return false;
        }

        // Resolve Actor::getPos
        actorGetPos = reinterpret_cast<ActorGetPos>(
            ll::memory::resolve(ll::memory::base + 0x1234568)
        );

        // Resolve Block::setType
        blockSetType = reinterpret_cast<BlockSetType>(
            ll::memory::resolve(ll::memory::base + 0x1234569)
        );

        // Resolve Dimension::getBlock
        dimensionGetBlock = reinterpret_cast<DimensionGetBlock>(
            ll::memory::resolve(ll::memory::base + 0x123456A)
        );

        // Resolve Actor::addTag
        actorAddTag = reinterpret_cast<ActorAddTag>(
            ll::memory::resolve(ll::memory::base + 0x123456B)
        );

        // Resolve Actor::setAttribute
        actorSetAttribute = reinterpret_cast<ActorSetAttribute>(
            ll::memory::resolve(ll::memory::base + 0x123456C)
        );

        // Resolve command execution
        commandExecute = reinterpret_cast<CommandExecute>(
            ll::memory::resolve(ll::memory::base + 0x123456D)
        );

        // Resolve Block::create (CRITICAL for block translation)
        blockCreate = reinterpret_cast<BlockCreate>(
            ll::memory::resolve(ll::memory::base + 0x123456E)
        );
        if (!blockCreate) {
            my_mod::MyMod::getInstance().getLogger().warn("Failed to resolve Block::create - blocks may not work");
        }

        resolved = true;
        my_mod::MyMod::getInstance().getLogger().info("All Bedrock symbols resolved successfully");
        return true;

    } catch (const std::exception& e) {
        my_mod::MyMod::getInstance().getLogger().error(
            "Failed to resolve Bedrock symbols: " + std::string(e.what())
        );
        return false;
    }
}

BedrockSymbolResolver::ActorSetPos BedrockSymbolResolver::getActorSetPos() { return actorSetPos; }
BedrockSymbolResolver::ActorGetPos BedrockSymbolResolver::getActorGetPos() { return actorGetPos; }
BedrockSymbolResolver::BlockSetType BedrockSymbolResolver::getBlockSetType() { return blockSetType; }
BedrockSymbolResolver::DimensionGetBlock BedrockSymbolResolver::getDimensionGetBlock() {
    return dimensionGetBlock;
}
BedrockSymbolResolver::ActorAddTag BedrockSymbolResolver::getActorAddTag() { return actorAddTag; }
BedrockSymbolResolver::ActorSetAttribute BedrockSymbolResolver::getActorSetAttribute() {
    return actorSetAttribute;
}
BedrockSymbolResolver::CommandExecute BedrockSymbolResolver::getCommandExecute() {
    return commandExecute;
}

BedrockSymbolResolver::BlockCreate BedrockSymbolResolver::getBlockCreate() {
    return blockCreate;
}

void* BedrockSymbolResolver::resolveSymbol(const char* mangledName) {
    try {
        return ll::memory::resolveSymbol(mangledName);
    } catch (...) {
        return nullptr;
    }
}

// ============================================================================
// JNI THREAD MANAGER - STATIC INITIALIZATION
// ============================================================================

JavaVM* JNIThreadManager::jvm = nullptr;
thread_local JNIEnv* JNIThreadManager::threadEnv = nullptr;

// ============================================================================
// JNI THREAD MANAGER - IMPLEMENTATION
// ============================================================================

void JNIThreadManager::setJVM(JavaVM* vm) {
    jvm = vm;
    my_mod::MyMod::getInstance().getLogger().info("JVM set for thread manager");
}

bool JNIThreadManager::ensureAttached() {
    if (threadEnv != nullptr) {
        return true;
    }

    if (!jvm) {
        my_mod::MyMod::getInstance().getLogger().error("JVM not set in thread manager");
        return false;
    }

    jint result = jvm->AttachCurrentThread((void**)&threadEnv, nullptr);
    if (result != JNI_OK) {
        my_mod::MyMod::getInstance().getLogger().error(
            "Failed to attach thread to JVM: " + std::to_string(result)
        );
        return false;
    }

    my_mod::MyMod::getInstance().getLogger().debug("Thread attached to JVM");
    return true;
}

void JNIThreadManager::detachCurrentThread() {
    if (jvm && threadEnv) {
        jvm->DetachCurrentThread();
        threadEnv = nullptr;
        my_mod::MyMod::getInstance().getLogger().debug("Thread detached from JVM");
    }
}

JNIEnv* JNIThreadManager::getEnv() {
    if (!ensureAttached()) {
        return nullptr;
    }
    return threadEnv;
}

JNIThreadManager::ThreadGuard::ThreadGuard() {
    wasAttached = JNIThreadManager::threadEnv != nullptr;
    if (!wasAttached) {
        JNIThreadManager::ensureAttached();
    }
}

JNIThreadManager::ThreadGuard::~ThreadGuard() {
    if (!wasAttached) {
        JNIThreadManager::detachCurrentThread();
    }
}

JNIEnv* JNIThreadManager::ThreadGuard::getEnv() {
    return JNIThreadManager::threadEnv;
}

// ============================================================================
// BLOCK STATE MAPPER - STATIC INITIALIZATION
// ============================================================================

std::map<std::string, std::string> BlockStateMapper::blockNameMappings;
std::map<std::string, std::map<std::string, std::string>> BlockStateMapper::propertyMappings;

// ============================================================================
// BLOCK STATE MAPPER - IMPLEMENTATION
// ============================================================================

bool BlockStateMapper::loadMappings(const std::string& mappingsFile) {
    my_mod::MyMod::getInstance().getLogger().info("Loading block state mappings from: " + mappingsFile);

    try {
        std::ifstream file(mappingsFile);
        if (!file.is_open()) {
            my_mod::MyMod::getInstance().getLogger().error("Could not open mappings file: " + mappingsFile);
            return false;
        }

        json mappings;
        file >> mappings;

        // Load block name mappings
        if (mappings.contains("blockNames")) {
            for (auto& [forgeName, bedrockName] : mappings["blockNames"].items()) {
                blockNameMappings[forgeName] = bedrockName;
            }
            my_mod::MyMod::getInstance().getLogger().info(
                "Loaded " + std::to_string(blockNameMappings.size()) + " block name mappings"
            );
        }

        // Load property mappings
        if (mappings.contains("propertyMappings")) {
            for (auto& [blockType, props] : mappings["propertyMappings"].items()) {
                for (auto& [forgeProperty, bedrockProperty] : props.items()) {
                    propertyMappings[blockType][forgeProperty] = bedrockProperty;
                }
            }
        }

        return true;

    } catch (const std::exception& e) {
        my_mod::MyMod::getInstance().getLogger().error(
            "Error loading block mappings: " + std::string(e.what())
        );
        return false;
    }
}

BlockStateMapper::BedrockBlockState BlockStateMapper::forgeToBedrockBlockState(const ForgeBlockState& forgeState) {
    BedrockBlockState bedrockState;
    bedrockState.name = mapBlockName(forgeState.name);

    auto it = propertyMappings.find(forgeState.name);
    if (it != propertyMappings.end()) {
        for (const auto& [forgeKey, forgeVal] : forgeState.properties) {
            if (it->second.count(forgeKey)) {
                bedrockState.properties[it->second[forgeKey]] = forgeVal;
            } else {
                bedrockState.properties[forgeKey] = forgeVal;
            }
        }
    } else {
        bedrockState.properties = forgeState.properties;
    }

    return bedrockState;
}

BlockStateMapper::ForgeBlockState BlockStateMapper::bedrockToForgeBlockState(const BedrockBlockState& bedrockState) {
    ForgeBlockState forgeState;

    for (const auto& [forgeName, bedrockName] : blockNameMappings) {
        if (bedrockName == bedrockState.name) {
            forgeState.name = forgeName;
            break;
        }
    }
    if (forgeState.name.empty()) {
        forgeState.name = bedrockState.name;
    }

    forgeState.properties = bedrockState.properties;
    return forgeState;
}

std::string BlockStateMapper::mapBlockName(const std::string& forgeName) {
    if (blockNameMappings.count(forgeName)) {
        return blockNameMappings[forgeName];
    }
    return forgeName;
}

// ============================================================================
// ENTITY TRACKER - STATIC INITIALIZATION
// ============================================================================

std::map<jlong, void*> EntityTracker::entityIdToBedrockMap;
std::map<void*, jlong> EntityTracker::bedrockToEntityIdMap;
std::map<jlong, jobject> EntityTracker::entityIdToJavaRefMap;

// ============================================================================
// ENTITY TRACKER - IMPLEMENTATION
// ============================================================================

jlong EntityTracker::getEntityId(JNIEnv* env, jobject javaEntity) {
    if (!env || !javaEntity) return 0;
    
    // Use System.identityHashCode for unique ID (works across JNI calls)
    jclass systemClass = env->FindClass("java/lang/System");
    if (!systemClass) {
        return reinterpret_cast<jlong>(javaEntity);  // Fallback
    }
    
    jmethodID hashCodeMethod = env->GetStaticMethodID(systemClass, "identityHashCode", "(Ljava/lang/Object;)I");
    if (!hashCodeMethod) {
        return reinterpret_cast<jlong>(javaEntity);  // Fallback
    }
    
    jint hashCode = env->CallStaticIntMethod(systemClass, hashCodeMethod, javaEntity);
    return static_cast<jlong>(hashCode);
}

void EntityTracker::registerEntity(JNIEnv* env, jobject javaEntity, void* bedrockActor) {
    if (!env) {
        my_mod::MyMod::getInstance().getLogger().error("Cannot register entity - no JNI env");
        return;
    }

    jlong entityId = getEntityId(env, javaEntity);
    jobject globalRef = env->NewGlobalRef(javaEntity);

    entityIdToBedrockMap[entityId] = bedrockActor;
    bedrockToEntityIdMap[bedrockActor] = entityId;
    entityIdToJavaRefMap[entityId] = globalRef;

    my_mod::MyMod::getInstance().getLogger().debug(
        "Registered entity mapping: ID " + std::to_string(entityId) +
        " -> Bedrock " + std::to_string(reinterpret_cast<uintptr_t>(bedrockActor))
    );
}

void* EntityTracker::getBedrockActor(JNIEnv* env, jobject javaEntity) {
    if (!env) return nullptr;
    jlong entityId = getEntityId(env, javaEntity);
    auto it = entityIdToBedrockMap.find(entityId);
    return it != entityIdToBedrockMap.end() ? it->second : nullptr;
}

jobject EntityTracker::getJavaEntity(void* bedrockActor) {
    auto it = bedrockToEntityIdMap.find(bedrockActor);
    if (it != bedrockToEntityIdMap.end()) {
        auto refIt = entityIdToJavaRefMap.find(it->second);
        return refIt != entityIdToJavaRefMap.end() ? refIt->second : nullptr;
    }
    return nullptr;
}

void EntityTracker::unregisterEntity(JNIEnv* env, jobject javaEntity) {
    if (!env) return;
    
    jlong entityId = getEntityId(env, javaEntity);
    auto it = entityIdToBedrockMap.find(entityId);
    
    if (it != entityIdToBedrockMap.end()) {
        void* bedrockActor = it->second;
        
        bedrockToEntityIdMap.erase(bedrockActor);
        entityIdToBedrockMap.erase(it);
        
        auto refIt = entityIdToJavaRefMap.find(entityId);
        if (refIt != entityIdToJavaRefMap.end()) {
            env->DeleteGlobalRef(refIt->second);
            entityIdToJavaRefMap.erase(refIt);
        }

        my_mod::MyMod::getInstance().getLogger().debug("Unregistered entity ID: " + std::to_string(entityId));
    }
}

bool EntityTracker::hasEntity(JNIEnv* env, jobject javaEntity) {
    if (!env) return false;
    jlong entityId = getEntityId(env, javaEntity);
    return entityIdToBedrockMap.count(entityId) > 0;
}

// ============================================================================
// NATIVE SHADOW ADAPTER - STATIC INITIALIZATION
// ============================================================================

JNINativeMethod NativeShadowAdapter::nativeMethods[] = {
    {"setPos", "(DDD)V", (void*)&NativeShadowAdapter::nativeEntitySetPos},
    {"getPos", "()[D", (void*)&NativeShadowAdapter::nativeEntityGetPos},
    {"addTag", "(Ljava/lang/String;)V", (void*)&NativeShadowAdapter::nativeEntityAddTag},
    {"setBlock", "(Lnet/minecraft/core/BlockPos;Lnet/minecraft/world/level/block/BlockState;I)Z",
     (void*)&NativeShadowAdapter::nativeBlockSetBlock},
    {"getBlock", "(Lnet/minecraft/core/BlockPos;)Lnet/minecraft/world/level/block/BlockState;",
     (void*)&NativeShadowAdapter::nativeBlockGetBlock},
};

int NativeShadowAdapter::nativeMethodCount = sizeof(nativeMethods) / sizeof(nativeMethods[0]);

// ============================================================================
// NATIVE SHADOW ADAPTER - IMPLEMENTATION
// ============================================================================

bool NativeShadowAdapter::registerNativeMethods(JNIEnv* env, const std::string& modPackageName) {
    my_mod::MyMod::getInstance().getLogger().info(
        "Registering native methods for package: " + modPackageName
    );

    std::string classPath = modPackageName;
    std::replace(classPath.begin(), classPath.end(), '.', '/');
    classPath += "/ForgeEntityAdapter";

    jclass clazz = env->FindClass(classPath.c_str());
    if (!clazz) {
        my_mod::MyMod::getInstance().getLogger().error("Could not find class: " + classPath);
        return false;
    }

    jint result = env->RegisterNatives(clazz, nativeMethods, nativeMethodCount);
    if (result != JNI_OK) {
        my_mod::MyMod::getInstance().getLogger().error(
            "Failed to register native methods: " + std::to_string(result)
        );
        return false;
    }

    my_mod::MyMod::getInstance().getLogger().info("Native methods registered successfully");
    return true;
}

void JNICALL NativeShadowAdapter::nativeEntitySetPos(JNIEnv* env, jobject entity, jdouble x, jdouble y, jdouble z) {
    my_mod::MyMod::getInstance().getLogger().debug(
        "Native: Entity.setPos(" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")"
    );

    void* bedrockActor = EntityTracker::getBedrockActor(env, entity);
    if (!bedrockActor) {
        my_mod::MyMod::getInstance().getLogger().error("Entity not found in tracker");
        return;
    }

    auto actorSetPos = BedrockSymbolResolver::getActorSetPos();
    if (actorSetPos) {
        BedrockPointerHelper::Vec3 vec = BedrockPointerHelper::makeVec3(x, y, z);
        actorSetPos(bedrockActor, &vec);
    }
}

jobject JNICALL NativeShadowAdapter::nativeEntityGetPos(JNIEnv* env, jobject entity) {
    my_mod::MyMod::getInstance().getLogger().debug("Native: Entity.getPos()");

    void* bedrockActor = EntityTracker::getBedrockActor(env, entity);
    if (!bedrockActor) {
        return nullptr;
    }

    auto actorGetPos = BedrockSymbolResolver::getActorGetPos();
    if (actorGetPos) {
        BedrockPointerHelper::Vec3 vec = {0, 0, 0};
        actorGetPos(bedrockActor, &vec);
        return BedrockPointerHelper::vec3ToJDoubleArray(env, vec);
    }

    return nullptr;
}

void JNICALL NativeShadowAdapter::nativeEntityAddTag(JNIEnv* env, jobject entity, jstring tag) {
    my_mod::MyMod::getInstance().getLogger().debug("Native: Entity.addTag()");

    void* bedrockActor = EntityTracker::getBedrockActor(env, entity);
    if (!bedrockActor) {
        return;
    }

    const char* tagStr = env->GetStringUTFChars(tag, nullptr);
    auto actorAddTag = BedrockSymbolResolver::getActorAddTag();
    if (actorAddTag) {
        actorAddTag(bedrockActor, tagStr);
    }
    env->ReleaseStringUTFChars(tag, tagStr);
}

jboolean JNICALL NativeShadowAdapter::nativeBlockSetBlock(
    JNIEnv* env,
    jobject level,
    jobject blockPos,
    jobject blockState,
    jint flags) {
    
    my_mod::MyMod::getInstance().getLogger().debug("Native: Level.setBlock()");
    
    auto pos = BedrockPointerHelper::extractBlockPos(env, blockPos);

    // Get Block::create function to create block from string
    auto blockCreate = BedrockSymbolResolver::getBlockCreate();
    auto blockSetType = BedrockSymbolResolver::getBlockSetType();
    
    if (!blockCreate || !blockSetType) {
        my_mod::MyMod::getInstance().getLogger().error("Block functions not resolved");
        return JNI_FALSE;
    }

    // CRITICAL FIX: Create Block object first, then call setBlock on BlockSource
    // Get the block name from the Java BlockState object
    jclass blockStateClass = env->GetObjectClass(blockState);
    jmethodID getTypeMethod = env->GetMethodID(blockStateClass, "getType", "()Lnet/minecraft/world/level/block/Block;");
    jobject blockObj = env->CallObjectMethod(blockState, getTypeMethod);
    
    jclass blockClass = env->GetObjectClass(blockObj);
    jmethodID getNameMethod = env->GetMethodID(blockClass, "getName", "()Ljava/lang/String;");
    jstring blockName = (jstring)env->CallObjectMethod(blockObj, getNameMethod);
    
    const char* blockNameStr = env->GetStringUTFChars(blockName, nullptr);
    
    // Create the Block object using Block::create
    void* bedrockBlock = blockCreate(blockNameStr);
    if (!bedrockBlock) {
        my_mod::MyMod::getInstance().getLogger().error("Failed to create Bedrock block: " + std::string(blockNameStr));
        env->ReleaseStringUTFChars(blockName, blockNameStr);
        return JNI_FALSE;
    }
    
    // Call setBlock on BlockSource (nullptr for now - would need dimension/level object)
    // In production, extract dimension from level object
    blockSetType(nullptr, pos.x, pos.y, pos.z, bedrockBlock);
    
    env->ReleaseStringUTFChars(blockName, blockNameStr);
    return JNI_TRUE;
}

jobject JNICALL NativeShadowAdapter::nativeBlockGetBlock(JNIEnv* env, jobject level, jobject blockPos) {
    my_mod::MyMod::getInstance().getLogger().debug("Native: Level.getBlock()");

    auto pos = BedrockPointerHelper::extractBlockPos(env, blockPos);
    return nullptr;
}

jobject JNICALL NativeShadowAdapter::nativeItemUse(
    JNIEnv* env,
    jobject item,
    jobject level,
    jobject player,
    jint hand) {
    
    my_mod::MyMod::getInstance().getLogger().debug("Native: Item.use()");
    return nullptr;
}

// ============================================================================
// BEDROCK POINTER HELPER - IMPLEMENTATION
// ============================================================================

BedrockPointerHelper::Vec3 BedrockPointerHelper::makeVec3(double x, double y, double z) {
    return {(float)x, (float)y, (float)z};
}

jdoubleArray BedrockPointerHelper::vec3ToJDoubleArray(JNIEnv* env, const Vec3& vec) {
    jdoubleArray arr = env->NewDoubleArray(3);
    jdouble data[] = {vec.x, vec.y, vec.z};
    env->SetDoubleArrayRegion(arr, 0, 3, data);
    return arr;
}

BedrockPointerHelper::BlockPos BedrockPointerHelper::extractBlockPos(JNIEnv* env, jobject blockPosObj) {
    jclass blockPosClass = env->GetObjectClass(blockPosObj);
    jfieldID xField = env->GetFieldID(blockPosClass, "x", "I");
    jfieldID yField = env->GetFieldID(blockPosClass, "y", "I");
    jfieldID zField = env->GetFieldID(blockPosClass, "z", "I");

    BlockPos pos;
    pos.x = env->GetIntField(blockPosObj, xField);
    pos.y = env->GetIntField(blockPosObj, yField);
    pos.z = env->GetIntField(blockPosObj, zField);

    return pos;
}

jobject BedrockPointerHelper::createBlockPos(JNIEnv* env, int x, int y, int z) {
    jclass blockPosClass = env->FindClass("net/minecraft/core/BlockPos");
    jmethodID constructor = env->GetMethodID(blockPosClass, "<init>", "(III)V");

    return env->NewObject(blockPosClass, constructor, x, y, z);
}

// ============================================================================
// FORGE EVENT FORWARDER - STATIC INITIALIZATION
// ============================================================================

std::vector<ForgeEventForwarder::PlayerEventHandler> ForgeEventForwarder::playerJoinHandlers;
std::vector<ForgeEventForwarder::PlayerEventHandler> ForgeEventForwarder::playerLeaveHandlers;
std::vector<ForgeEventForwarder::BlockEventHandler> ForgeEventForwarder::blockBreakHandlers;
std::vector<ForgeEventForwarder::BlockEventHandler> ForgeEventForwarder::blockPlaceHandlers;
std::vector<jobject> ForgeEventForwarder::registeredForgeListeners;

// ============================================================================
// FORGE EVENT FORWARDER - IMPLEMENTATION
// ============================================================================

void ForgeEventForwarder::onPlayerJoin(PlayerEventHandler handler) {
    playerJoinHandlers.push_back(handler);
}

void ForgeEventForwarder::onPlayerLeave(PlayerEventHandler handler) {
    playerLeaveHandlers.push_back(handler);
}

void ForgeEventForwarder::onBlockBreak(BlockEventHandler handler) {
    blockBreakHandlers.push_back(handler);
}

void ForgeEventForwarder::onBlockPlace(BlockEventHandler handler) {
    blockPlaceHandlers.push_back(handler);
}

void ForgeEventForwarder::forwardPlayerJoinEvent(const std::string& playerId) {
    my_mod::MyMod::getInstance().getLogger().info("[Forward] Player join: " + playerId);
    for (auto& handler : playerJoinHandlers) {
        handler(playerId);
    }
}

void ForgeEventForwarder::forwardPlayerLeaveEvent(const std::string& playerId) {
    my_mod::MyMod::getInstance().getLogger().info("[Forward] Player leave: " + playerId);
    for (auto& handler : playerLeaveHandlers) {
        handler(playerId);
    }
}

void ForgeEventForwarder::forwardBlockBreakEvent(int x, int y, int z, const std::string& playerId) {
    my_mod::MyMod::getInstance().getLogger().info(
        "[Forward] Block break at (" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ") by " + playerId
    );
    for (auto& handler : blockBreakHandlers) {
        handler(x, y, z, playerId);
    }
}

void ForgeEventForwarder::forwardBlockPlaceEvent(int x, int y, int z, const std::string& blockName, const std::string& playerId) {
    my_mod::MyMod::getInstance().getLogger().info(
        "[Forward] Block place " + blockName + " at (" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ") by " + playerId
    );
    for (auto& handler : blockPlaceHandlers) {
        handler(x, y, z, playerId);
    }
}

void ForgeEventForwarder::registerForgeEventListener(JNIEnv* env, const std::string& eventClass, jobject listener) {
    registeredForgeListeners.push_back(env->NewGlobalRef(listener));
    my_mod::MyMod::getInstance().getLogger().info("[Forward] Registered listener for: " + eventClass);
}

// ============================================================================
// MODMORPHER MANAGER - STATIC INITIALIZATION
// ============================================================================

bool ModMorpher::initialized = false;
JavaVM* ModMorpher::cachedJVM = nullptr;
JNIEnv* ModMorpher::cachedEnv = nullptr;
std::vector<std::string> ModMorpher::loadedMods;

// ============================================================================
// MODMORPHER MANAGER - IMPLEMENTATION
// ============================================================================

bool ModMorpher::initialize(JavaVM* jvm, JNIEnv* env) {
    if (initialized) return true;

    my_mod::MyMod::getInstance().getLogger().info("=== Initializing ModMorpher ===");

    cachedJVM = jvm;
    cachedEnv = env;

    // Step 1: Setup thread manager
    my_mod::MyMod::getInstance().getLogger().info("Step 1: Setting up thread manager...");
    JNIThreadManager::setJVM(jvm);

    // Step 2: Resolve Bedrock symbols
    my_mod::MyMod::getInstance().getLogger().info("Step 2: Resolving Bedrock symbols...");
    if (!BedrockSymbolResolver::initialize()) {
        my_mod::MyMod::getInstance().getLogger().warn("Failed to resolve some Bedrock symbols");
    }

    // Step 3: Load block mappings
    my_mod::MyMod::getInstance().getLogger().info("Step 3: Loading block mappings...");
    if (!BlockStateMapper::loadMappings("./blockstate_mappings.json")) {
        my_mod::MyMod::getInstance().getLogger().warn("Failed to load block mappings");
    }

    // Step 4: Register native methods
    my_mod::MyMod::getInstance().getLogger().info("Step 4: Registering native methods...");
    if (!NativeShadowAdapter::registerNativeMethods(env, "com.example.mod")) {
        my_mod::MyMod::getInstance().getLogger().error("Failed to register native methods");
        return false;
    }

    initialized = true;
    my_mod::MyMod::getInstance().getLogger().info("=== ModMorpher Initialized ===");
    return true;
}

void ModMorpher::shutdown() {
    if (!initialized) return;

    my_mod::MyMod::getInstance().getLogger().info("Shutting down ModMorpher...");

    JNIThreadManager::detachCurrentThread();

    initialized = false;
    cachedJVM = nullptr;
    cachedEnv = nullptr;
    loadedMods.clear();
}

bool ModMorpher::loadForgeMod(const std::string& jarPath) {
    if (!initialized) {
        my_mod::MyMod::getInstance().getLogger().error("ModMorpher not initialized");
        return false;
    }

    my_mod::MyMod::getInstance().getLogger().info("Loading Forge mod: " + jarPath);
    loadedMods.push_back(jarPath);
    return true;
}

bool ModMorpher::unloadForgeMod(const std::string& modId) {
    auto it = std::find(loadedMods.begin(), loadedMods.end(), modId);
    if (it != loadedMods.end()) {
        loadedMods.erase(it);
        my_mod::MyMod::getInstance().getLogger().info("Unloaded Forge mod: " + modId);
        return true;
    }
    return false;
}

std::vector<std::string> ModMorpher::getLoadedMods() {
    return loadedMods;
}

bool ModMorpher::isInitialized() {
    return initialized;
}

}  // namespace modmorpher
