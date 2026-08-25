##!/usr/bin/env python3

"""
1) Парсит входную трассу (R/W) и строит 'эталонную' память
   по всем хранилищам кэш/память (состояние после всей трассы).

2) Парсит dump-файлы из каталога (--dump-dir)

3) Проверки:
   1. Cверка с эталонной памятью каждого байта

   2. Для каждой пары W  последующий R по пересекающимся адресам
"""



import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Tuple

# ---------------------------------------------------------------------------
# Trace parser
# ---------------------------------------------------------------------------

@dataclass
class Op:
    kind: str          # 'R' | 'W'
    addr: int
    size: int
    data: bytes

def parse_trace(path: Path) -> List[Op]:
    ops: List[Op] = []
    with path.open(encoding="utf-8") as f:
        for line in f:
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            parts = s.split()
            if not parts:
                continue
            
            op = parts[0].upper()
            if op not in ("R", "W"):
                continue
            
            addr_str = parts[1]
            if addr_str.startswith(("0x", "0X")):
                addr = int(addr_str, 16)
            else:
                addr = int(addr_str, 16)
            
            if op == "R":
                size = int(parts[2]) if len(parts) >= 3 else 4
                ops.append(Op("R", addr, size, b""))
            else:
                data = bytes(int(b, 16) for b in parts[2:])
                ops.append(Op("W", addr, len(data), data))
    return ops


# ---------------------------------------------------------------------------
# Golden memory
# ---------------------------------------------------------------------------

class GoldenMemory:
    def __init__(self):
        self.bytes: Dict[int, int] = {}

    def write(self, addr: int, data: bytes):
        for i, b in enumerate(data):
            self.bytes[addr + i] = b

    def apply_trace(self, ops: List[Op]):
        for op in ops:
            if op.kind == "W":
                self.write(op.addr, op.data)

    def get(self, addr: int) -> int:
        return self.bytes.get(addr, 0)


# ---------------------------------------------------------------------------
# Dump parser
# ---------------------------------------------------------------------------

def words_to_bytes(data_field: str) -> bytes:
    if data_field == "0":
        return b""
    out = bytearray()
    for word in data_field.split("'"):
        if not word:
            continue
        if len(word) % 2:
            word = "0" + word
        for i in range(0, len(word), 2):
            out.append(int(word[i:i+2], 16))
    return bytes(out)

@dataclass
class DumpBlock:
    key: int
    data: bytes

def parse_dump_file(path: Path) -> List[DumpBlock]:
    blocks: List[DumpBlock] = []
    with path.open(encoding="utf-8") as f:
        for line in f:
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            
            km = re.match(r"^(0x[0-9a-fA-F]+)\b", s)
            if not km:
                continue
            key = int(km.group(1), 16)
            
            dm = re.search(r"data=([0-9a-fA-F']+|0)", s)
            if not dm:
                continue
            
            data = words_to_bytes(dm.group(1))
            blocks.append(DumpBlock(key, data))
    return blocks


# ---------------------------------------------------------------------------
# Проверка
# ---------------------------------------------------------------------------

@dataclass
class Stats:
    level: str
    total_bytes: int = 0
    matched: int = 0
    mismatched: int = 0
    errors: List[Tuple[int, int, int]] = field(default_factory=list)

def check_dump_vs_golden(golden: GoldenMemory, dump_dir: Path) -> Dict[str, Stats]:
    stats: Dict[str, Stats] = {}
    
    for state_file in sorted(dump_dir.glob("*.state")):
        level = state_file.stem
        blocks = parse_dump_file(state_file)
        
        stat = Stats(level=level)
        
        for block in blocks:
            for offset, byte_val in enumerate(block.data):
                addr = block.key + offset
                golden_val = golden.get(addr)
                
                stat.total_bytes += 1
                if byte_val == golden_val:
                    stat.matched += 1
                else:
                    stat.mismatched += 1
                    stat.errors.append((addr, byte_val, golden_val))
        
        stats[level] = stat
    
    return stats


def print_stats(stats: Dict[str, Stats]):
    print("\n" + "="*70)
    print("STATISTICS BY CACHE LEVELS")
    print("="*70)
    
    total_errors = 0
    
    for level, stat in sorted(stats.items()):
        print(f"\n[{level}]")
        print(f"  Total bytes: {stat.total_bytes}")
        
        if stat.total_bytes > 0:
            match_pct = stat.matched / stat.total_bytes * 100
            print(f"  Matched:    {stat.matched} ({match_pct:.1f}%)")
            print(f"  Mismatched: {stat.mismatched} ({100 - match_pct:.1f}%)")
        else:
            print("  No data")
        
        if stat.errors:
            total_errors += len(stat.errors)
            print(f"\n  Mismatched addresses:")
            for addr, dump_val, golden_val in stat.errors:
                print(f"    addr=0x{addr:016x} dump=0x{dump_val:02x} golden=0x{golden_val:02x}")
    
    print("\n" + "="*70)
    if total_errors == 0:
        print("ALL DATA MATCHES GOLDEN")
    else:
        print(f"TOTAL MISMATCHES: {total_errors}")
    print("="*70 + "\n")


def main():
    parser = argparse.ArgumentParser(description="Check cache data against golden model")
    parser.add_argument("-t", "--trace", required=True, type=Path, help="trace file")
    parser.add_argument("-d", "--dump-dir", required=True, type=Path, help="directory with *.state files")
    args = parser.parse_args()

    if not args.trace.is_file():
        print(f"Error: trace file not found: {args.trace}", file=sys.stderr)
        return 1
    
    if not args.dump_dir.is_dir():
        print(f"Error: dump directory not found: {args.dump_dir}", file=sys.stderr)
        return 1

    ops = parse_trace(args.trace)
    golden = GoldenMemory()
    golden.apply_trace(ops)
    
    print(f"Trace: {args.trace} ({len(ops)} operations)")
    print(f"Dumps: {args.dump_dir}")
    
    stats = check_dump_vs_golden(golden, args.dump_dir)
    print_stats(stats)
    
    total_errors = sum(s.mismatched for s in stats.values())
    return 1 if total_errors > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
