#include "modmorpher.h"
#include "mod/MyMod.h"
#include <algorithm>
#include <chrono>
#include <sstream>
#include <fstream>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>

// ============================================================================
// LEVILAMINA INCLUDES
// ============================================================================
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/player/PlayerDestroyBlockEvent.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "ll/api/event/player/PlayerAttackEvent.h"
#include "ll/api/event/player/PlayerChatEvent.h"
#include "ll/api/event/player/PlayerDieEvent.h"
#include "ll/api/event/player/PlayerRespawnEvent.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerPickUpItemEvent.h"
#include "ll/api/event/entity/MobDieEvent.h"
#include "ll/api/event/entity/ActorHurtEvent.h"
#include "ll/api/event/world/SpawnMobEvent.h"
#include "ll/api/event/world/LevelTickEvent.h"
#include "ll/api/event/server/ServerStartedEvent.h"
#include "ll/api/event/server/ServerStoppingEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/memory/Memory.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/math/vector/Vecs.h"
#include "mc/nbt/CompoundTag.h"

namespace modmorpher {

// ============================================================================
// ASYNC JNI CALL QUEUE
// ============================================================================

struct JNICall {
    std::function<void(JNIEnv*)> fn;
    std::string description;
};

static std::queue<JNICall>  gJNIQueue;
static std::mutex           gJNIQueueMutex;
static std::atomic<bool>    gJNIWorkerRunning{false};
static std::thread          gJNIWorkerThread;

static void jniWorkerLoop() {
    auto& logger = my_mod::MyMod::getInstance().getSelf().getLogger();
    logger.info("JNI worker thread started");
    while (gJNIWorkerRunning) {
        std::unique_lock<std::mutex> lock(gJNIQueueMutex);
        if (gJNIQueue.empty()) {
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        JNICall call = std::move(gJNIQueue.front());
        gJNIQueue.pop();
        lock.unlock();
        JNIThreadManager::ThreadGuard guard;
        JNIEnv* env = guard.getEnv();
        if (env) call.fn(env);
        else logger.error("JNI worker: no env for call: " + call.description);
    }
    logger.info("JNI worker thread stopped");
}

static void enqueueJNICall(std::string desc, std::function<void(JNIEnv*)> fn) {
    std::lock_guard<std::mutex> lock(gJNIQueueMutex);
    gJNIQueue.push({std::move(fn), std::move(desc)});
}

// ============================================================================
// BEDROCK SYMBOL RESOLVER
// ============================================================================

BedrockSymbolResolver::ActorSetPos       BedrockSymbolResolver::actorSetPos       = nullptr;
BedrockSymbolResolver::ActorGetPos       BedrockSymbolResolver::actorGetPos       = nullptr;
BedrockSymbolResolver::BlockSetType      BedrockSymbolResolver::blockSetType      = nullptr;
BedrockSymbolResolver::DimensionGetBlock BedrockSymbolResolver::dimensionGetBlock = nullptr;
BedrockSymbolResolver::ActorAddTag       BedrockSymbolResolver::actorAddTag       = nullptr;
BedrockSymbolResolver::ActorSetAttribute BedrockSymbolResolver::actorSetAttribute = nullptr;
BedrockSymbolResolver::CommandExecute    BedrockSymbolResolver::commandExecute    = nullptr;
BedrockSymbolResolver::BlockCreate       BedrockSymbolResolver::blockCreate       = nullptr;
bool                                     BedrockSymbolResolver::resolved          = false;

bool BedrockSymbolResolver::initialize() {
    if (resolved) return true;
    auto& logger = my_mod::MyMod::getInstance().getSelf().getLogger();
    logger.info("ModMorpher: Resolving Bedrock symbols...");

    struct SymbolDef { const char* name; void** target; };
    SymbolDef symbols[] = {
        {"?setPos@Actor@@UEAAXAEBVVec3@@@Z",
            reinterpret_cast<void**>(&actorSetPos)},
        {"?getPosition@Actor@@UEBAAEBVVec3@@@Z",
            reinterpret_cast<void**>(&actorGetPos)},
        {"?setBlock@BlockSource@@QEAA_NAEBVBlockPos@@AEBVBlock@@HPEAUActorBlockSyncMessage@@@Z",
            reinterpret_cast<void**>(&blockSetType)},
        {"?addTag@Actor@@QEAAXAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z",
            reinterpret_cast<void**>(&actorAddTag)},
        {"?setAttribute@Actor@@QEAAXAEBVAttribute@@M@Z",
            reinterpret_cast<void**>(&actorSetAttribute)},
    };

    int ok = 0;
    for (auto& s : symbols) {
        *s.target = ll::memory::getSymbol<void*>(s.name);
        if (*s.target) { ok++; logger.info("  [OK] " + std::string(s.name)); }
        else             logger.warn("  [!!] " + std::string(s.name));
    }
    logger.info("ModMorpher: " + std::to_string(ok) + "/" +
                std::to_string(sizeof(symbols)/sizeof(symbols[0])) + " symbols resolved");
    resolved = true;
    return true;
}

void* BedrockSymbolResolver::resolveSymbol(const char* n) {
    return ll::memory::getSymbol<void*>(n);
}

BedrockSymbolResolver::ActorSetPos       BedrockSymbolResolver::getActorSetPos()       { return actorSetPos; }
BedrockSymbolResolver::ActorGetPos       BedrockSymbolResolver::getActorGetPos()       { return actorGetPos; }
BedrockSymbolResolver::BlockSetType      BedrockSymbolResolver::getBlockSetType()      { return blockSetType; }
BedrockSymbolResolver::DimensionGetBlock BedrockSymbolResolver::getDimensionGetBlock() { return dimensionGetBlock; }
BedrockSymbolResolver::ActorAddTag       BedrockSymbolResolver::getActorAddTag()       { return actorAddTag; }
BedrockSymbolResolver::ActorSetAttribute BedrockSymbolResolver::getActorSetAttribute() { return actorSetAttribute; }
BedrockSymbolResolver::CommandExecute    BedrockSymbolResolver::getCommandExecute()    { return commandExecute; }
BedrockSymbolResolver::BlockCreate       BedrockSymbolResolver::getBlockCreate()       { return blockCreate; }

// ============================================================================
// JNI THREAD MANAGER
// ============================================================================

JavaVM*              JNIThreadManager::jvm       = nullptr;
thread_local JNIEnv* JNIThreadManager::threadEnv = nullptr;

void JNIThreadManager::setJVM(JavaVM* vm) { jvm = vm; }

bool JNIThreadManager::ensureAttached() {
    if (threadEnv) return true;
    if (!jvm) return false;
    jint r = jvm->AttachCurrentThread((void**)&threadEnv, nullptr);
    if (r != JNI_OK) { threadEnv = nullptr; return false; }
    return true;
}

void JNIThreadManager::detachCurrentThread() {
    if (jvm && threadEnv) { jvm->DetachCurrentThread(); threadEnv = nullptr; }
}

JNIEnv* JNIThreadManager::getEnv() { return ensureAttached() ? threadEnv : nullptr; }

JNIThreadManager::ThreadGuard::ThreadGuard() {
    wasAttached = (threadEnv != nullptr);
    if (!wasAttached) ensureAttached();
}
JNIThreadManager::ThreadGuard::~ThreadGuard() {
    if (!wasAttached) detachCurrentThread();
}
JNIEnv* JNIThreadManager::ThreadGuard::getEnv() { return threadEnv; }

// ============================================================================
// JAVA CLASS CACHE
// ============================================================================

namespace JavaClassCache {
    struct CachedClass {
        jclass ref       = nullptr;
        bool   attempted = false;
    };
    static std::map<std::string, CachedClass> classes;
    static std::map<std::string, jmethodID>   methods;
    static std::mutex                          cacheMutex;

    jclass getClass(JNIEnv* env, const std::string& name) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto& cached = classes[name];
        if (cached.attempted) return cached.ref;
        cached.attempted = true;
        jclass local = env->FindClass(name.c_str());
        if (!local) { env->ExceptionClear(); return nullptr; }
        cached.ref = (jclass)env->NewGlobalRef(local);
        env->DeleteLocalRef(local);
        return cached.ref;
    }

    jmethodID getStaticMethod(JNIEnv* env, const std::string& cls,
                              const std::string& method, const std::string& sig) {
        std::string key = cls + "::" + method + sig;
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = methods.find(key);
        if (it != methods.end()) return it->second;
        jclass c = env->FindClass(cls.c_str());
        if (!c) { env->ExceptionClear(); return nullptr; }
        jmethodID m = env->GetStaticMethodID(c, method.c_str(), sig.c_str());
        if (!m) { env->ExceptionClear(); env->DeleteLocalRef(c); return nullptr; }
        methods[key] = m;
        env->DeleteLocalRef(c);
        return m;
    }

    void clear(JNIEnv* env) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        for (auto& [k, v] : classes)
            if (v.ref) { env->DeleteGlobalRef(v.ref); v.ref = nullptr; }
        classes.clear();
        methods.clear();
    }
}

static void fireJavaBridge(std::string eventName, std::function<void(JNIEnv*, jclass)> call) {
    enqueueJNICall(eventName, [call = std::move(call)](JNIEnv* env) {
        jclass cls = JavaClassCache::getClass(env, "com/example/mod/ForgeEventBridge");
        if (!cls) return;
        call(env, cls);
    });
}

// ============================================================================
// BLOCK STATE MAPPER
// ============================================================================

std::map<std::string, std::string>                       BlockStateMapper::blockNameMappings;
std::map<std::string, std::map<std::string,std::string>> BlockStateMapper::propertyMappings;

bool BlockStateMapper::loadMappings(const std::string&) {
    blockNameMappings = {
        {"minecraft:stone","minecraft:stone"},{"minecraft:granite","minecraft:stone"},
        {"minecraft:diorite","minecraft:stone"},{"minecraft:andesite","minecraft:stone"},
        {"minecraft:cobblestone","minecraft:cobblestone"},
        {"minecraft:mossy_cobblestone","minecraft:mossy_cobblestone"},
        {"minecraft:stone_bricks","minecraft:stonebrick"},
        {"minecraft:mossy_stone_bricks","minecraft:stonebrick"},
        {"minecraft:cracked_stone_bricks","minecraft:stonebrick"},
        {"minecraft:chiseled_stone_bricks","minecraft:stonebrick"},
        {"minecraft:dirt","minecraft:dirt"},{"minecraft:coarse_dirt","minecraft:dirt"},
        {"minecraft:grass_block","minecraft:grass"},{"minecraft:podzol","minecraft:podzol"},
        {"minecraft:mycelium","minecraft:mycelium"},{"minecraft:farmland","minecraft:farmland"},
        {"minecraft:dirt_path","minecraft:grass_path"},
        {"minecraft:oak_log","minecraft:log"},{"minecraft:spruce_log","minecraft:log"},
        {"minecraft:birch_log","minecraft:log"},{"minecraft:jungle_log","minecraft:log"},
        {"minecraft:acacia_log","minecraft:log2"},{"minecraft:dark_oak_log","minecraft:log2"},
        {"minecraft:mangrove_log","minecraft:mangrove_log"},
        {"minecraft:cherry_log","minecraft:cherry_log"},
        {"minecraft:bamboo_block","minecraft:bamboo_block"},
        {"minecraft:oak_planks","minecraft:planks"},
        {"minecraft:spruce_planks","minecraft:planks"},
        {"minecraft:birch_planks","minecraft:planks"},
        {"minecraft:jungle_planks","minecraft:planks"},
        {"minecraft:acacia_planks","minecraft:planks"},
        {"minecraft:dark_oak_planks","minecraft:planks"},
        {"minecraft:mangrove_planks","minecraft:mangrove_planks"},
        {"minecraft:cherry_planks","minecraft:cherry_planks"},
        {"minecraft:bamboo_planks","minecraft:bamboo_planks"},
        {"minecraft:oak_leaves","minecraft:leaves"},
        {"minecraft:spruce_leaves","minecraft:leaves"},
        {"minecraft:birch_leaves","minecraft:leaves"},
        {"minecraft:jungle_leaves","minecraft:leaves"},
        {"minecraft:acacia_leaves","minecraft:leaves2"},
        {"minecraft:dark_oak_leaves","minecraft:leaves2"},
        {"minecraft:mangrove_leaves","minecraft:mangrove_leaves"},
        {"minecraft:cherry_leaves","minecraft:cherry_leaves"},
        {"minecraft:coal_ore","minecraft:coal_ore"},
        {"minecraft:iron_ore","minecraft:iron_ore"},
        {"minecraft:gold_ore","minecraft:gold_ore"},
        {"minecraft:diamond_ore","minecraft:diamond_ore"},
        {"minecraft:emerald_ore","minecraft:emerald_ore"},
        {"minecraft:lapis_ore","minecraft:lapis_ore"},
        {"minecraft:redstone_ore","minecraft:redstone_ore"},
        {"minecraft:copper_ore","minecraft:copper_ore"},
        {"minecraft:deepslate_coal_ore","minecraft:deepslate_coal_ore"},
        {"minecraft:deepslate_iron_ore","minecraft:deepslate_iron_ore"},
        {"minecraft:deepslate_gold_ore","minecraft:deepslate_gold_ore"},
        {"minecraft:deepslate_diamond_ore","minecraft:deepslate_diamond_ore"},
        {"minecraft:deepslate_emerald_ore","minecraft:deepslate_emerald_ore"},
        {"minecraft:deepslate_lapis_ore","minecraft:deepslate_lapis_ore"},
        {"minecraft:deepslate_redstone_ore","minecraft:deepslate_redstone_ore"},
        {"minecraft:deepslate_copper_ore","minecraft:deepslate_copper_ore"},
        {"minecraft:nether_gold_ore","minecraft:nether_gold_ore"},
        {"minecraft:nether_quartz_ore","minecraft:quartz_ore"},
        {"minecraft:ancient_debris","minecraft:ancient_debris"},
        {"minecraft:crafting_table","minecraft:crafting_table"},
        {"minecraft:furnace","minecraft:furnace"},
        {"minecraft:blast_furnace","minecraft:blast_furnace"},
        {"minecraft:smoker","minecraft:smoker"},
        {"minecraft:chest","minecraft:chest"},
        {"minecraft:trapped_chest","minecraft:trapped_chest"},
        {"minecraft:ender_chest","minecraft:ender_chest"},
        {"minecraft:barrel","minecraft:barrel"},
        {"minecraft:shulker_box","minecraft:shulker_box"},
        {"minecraft:anvil","minecraft:anvil"},
        {"minecraft:enchanting_table","minecraft:enchanting_table"},
        {"minecraft:bookshelf","minecraft:bookshelf"},
        {"minecraft:jukebox","minecraft:jukebox"},
        {"minecraft:beacon","minecraft:beacon"},
        {"minecraft:brewing_stand","minecraft:brewing_stand"},
        {"minecraft:cauldron","minecraft:cauldron"},
        {"minecraft:hopper","minecraft:hopper"},
        {"minecraft:dropper","minecraft:dropper"},
        {"minecraft:dispenser","minecraft:dispenser"},
        {"minecraft:observer","minecraft:observer"},
        {"minecraft:piston","minecraft:piston"},
        {"minecraft:sticky_piston","minecraft:sticky_piston"},
        {"minecraft:grindstone","minecraft:grindstone"},
        {"minecraft:cartography_table","minecraft:cartography_table"},
        {"minecraft:fletching_table","minecraft:fletching_table"},
        {"minecraft:smithing_table","minecraft:smithing_table"},
        {"minecraft:loom","minecraft:loom"},
        {"minecraft:stonecutter","minecraft:stonecutter"},
        {"minecraft:composter","minecraft:composter"},
        {"minecraft:bell","minecraft:bell"},
        {"minecraft:lectern","minecraft:lectern"},
        {"minecraft:beehive","minecraft:beehive"},
        {"minecraft:bee_nest","minecraft:bee_nest"},
        {"minecraft:redstone_wire","minecraft:redstone_wire"},
        {"minecraft:redstone_torch","minecraft:redstone_torch"},
        {"minecraft:lever","minecraft:lever"},
        {"minecraft:stone_button","minecraft:stone_button"},
        {"minecraft:oak_button","minecraft:wooden_button"},
        {"minecraft:stone_pressure_plate","minecraft:stone_pressure_plate"},
        {"minecraft:oak_pressure_plate","minecraft:wooden_pressure_plate"},
        {"minecraft:repeater","minecraft:repeater"},
        {"minecraft:comparator","minecraft:comparator"},
        {"minecraft:daylight_detector","minecraft:daylight_detector"},
        {"minecraft:tripwire_hook","minecraft:tripwire_hook"},
        {"minecraft:target","minecraft:target"},
        {"minecraft:lightning_rod","minecraft:lightning_rod"},
        {"minecraft:glass","minecraft:glass"},
        {"minecraft:glass_pane","minecraft:glass_pane"},
        {"minecraft:tinted_glass","minecraft:tinted_glass"},
        {"minecraft:sand","minecraft:sand"},
        {"minecraft:red_sand","minecraft:sand"},
        {"minecraft:gravel","minecraft:gravel"},
        {"minecraft:white_concrete","minecraft:concrete"},
        {"minecraft:white_concrete_powder","minecraft:concrete_powder"},
        {"minecraft:ice","minecraft:ice"},
        {"minecraft:packed_ice","minecraft:packed_ice"},
        {"minecraft:blue_ice","minecraft:blue_ice"},
        {"minecraft:frosted_ice","minecraft:frosted_ice"},
        {"minecraft:snow_block","minecraft:snow"},
        {"minecraft:clay","minecraft:clay"},
        {"minecraft:netherrack","minecraft:netherrack"},
        {"minecraft:soul_sand","minecraft:soul_sand"},
        {"minecraft:soul_soil","minecraft:soul_soil"},
        {"minecraft:glowstone","minecraft:glowstone"},
        {"minecraft:obsidian","minecraft:obsidian"},
        {"minecraft:crying_obsidian","minecraft:crying_obsidian"},
        {"minecraft:bedrock","minecraft:bedrock"},
        {"minecraft:end_stone","minecraft:end_stone"},
        {"minecraft:end_stone_bricks","minecraft:end_brick"},
        {"minecraft:purpur_block","minecraft:purpur_block"},
        {"minecraft:nether_bricks","minecraft:nether_brick"},
        {"minecraft:red_nether_bricks","minecraft:red_nether_brick"},
        {"minecraft:quartz_block","minecraft:quartz_block"},
        {"minecraft:prismarine","minecraft:prismarine"},
        {"minecraft:sea_lantern","minecraft:sea_lantern"},
        {"minecraft:magma_block","minecraft:magma"},
        {"minecraft:shroomlight","minecraft:shroomlight"},
        {"minecraft:blackstone","minecraft:blackstone"},
        {"minecraft:basalt","minecraft:basalt"},
        {"minecraft:polished_basalt","minecraft:polished_basalt"},
        {"minecraft:calcite","minecraft:calcite"},
        {"minecraft:tuff","minecraft:tuff"},
        {"minecraft:dripstone_block","minecraft:dripstone_block"},
        {"minecraft:moss_block","minecraft:moss_block"},
        {"minecraft:rooted_dirt","minecraft:dirt_with_roots"},
        {"minecraft:deepslate","minecraft:deepslate"},
        {"minecraft:cobbled_deepslate","minecraft:cobbled_deepslate"},
        {"minecraft:sculk","minecraft:sculk"},
        {"minecraft:sculk_catalyst","minecraft:sculk_catalyst"},
        {"minecraft:sculk_shrieker","minecraft:sculk_shrieker"},
        {"minecraft:sculk_sensor","minecraft:sculk_sensor"},
        {"minecraft:amethyst_block","minecraft:amethyst_block"},
        {"minecraft:budding_amethyst","minecraft:budding_amethyst"},
        {"minecraft:grass","minecraft:tallgrass"},
        {"minecraft:fern","minecraft:tallgrass"},
        {"minecraft:dead_bush","minecraft:deadbush"},
        {"minecraft:dandelion","minecraft:yellow_flower"},
        {"minecraft:poppy","minecraft:red_flower"},
        {"minecraft:cactus","minecraft:cactus"},
        {"minecraft:sugar_cane","minecraft:reeds"},
        {"minecraft:bamboo","minecraft:bamboo"},
        {"minecraft:vine","minecraft:vine"},
        {"minecraft:lily_pad","minecraft:waterlily"},
        {"minecraft:kelp","minecraft:kelp"},
        {"minecraft:sea_pickle","minecraft:sea_pickle"},
        {"minecraft:wheat","minecraft:wheat"},
        {"minecraft:potatoes","minecraft:potatoes"},
        {"minecraft:carrots","minecraft:carrots"},
        {"minecraft:beetroots","minecraft:beetroot"},
        {"minecraft:melon","minecraft:melon_block"},
        {"minecraft:pumpkin","minecraft:pumpkin"},
        {"minecraft:water","minecraft:water"},
        {"minecraft:lava","minecraft:lava"},
        {"minecraft:air","minecraft:air"},
        {"minecraft:cave_air","minecraft:air"},
        {"minecraft:void_air","minecraft:air"},
    };

    propertyMappings["minecraft:oak_log"]    = {{"axis","pillar_axis"}};
    propertyMappings["minecraft:stone"]      = {{"stone_type","stone_type"}};
    propertyMappings["minecraft:dirt"]       = {{"dirt_type","dirt_type"}};
    propertyMappings["minecraft:piston"]     = {{"facing","facing_direction"},{"extended","extended"}};
    propertyMappings["minecraft:repeater"]   = {{"delay","repeater_delay"},{"facing","direction"},{"powered","powered"}};
    propertyMappings["minecraft:comparator"] = {{"facing","direction"},{"mode","output_subtract_bit"},{"powered","output_lit_bit"}};

    auto& logger = my_mod::MyMod::getInstance().getSelf().getLogger();
    logger.info("BlockStateMapper: " + std::to_string(blockNameMappings.size()) + " blocks, " +
                std::to_string(propertyMappings.size()) + " property remaps loaded");
    return true;
}

std::string BlockStateMapper::mapBlockName(const std::string& forgeName) {
    auto it = blockNameMappings.find(forgeName);
    if (it != blockNameMappings.end()) return it->second;
    return forgeName;
}

BlockStateMapper::BedrockBlockState BlockStateMapper::forgeToBedrockBlockState(const ForgeBlockState& fs) {
    BedrockBlockState out;
    out.name = mapBlockName(fs.name);
    auto remap = propertyMappings.find(fs.name);
    for (const auto& [k, v] : fs.properties) {
        if (remap != propertyMappings.end() && remap->second.count(k))
            out.properties[remap->second.at(k)] = v;
        else
            out.properties[k] = v;
    }
    return out;
}

BlockStateMapper::ForgeBlockState BlockStateMapper::bedrockToForgeBlockState(const BedrockBlockState& bs) {
    ForgeBlockState out;
    for (const auto& [forge, bedrock] : blockNameMappings)
        if (bedrock == bs.name) { out.name = forge; break; }
    if (out.name.empty()) out.name = bs.name;
    out.properties = bs.properties;
    return out;
}

// ============================================================================
// ENTITY TRACKER
// ============================================================================

std::map<jlong, void*>   EntityTracker::entityIdToBedrockMap;
std::map<void*, jlong>   EntityTracker::bedrockToEntityIdMap;
std::map<jlong, jobject> EntityTracker::entityIdToJavaRefMap;

jlong EntityTracker::getEntityId(JNIEnv* env, jobject javaEntity) {
    if (!env || !javaEntity) return 0;
    jclass sys = env->FindClass("java/lang/System");
    if (!sys) return reinterpret_cast<jlong>(javaEntity);
    jmethodID m = env->GetStaticMethodID(sys, "identityHashCode", "(Ljava/lang/Object;)I");
    env->DeleteLocalRef(sys);
    if (!m) return reinterpret_cast<jlong>(javaEntity);
    return static_cast<jlong>(env->CallStaticIntMethod(sys, m, javaEntity));
}

void EntityTracker::registerEntity(JNIEnv* env, jobject javaEntity, void* bedrockActor) {
    if (!env || !javaEntity || !bedrockActor) return;
    jlong id = getEntityId(env, javaEntity);
    auto old = entityIdToJavaRefMap.find(id);
    if (old != entityIdToJavaRefMap.end()) env->DeleteGlobalRef(old->second);
    entityIdToBedrockMap[id]          = bedrockActor;
    bedrockToEntityIdMap[bedrockActor] = id;
    entityIdToJavaRefMap[id]          = env->NewGlobalRef(javaEntity);
}

void* EntityTracker::getBedrockActor(JNIEnv* env, jobject javaEntity) {
    if (!env) return nullptr;
    auto it = entityIdToBedrockMap.find(getEntityId(env, javaEntity));
    return it != entityIdToBedrockMap.end() ? it->second : nullptr;
}

jobject EntityTracker::getJavaEntity(void* bedrockActor) {
    auto it = bedrockToEntityIdMap.find(bedrockActor);
    if (it == bedrockToEntityIdMap.end()) return nullptr;
    auto r = entityIdToJavaRefMap.find(it->second);
    return r != entityIdToJavaRefMap.end() ? r->second : nullptr;
}

void EntityTracker::unregisterEntity(JNIEnv* env, jobject javaEntity) {
    if (!env) return;
    jlong id = getEntityId(env, javaEntity);
    auto it = entityIdToBedrockMap.find(id);
    if (it == entityIdToBedrockMap.end()) return;
    bedrockToEntityIdMap.erase(it->second);
    entityIdToBedrockMap.erase(it);
    auto r = entityIdToJavaRefMap.find(id);
    if (r != entityIdToJavaRefMap.end()) {
        env->DeleteGlobalRef(r->second);
        entityIdToJavaRefMap.erase(r);
    }
}

bool EntityTracker::hasEntity(JNIEnv* env, jobject javaEntity) {
    if (!env) return false;
    return entityIdToBedrockMap.count(getEntityId(env, javaEntity)) > 0;
}

// ============================================================================
// NATIVE SHADOW ADAPTER
// ============================================================================

JNINativeMethod NativeShadowAdapter::nativeMethods[] = {
    {(char*)"setPos",   (char*)"(DDD)V",   (void*)&nativeEntitySetPos},
    {(char*)"getPos",   (char*)"()[D",     (void*)&nativeEntityGetPos},
    {(char*)"addTag",   (char*)"(Ljava/lang/String;)V", (void*)&nativeEntityAddTag},
    {(char*)"setBlock", (char*)"(Lnet/minecraft/core/BlockPos;Lnet/minecraft/world/level/block/BlockState;I)Z",
                                           (void*)&nativeBlockSetBlock},
    {(char*)"getBlock", (char*)"(Lnet/minecraft/core/BlockPos;)Lnet/minecraft/world/level/block/BlockState;",
                                           (void*)&nativeBlockGetBlock},
};
int NativeShadowAdapter::nativeMethodCount = sizeof(nativeMethods) / sizeof(nativeMethods[0]);

bool NativeShadowAdapter::registerNativeMethods(JNIEnv* env, const std::string& pkg) {
    std::string path = pkg;
    std::replace(path.begin(), path.end(), '.', '/');
    path += "/ForgeEntityAdapter";
    jclass c = env->FindClass(path.c_str());
    if (!c) { env->ExceptionClear(); return false; }
    bool ok = env->RegisterNatives(c, nativeMethods, nativeMethodCount) == JNI_OK;
    env->DeleteLocalRef(c);
    return ok;
}

void JNICALL NativeShadowAdapter::nativeEntitySetPos(JNIEnv* env, jobject entity, jdouble x, jdouble y, jdouble z) {
    void* actor = EntityTracker::getBedrockActor(env, entity);
    if (!actor) return;
    auto fn = BedrockSymbolResolver::getActorSetPos();
    if (!fn) return;
    ::Vec3 v = BedrockPointerHelper::makeVec3(x, y, z);
    fn(actor, &v);
}

jobject JNICALL NativeShadowAdapter::nativeEntityGetPos(JNIEnv* env, jobject entity) {
    void* actor = EntityTracker::getBedrockActor(env, entity);
    if (!actor) return nullptr;
    auto fn = BedrockSymbolResolver::getActorGetPos();
    if (!fn) return nullptr;
    ::Vec3 v{0,0,0};
    fn(actor, &v);
    return BedrockPointerHelper::vec3ToJDoubleArray(env, v);
}

void JNICALL NativeShadowAdapter::nativeEntityAddTag(JNIEnv* env, jobject entity, jstring tag) {
    void* actor = EntityTracker::getBedrockActor(env, entity);
    if (!actor) return;
    auto fn = BedrockSymbolResolver::getActorAddTag();
    if (!fn) return;
    const char* s = env->GetStringUTFChars(tag, nullptr);
    fn(actor, s);
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
    const char* name = env->GetStringUTFChars(jn, nullptr);
    std::string bedrockName = BlockStateMapper::mapBlockName(name);
    env->ReleaseStringUTFChars(jn, name);
    env->DeleteLocalRef(bsc); env->DeleteLocalRef(bo);
    env->DeleteLocalRef(bc);  env->DeleteLocalRef(jn);
    void* block = bct(bedrockName.c_str());
    if (!block) return JNI_FALSE;
    bst(nullptr, pos.x, pos.y, pos.z, block);
    return JNI_TRUE;
}

jobject JNICALL NativeShadowAdapter::nativeBlockGetBlock(JNIEnv*, jobject, jobject) { return nullptr; }
jobject JNICALL NativeShadowAdapter::nativeItemUse(JNIEnv*, jobject, jobject, jobject, jint) { return nullptr; }

// ============================================================================
// BEDROCK POINTER HELPER
// ============================================================================

::Vec3 BedrockPointerHelper::makeVec3(double x, double y, double z) {
    return {(float)x, (float)y, (float)z};
}
jdoubleArray BedrockPointerHelper::vec3ToJDoubleArray(JNIEnv* env, const ::Vec3& v) {
    jdoubleArray a = env->NewDoubleArray(3);
    jdouble d[] = {v.x, v.y, v.z};
    env->SetDoubleArrayRegion(a, 0, 3, d);
    return a;
}
BedrockPointerHelper::BlockPos BedrockPointerHelper::extractBlockPos(JNIEnv* env, jobject obj) {
    jclass c = env->GetObjectClass(obj);
    BlockPos p;
    p.x = env->GetIntField(obj, env->GetFieldID(c, "x", "I"));
    p.y = env->GetIntField(obj, env->GetFieldID(c, "y", "I"));
    p.z = env->GetIntField(obj, env->GetFieldID(c, "z", "I"));
    env->DeleteLocalRef(c);
    return p;
}
jobject BedrockPointerHelper::createBlockPos(JNIEnv* env, int x, int y, int z) {
    jclass c = env->FindClass("net/minecraft/core/BlockPos");
    jobject o = env->NewObject(c, env->GetMethodID(c, "<init>", "(III)V"), x, y, z);
    env->DeleteLocalRef(c);
    return o;
}

// ============================================================================
// FORGE EVENT FORWARDER
// ============================================================================

std::vector<ForgeEventForwarder::PlayerEventHandler> ForgeEventForwarder::playerJoinHandlers;
std::vector<ForgeEventForwarder::PlayerEventHandler> ForgeEventForwarder::playerLeaveHandlers;
std::vector<ForgeEventForwarder::BlockEventHandler>  ForgeEventForwarder::blockBreakHandlers;
std::vector<ForgeEventForwarder::BlockEventHandler>  ForgeEventForwarder::blockPlaceHandlers;
std::vector<jobject>                                 ForgeEventForwarder::registeredForgeListeners;

void ForgeEventForwarder::registerLeviLaminaHooks() {
    using namespace ll::event;
    auto& bus = EventBus::getInstance();

    // --- Player Join ---
    bus.emplaceListener<PlayerJoinEvent>([](PlayerJoinEvent& ev) {
        std::string uuid = ev.mEntity.getUuid().asString();
        std::string name = ev.mEntity.getRealName();
        forwardPlayerJoinEvent(uuid);
        fireJavaBridge("PlayerJoin", [uuid, name](JNIEnv* env, jclass cls) {
            jmethodID m = JavaClassCache::getStaticMethod(env, "com/example/mod/ForgeEventBridge",
                              "onPlayerJoin", "(Ljava/lang/String;Ljava/lang/String;)V");
            if (!m) return;
            jstring ju = env->NewStringUTF(uuid.c_str());
            jstring jn = env->NewStringUTF(name.c_str());
            env->CallStaticVoidMethod(cls, m, ju, jn);
            env->DeleteLocalRef(ju); env->DeleteLocalRef(jn);
        });
    });

    // --- Player Disconnect ---
    bus.emplaceListener<PlayerDisconnectEvent>([](PlayerDisconnectEvent& ev) {
        std::string uuid = ev.mEntity.getUuid().asString();
        forwardPlayerLeaveEvent(uuid);
        JNIThreadManager::ThreadGuard guard;
        if (JNIEnv* env = guard.getEnv()) {
            jobject ref = EntityTracker::getJavaEntity(&ev.mEntity);
            if (ref) EntityTracker::unregisterEntity(env, ref);
        }
        fireJavaBridge("PlayerLeave", [uuid](JNIEnv* env, jclass cls) {
            jmethodID m = JavaClassCache::getStaticMethod(env, "com/example/mod/ForgeEventBridge",
                              "onPlayerLeave", "(Ljava/lang/String;)V");
            if (!m) return;
            jstring ju = env->NewStringUTF(uuid.c_str());
            env->CallStaticVoidMethod(cls, m, ju);
            env->DeleteLocalRef(ju);
        });
    });

    // --- Player Chat ---
    bus.emplaceListener<PlayerChatEvent>([](PlayerChatEvent& ev) {
        std::string uuid    = ev.mEntity.getUuid().asString();
        std::string message = ev.message();
        fireJavaBridge("PlayerChat", [uuid, message](JNIEnv* env, jclass cls) {
            jmethodID m = JavaClassCache::getStaticMethod(env, "com/example/mod/ForgeEventBridge",
                              "onPlayerChat", "(Ljava/lang/String;Ljava/lang/String;)V");
            if (!m) return;
            jstring ju = env->NewStringUTF(uuid.c_str());
            jstring jm = env->NewStringUTF(message.c_str());
            env->CallStaticVoidMethod(cls, m, ju, jm);
            env->DeleteLocalRef(ju); env->DeleteLocalRef(jm);
        });
    });

    // --- Player Die ---
    bus.emplaceListener<PlayerDieEvent>([](PlayerDieEvent& ev) {
        std::string uuid  = ev.mEntity.getUuid().asString();
        std::string cause = std::to_string((int)ev.source().getCause());
        fireJavaBridge("PlayerDie", [uuid, cause](JNIEnv* env, jclass cls) {
            jmethodID m = JavaClassCache::getStaticMethod(env, "com/example/mod/ForgeEventBridge",
                              "onPlayerDeath", "(Ljava/lang/String;Ljava/lang/String;)V");
            if (!m) return;
            jstring ju = env->NewStringUTF(uuid.c_str());
            jstring jc = env->NewStringUTF(cause.c_str());
            env->CallStaticVoidMethod(cls, m, ju, jc);
            env->DeleteLocalRef(ju); env->DeleteLocalRef(jc);
        });
    });

    // --- Player Respawn ---
    bus.emplaceListener<PlayerRespawnEvent>([](PlayerRespawnEvent& ev) {
        std::string uuid = ev.mEntity.getUuid().asString();
        fireJavaBridge("PlayerRespawn", [uuid](JNIEnv* env, jclass cls) {
            jmethodID m = JavaClassCache::getStaticMethod(env, "com/example/mod/ForgeEventBridge",
                              "onPlayerRespawn", "(Ljava/lang/String;)V");
            if (!m) return;
            jstring ju = env->NewStringUTF(uuid.c_str());
            env->CallStaticVoidMethod(cls, m, ju);
            env->DeleteLocalRef(ju);
        });
    });

    // --- Player Attack ---
    bus.emplaceListener<PlayerAttackEvent>([](PlayerAttackEvent& ev) {
        std::string uuid   = ev.mEntity.getUuid().asString();
        std::string target = ev.target().getTypeName();
        fireJavaBridge("PlayerAttack", [uuid, target](JNIEnv* env, jclass cls) {
            jmethodID m = JavaClassCache::getStaticMethod(env, "com/example/mod/ForgeEventBridge",
                              "onPlayerAttack", "(Ljava/lang/String;Ljava/lang/String;)V");
            if (!m) return;
            jstring ju = env->NewStringUTF(uuid.c_str());
            jstring jt = env->NewStringUTF(target.c_str());
            env->CallStaticVoidMethod(cls, m, ju, jt);
            env->DeleteLocalRef(ju); env->DeleteLocalRef(jt);
        });
    });

    // --- Player Interact Block ---
    bus.emplaceListener<PlayerInteractBlockEvent>([&](PlayerInteractBlockEvent& ev) {
        std::string uuid  = ev.mEntity.getUuid().asString();
        auto&       pos   = ev.mBlockPos;
        std::string block = ev.block().getTypeName();
        fireJavaBridge("PlayerInteractBlock", [uuid, x=pos.x, y=pos.y, z=pos.z, block](JNIEnv* env, jclass cls) {
            jmethodID m = JavaClassCache::getStaticMethod(env, "com/example/mod/ForgeEventBridge",
                              "onPlayerInteractBlock", "(Ljava/lang/String;IIILjava/lang/String;)V");
            if (!m) return;
            jstring ju = env->NewStringUTF(uuid.c_str());
            jstring jb = env->NewStringUTF(block.c_str());
            env->CallStaticVoidMethod(cls, m, ju, x, y, z, jb);
            env->DeleteLocalRef(ju); env->DeleteLocalRef(jb);
        });
    });

    // --- Player Pick Up Item ---
    bus.emplaceListener<PlayerPickUpItemEvent>([](PlayerPickUpItemEvent& ev) {
        std::string uuid = ev.mEntity.getUuid().asString();
        std::string item = ev.itemActor().getTypeName();
        fireJavaBridge("PlayerPickUpItem", [uuid, item](JNIEnv* env, jclass cls) {
            jmethodID m = JavaClassCache::getStaticMethod(env, "com/example/mod/ForgeEventBridge",
                              "onPlayerPickupItem", "(Ljava/lang/String;Ljava/lang/String;)V");
            if (!m) return;
            jstring ju = env->NewStringUTF(uuid.c_str());
            jstring ji = env->NewStringUTF(item.c_str());
            env->CallStaticVoidMethod(cls, m, ju, ji);
            env->DeleteLocalRef(ju); env->DeleteLocalRef(ji);
        });
    });

    // --- Block Break ---
    bus.emplaceListener<PlayerDestroyBlockEvent>([&](PlayerDestroyBlockEvent& ev) {
        auto&       pos   = ev.mBlockPos;
        std::string uuid  = ev.mEntity.getUuid().asString();
        std::string block = ev.mEntity.getDimension()
                              .getBlockSourceFromMainChunkLoadState()
                              .getBlock(pos).getTypeName();
        forwardBlockBreakEvent(pos.x, pos.y, pos.z, uuid);
        fireJavaBridge("BlockBreak", [uuid, x=pos.x, y=pos.y, z=pos.z, block](JNIEnv* env, jclass cls) {
            jmethodID m = JavaClassCache::getStaticMethod(env, "com/example/mod/ForgeEventBridge",
                              "onBlockBreak", "(IIILjava/lang/String;Ljava/lang/String;)V");
            if (!m) return;
            jstring ju = env->NewStringUTF(uuid.c_str());
            jstring jb = env->NewStringUTF(block.c_str());
            env->CallStaticVoidMethod(cls, m, x, y, z, jb, ju);
            env->DeleteLocalRef(ju); env->DeleteLocalRef(jb);
        });
    });

    // --- Block Place ---
    bus.emplaceListener<PlayerPlaceBlockEvent>([&](PlayerPlaceBlockEvent& ev) {
        auto&       pos   = ev.mBlockPos;
        std::string uuid  = ev.mEntity.getUuid().asString();
        std::string block = ev.block().getTypeName();
        forwardBlockPlaceEvent(pos.x, pos.y, pos.z, block, uuid);
        fireJavaBridge("BlockPlace", [uuid, x=pos.x, y=pos.y, z=pos.z, block](JNIEnv* env, jclass cls) {
            jmethodID m = JavaClassCache::getStaticMethod(env, "com/example/mod/ForgeEventBridge",
                              "onBlockPlace", "(IIILjava/lang/String;Ljava/lang/String;)V");
            if (!m) return;
            jstring ju = env->NewStringUTF(uuid.c_str());
            jstring jb = env->NewStringUTF(block.c_str());
            env->CallStaticVoidMethod(cls, m, x, y, z, jb, ju);
            env->DeleteLocalRef(ju); env->DeleteLocalRef(jb);
        });
    });

    // --- Actor Hurt ---
    bus.emplaceListener<ActorHurtEvent>([](ActorHurtEvent& ev) {
        std::string type  = ev.mEntity.getTypeName();
        float       dmg   = ev.damage();
        std::string cause = std::to_string((int)ev.source().getCause());
        fireJavaBridge("EntityHurt", [type, dmg, cause](JNIEnv* env, jclass cls) {
            jmethodID m = JavaClassCache::getStaticMethod(env, "com/example/mod/ForgeEventBridge",
                              "onEntityHurt", "(Ljava/lang/String;FLjava/lang/String;)V");
            if (!m) return;
            jstring jt = env->NewStringUTF(type.c_str());
            jstring jc = env->NewStringUTF(cause.c_str());
            env->CallStaticVoidMethod(cls, m, jt, (jfloat)dmg, jc);
            env->DeleteLocalRef(jt); env->DeleteLocalRef(jc);
        });
    });

    // --- Mob Die ---
    bus.emplaceListener<MobDieEvent>([](MobDieEvent& ev) {
        std::string type  = ev.mEntity.getTypeName();
        std::string cause = std::to_string((int)ev.source().getCause());
        fireJavaBridge("EntityDie", [type, cause](JNIEnv* env, jclass cls) {
            jmethodID m = JavaClassCache::getStaticMethod(env, "com/example/mod/ForgeEventBridge",
                              "onEntityDeath", "(Ljava/lang/String;Ljava/lang/String;)V");
            if (!m) return;
            jstring jt = env->NewStringUTF(type.c_str());
            jstring jc = env->NewStringUTF(cause.c_str());
            env->CallStaticVoidMethod(cls, m, jt, jc);
            env->DeleteLocalRef(jt); env->DeleteLocalRef(jc);
        });
    });

    // --- Mob Spawn ---
    bus.emplaceListener<SpawnMobEvent>([](SpawnMobEvent& ev) {
        auto& self = ev.mEntity;
        std::string type = ev.mEntity.getTypeName();
        fireJavaBridge("MobSpawn", [type](JNIEnv* env, jclass cls) {
            jmethodID m = JavaClassCache::getStaticMethod(env, "com/example/mod/ForgeEventBridge",
                              "onMobSpawn", "(Ljava/lang/String;)V");
            if (!m) return;
            jstring jt = env->NewStringUTF(type.c_str());
            env->CallStaticVoidMethod(cls, m, jt);
            env->DeleteLocalRef(jt);
        });
    });

    // --- Server Started ---
    bus.emplaceListener<ServerStartedEvent>([](ServerStartedEvent&) {
        fireJavaBridge("ServerStarted", [](JNIEnv* env, jclass cls) {
            jmethodID m = JavaClassCache::getStaticMethod(env, "com/example/mod/ForgeEventBridge",
                              "onServerStarted", "()V");
            if (m) env->CallStaticVoidMethod(cls, m);
        });
    });

    // --- Server Stopping ---
    bus.emplaceListener<ServerStoppingEvent>([](ServerStoppingEvent&) {
        fireJavaBridge("ServerStopped", [](JNIEnv* env, jclass cls) {
            jmethodID m = JavaClassCache::getStaticMethod(env, "com/example/mod/ForgeEventBridge",
                              "onServerStopped", "()V");
            if (m) env->CallStaticVoidMethod(cls, m);
        });
    });

    my_mod::MyMod::getInstance().getSelf().getLogger().info(
        "ForgeEventForwarder: all LeviLamina hooks registered"
    );
}

void ForgeEventForwarder::onPlayerJoin(PlayerEventHandler h)  { playerJoinHandlers.push_back(h); }
void ForgeEventForwarder::onPlayerLeave(PlayerEventHandler h) { playerLeaveHandlers.push_back(h); }
void ForgeEventForwarder::onBlockBreak(BlockEventHandler h)   { blockBreakHandlers.push_back(h); }
void ForgeEventForwarder::onBlockPlace(BlockEventHandler h)   { blockPlaceHandlers.push_back(h); }

void ForgeEventForwarder::forwardPlayerJoinEvent(const std::string& id)  { for (auto& h : playerJoinHandlers)  h(id); }
void ForgeEventForwarder::forwardPlayerLeaveEvent(const std::string& id) { for (auto& h : playerLeaveHandlers) h(id); }
void ForgeEventForwarder::forwardBlockBreakEvent(int x,int y,int z,const std::string& id) { for(auto& h:blockBreakHandlers) h(x,y,z,id); }
void ForgeEventForwarder::forwardBlockPlaceEvent(int x,int y,int z,const std::string& b,const std::string& id) { for(auto& h:blockPlaceHandlers) h(x,y,z,id); }
void ForgeEventForwarder::registerForgeEventListener(JNIEnv* env,const std::string&,jobject l) {
    registeredForgeListeners.push_back(env->NewGlobalRef(l));
}

// ============================================================================
// LEVILAMINA HOOKS
// ============================================================================

LL_AUTO_TYPE_INSTANCE_HOOK(
    ChatHook,
    ll::memory::HookPriority::Normal,
    Player,
    nullptr,
    void,
    std::string const& message
) {
    bool cancelled = false;
    JNIThreadManager::ThreadGuard guard;
    JNIEnv* env = guard.getEnv();
    if (env) {
        jclass cls = JavaClassCache::getClass(env, "com/example/mod/ForgeEventBridge");
        if (cls) {
            jmethodID m = JavaClassCache::getStaticMethod(env,
                "com/example/mod/ForgeEventBridge", "canChat",
                "(Ljava/lang/String;Ljava/lang/String;)Z");
            if (m) {
                jstring juuid = env->NewStringUTF(this->getUuid().asString().c_str());
                jstring jmsg  = env->NewStringUTF(message.c_str());
                cancelled = !env->CallStaticBooleanMethod(cls, m, juuid, jmsg);
                env->DeleteLocalRef(juuid); env->DeleteLocalRef(jmsg);
            }
        }
    }
    if (!cancelled) origin(message);
}

// ============================================================================
// FORGE COMMAND BRIDGE
// ============================================================================

static void registerForgeCommands() {
    using namespace ll::command;

    struct ForgeCmd {
        enum class Sub { status, reload, mods, gc } sub;
        std::string modId;
    };

    auto& cmd = CommandRegistrar::getInstance(my_mod::MyMod::getInstance().getSelf().getModContext()).getOrCreateCommand(
        "forge", "Forge-Bedrock translator control"
    );

    cmd.overload<ForgeCmd>()
        .text("status")
        .execute([](CommandOrigin const&, CommandOutput& out, ForgeCmd const&) {
            auto mods = ModMorpher::getLoadedMods();
            out.success("ModMorpher: {} | {} forge mod(s) loaded",
                ModMorpher::isInitialized() ? "RUNNING" : "STOPPED",
                mods.size());
            for (auto& m : mods) out.success("  - {}", m);
        });

    cmd.overload<ForgeCmd>()
        .text("mods")
        .execute([](CommandOrigin const&, CommandOutput& out, ForgeCmd const&) {
            auto mods = ModMorpher::getLoadedMods();
            if (mods.empty()) { out.success("No Forge mods loaded."); return; }
            for (auto& m : mods) out.success("  {}", m);
        });

    cmd.overload<ForgeCmd>()
        .text("gc")
        .execute([](CommandOrigin const&, CommandOutput& out, ForgeCmd const&) {
            enqueueJNICall("ManualGC", [](JNIEnv* env) {
                jclass rt       = env->FindClass("java/lang/Runtime");
                jmethodID getRt = env->GetStaticMethodID(rt, "getRuntime", "()Ljava/lang/Runtime;");
                jobject rtInst  = env->CallStaticObjectMethod(rt, getRt);
                jmethodID gc    = env->GetMethodID(rt, "gc", "()V");
                env->CallVoidMethod(rtInst, gc);
                env->DeleteLocalRef(rt); env->DeleteLocalRef(rtInst);
            });
            out.success("Java GC triggered.");
        });

    cmd.overload<ForgeCmd>()
        .text("reload")
        .execute([](CommandOrigin const&, CommandOutput& out, ForgeCmd const&) {
            out.success("Reload not yet implemented — restart server to reload Forge mods.");
        });

    my_mod::MyMod::getInstance().getSelf().getLogger().info(
        "ForgeCommandBridge: /forge command registered"
    );
}

// ============================================================================
// MODMORPHER MANAGER
// ============================================================================

bool                     ModMorpher::initialized = false;
JavaVM*                  ModMorpher::cachedJVM   = nullptr;
JNIEnv*                  ModMorpher::cachedEnv   = nullptr;
std::vector<std::string> ModMorpher::loadedMods;

bool ModMorpher::initialize(JavaVM* jvm, JNIEnv* env) {
    if (initialized) return true;
    auto& logger = my_mod::MyMod::getInstance().getSelf().getLogger();

    cachedJVM = jvm;
    cachedEnv = env;

    JNIThreadManager::setJVM(jvm);
    BedrockSymbolResolver::initialize();
    BlockStateMapper::loadMappings("");
    NativeShadowAdapter::registerNativeMethods(env, "com.example.mod");
    ForgeEventForwarder::registerLeviLaminaHooks();
    registerForgeCommands();

    gJNIWorkerRunning = true;
    gJNIWorkerThread  = std::thread(jniWorkerLoop);

    initialized = true;
    logger.info("ModMorpher: fully initialized — JNI worker running");
    return true;
}

void ModMorpher::shutdown() {
    if (!initialized) return;
    auto& logger = my_mod::MyMod::getInstance().getSelf().getLogger();

    gJNIWorkerRunning = false;
    if (gJNIWorkerThread.joinable()) gJNIWorkerThread.join();

    if (cachedEnv) {
        JavaClassCache::clear(cachedEnv);
        for (auto ref : ForgeEventForwarder::registeredForgeListeners)
            cachedEnv->DeleteGlobalRef(ref);
        ForgeEventForwarder::registeredForgeListeners.clear();
    }

    JNIThreadManager::detachCurrentThread();

    initialized = false;
    cachedJVM   = nullptr;
    cachedEnv   = nullptr;
    loadedMods.clear();
    logger.info("ModMorpher: shutdown complete");
}

bool ModMorpher::loadForgeMod(const std::string& jarPath) {
    if (!initialized) return false;
    auto& logger = my_mod::MyMod::getInstance().getSelf().getLogger();

    enqueueJNICall("LoadForgeMod:" + jarPath, [jarPath, &logger](JNIEnv* env) {
        jclass urlClass    = env->FindClass("java/net/URL");
        jclass loaderClass = env->FindClass("java/net/URLClassLoader");
        if (!urlClass || !loaderClass) {
            logger.error("loadForgeMod: URLClassLoader unavailable");
            return;
        }
        jmethodID urlCtor    = env->GetMethodID(urlClass, "<init>", "(Ljava/lang/String;)V");
        jmethodID loaderCtor = env->GetMethodID(loaderClass, "<init>", "([Ljava/net/URL;)V");
        jstring jpath        = env->NewStringUTF(("file:" + jarPath).c_str());
        jobject url          = env->NewObject(urlClass, urlCtor, jpath);
        env->DeleteLocalRef(jpath);
        jobjectArray urls = env->NewObjectArray(1, urlClass, url);
        jobject loader    = env->NewObject(loaderClass, loaderCtor, urls);
        env->DeleteLocalRef(urls); env->DeleteLocalRef(url);
        if (!loader) { logger.error("loadForgeMod: failed to create URLClassLoader"); return; }

        jmethodID findClass = env->GetMethodID(loaderClass, "loadClass",
                                               "(Ljava/lang/String;)Ljava/lang/Class;");
        const char* entryPoints[] = {
            "net.minecraftforge.fml.common.Mod",
            "com.example.mod.ModMain",
            "Main",
        };
        for (auto ep : entryPoints) {
            jstring jep   = env->NewStringUTF(ep);
            jobject clazz = env->CallObjectMethod(loader, findClass, jep);
            env->DeleteLocalRef(jep);
            if (env->ExceptionCheck()) { env->ExceptionClear(); continue; }
            if (clazz) {
                logger.info("loadForgeMod: found entry point -> " + std::string(ep));
                env->DeleteLocalRef(clazz);
                break;
            }
        }
        env->DeleteLocalRef(loader);
        logger.info("loadForgeMod: JAR staged -> " + jarPath);
    });

    loadedMods.push_back(jarPath);
    return true;
}

bool ModMorpher::unloadForgeMod(const std::string& modId) {
    auto it = std::find(loadedMods.begin(), loadedMods.end(), modId);
    if (it == loadedMods.end()) return false;
    loadedMods.erase(it);
    return true;
}

std::vector<std::string> ModMorpher::getLoadedMods() { return loadedMods; }
bool                     ModMorpher::isInitialized()  { return initialized; }

} // namespace modmorpher