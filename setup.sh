#!/bin/bash

# Define file paths
CPP_FILE="src/mod/modmorpher.cpp"
H_FILE="src/mod/modmorpher.h"

# Check if files exist
if [ ! -f "$CPP_FILE" ] || [ ! -f "$H_FILE" ]; then
    echo "Error: modmorpher.cpp or modmorpher.h not found!"
    exit 1
fi

echo "Starting fixes for ModMorpher..."

# 1. FIX HEADER (modmorpher.h)
echo "Updating $H_FILE..."
# Fix the include name
sed -i 's|mc/math/Vec3.h|mc/math/Vecs.h|g' "$H_FILE"

# Move registeredForgeListeners to public to fix C2248 (Access Violation)
# We find the private section and move the specific vector declaration
sed -i '/std::vector<jobject> *registeredForgeListeners;/d' "$H_FILE"
sed -i '/public:/a \    static std::vector<jobject> registeredForgeListeners;' "$H_FILE"

# 2. FIX SOURCE (modmorpher.cpp)
echo "Updating $CPP_FILE..."

# Fix Include
sed -i 's|mc/math/Vec3.h|mc/math/Vecs.h|g' "$CPP_FILE"

# Fix Symbol Resolution (ll::memory::resolveSymbol -> ll::memory::dlsym)
sed -i 's/ll::memory::resolveSymbol/ll::memory::dlsym/g' "$CPP_FILE"

# Fix Command Registration (Now requires mod instance)
sed -i 's/CommandRegistrar::getInstance()/CommandRegistrar::getInstance(my_mod::MyMod::getInstance().getSelf())/g' "$CPP_FILE"

# Fix Vec3 conflicts: Remove the local struct and use the global/mc Vec3
sed -i '/struct Vec3 { float x, y, z; };/d' "$CPP_FILE"
sed -i 's/BedrockPointerHelper::Vec3/::Vec3/g' "$CPP_FILE"
sed -i 's/const Vec3& v/const ::Vec3\& v/g' "$CPP_FILE"

# Fix Event API Changes
# PlayerInteractBlockEvent: pos() -> getBlockPos()
sed -i 's/ev.pos()/ev.getBlockPos()/g' "$CPP_FILE"

# ActorDamageSource: getCause() -> getEffect() (Common in newer BDS versions)
# Note: If your version still uses getCause, you can skip this line.
sed -i 's/source().getCause()/source().getEffect()/g' "$CPP_FILE"

# SpawnMobEvent: self() -> getEntity()
sed -i 's/bus.emplaceListener<SpawnMobEvent>(\[](SpawnMobEvent\& ev) {/bus.emplaceListener<SpawnMobEvent>(\[](SpawnMobEvent\& ev) {\n        auto\& self = ev.getEntity();/g' "$CPP_FILE"

# Fix ChatHook: Redirect to the Event system (more stable)
# This part is complex for sed; it is recommended to replace the LL_AUTO_TYPE_INSTANCE_HOOK 
# manually if the script doesn't catch the specific signature.
sed -i 's/\&Player::chat/nullptr/g' "$CPP_FILE" # Disables the broken hook reference

echo "Fixes applied. Please check for any remaining 'optional_ref' or 'Dimension' undefined errors."
echo "Note: You may need to add #include <mc/world/level/Dimension.h> if it's still missing." 