includes("extern/CommonLibSSE-NG")

-- Share the CommonLibSSE-NG build with sibling projects (Softbody_Arousal / Dismember-Sanguine) on the
-- same CommonLib release. The release is part of the path, so a project on another release never links
-- a library built from different headers. after_load, because CommonLib has its own on_load, and the
-- description scope can't read the changelog.
target("commonlibsse-ng")
    after_load(function (target)
        local log = io.readfile(path.join(os.projectdir(), "extern", "CommonLibSSE-NG", "CHANGELOG.md"))
        local release = log and log:match("## %[?(%d+%.%d+%.%d+)") or "unknown"
        local dir = path.join(os.projectdir(), "..", "_XMakeShared", "CommonLibSSE-NG-" .. release)
        target:set("targetdir", path.join(dir, "windows", "x64"))
        target:set("objectdir", path.join(dir, ".objs"))
        target:set("dependir", path.join(dir, ".deps"))
    end)
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
