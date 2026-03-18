#!/bin/bash

# Define file paths
CPP_FILE="src/mod/modmorpher.cpp"
H_FILE="src/mod/modmorpher.h"

# Verify files exist
if [ ! -f "$CPP_FILE" ]; then
    echo "Error: $CPP_FILE not found!"
    exit 1
fi

echo "Applying precise fixes for ModMorpher..."

# 1. Fix Memory API: ll::memory::resolveSymbol is the correct name (not dlsym or getSymbol)
# The error C2039 'getSymbol' is not a member shows the previous script's change was wrong.
sed -i 's/ll::memory::getSymbol/ll::memory::resolveSymbol/g' "$CPP_FILE"
sed -i 's/ll::memory::dlsym/ll::memory::resolveSymbol/g' "$CPP_FILE"

# 2. Fix Event Entity Access: Player events use .self() or .getPlayer() depending on version
# The error C2039 'mEntity' is not a member shows LL uses accessors, not raw members.
# We will revert mEntity back to self() which returns the entity/player reference.
sed -i 's/ev.mEntity/ev.self()/g' "$CPP_FILE"
sed -i 's/ev.mBlockPos/ev.pos()/g' "$CPP_FILE"

# 3. Fix ActorDamageSource: getCause() vs getEffect()
# The error C2039 'getCause' is not a member of 'ActorDamageSource'
# In newer BDS headers, the cause is often accessed via getEntityDamageCause()
sed -i 's/ev.source().getCause()/static_cast<int>(ev.source().getEntityDamageCause())/g' "$CPP_FILE"

# 4. Fix Vec3 Conflict: Error C2440 (Cannot convert BedrockPointerHelper::Vec3 to Vec3)
# We must ensure we are using the global ::Vec3 from the BDS headers everywhere.
sed -i 's/BedrockPointerHelper::Vec3/::Vec3/g' "$CPP_FILE"
sed -i 's/const Vec3\& v/const ::Vec3\& v/g' "$CPP_FILE"

# 5. Fix PlayerInteractBlockEvent: mBlockPos is private (Error C2248)
# Use the public accessor .pos() instead.
sed -i 's/ev.mBlockPos/ev.pos()/g' "$CPP_FILE"

# 6. Fix Command Registration: Error C2039 'getModContext' is not a member
# CommandRegistrar::getInstance() in the latest template usually takes a bool (isRegistry) 
# or the mod's self reference depending on the LL version. 
# We'll try the most compatible version: passing the mod instance directly.
sed -i 's/CommandRegistrar::getInstance(my_mod::MyMod::getInstance().getSelf().getModContext())/CommandRegistrar::getInstance(my_mod::MyMod::getInstance().getSelf())/g' "$CPP_FILE"

# 7. Fix Lambda Captures: Error C3493 (x, y, z cannot be implicitly captured)
# Ensure lambdas use [&] to capture the position variables.
sed -i 's/\[](PlayerInteractBlockEvent/\[\&](PlayerInteractBlockEvent/g' "$CPP_FILE"
sed -i 's/\[](PlayerDestroyBlockEvent/\[\&](PlayerDestroyBlockEvent/g' "$CPP_FILE"
sed -i 's/\[](PlayerPlaceBlockEvent/\[\&](PlayerPlaceBlockEvent/g' "$CPP_FILE"

# 8. Fix Block getTypeName: Error C2039 (getTypeName is not a member of optional_ref)
# We need to check if the optional_ref is valid and then call getRawName() or getTypeName()
sed -i 's/ev.block().getTypeName()/ev.block() ? ev.block()->getTypeName() : "air"/g' "$CPP_FILE"

# 9. Fix Forwarder Argument mismatch: Error C2660 (forwardBlockBreakEvent takes more args)
# Update the calls to match the header: (x, y, z, uuid)
sed -i 's/forwardBlockBreakEvent(pos.x, pos.y, pos.z, uuid)/forwardBlockBreakEvent((int)pos.x, (int)pos.y, (int)pos.z, uuid)/g' "$CPP_FILE"
sed -i 's/forwardBlockPlaceEvent(pos.x, pos.y, pos.z, block, uuid)/forwardBlockPlaceEvent((int)pos.x, (int)pos.y, (int)pos.z, block, uuid)/g' "$CPP_FILE"

echo "Fixes applied. Please run the build command."