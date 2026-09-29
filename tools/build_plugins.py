"""Regenerate the four OSED plugins from source.

The shipped plugins had to be edited by hand in xEdit. Building them here keeps every
record reviewable in git. FormIDs match the originals, because the scripts look forms up
with Game.GetFormFromFile(0x800, ...).

Fixes relative to the originals:
  * No VMAD overrides of bEnabled / bOwnVoice. The originals forced them True, although
    the READMEs and the script defaults say "default OFF".
  * Every MCM quest gets a player alias running SKI_PlayerLoadGameAlias, so SkyUI's
    OnGameReload (version checks, re-registration) runs on each load.
  * Each add-on engine quest gets a player alias running OSED_AddonLoadAlias. Quest
    scripts never receive OnPlayerLoadGame, so the add-ons' load maintenance never ran.
  * Complete QUST VMAD fragment sections (the Core MCM quest had none).
  * Lip-Sync: short quest/topic EDIDs so voice file names aren't truncated. Topics are
    Custom (CUST) instead of OutOfBreath (OUTB), which the engine could pick by itself.

Usage:  python tools/build_plugins.py        (writes into mods/<mod>/)
"""
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FORM_VERSION = 44
ESL_FLAG = 0x200
PLAYER_REF = 0x00000014


# ---------------------------------------------------------------- low-level writers

def zstring(s):
    return s.encode('cp1252') + b'\0'


def sub(sig, data):
    assert len(data) < 0x10000, sig
    return sig.encode('ascii') + struct.pack('<H', len(data)) + data


def record(sig, fid, subs, flags=0):
    data = b''.join(subs)
    return sig.encode('ascii') + struct.pack('<IIIIHH', len(data), flags, fid, 0, FORM_VERSION, 0) + data


def group(label, gtype, body):
    if isinstance(label, str):
        label = label.encode('ascii')
    return b'GRUP' + struct.pack('<I', 24 + len(body)) + label + struct.pack('<iHHI', gtype, 0, 0, 0) + body


def u8(v):
    return struct.pack('<B', v)


def u16(v):
    return struct.pack('<H', v)


def u32(v):
    return struct.pack('<I', v)


def f32(v):
    return struct.pack('<f', v)


def lstr(s):
    b = s.encode('cp1252')
    return u16(len(b)) + b


# ---------------------------------------------------------------- VMAD (object format 2)

def vm_object(fid, alias=-1):
    return u16(0) + struct.pack('<h', alias) + u32(fid)


def vm_script(name, props=()):
    out = lstr(name) + u8(0) + u16(len(props))
    for pname, ptype, value in props:
        out += lstr(pname) + u8(ptype) + u8(1)
        if ptype == 1:
            out += vm_object(value)
        elif ptype == 2:
            out += lstr(value)
        elif ptype == 3:
            out += struct.pack('<i', value)
        elif ptype == 4:
            out += f32(value)
        elif ptype == 5:
            out += u8(1 if value else 0)
        else:
            raise ValueError(ptype)
    return out


def quest_vmad(quest_fid, scripts, alias_scripts):
    """scripts: [(name, props)]; alias_scripts: {alias_id: [(name, props)]}"""
    out = struct.pack('<hh', 5, 2) + u16(len(scripts))
    for name, props in scripts:
        out += vm_script(name, props)
    # Fragment section: version 2, no stage fragments, empty file name.
    out += u8(2) + u16(0) + lstr('')
    out += u16(len(alias_scripts))
    for alias_id, ascripts in sorted(alias_scripts.items()):
        out += vm_object(quest_fid, alias_id) + struct.pack('<hh', 5, 2) + u16(len(ascripts))
        for name, props in ascripts:
            out += vm_script(name, props)
    return out


# ---------------------------------------------------------------- record builders

QUEST_FLAGS = 0x0111  # Start Game Enabled | Starts Enabled | Run Once (same as the originals)


def quest(fid, edid, scripts, player_alias_script=None, alias_flags=0x82):
    aliases = {0: [(player_alias_script, [])]} if player_alias_script else {}
    subs = [
        sub('EDID', zstring(edid)),
        sub('VMAD', quest_vmad(fid, scripts, aliases)),
        sub('DNAM', u16(QUEST_FLAGS) + u8(0) + u8(0xFF) + u32(0) + u32(0)),
        sub('NEXT', b''),
        sub('ANAM', u32(len(aliases))),
    ]
    if player_alias_script:
        subs += [
            sub('ALST', u32(0)),
            sub('ALID', zstring('PlayerAlias')),
            sub('FNAM', u32(alias_flags)),  # 0x82 = Optional | Allow Disabled (as in the Core original)
            sub('ALFR', u32(PLAYER_REF)),
            sub('ALED', b''),
        ]
    return record('QUST', fid, subs)


def formlist(fid, edid, entries):
    return record('FLST', fid, [sub('EDID', zstring(edid))] + [sub('LNAM', u32(e)) for e in entries])


def custom_topic(fid, edid, name, quest_fid):
    return record('DIAL', fid, [
        sub('EDID', zstring(edid)),
        sub('FULL', zstring(name)),
        sub('PNAM', f32(50.0)),
        sub('QNAM', u32(quest_fid)),
        # flags 0, category 0 (Topic), subtype 0 (Custom). A topic with no branch (BNAM)
        # never appears in the dialogue menu; it only plays through Say().
        sub('DATA', u8(0) + u8(0) + u16(0)),
        sub('SNAM', b'CUST'),
        sub('TIFC', u32(1)),
    ])


def info(fid, edid):
    return record('INFO', fid, [
        sub('EDID', zstring(edid)),
        sub('ENAM', u16(0) + u16(0)),
        # emotion Neutral 50, response number 1, no sound, no flags
        sub('TRDT', u32(0) + u32(50) + u32(0) + u8(1) + b'\0\0\0' + u32(0) + u8(0) + b'\0\0\0'),
        sub('NAM1', zstring(' ')),
        sub('NAM2', zstring('')),
        sub('NAM3', zstring('')),
    ])


def plugin(masters, groups, author='OSED Reborn', description=''):
    """groups: [(type, [record bytes] or [(dial_bytes, dial_fid, [info bytes])])]"""
    body = b''
    nrec = 0
    max_id = 0x7FF
    for gsig, items in groups:
        inner = b''
        for it in items:
            if gsig == 'DIAL':
                dial, dial_fid, infos = it
                inner += dial
                inner += group(u32(dial_fid), 7, b''.join(infos))
                nrec += 2 + len(infos)  # topic, its children GRUP, infos
                max_id = max(max_id, dial_fid & 0xFFF, *[struct.unpack_from('<I', i, 12)[0] & 0xFFF for i in infos])
            else:
                inner += it
                nrec += 1
                max_id = max(max_id, struct.unpack_from('<I', it, 12)[0] & 0xFFF)
        body += group(gsig, 0, inner)
        nrec += 1  # the CK counts GRUPs in HEDR too
    subs = [sub('HEDR', f32(1.7) + u32(nrec) + u32(max_id + 1)), sub('CNAM', zstring(author))]
    if description:
        subs.append(sub('SNAM', zstring(description)))
    for m in masters:
        subs += [sub('MAST', zstring(m)), sub('DATA', b'\0' * 8)]
    assert max_id <= 0xFFF, 'ESL range'
    return record('TES4', 0, subs, flags=ESL_FLAG) + body


# ---------------------------------------------------------------- the four plugins

CORE_ESP = 'OStimExpressionDirector.esp'


def build_core():
    # masters: Skyrim.esm = 00, self = 01
    self_ = 0x01000000
    return plugin(['Skyrim.esm'], [
        ('QUST', [
            quest(self_ | 0x800, 'OSExpressionFacesQuest',
                  [('OSExpressionFaces', [])], 'OSExpressionFacesPlayerAlias'),
            quest(self_ | 0xD63, 'OSED_MCMQuest',
                  [('OSExpressionFacesMCM', [('Director', 1, self_ | 0x800)])], 'SKI_PlayerLoadGameAlias'),
        ]),
    ], description='OStim Expression Director (OSED Reborn build)')


def build_addon(edid_prefix, engine_script, mcm_script, desc):
    # masters: Skyrim.esm = 00 (PlayerRef for the aliases), Core = 01, self = 02
    core, self_ = 0x01000000, 0x02000000
    return plugin(['Skyrim.esm', CORE_ESP], [
        ('QUST', [
            quest(self_ | 0x800, edid_prefix + 'Quest',
                  [(engine_script, [('Core', 1, core | 0x800)])], 'OSED_AddonLoadAlias'),
            quest(self_ | 0x801, edid_prefix + 'MCMQuest',
                  [(mcm_script, [('Engine', 1, self_ | 0x800)])], 'SKI_PlayerLoadGameAlias'),
        ]),
    ], description=desc)


# Order and FormIDs of the vanilla voice types, as in the original OSED_LipSync_VoiceTypes list.
LIPSYNC_VOICE_TYPES = [
    0x13ADC, 0x13ADD, 0x13AE0, 0x13AE3, 0x13ADE, 0x13AE4, 0x13AE5, 0x13AE7, 0x13AE2, 0x13AE1,
    0x13AEB, 0x13AF1, 0x13AF3, 0x13AEF, 0x13AED, 0x13AD1, 0x13AD2, 0xEA267, 0x13AD8, 0x13AD3,
    0xEA266, 0x13AD9, 0x13ADB, 0x13AE6, 0x13AD7, 0x13AD6, 0x13AEA, 0x13AF2, 0x13AEE, 0x13AEC,
    0x13ADA, 0x13AD5, 0x13AD4, 0xAA8D3, 0x1B55F, 0x9843B, 0x9843A,
]

# Voice files are <QuestEDID>_<TopicEDID>_<INFO FormID>_1. The engine truncates long
# EDIDs (quest to 10, topic to 15 chars), so both stay within those lengths.
LIPSYNC_QUEST_EDID = 'OSED_LSQ'
LIPSYNC_BEATS = [  # (name, topic fid, info fid, default list fid, per-voice list fid)
    ('Build', 0x801, 0x80F, 0x805, 0x80A),
    ('Sustain', 0x802, 0x810, 0x806, 0x80B),
    ('Peak', 0x803, 0x811, 0x807, 0x80C),
    ('Climax', 0x804, 0x812, 0x808, 0x80D),
]


def build_lipsync():
    core, self_ = 0x01000000, 0x02000000
    quest_fid = self_ | 0x800
    dials, flsts = [], []
    props = [('Core', 1, core | 0x800)]
    for name, dial, inf, deflist, vlist in LIPSYNC_BEATS:
        topic_edid = 'OSED_LS_' + name
        assert len(LIPSYNC_QUEST_EDID) <= 10 and len(topic_edid) <= 15
        dials.append((custom_topic(self_ | dial, topic_edid, 'OSED Lip-Sync ' + name, quest_fid), self_ | dial,
                      [info(self_ | inf, topic_edid + '_INFO')]))
        flsts.append(formlist(self_ | deflist, 'OSED_LipSync_Default%sTopics' % name, [self_ | dial]))
        props.append(('Default%sTopics' % name, 1, self_ | deflist))
    flsts.append(formlist(self_ | 0x809, 'OSED_LipSync_VoiceTypes', LIPSYNC_VOICE_TYPES))
    props.append(('VoiceTypes', 1, self_ | 0x809))
    for name, dial, inf, deflist, vlist in LIPSYNC_BEATS:
        flsts.append(formlist(self_ | vlist, 'OSED_LipSync_Voice%sTopicLists' % name,
                              [self_ | deflist] * len(LIPSYNC_VOICE_TYPES)))
        props.append(('Voice%sTopicLists' % name, 1, self_ | vlist))
    return plugin(['Skyrim.esm', CORE_ESP], [
        ('DIAL', dials),
        ('QUST', [
            quest(quest_fid, LIPSYNC_QUEST_EDID, [('OSED_LipSync', props)], 'OSED_AddonLoadAlias'),
            quest(self_ | 0x80E, 'OSED_LipSyncMCMQuest',
                  [('OSED_LipSyncMCM', [('Engine', 1, quest_fid)])], 'SKI_PlayerLoadGameAlias'),
        ]),
        ('FLST', flsts),
    ], description='OSED - Lip-Sync (OSED Reborn build)')


TARGETS = [
    ('OSED Core', CORE_ESP, build_core),
    ('OSED Body', 'OSED_Body.esp',
     lambda: build_addon('OSED_Body', 'OSED_Body', 'OSED_BodyMCM', 'OSED - Body (OSED Reborn build)')),
    ('OSED Living Skin', 'OSED_LivingSkin.esp',
     lambda: build_addon('OSED_LivingSkin', 'OSED_LivingSkin', 'OSED_LivingSkinMCM',
                         'OSED - Living Skin (OSED Reborn build)')),
    ('OSED Lip-Sync', 'OSED_LipSync.esp', build_lipsync),
]


def main():
    for mod, esp, fn in TARGETS:
        path = os.path.join(ROOT, 'mods', mod, esp)
        data = fn()
        with open(path, 'wb') as f:
            f.write(data)
        print('%-45s %6d bytes' % (os.path.relpath(path, ROOT), len(data)))


if __name__ == '__main__':
    main()
