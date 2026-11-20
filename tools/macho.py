#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Wrap the ordinary raw m1n1 image for a Mach-O handoff without SPTM.

The raw entry receives BootArgs in x0 and derives its runtime base itself.
The preferred virtual addresses below describe the container, not a physical
load address. The complete raw image stays contiguous in __TEXT_EXEC.
"""
import argparse
import hashlib
from pathlib import Path
import plistlib
import struct

PAGE_SIZE = 0x4000
PREFERRED_BASE = 0xfffffe0007004000
RAW_ENTRY = 0x800


def segment(name, offset, size, protection, section=None):
    """Emit one file-backed segment, optionally with a single section."""
    sections = b''
    if section is not None:
        section_name, section_size, flags = section
        sections = struct.pack(
            '<16s16sQQ8I', section_name.encode(), name.encode(),
            PREFERRED_BASE+offset, section_size, offset, 14, 0, 0, flags, 0, 0, 0)
    return struct.pack(
        '<II16sQQQQ4I', 0x19, 72+len(sections), name.encode(),
        PREFERRED_BASE+offset, size, offset, size,
        protection, protection, int(section is not None), 0)+sections


def build(raw):
    if len(raw) < RAW_ENTRY+4:
        raise ValueError('Raw image does not contain the m1n1 entry point')
    if len(raw) > 0xffffffff-5*PAGE_SIZE:
        raise ValueError('Raw image exceeds Mach-O section offset limits')
    raw_size = (len(raw)+PAGE_SIZE-1) & -PAGE_SIZE
    info_offset = PAGE_SIZE+raw_size
    data_offset = info_offset+PAGE_SIZE
    link_offset = data_offset+PAGE_SIZE
    identity = hashlib.sha256(raw).digest()[:16]
    info = plistlib.dumps({'_PrelinkInfoDictionary': [], '_PrelinkKCID': identity},
                          fmt=plistlib.FMT_XML, sort_keys=True)+b'\0'
    if len(info) > PAGE_SIZE:
        raise ValueError('Prelink information exceeds its reserved page')

    # kmutil requires prelink and linkedit metadata even with no extensions.
    # Keep it after the raw image so its internal offsets remain unchanged.
    commands = [
        segment('__TEXT', 0, PAGE_SIZE, 1),
        segment('__TEXT_EXEC', PAGE_SIZE, raw_size, 5,
                ('__text', len(raw), 0x80000400)),
        segment('__PRELINK_INFO', info_offset, PAGE_SIZE, 3,
                ('__info', len(info), 0)),
        segment('__DATA', data_offset, PAGE_SIZE, 3),
        segment('__LINKEDIT', link_offset, PAGE_SIZE, 1),
        struct.pack('<II16s', 0x1b, 24, identity),  # LC_UUID
    ]
    # ARM_THREAD_STATE64: x16 records the vector address, PC the raw entry.
    registers = [0]*34
    registers[16] = PREFERRED_BASE+PAGE_SIZE
    registers[32] = PREFERRED_BASE+PAGE_SIZE+RAW_ENTRY
    commands.append(struct.pack('<4I34Q', 5, 288, 6, 68, *registers))
    # Generic ARM64 MH_EXECUTE with flags zero. Omitting __TEXT_BOOT_EXEC
    # selects the handoff used by the ordinary raw startup.
    header = struct.pack('<8I', 0xfeedfacf, 0x100000c, 0, 2, len(commands),
                         sum(map(len, commands)), 0, 0)+b''.join(commands)
    if len(header) > PAGE_SIZE:
        raise ValueError('Mach-O header exceeds its reserved page')
    image = bytearray(link_offset+PAGE_SIZE)
    image[:len(header)] = header
    image[PAGE_SIZE:PAGE_SIZE+len(raw)] = raw
    image[info_offset:info_offset+len(info)] = info
    return bytes(image)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('raw', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.write_bytes(build(args.raw.read_bytes()))


if __name__ == '__main__':
    main()
