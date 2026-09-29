"""Structural checks for the OSED Papyrus sources, for use until a real compiler is set up.

Not a compiler. It catches the mistakes hand edits tend to make:
  * unbalanced If/While/Function/Event/State/Property blocks
  * functions or events defined twice in one script
  * assignment to a name that is not declared anywhere in scope
  * Core./Engine./OActor./MfgConsoleFuncExt./JsonUtil./MiscUtil./StorageUtil. members that
    don't exist in the target script (read from the installed sources)

Usage: python tools/papyrus_lint.py [extra .psc search dirs...]
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MO2 = os.path.join('D:' + os.sep, 'SkyrimSE-MO2', 'mods')

# Where the scripts' own dependencies live in the user's MO2 setup.
DEP_DIRS = [
    os.path.join(MO2, 'OStim Standalone - Advanced Adult Animation Framework', 'Scripts', 'Source'),
    os.path.join(MO2, 'Mfg Fix NG', 'source', 'scripts'),
    os.path.join(MO2, 'PapyrusUtil SE - Modders Scripting Utility Functions', 'Scripts', 'Source'),
]

# Script-qualified calls we can verify: prefix -> script name (None = the typed property).
QUALIFIED = {
    'OActor': 'OActor', 'OThread': 'OThread', 'OMetadata': 'OMetadata', 'OData': 'OData',
    'MfgConsoleFuncExt': 'MfgConsoleFuncExt', 'JsonUtil': 'JsonUtil', 'MiscUtil': 'MiscUtil',
    'StorageUtil': 'StorageUtil',
}
TYPED_PROPS = {  # property name -> script type, per consuming script
    'Core': 'OSExpressionFaces',
}
ENGINE_OF = {
    'OSExpressionFacesMCM': ('Director', 'OSExpressionFaces'),
    'OSED_BodyMCM': ('Engine', 'OSED_Body'),
    'OSED_LivingSkinMCM': ('Engine', 'OSED_LivingSkin'),
    'OSED_LipSyncMCM': ('Engine', 'OSED_LipSync'),
}

SKI_INHERITED = {'modname', 'pages', 'currentpage'}

TYPES = r'(?:Int|Float|Bool|String|Form|Actor|ObjectReference|Quest|Topic|FormList|VoiceType|Keyword|Faction|Spell|Armor|Race|ActorBase|MagicEffect|Package|Idle|Sound|GlobalVariable|ReferenceAlias|Alias|Cell|Location|Static|Weapon|Potion|Message|Light|MiscObject|Book|Scene|TextureSet|HeadPart|ColorForm|\w+)'


def strip(text):
    """Remove block comments, doc comments, line comments and string contents."""
    text = re.sub(r';/.*?/;', lambda m: '\n' * m.group(0).count('\n'), text, flags=re.S)
    text = re.sub(r'\{[^}]*\}', lambda m: '\n' * m.group(0).count('\n'), text)
    out = []
    for line in text.split('\n'):
        line = re.sub(r'"(?:[^"\\]|\\.)*"', '""', line)
        line = line.split(';', 1)[0]
        out.append(line.rstrip())
    # join line continuations
    joined, buf = [], ''
    for line in out:
        if line.endswith('\\'):
            buf += line[:-1] + ' '
            joined.append('')
        else:
            joined.append(buf + line)
            buf = ''
    return joined


def members(path):
    """Function/event names and property/variable names declared in a script."""
    funcs, props = set(), set()
    for line in strip(open(path, encoding='utf-8-sig', errors='replace').read()):
        m = re.match(r'\s*(?:\w+(?:\[\])?\s+)?(?:Function|Event)\s+(\w+)\s*\(', line, re.I)
        if m:
            funcs.add(m.group(1).lower())
            continue
        m = re.match(r'\s*\w+(?:\[\])?\s+Property\s+(\w+)', line, re.I)
        if m:
            props.add(m.group(1).lower())
            continue
        m = re.match(r'\s*\w+(?:\[\])?\s+(\w+)\s*(?:=|$)', line)
        if m and not re.match(r'\s*(If|ElseIf|While|Return|Else|EndIf|EndWhile|EndFunction|EndEvent|Scriptname|Import|State|EndState|Auto)\b', line, re.I):
            props.add(m.group(1).lower())
    return funcs, props


def extends_of(path):
    for line in strip(open(path, encoding='utf-8-sig', errors='replace').read()):
        m = re.match(r'\s*Scriptname\s+(\w+)(?:\s+extends\s+(\w+))?', line, re.I)
        if m:
            return m.group(1), m.group(2)
    return None, None


def lint(path, index):
    problems = []
    lines = strip(open(path, encoding='utf-8-sig', errors='replace').read())
    name, parent = extends_of(path)
    stack = []
    defined = {}
    script_vars = set()
    params = set()
    locals_ = set()
    in_func = False
    own_funcs, own_props = members(path)
    if parent and parent.lower() == 'ski_configbase':
        own_props |= SKI_INHERITED  # SkyUI SDK source isn't installed

    for n, line in enumerate(lines, 1):
        s = line.strip()
        if not s:
            continue
        low = s.lower()
        word = re.match(r'(\w+)', s)
        first = word.group(1).lower() if word else ''

        def push(kind):
            stack.append((kind, n))

        def pop(kind):
            if not stack or stack[-1][0] != kind:
                problems.append('%d: %s without open %s (stack: %s)' % (n, s.split()[0], kind, stack[-1] if stack else None))
            else:
                stack.pop()

        m = re.match(r'(?:\w+(?:\[\])?\s+)?(function|event)\s+(\w+)\s*\((.*)\)\s*(.*)$', s, re.I)
        if m and first not in ('if', 'elseif', 'while', 'return'):
            kind, fname, args, tail = m.group(1).lower(), m.group(2), m.group(3), m.group(4).lower()
            key = fname.lower()
            if key in defined and not stack:
                problems.append('%d: %s %s already defined at line %d' % (n, kind, fname, defined[key]))
            if not stack:
                defined[key] = n
            if 'native' not in tail.split():
                push(kind)
                in_func = True
                params = {a.strip().split()[-1].split('=')[0].strip().lower() for a in args.split(',') if a.strip()}
                params = {p.split('=')[0].strip() for p in params}
                locals_ = set()
            continue
        if first in ('endfunction', 'endevent'):
            pop(first[3:])
            in_func = False
            continue
        if first == 'if':
            push('if')
        elif first in ('elseif', 'else'):
            if not stack or stack[-1][0] != 'if':
                problems.append('%d: %s outside If' % (n, first))
        elif first == 'endif':
            pop('if')
        elif first == 'while':
            push('while')
        elif first == 'endwhile':
            pop('while')
        elif first == 'state' or (first == 'auto' and re.match(r'auto\s+state\b', low)):
            push('state')
        elif first == 'endstate':
            pop('state')
        elif re.match(r'\w+(?:\[\])?\s+property\s+\w+', low):
            if not re.search(r'\bauto(readonly)?\b', low):
                push('property')
            script_vars.add(re.match(r'\w+(?:\[\])?\s+property\s+(\w+)', low).group(1))
        elif first == 'endproperty':
            pop('property')

        # declarations
        m = re.match(r'(\w+)(\[\])?\s+(\w+)\s*(=.*)?$', s)
        if m and m.group(1).lower() not in ('return', 'if', 'elseif', 'while', 'else', 'scriptname', 'import', 'state', 'auto', 'endif', 'endwhile'):
            (locals_ if in_func else script_vars).add(m.group(3).lower())
            continue

        # assignment targets
        m = re.match(r'(\w+)\s*(=|\+=|-=|\*=|/=)(?!=)', s)
        if m and in_func:
            v = m.group(1).lower()
            if v not in locals_ and v not in params and v not in script_vars and v not in own_props:
                problems.append('%d: assignment to undeclared %s' % (n, m.group(1)))

        # qualified calls
        for pre, mem in re.findall(r'\b(\w+)\.(\w+)\s*\(', s):
            target = None
            if pre in QUALIFIED:
                target = QUALIFIED[pre]
            elif pre in TYPED_PROPS and name != TYPED_PROPS[pre]:
                target = TYPED_PROPS[pre]
            elif name in ENGINE_OF and pre == ENGINE_OF[name][0]:
                target = ENGINE_OF[name][1]
            if target and target.lower() in index:
                funcs, _ = index[target.lower()]
                if mem.lower() not in funcs:
                    problems.append('%d: %s.%s() not found in %s' % (n, pre, mem, target))
        if name in ENGINE_OF:
            eng_prop, eng = ENGINE_OF[name]
            for mem in re.findall(r'\b%s\.(\w+)\b(?!\s*\()' % eng_prop, s):
                if eng.lower() in index and mem.lower() not in index[eng.lower()][1]:
                    problems.append('%d: %s.%s property not found in %s' % (n, eng_prop, mem, eng))
    for kind, n in stack:
        problems.append('%d: %s never closed' % (n, kind))
    return problems


def main():
    ours = sorted(glob.glob(os.path.join(ROOT, 'mods', '*', 'Scripts', 'Source', '*.psc')))
    index = {}
    for d in DEP_DIRS + sys.argv[1:]:
        for p in glob.glob(os.path.join(d, '*.psc')):
            index[os.path.splitext(os.path.basename(p))[0].lower()] = members(p)
    for p in ours:
        index[os.path.splitext(os.path.basename(p))[0].lower()] = members(p)
    total = 0
    for p in ours:
        probs = lint(p, index)
        total += len(probs)
        print('%-60s %s' % (os.path.relpath(p, ROOT), 'ok' if not probs else '%d problem(s)' % len(probs)))
        for pr in probs:
            print('    ' + pr)
    return 1 if total else 0


if __name__ == '__main__':
    sys.exit(main())
