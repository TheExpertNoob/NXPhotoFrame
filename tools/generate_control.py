#!/usr/bin/env python3
# generate_control.py
# Usage: python3 generate_control.py <icon.jpg> <output_dir>

import struct
import sys
import os
import shutil

LANGUAGES = [
    "AmericanEnglish",
    "BritishEnglish",
    "Japanese",
    "French",
    "German",
    "LatinAmericanSpanish",
    "Spanish",
    "Italian",
    "Dutch",
    "CanadianFrench",
    "Portuguese",
    "Russian",
    "Korean",
    "TraditionalChinese",
    "SimplifiedChinese",
    "BrazilianPortuguese",
]

def generate_nacp(output_path):
    nacp = bytearray(0x4000)

    def write_str(offset, s, max_len):
        b = s.encode('utf-8')[:max_len]
        nacp[offset:offset+len(b)] = b

    def write_u64(offset, val):
        struct.pack_into('<Q', nacp, offset, val)

    def write_u32(offset, val):
        struct.pack_into('<I', nacp, offset, val)

    def write_u8(offset, val):
        nacp[offset] = val

    # Title entries (0x300 * 16 = 0x3000)
    name   = "NX Photo Frame"
    author = "Friar Pooh"
    for i in range(16):
        base = i * 0x300
        write_str(base,         name,   0x200)
        write_str(base + 0x200, author, 0x100)

    # SupportedLanguageFlag @ 0x302C — bits 0-15, all 16 language entries
    write_u32(0x302C, 0x0000FFFF)

    # StartupUserAccount @ 0x3025 = 1 (Required)
    write_u8(0x3025, 1)

    # Screenshot @ 0x3034 = 0 (Allow)
    write_u8(0x3034, 0)

    # VideoCapture @ 0x3035 = 1 (Manual)
    write_u8(0x3035, 1)

    # PresenceGroupId @ 0x3038
    write_u64(0x3038, 0x0100BAEFAE420000)

    # DisplayVersion @ 0x3060
    write_str(0x3060, "1.0.2", 0x10)

    # AddOnContentBaseId @ 0x3070 (TitleID + 0x1000)
    write_u64(0x3070, 0x0100BAEFAE421000)

    # SaveDataOwnerId @ 0x3078
    write_u64(0x3078, 0x0100BAEFAE420000)

    # UserAccountSaveDataSize @ 0x3080 — 64MB
    write_u64(0x3080, 0x4000000)

    # UserAccountSaveDataJournalSize @ 0x3088 — 4MB
    write_u64(0x3088, 0x400000)

    # UserAccountSaveDataSizeMax @ 0x3148 — 64MB
    write_u64(0x3148, 0x4000000)

    # UserAccountSaveDataJournalSizeMax @ 0x3150 — 4MB
    write_u64(0x3150, 0x400000)

    # LogoType @ 0x30F0 = 0 (LicensedByNintendo)
    write_u8(0x30F0, 0)

    # LogoHandling @ 0x30F1 = 0 (Auto)
    write_u8(0x30F1, 0)

    # CrashReport @ 0x30F6 = 1 (Allow)
    write_u8(0x30F6, 1)

    # JitConfiguration @ 0x33B0
    # Flag = 0 (None), MemorySize = 64MB
    write_u64(0x33B0, 0x0000000000000000)
    write_u64(0x33B8, 0x0000000004000000)

    with open(output_path, 'wb') as f:
        f.write(nacp)
    print(f"NACP written to {output_path}")

def generate_control_romfs(icon_path, output_dir):
    os.makedirs(output_dir, exist_ok=True)

    generate_nacp(os.path.join(output_dir, "control.nacp"))

    for lang in LANGUAGES:
        dst = os.path.join(output_dir, f"icon_{lang}.dat")
        shutil.copy(icon_path, dst)
        print(f"Written {dst}")

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: generate_control.py <icon.jpg> <output_dir>")
        sys.exit(1)
    generate_control_romfs(sys.argv[1], sys.argv[2])