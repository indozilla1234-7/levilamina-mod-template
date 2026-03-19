add_rules("mode.debug", "mode.release")

-- 1. Set global language to C++20 to avoid the C++23 Formatter bug
set_languages("c++20")

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

option("target_type")
    set_default("server")
    set_showmenu(true)
    set_values("server", "client")
option_end()

-- 2. Added a specific version here. Using the latest stable helps Xmake find it faster.
add_requires("levilamina 1.9.8", {configs = {target_type = get_config("target_type")}})
add_requires("levibuildscript")

if is_plat("windows") then
    set_runtimes("MD")
end

target("pax-bedrockia") -- Changed to your cool mod name!
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker")
    
    -- 3. REMOVED the redundant /W4 and warning flags that cause header panics
    -- Added /permissive- which is the "magic fix" for many MSVC template errors
    add_cxflags("/EHa", "/utf-8", "/permissive-")
    add_defines("NOMINMAX", "UNICODE")
    
    add_packages("levilamina")
    
    set_exceptions("none") 
    set_kind("shared")
    set_symbols("debug")

    -- 4. Standard file selection
    add_headerfiles("src/**.h")
    add_files("src/**.cpp")
    add_includedirs("src")

    if is_config("target_type", "server") then
        -- server specific logic
    end