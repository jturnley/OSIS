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

set_project("OSIS")
set_version("1.3.5")
set_languages("c++23")
set_warnings("allextra")

-- Two editions of OSIS.dll ("OStim Standalone Immersive Sex") from the same source:
--   OSIS       the full plugin (LoversLab), build/windows/x64/<mode>
--   OSISLite  the Lite edition, build/lite/windows/x64/<mode>: compiled with OSIS_LITE, which
--              leaves out the non-consent features (their code, settings and menu pages), not
--              just switches them off.
-- Both are the same SKSE plugin ("OSIS"); install one or the other.
local function osis_plugin(targetname, lite)
    target(targetname)
        set_basename("OSIS")
        set_symbols("debug")
        add_rules("commonlibsse-ng.plugin", {
            name = "OSIS",
            author = "jturnley",
            description = "OStim Standalone Immersive Sex: faces, lip-sync, skin and softbody arousal in one SKSE plugin"
        })

        add_packages("simpleini", "nlohmann_json")
        add_includedirs("src", "include")
        -- SKSEMenuFramework.h is vendored and still uses <codecvt>.
        add_defines("_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING")
        add_headerfiles("src/**.h")
        if lite then
            add_defines("OSIS_LITE=1")
            add_files("src/**.cpp|Voice.cpp|SceneLock.cpp|SpellCast.cpp")
            set_targetdir("build/lite/$(plat)/$(arch)/$(mode)")
        else
            add_files("src/**.cpp")
        end
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
end

osis_plugin("OSIS", false)
osis_plugin("OSISLite", true)
