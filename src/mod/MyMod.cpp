#include "mod/MyMod.h"
#include "ll/api/mod/RegisterHelper.h"
#include "modmorpher.h"
#include <string>

// Platform-specific includes
#ifdef _WIN32
    #include <windows.h>  // Windows library loader
#else
    #include <dlfcn.h>    // Linux library loader
#endif

#include "jni.h"    // Your uploaded JNI header

namespace my_mod {

// We need to store these so we can shut Java down later
JavaVM* jvm = nullptr;
JNIEnv* env = nullptr;

MyMod& MyMod::getInstance() {
    static MyMod instance;
    return instance;
}

bool MyMod::load() {
    getSelf().getLogger().info("=== Forge-Bedrock Translator Initialization ===");

    // ===== STEP 1: Load JVM =====
    getSelf().getLogger().info("Step 1: Loading JVM...");
    
#ifdef _WIN32
    // Windows: Use LoadLibrary for .dll files
    const char* libPath = "./plugins/MyMod/jvm.dll";
    HMODULE handle = LoadLibraryA(libPath);
    if (!handle) {
        DWORD error = GetLastError();
        getSelf().getLogger().error("Failed to load jvm.dll! Error code: " + std::to_string(error));
        return false;
    }

    typedef jint (*CreateJVM)(JavaVM**, void**, void*);
    auto JNI_CreateJavaVM_ptr = (CreateJVM)GetProcAddress(handle, "JNI_CreateJavaVM");
    if (!JNI_CreateJavaVM_ptr) {
        getSelf().getLogger().error("Could not find JNI_CreateJavaVM in the dll file!");
        FreeLibrary(handle);
        return false;
    }
#else
    // Linux: Use dlopen for .so files
    const char* libPath = "./plugins/MyMod/libjvm.so"; 
    void* handle = dlopen(libPath, RTLD_NOW);
    if (!handle) {
        getSelf().getLogger().error("Failed to load libjvm.so! Check path.");
        return false;
    }

    typedef jint (*CreateJVM)(JavaVM**, void**, void*);
    auto JNI_CreateJavaVM_ptr = (CreateJVM)dlsym(handle, "JNI_CreateJavaVM");
    if (!JNI_CreateJavaVM_ptr) {
        getSelf().getLogger().error("Could not find JNI_CreateJavaVM in the .so file!");
        return false;
    }
#endif

    JavaVMInitArgs vm_args;
    JavaVMOption options[1];
    options[0].optionString = (char*)"-Djava.class.path=./plugins/MyMod/mods/";
    
    vm_args.version = JNI_VERSION_1_8;
    vm_args.nOptions = 1;
    vm_args.options = options;
    vm_args.ignoreUnrecognized = false;

    jint rc = JNI_CreateJavaVM_ptr(&jvm, (void**)&env, &vm_args);
    if (rc != JNI_OK) {
        getSelf().getLogger().error("Java failed to start. Error code: " + std::to_string(rc));
        return false;
    }

    getSelf().getLogger().info("Java Virtual Machine started successfully!");

    // ===== STEP 2+: Initialize ModMorpher (consolidates all setup) =====
    getSelf().getLogger().info("Step 2+: Initializing ModMorpher system...");
    if (!modmorpher::ModMorpher::initialize(jvm, env)) {
        getSelf().getLogger().error("Failed to initialize ModMorpher");
        return false;
    }

    getSelf().getLogger().info("=== Forge-Bedrock Translator Ready ===");
    return true;
}

bool MyMod::enable() {
    getSelf().getLogger().debug("Enabling Java Hooks...");
    // This is where you'd tell your Java mods to start running their logic
    return true;
}

bool MyMod::disable() {
    getSelf().getLogger().debug("Shutting down Forge-Bedrock Translator...");
    
    // Shutdown ModMorpher (handles thread detachment and cleanup)
    modmorpher::ModMorpher::shutdown();
    
    if (jvm) {
        getSelf().getLogger().debug("Destroying Java Virtual Machine...");
        jvm->DestroyJavaVM();
        jvm = nullptr;
        env = nullptr;
    }
    
    getSelf().getLogger().info("Shutdown complete");
    return true;
}

} // namespace my_mod

LL_REGISTER_MOD(my_mod::MyMod, my_mod::MyMod::getInstance());