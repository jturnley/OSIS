"""Dump the records of a Skyrim SE plugin (uncompressed records only)."""
import struct, sys, zlib

def subrecords(data):
    i, big = 0, None
    while i < len(data):
        sig = data[i:i+4].decode('latin1'); size = struct.unpack_from('<H', data, i+4)[0]; i += 6
        if sig == 'XXXX':
            big = struct.unpack_from('<I', data, i)[0]; i += size; continue
        if big is not None:
            size, big = big, None
        yield sig, data[i:i+size]; i += size

def records(buf, off=0, end=None, depth=0):
    end = len(buf) if end is None else end
    while off < end:
        sig = buf[off:off+4].decode('latin1')
        size, = struct.unpack_from('<I', buf, off+4)
        if sig == 'GRUP':
            label = buf[off+8:off+12]; gtype, = struct.unpack_from('<i', buf, off+12)
            yield ('GRUP', depth, label, gtype)
            yield from records(buf, off+24, off+size, depth+1)
            off += size
        else:
            flags, fid = struct.unpack_from('<II', buf, off+8)
            data = buf[off+24:off+24+size]
            if flags & 0x40000:
                data = zlib.decompress(data[4:])
            yield (sig, depth, flags, fid, data)
            off += 24 + size

def show(path):
    buf = open(path, 'rb').read()
    for r in records(buf):
        if r[0] == 'GRUP':
            print('  ' * r[1] + f'GRUP {r[2]!r} type={r[3]}')
            continue
        sig, depth, flags, fid, data = r
        print('  ' * depth + f'{sig} {fid:08X} flags={flags:08X}')
        for s, d in subrecords(data):
            txt = d[:-1].decode('latin1') if d.endswith(b'\0') and all(32 <= c < 127 for c in d[:-1]) else d.hex()
            if len(txt) > 160: txt = txt[:160] + '...'
            print('  ' * (depth+1) + f'{s} [{len(d)}] {txt}')

if __name__ == '__main__':
    for p in sys.argv[1:]:
        print('=====', p); show(p)
