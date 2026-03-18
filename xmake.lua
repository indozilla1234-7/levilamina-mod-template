add_rules("mode.debug", "mode.release")

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

option("target_type")
    set_default("server")
    set_showmenu(true)
    set_values("server", "client")
option_end()

add_requires("levilamina", {configs = {target_type = get_config("target_type")}})
add_requires("levibuildscript")

if is_os("windows") or is_plat("windows") then
    if not has_config("vs_runtime") then
        set_runtimes("MD")
    end
end

target("my-mod")
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker")
    
    if is_os("windows") or is_plat("windows") then
        add_cxflags("/EHa", "/utf-8", "/W4", "/w44265", "/w44289", "/w44296", "/w45263", "/w44738", "/w45204")
        add_defines("NOMINMAX", "UNICODE")
        set_exceptions("none")
    else
        add_cxflags("-Wall", "-Wextra", "-fexceptions")
    end
    
    add_packages("levilamina")
    set_kind("shared")
    set_languages("c++20")
    set_symbols("debug")
    add_headerfiles("src/**.h")
    add_files("src/**.cpp|ll/**")
    add_includedirs("src")
    if is_config("target_type", "server") then
    --  add_includedirs("src-server")
    --  add_files("src-server/**.cpp")
    else
    --  add_includedirs("src-client")
    --  add_files("src-client/**.cpp")
    end