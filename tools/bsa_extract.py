"""List or extract files from a Skyrim SE BSA (version 105). Pure Python, including LZ4.

Usage:
  python tools/bsa_extract.py <archive.bsa> list [substring]
  python tools/bsa_extract.py <archive.bsa> extract <out_dir> <substring>...
"""
import os
import struct
import sys


def lz4_block(src, out):
    i = 0
    n = len(src)
    while i < n:
        token = src[i]; i += 1
        lit = token >> 4
        if lit == 15:
            while True:
                b = src[i]; i += 1
                lit += b
                if b != 255:
                    break
        out += src[i:i + lit]; i += lit
        if i >= n:
            break
        off = src[i] | (src[i + 1] << 8); i += 2
        ml = token & 15
        if ml == 15:
            while True:
                b = src[i]; i += 1
                ml += b
                if b != 255:
                    break
        ml += 4
        start = len(out) - off
        for k in range(ml):  # byte-wise: matches may overlap
            out.append(out[start + k])
    return out


def lz4_frame(data):
    assert data[:4] == b'\x04\x22\x4d\x18', 'not an LZ4 frame'
    flg = data[4]
    i = 6  # magic, FLG, BD
    if flg & 0x08:
        i += 8  # content size
    if flg & 0x01:
        i += 4  # dictionary id
    i += 1  # header checksum
    out = bytearray()
    while True:
        size, = struct.unpack_from('<I', data, i); i += 4
        if size == 0:
            break
        raw = size & 0x80000000
        size &= 0x7FFFFFFF
        block = data[i:i + size]; i += size
        if raw:
            out += block
        else:
            lz4_block(block, out)
        if flg & 0x10:
            i += 4  # block checksum
    return bytes(out)


def read_bsa(path):
    f = open(path, 'rb')
    hdr = f.read(36)
    magic, ver, folder_off, aflags, nfolders, nfiles, fnlen, filnlen, fileflags = struct.unpack('<4sIIIIIIIH', hdr[:34])
    assert magic == b'BSA\0' and ver == 105, (magic, ver)
    f.seek(folder_off)
    folders = [struct.unpack('<QIIQ', f.read(24)) for _ in range(nfolders)]
    entries = []
    for h, count, _pad, _off in folders:
        name = ''
        if aflags & 1:
            ln = f.read(1)[0]
            name = f.read(ln)[:-1].decode('cp1252')
        for _ in range(count):
            fh, size, off = struct.unpack('<QII', f.read(16))
            entries.append([name, None, size, off])
    names = f.read(filnlen).split(b'\0')
    for e, nm in zip(entries, names):
        e[1] = nm.decode('cp1252')
    return f, aflags, entries


def extract(f, aflags, entry):
    folder, name, size, off = entry
    compressed = bool(aflags & 4) ^ bool(size & 0x40000000)
    size &= 0x3FFFFFFF
    f.seek(off)
    if aflags & 0x100:
        ln = f.read(1)[0]
        f.read(ln)
        size -= ln + 1
    data = f.read(size)
    if compressed:
        data = lz4_frame(data[4:])
    return data


def main():
    path, cmd = sys.argv[1], sys.argv[2]
    f, aflags, entries = read_bsa(path)
    if cmd == 'list':
        sub = sys.argv[3].lower() if len(sys.argv) > 3 else ''
        for folder, name, size, off in entries:
            full = folder + '\\' + name
            if sub in full.lower():
                print(full)
    elif cmd == 'extract':
        out_dir, subs = sys.argv[3], [s.lower() for s in sys.argv[4:]]
        for e in entries:
            full = e[0] + '\\' + e[1]
            if any(s in full.lower() for s in subs):
                data = extract(f, aflags, e)
                dest = os.path.join(out_dir, *full.split('\\'))
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                open(dest, 'wb').write(data)
                print('%-70s %7d bytes' % (full, len(data)))


if __name__ == '__main__':
    main()
