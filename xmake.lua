add_rules("mode.debug", "mode.release")

-- Use the official repository
add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

-- Define dependencies
-- Note: 'levibuildscript' is essential for the rules below to work
add_requires("levilamina 1.9.8") 
add_requires("levibuildscript")

target("my-mod")
    -- Rules for building a LeviLamina mod
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker")
    
    -- Library and Language Settings
    add_packages("levilamina")
    set_kind("shared")
    set_languages("c++20") -- Stick to C++20 to avoid the MSVC 14.44 VectorBase bug
    set_symbols("debug")
    set_exceptions("none")

    -- Compiler Flags: Added /permissive- to help with the 'formatter' errors
    if is_plat("windows") then
        add_cxflags("/EHa", "/utf-8", "/W4", "/permissive-")
        add_defines("NOMINMAX", "UNICODE")
    end

    -- CLEAN FILE SELECTION
    -- Only include your actual source files. 
    -- Do NOT manually include or exclude 'll' or 'mc' folders here.
    add_headerfiles("src/**.h")
    add_files("src/**.cpp")

    -- Include Directories
    add_includedirs("src")