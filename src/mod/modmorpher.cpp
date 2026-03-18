#include "modmorpher.h"
#include "mod/MyMod.h"
#include <algorithm>

namespace modmorpher {

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

    my_mod::MyMod::getInstance().getSelf().getLogger().info("ModMorpher: Initializing symbol resolver");
    resolved = true;
    return true;
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
    (void)mangledName;
    return nullptr;
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
    my_mod::MyMod::getInstance().getSelf().getLogger().info("JVM set for thread manager");
}

bool JNIThreadManager::ensureAttached() {
    if (threadEnv != nullptr) {
        return true;
    }

    if (!jvm) {
        my_mod::MyMod::getInstance().getSelf().getLogger().error("JVM not set");
        return false;
    }

    jint result = jvm->AttachCurrentThread((void**)&threadEnv, nullptr);
    if (result != JNI_OK) {
        my_mod::MyMod::getInstance().getSelf().getLogger().error("Failed to attach thread");
        return false;
    }

    my_mod::MyMod::getInstance().getSelf().getLogger().debug("Thread attached to JVM");
    return true;
}

void JNIThreadManager::detachCurrentThread() {
    if (jvm && threadEnv) {
        jvm->DetachCurrentThread();
        threadEnv = nullptr;
        my_mod::MyMod::getInstance().getSelf().getLogger().debug("Thread detached from JVM");
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
    my_mod::MyMod::getInstance().getSelf().getLogger().info("Loading mappings: " + mappingsFile);
    return true;
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
        return;
    }

    jlong entityId = getEntityId(env, javaEntity);
    jobject globalRef = env->NewGlobalRef(javaEntity);

    entityIdToBedrockMap[entityId] = bedrockActor;
    bedrockToEntityIdMap[bedrockActor] = entityId;
    entityIdToJavaRefMap[entityId] = globalRef;
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
    {(char*)"setPos", (char*)"(DDD)V", (void*)&NativeShadowAdapter::nativeEntitySetPos},
    {(char*)"getPos", (char*)"()[D", (void*)&NativeShadowAdapter::nativeEntityGetPos},
    {(char*)"addTag", (char*)"(Ljava/lang/String;)V", (void*)&NativeShadowAdapter::nativeEntityAddTag},
    {(char*)"setBlock", (char*)"(Lnet/minecraft/core/BlockPos;Lnet/minecraft/world/level/block/BlockState;I)Z",
     (void*)&NativeShadowAdapter::nativeBlockSetBlock},
    {(char*)"getBlock", (char*)"(Lnet/minecraft/core/BlockPos;)Lnet/minecraft/world/level/block/BlockState;",
     (void*)&NativeShadowAdapter::nativeBlockGetBlock},
};

int NativeShadowAdapter::nativeMethodCount = sizeof(nativeMethods) / sizeof(nativeMethods[0]);

// ============================================================================
// NATIVE SHADOW ADAPTER - IMPLEMENTATION
// ============================================================================

bool NativeShadowAdapter::registerNativeMethods(JNIEnv* env, const std::string& modPackageName) {
    std::string classPath = modPackageName;
    std::replace(classPath.begin(), classPath.end(), '.', '/');
    classPath += "/ForgeEntityAdapter";

    jclass clazz = env->FindClass(classPath.c_str());
    if (!clazz) {
        return false;
    }

    jint result = env->RegisterNatives(clazz, nativeMethods, nativeMethodCount);
    if (result != JNI_OK) {
        return false;
    }

    return true;
}

void JNICALL NativeShadowAdapter::nativeEntitySetPos(JNIEnv* env, jobject entity, jdouble x, jdouble y, jdouble z) {
    void* bedrockActor = EntityTracker::getBedrockActor(env, entity);
    (void)x; (void)y; (void)z;
    if (!bedrockActor) {
        return;
    }

    auto actorSetPos = BedrockSymbolResolver::getActorSetPos();
    if (actorSetPos) {
        BedrockPointerHelper::Vec3 vec = BedrockPointerHelper::makeVec3(x, y, z);
        actorSetPos(bedrockActor, &vec);
    }
}

jobject JNICALL NativeShadowAdapter::nativeEntityGetPos(JNIEnv* env, jobject entity) {
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
    
    auto pos = BedrockPointerHelper::extractBlockPos(env, blockPos);

    // Get Block::create function to create block from string
    auto blockCreate = BedrockSymbolResolver::getBlockCreate();
    auto blockSetType = BedrockSymbolResolver::getBlockSetType();
    
    if (!blockCreate || !blockSetType) {
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
    auto pos = BedrockPointerHelper::extractBlockPos(env, blockPos);
    return nullptr;
}

jobject JNICALL NativeShadowAdapter::nativeItemUse(
    JNIEnv* env,
    jobject item,
    jobject level,
    jobject player,
    jint hand) {
    
    (void)env; (void)item; (void)level; (void)player; (void)hand;
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
    for (auto& handler : playerJoinHandlers) {
        handler(playerId);
    }
}

void ForgeEventForwarder::forwardPlayerLeaveEvent(const std::string& playerId) {
    for (auto& handler : playerLeaveHandlers) {
        handler(playerId);
    }
}

void ForgeEventForwarder::forwardBlockBreakEvent(int x, int y, int z, const std::string& playerId) {
    for (auto& handler : blockBreakHandlers) {
        handler(x, y, z, playerId);
    }
}

void ForgeEventForwarder::forwardBlockPlaceEvent(int x, int y, int z, const std::string& blockName, const std::string& playerId) {
    for (auto& handler : blockPlaceHandlers) {
        handler(x, y, z, playerId);
    }
}

void ForgeEventForwarder::registerForgeEventListener(JNIEnv* env, const std::string& eventClass, jobject listener) {
    registeredForgeListeners.push_back(env->NewGlobalRef(listener));
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

    cachedJVM = jvm;
    cachedEnv = env;

    JNIThreadManager::setJVM(jvm);
    BedrockSymbolResolver::initialize();
    BlockStateMapper::loadMappings("./blockstate_mappings.json");
    NativeShadowAdapter::registerNativeMethods(env, "com.example.mod");

    initialized = true;
    return true;
}

void ModMorpher::shutdown() {
    if (!initialized) return;

    JNIThreadManager::detachCurrentThread();

    initialized = false;
    cachedJVM = nullptr;
    cachedEnv = nullptr;
    loadedMods.clear();
}

bool ModMorpher::loadForgeMod(const std::string& jarPath) {
    if (!initialized) {
        return false;
    }

    loadedMods.push_back(jarPath);
    return true;
}

bool ModMorpher::unloadForgeMod(const std::string& modId) {
    auto it = std::find(loadedMods.begin(), loadedMods.end(), modId);
    if (it != loadedMods.end()) {
        loadedMods.erase(it);
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
