includes("extern/CommonLibSSE-NG")

-- Share the CommonLibSSE-NG build with sibling projects (same commit as Softbody_Arousal / Dismember-Sanguine).
local sharedRoot = path.join(os.projectdir(), "..", "_XMakeShared")
target("commonlibsse-ng")
    set_targetdir(path.join(sharedRoot, "CommonLibSSE-NG", "windows", "x64"))
    set_objectdir(path.join(sharedRoot, "CommonLibSSE-NG", ".objs"))
    set_dependir(path.join(sharedRoot, "CommonLibSSE-NG", ".deps"))
target_end()

add_rules("mode.debug", "mode.releasedbg")

add_requires("simpleini", "nlohmann_json")

local projectRoot = os.projectdir():gsub("/", "\\") .. "\\"

set_project("OSEDReborn")
set_version("3.0.0")
set_languages("c++23")
set_warnings("allextra")

target("OSEDReborn")
    set_symbols("debug")
    add_rules("commonlibsse-ng.plugin", {
        name = "OSEDReborn",
        author = "jturnley",
        description = "OStim Expression Director Reborn: faces, body, skin, lip-sync and softbody arousal in one SKSE plugin"
    })

    add_packages("simpleini", "nlohmann_json")
    add_includedirs("src", "include")
    -- SKSEMenuFramework.h is vendored and still uses <codecvt>.
    add_defines("_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING")
    add_headerfiles("src/**.h")
    add_files("src/**.cpp")
    set_pcxxheader("include/PCH.h")

    if is_mode("releasedbg") then
        set_optimize("smallest")
        add_cxxflags("/Zo", "/Oy-", { force = true })
        -- /d1trimfile cannot take a path with spaces ("OSED Reborn"), so it is only used without them.
        if not projectRoot:find(" ") then
            add_cxxflags("/d1trimfile:" .. projectRoot, { force = true })
        end
        add_ldflags("/DEBUG:FULL", "/OPT:REF", "/OPT:ICF", { force = true })
    end
target_end()
