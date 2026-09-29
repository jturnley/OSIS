"""Compile every mod's Papyrus sources with the Creation Kit compiler.

SkyUI ships SKI_ConfigBase/SKI_QuestBase/SKI_WidgetBase only as .pex inside SkyUI_SE.bsa, and NiOverride.psc
only inside RaceMenu.bsa. Both are pulled from the local archives into build/papyrus-deps
(SkyUI as compile-only headers via pex_stub.py). Nothing from there is shipped.

Usage: python tools/compile_papyrus.py [mod folder name...]   (default: all four)
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bsa_extract  # noqa: E402
import pex_stub  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
D = 'D:' + os.sep
STEAM = os.path.join(D, 'SteamLibrary', 'steamapps', 'common', 'Skyrim Special Edition')
MO2 = os.path.join(D, 'SkyrimSE-MO2')
MODS = os.path.join(MO2, 'mods')

COMPILER = os.path.join(STEAM, 'Papyrus Compiler', 'PapyrusCompiler.exe')
VANILLA = os.path.join(STEAM, 'Data', 'Source', 'Scripts')
SKSE = os.path.join(MO2, 'Skyrim Special Edition', 'Data', 'Scripts', 'Source')
DEP_SOURCES = [
    os.path.join(MODS, 'PapyrusUtil SE - Modders Scripting Utility Functions', 'Scripts', 'Source'),
    os.path.join(MODS, 'OStim Standalone - Advanced Adult Animation Framework', 'Scripts', 'Source'),
    os.path.join(MODS, 'Mfg Fix NG', 'source', 'scripts'),
    # Only needed because the compiler type-checks OStim's own sources, which use these:
    os.path.join(MODS, 'JContainers SE', 'scripts', 'source'),
    os.path.join(MODS, 'ConsoleUtilSSE NG', 'Scripts', 'Source'),
]
SKYUI_BSA = os.path.join(MODS, 'SkyUI', 'SkyUI_SE.bsa')
RACEMENU_BSA = os.path.join(MODS, 'RaceMenu', 'RaceMenu.bsa')
DEPS = os.path.join(ROOT, 'build', 'papyrus-deps')

ALL_MODS = ['OSED Core', 'OSED Body', 'OSED Living Skin', 'OSED Lip-Sync']


def bsa_member(bsa, wanted):
    f, aflags, entries = bsa_extract.read_bsa(bsa)
    for e in entries:
        if (e[0] + '\\' + e[1]).lower() == wanted:
            return bsa_extract.extract(f, aflags, e)
    raise SystemExit('%s not found in %s' % (wanted, bsa))


def prepare_deps():
    os.makedirs(DEPS, exist_ok=True)
    for name in ('SKI_QuestBase', 'SKI_ConfigBase', 'SKI_WidgetBase'):  # WidgetBase: OStim's bars
        pex = bsa_member(SKYUI_BSA, 'scripts\\%s.pex' % name.lower())
        obj = pex_stub.parse(pex)[0]
        with open(os.path.join(DEPS, name + '.psc'), 'w', encoding='utf-8', newline='\r\n') as out:
            out.write(pex_stub.render(obj))
    with open(os.path.join(DEPS, 'NiOverride.psc'), 'wb') as out:
        out.write(bsa_member(RACEMENU_BSA, 'scripts\\source\\nioverride.psc'))


def compile_mod(mod):
    src = os.path.join(ROOT, 'mods', mod, 'Scripts', 'Source')
    out = os.path.join(ROOT, 'mods', mod, 'Scripts')
    imports = [src]
    if mod != 'OSED Core':
        imports.append(os.path.join(ROOT, 'mods', 'OSED Core', 'Scripts', 'Source'))
    imports += [DEPS, SKSE] + DEP_SOURCES + [VANILLA]  # SKSE before vanilla: its natives win
    cmd = [COMPILER, src, '-all', '-f=TESV_Papyrus_Flags.flg', '-o=' + out, '-i=' + ';'.join(imports)]
    print('== %s' % mod, flush=True)
    rc = subprocess.call(cmd)
    return rc if rc else check_inherited_vars(mod)


_real_vars = {}


def real_vars(script):
    """Variables of a parent script as the game will load it, or None if we can't know."""
    key = script.lower()
    if key not in _real_vars:
        found = None
        if key.startswith('ski_'):
            pex = bsa_member(SKYUI_BSA, 'scripts\\%s.pex' % key)
            obj = pex_stub.parse(pex)[0]
            parent = real_vars(obj['parent']) if obj['parent'] else set()
            found = obj['vars'] | (parent or set())
        else:
            for mod in ALL_MODS:
                p = os.path.join(ROOT, 'mods', mod, 'Scripts', script + '.pex')
                if os.path.exists(p):
                    obj = pex_stub.parse(open(p, 'rb').read())[0]
                    parent = real_vars(obj['parent']) if obj['parent'] else set()
                    found = obj['vars'] | (parent or set())
        _real_vars[key] = found
    return _real_vars[key]


def check_inherited_vars(mod):
    """Every hidden auto-property variable (::X_var) a compiled script reads must exist in that
    script or in the real parent chain. A compile-only header that got a property's kind wrong
    produces exactly this, and the game only reports it at runtime."""
    src = os.path.join(ROOT, 'mods', mod, 'Scripts', 'Source')
    bad = 0
    for psc in sorted(os.listdir(src)):
        name = os.path.splitext(psc)[0]
        obj = pex_stub.parse(open(os.path.join(ROOT, 'mods', mod, 'Scripts', name + '.pex'), 'rb').read())[0]
        wanted = {i for i in obj['idents'] if i.startswith('::') and i.lower().endswith('_var')}
        have = set(obj['vars'])
        parent = real_vars(obj['parent']) if obj['parent'] else set()
        missing = sorted(v for v in wanted - have if parent is None or v not in parent)
        for v in missing:
            print('  %s.pex reads %s, which %s does not have' % (name, v, obj['parent'] or name))
        bad += len(missing)
    if bad:
        print('  %d unresolvable variable reference(s)' % bad)
    return 1 if bad else 0


def main():
    for p in [COMPILER, VANILLA, SKSE, SKYUI_BSA, RACEMENU_BSA] + DEP_SOURCES:
        if not os.path.exists(p):
            raise SystemExit('missing: ' + p)
    prepare_deps()
    failed = [m for m in (sys.argv[1:] or ALL_MODS) if compile_mod(m) != 0]
    if failed:
        print('FAILED: ' + ', '.join(failed))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
