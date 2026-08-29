##!/usr/bin/env python3


import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

# ---------------------------------------------------------------------------
# Trace parser
# ---------------------------------------------------------------------------

# Структура данных, которую возвращает парсер
@dataclass
class Op:
    index: int           # номер операции в трассе (начиная с 1)
    kind: str            # 'R' | 'W'
    addr: int
    size: int
    data: bytes

# Парсер входных данных
def parse_trace(path: Path) -> List[Op]:
    ops: List[Op] = []
    with path.open(encoding="utf-8") as f:
        for idx, line in enumerate(f, 1):
            s = line.strip()
            # Пропуск комментариев и пустых строк 
            if not s or s.startswith("#"):
                continue
             
            parts = s.split()
            # Пропуск пустых списков
            if not parts:
                continue
             
            op = parts[0].upper()
            # Пропуск неправильных операций
            if op not in ("R", "W"):
                continue
             
            addr_str = parts[1]
            addr = int(addr_str, 16)
             
            if op == "R":
                size = int(parts[2])
                ops.append(Op(idx, "R", addr, size, b""))
            else:
                data = bytearray(int(b, 16) for b in parts[2:])
                data.reverse()
                ops.append(Op(idx, "W", addr, len(data), bytes(data)))
    return ops

# Состояние одного блока эталонной памяти
@dataclass
class BlockState:
    block_size: int
    # Данные блока. None, если байт не инициализирован
    data: List[Optional[int]] = field(default_factory=list)
    # Первая операция, инициализировавшая блок
    init_op: Optional[int] = None          # запрос, инициализировавший блок
    # Операции чтения и записи
    read_ops: Set[int] = field(default_factory=set)   # {op_index: size}
    write_ops: Set[int] = field(default_factory=set) # {op_index: data}
    
    def __post_init__(self):
        if not self.data:
            self.data = [None] * self.block_size

# Эталонная память
class GoldenMemory:
    def __init__(self, block_size: int = 64):
        self.block_size = block_size
        # Данные блока {addr: BlockState}
        self.blocks: Dict[int, BlockState] = {}

    # Адресс блока
    def _get_block_addr(self, addr: int) -> int:
        return (addr // self.block_size) * self.block_size

    # Создать или получить блок
    def _get_or_create_block(self, addr: int) -> BlockState:
        block_addr: int = self._get_block_addr(addr)
        if block_addr not in self.blocks:
            self.blocks[block_addr] = BlockState(block_size=self.block_size)
        return self.blocks[block_addr]

    # Обработка записи 
    def write(self, addr: int, data: bytes, op_index: int) -> None:
        block_addr: int = self._get_block_addr(addr)
        offset: int = addr - block_addr
        data_len: int = len(data)
        
        # Текущая позиция в данных
        pos: int = 0
        
        # Пока есть данные для записи
        while pos < data_len:
            # Получаем текущий блок
            block = self._get_or_create_block(block_addr)
            
            # Вычисляем сколько байт можно записать в текущий блок
            # (до конца блока)
            available: int = self.block_size - offset
            chunk_size: int = min(data_len - pos, available)
            
            # Записываем чанк данных
            for i in range(chunk_size):
                block.data[offset + i] = data[pos + i]
            
            # Обновляем метаданные блока
            if block.init_op is None:
                block.init_op = op_index
            block.write_ops.add(op_index)
            
            # Переходим к следующему блоку
            pos += chunk_size
            block_addr += self.block_size
            offset = 0    

    # Обработка чтения
    def read(self, addr: int, size: int, op_index: int):
        result: bytearray = bytearray()
        current_addr: int = addr
        bytes_left: int = size
        
        while bytes_left > 0:
            # Выравниваем адрес до границы блока
            block_addr: int = self._get_block_addr(current_addr)
            
            offset: int = current_addr - block_addr
            
            # Сколько байт можно прочитать из этого блока
            bytes_to_read: int = min(bytes_left, self.block_size - offset)
            
            block: BlockState = self._get_or_create_block(block_addr)
            
            # Отмечаем операцию чтения и инициализацию блока
            if block.init_op is None:
                block.init_op = op_index
            block.read_ops.add(op_index)
            
            # Читаем данные из блока
            for i in range(bytes_to_read):
                value = block.data[offset + i]
                # Возвращаем 0 для неинициализированных байтов
                # но не заполняем их
                result.append(value if value is not None else 0)
            
            # Переходим к следующей части данных
            bytes_left -= bytes_to_read
            current_addr += bytes_to_read
        
        return bytes(result)


    # Моделирование записи 
    def apply_trace(self, ops: List[Op]):
        for op in ops:
            if op.kind == "W":
                self.write(op.addr, op.data, op.index)
            else:
                self.read(op.addr, op.size, op.index)

    # Получить значение байта
    def get(self, addr: int) -> int:
        block_addr: int = self._get_block_addr(addr)
        if block_addr not in self.blocks:
            return 0
        offset = addr - block_addr
        block = self.blocks[block_addr]
        if offset < len(block.data):
            value = block.data[offset]
            return value if value is not None else 0
        return 0

    # Получить блок или None
    def get_block(self, block_addr: int) -> Optional[BlockState]:
        return self.blocks.get(block_addr)
    
    # На какой операции блок инициализирован
    def get_init_op(self, block_addr: int) -> Optional[int]:
        block = self.blocks.get(block_addr)
        return block.init_op if block else None
    
    # Все операции чтения
    def get_read_ops(self, block_addr: int) -> Set[int]:
        block = self.blocks.get(block_addr)
        return block.read_ops if block else set()
    
    # Все операции записи
    def get_write_ops(self, block_addr: int) -> Set[int]:
        block = self.blocks.get(block_addr)
        return block.write_ops if block else set()
    
    # Получить данные блока
    def get_block_data(self, block_addr: int) -> bytes:
        block = self.blocks.get(block_addr)
        if not block:
            return bytes(self.block_size)
        return bytes(v if v is not None else 0 for v in block.data)
    
    # инициализирован ли адрес
    def is_initialized(self, addr: int) -> bool:
        block_addr = self._get_block_addr(addr)
        if block_addr not in self.blocks:
            return False
        offset = addr - block_addr
        block = self.blocks[block_addr]
        if offset < len(block.data):
            return block.data[offset] is not None
        return False
    
    # Получить список блоков
    def get_blocks(self) -> Dict[int, BlockState]:
        return self.blocks
# ---------------------------------------------------------------------------
# Dump parser
# ---------------------------------------------------------------------------

# Перевод входных текствых данных в память
def words_to_bytes(data_field: str) -> bytes:
    if data_field == "0":
        return b""
    
    # Просто удаляем разделители '
    hex_string = data_field.replace("'", "")
    
    # Если длина нечетная, дополняем нулем слева
    if len(hex_string) % 2:
        hex_string = "0" + hex_string
    
    # Превращаем hex в байты
    out = bytearray()
    for i in range(0, len(hex_string), 2):
        out.append(int(hex_string[i:i+2], 16))

    out.reverse()
    return bytes(out)

# Набор данных для вывода 
@dataclass
class DumpBlock:
    key: int
    data: bytes

# Парсер dump-а
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


# Проверка dump vs golden (существующий режим)

# Данные статистики
@dataclass
class Stats:
    level: str
    total_bytes: int = 0
    matched: int = 0
    mismatched: int = 0
    errors: List[Tuple[int, int, int]] = field(default_factory=list)

# Сравнение эталонной памяти с dump-ом
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


# Вывод статистики
def print_stats(stats: Dict[str, Stats]) -> None:
    print("\n" + "="*70)
    print("STATISTICS BY CACHE LEVELS (dump vs golden)")
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
            error_blocks = {}
            for addr, dump_val, golden_val in stat.errors:
                base = addr & ~0x3
                offset = addr - base
                error_blocks.setdefault(base, []).append((offset, dump_val, golden_val))

            print(f"\n  Mismatched blocks (showing first 20):")
            for base, errs in sorted(error_blocks.items())[:20]:
                dump_bytes = bytearray(4)
                golden_bytes = bytearray(4)
                for off, d, g in errs:
                    dump_bytes[off] = d
                    golden_bytes[off] = g
                dump_hex = ''.join(f'{b:02x}' for b in dump_bytes)
                golden_hex = ''.join(f'{b:02x}' for b in golden_bytes)
                print(f"    0x{base:016x}: dump={dump_hex} golden={golden_hex}")
            if len(error_blocks) > 20:
                print(f"    ... and {len(error_blocks)-20} more blocks")

    print("\n" + "="*70)
    if total_errors == 0:
        print("ALL DATA MATCHES GOLDEN")
    else:
        print(f"TOTAL MISMATCHED BYTES: {total_errors}")
    print("="*70 + "\n")

# Сравнение данных в dump-ах и эталонной памяти
def analyze_distribution(golden: GoldenMemory, dumps: Dict[str, List[DumpBlock]]) -> Dict[int, Dict[str, str]]:
    level_data = {}
    for level, blocks in dumps.items():
        m = {}
        for block in blocks:
            for offset, val in enumerate(block.data):
                m[block.key + offset] = val
        level_data[level] = m

    distribution = {}
    for block_addr, block in golden.get_blocks().items():
        for offset, value in enumerate(block.data):
            if value is not None:
                addr = block_addr + offset
                status = {}
                # Сравниваем и указываем для кождого байта статус
                for level, data in level_data.items():
                    if addr in data:
                        if data[addr] == value:
                            status[level] = "CURRENT"
                        else:
                            status[level] = "STALE"
                    else:
                        status[level] = "ABSENT"
                distribution[addr] = status
    return distribution


# вывод статусов данных
def print_distribution(golden: GoldenMemory, distribution: Dict[int, Dict[str, str]]) -> None:
    print("\n" + "="*70)
    print("DATA DISTRIBUTION BY CACHE LEVELS (final state)")
    print("="*70)

    if not distribution:
        print("No data in golden memory (no writes in trace)")
        return

    blocks = {}
    # Записываем статусы и данные по адресу
    for addr, statuses in distribution.items():
        base = addr & ~0x3
        offset = addr - base
        blocks.setdefault(base, []).append((offset, golden.get(addr), statuses))

    for base, entries in sorted(blocks.items()):
        if not entries:
            continue
        levels = sorted(entries[0][2].keys())
        print(f"0x{base:016x}:")
        # Формируем вывод
        for level in levels:
            vals = []
            for off, val, stat in entries:
                st = stat.get(level, "ABSENT")
                if st == "CURRENT":
                    vals.append(f"{val:02x}")
                elif st == "STALE":
                    vals.append(f"({val:02x})")
                else:
                    vals.append("--")
            line = f"  {level}: "
            for i in range(0, len(vals), 4):
                if i > 0:
                    line += "'"
                line += ''.join(vals[i:i+4])
            print(line)
        print()


# Отчет по записям
def print_write_report(ops: List[Op], golden: GoldenMemory):
    writes = [op for op in ops if op.kind == "W"]
    if not writes:
        print("\nNo write operations in trace.")
        return

    print("\n" + "="*70)
    print("WRITE OPERATIONS REPORT")
    print("="*70)

    for w in writes:
        print(f"W #{w.index}: addr=0x{w.addr:x} size={w.size} data={w.data.hex()}")
        
        for off, b in enumerate(w.data):
            addr = w.addr + off
            current_val = golden.get(addr)
            block_addr = golden._get_block_addr(addr)
            later_writes = [op_idx for op_idx in golden.get_write_ops(block_addr) if op_idx > w.index]
            
            if later_writes:
                print(f"  byte 0x{addr:x} ({b:02x}) -> OVERWRITTEN by W #{later_writes[0]}")
            else:
                if current_val == b:
                    status = "CURRENT"
                else:
                    status = f"STALE (current={current_val:02x})"
                print(f"  byte 0x{addr:x} ({b:02x}) -> {status}")
        
        print()


# ============================================================================
# Read report
# ============================================================================

def print_read_report(ops: List[Op], golden: GoldenMemory):
    reads = [op for op in ops if op.kind == "R"]
    if not reads:
        print("\nNo read operations in trace.")
        return

    print("\n" + "="*70)
    print("READ OPERATIONS REPORT")
    print("="*70)

    temp_mem = GoldenMemory(golden.block_size)
    
    for op in ops:
        if op.kind == "W":
            temp_mem.write(op.addr, op.data, op.index)
        else:
            expected = temp_mem.read(op.addr, op.size, op.index)
            
            last_write = None
            for w in ops[:op.index]:
                if w.kind == "W":
                    start = max(op.addr, w.addr)
                    end = min(op.addr + op.size, w.addr + w.size)
                    if start < end:
                        last_write = w.index
            
            later_overwrite = False
            for later_op in ops[op.index:]:
                if later_op.kind == "W":
                    start = max(op.addr, later_op.addr)
                    end = min(op.addr + op.size, later_op.addr + later_op.size)
                    if start < end:
                        later_overwrite = True
                        break
            
            print(f"R #{op.index}: addr=0x{op.addr:x} size={op.size}")
            print(f"  Expected: {expected.hex()}")
            if last_write:
                print(f"  Source: W #{last_write}")
            else:
                print(f"  Source: initial (zeros)")
            if later_overwrite:
                print("  Later overwritten.")
            else:
                print("  Not overwritten later.")
            print()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Check cache data against golden model")
    parser.add_argument("-t", "--trace", required=True, type=Path, help="trace file")
    parser.add_argument("-d", "--dump-dir", required=True, type=Path, help="directory with *.state files")
    parser.add_argument("--block-size", type=int, default=64, help="block size for golden memory (default: 64)")
    args = parser.parse_args()

    if not args.trace.is_file():
        print(f"Error: trace file not found: {args.trace}", file=sys.stderr)
        return 1

    if not args.dump_dir.is_dir():
        print(f"Error: dump directory not found: {args.dump_dir}", file=sys.stderr)
        return 1

    ops = parse_trace(args.trace)
    golden = GoldenMemory(block_size=args.block_size)
    golden.apply_trace(ops)

    print(f"Trace: {args.trace} ({len(ops)} operations)")
    print(f"Dumps: {args.dump_dir}")
    print(f"Block size: {args.block_size}")

    # 1. Статистика dump vs golden
    stats = check_dump_vs_golden(golden, args.dump_dir)
    print_stats(stats)

    # 2. Распределение по уровням
    dumps = {}
    for state_file in args.dump_dir.glob("*.state"):
        level = state_file.stem
        blocks = parse_dump_file(state_file)
        dumps[level] = blocks
    
    if dumps:
        dist = analyze_distribution(golden, dumps)
        print_distribution(golden, dist)
    else:
        print("No dump files found for distribution analysis.")

    # 3. Отчёт по записям
    print_write_report(ops, golden)

    # 4. Отчёт по чтениям
    print_read_report(ops, golden)

    total_errors = sum(s.mismatched for s in stats.values())
    if total_errors == 0:
        print("\nAll checks passed.")
    else:
        print(f"\nErrors found: {total_errors}")
    return 1 if total_errors > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
