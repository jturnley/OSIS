"""Minimal Skyrim VMAD reader (object format 2) for checking plugins."""
import struct

class R:
    def __init__(s, b): s.b, s.i = b, 0
    def u8(s): v = s.b[s.i]; s.i += 1; return v
    def u16(s): v, = struct.unpack_from('<H', s.b, s.i); s.i += 2; return v
    def i16(s): v, = struct.unpack_from('<h', s.b, s.i); s.i += 2; return v
    def u32(s): v, = struct.unpack_from('<I', s.b, s.i); s.i += 4; return v
    def i32(s): v, = struct.unpack_from('<i', s.b, s.i); s.i += 4; return v
    def f32(s): v, = struct.unpack_from('<f', s.b, s.i); s.i += 4; return v
    def s(s): n = s.u16(); v = s.b[s.i:s.i+n].decode('latin1'); s.i += n; return v
    def left(s): return len(s.b) - s.i

def obj(r):
    r.u16(); alias = r.i16(); fid = r.u32(); return ('obj', fid, alias)

def value(r, t):
    return {1: lambda: obj(r), 2: r.s, 3: r.i32, 4: r.f32, 5: lambda: bool(r.u8())}[t]() if t < 10 else \
        [value(r, t - 10) for _ in range(r.u32())]

def scripts(r):
    out = []
    for _ in range(r.u16()):
        name = r.s(); status = r.u8(); props = []
        for _ in range(r.u16()):
            pn = r.s(); t = r.u8(); st = r.u8(); props.append((pn, t, value(r, t)))
        out.append((name, props))
    return out

def parse_quest(b):
    r = R(b); ver = r.i16(); fmt = r.i16(); assert fmt == 2
    res = {'scripts': scripts(r), 'fragments': None, 'aliases': []}
    if r.left():
        fv = r.u8(); n = r.u16(); fn = r.s()
        frags = []
        for _ in range(n):
            frags.append((r.u16(), r.i16(), r.i32(), r.u8(), r.s(), r.s()))
        res['fragments'] = (fv, fn, frags)
        for _ in range(r.u16()):
            o = obj(r); r.i16(); r.i16()
            res['aliases'].append((o, scripts(r)))
    assert r.left() == 0, r.left()
    return res
