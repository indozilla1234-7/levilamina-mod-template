add_rules("mode.debug", "mode.release")

-- Add the LiteLDev repository for LeviLamina dependencies
add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

-- Configuration options for Server vs Client build
option("target_type")
    set_default("server")
    set_showmenu(true)
    set_values("server", "client")
option_end()

-- Define dependencies
add_requires("levilamina", {configs = {target_type = get_config("target_type")}})
add_requires("levibuildscript")

-- Windows-specific runtime handling
if is_os("windows") or is_plat("windows") then
    if not has_config("vs_runtime") then
        set_runtimes("MD")
    end
end

target("my-mod")
    -- Rules for building a LeviLamina mod
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker")
    
    -- Compiler Flags
    if is_os("windows") or is_plat("windows") then
        add_cxflags("/EHa", "/utf-8", "/W4", "/w44265", "/w44289", "/w44296", "/w45263", "/w44738", "/w45204")
        add_defines("NOMINMAX", "UNICODE")
        set_exceptions("none")
    else
        add_cxflags("-Wall", "-Wextra", "-fexceptions")
    end
    
    -- Library and Language Settings
    add_packages("levilamina")
    set_kind("shared")
    set_languages("c++20")
    set_symbols("debug")

    -- FILE SELECTION AND EXCLUSION
    -- We include everything in src, but explicitly EXCLUDE the 'll' and 'mc' folders
    add_headerfiles("src/**.h|ll/**|mc/**")
    add_files("src/**.cpp|ll/**|mc/**")

    -- Include Directories
    add_includedirs("src")

    -- Conditional Source Handling
    if is_config("target_type", "server") then
        -- add_includedirs("src-server")
        -- add_files("src-server/**.cpp")
    else
        -- add_includedirs("src-client")
        -- add_files("src-client/**.cpp")
    end