##!/usr/bin/env python3

import random
import sys
from pathlib import Path

# Генерация случайного числа входных байтов
def random_bytes(size, rng):
    return bytes(rng.randint(0, 255) for _ in range(size))

# Генерация запроса на запись
def W(addr, data):
    hex_data = ' '.join(f'{b:02x}' for b in data)
    return f'W 0x{addr:x} {hex_data}'

# Генерация запроса на чтение
def R(addr, size=4):
    return f'R 0x{addr:x} {size}'

# Генерация набора зарпосов
def generate_trace(seed=42, ops=10, block_size=64, num_blocks=8):

    rng = random.Random(seed)
    # Размер адресного пространства запросов
    addr_space = block_size * num_blocks
    
    trace = []
    trace.append(f'# Test trace (seed={seed}, ops={ops}, block_size={block_size})')
    trace.append('')
    
    # Активные адреса для повторных обращений
    active_addresses = []
    # История записей для проверки
    write_history = {}
    
    for i in range(ops):
        # Выбираем тип операции: запись или чтение
        # 40% записи, 60% чтения
        is_write = rng.random() < 0.4
        
        # Стратегия выбора адреса:
        # - 30% обращение к недавно использованному адресу
        # - 30% обращение к случайному адресу
        # - 20% обращение к адресу, который только что записали
        # - 20% обращение к адресу на границе блока
        if active_addresses and rng.random() < 0.3:
            # Используем существующий адрес
            addr = rng.choice(active_addresses)
        elif write_history and rng.random() < 0.2:
            # Проверяем недавно записанные данные
            addr = rng.choice(list(write_history.keys()))
        else:
            max_size = rng.choice([1, 2, 4, 8, 16])
            addr = rng.randint(0, addr_space - max_size)
            # Иногда делаем адрес выровненным
            if rng.random() < 0.5:
                align = rng.choice([4, 8, 16])
                addr = (addr // align) * align
        
        # Корректируем адрес, чтобы не выйти за границы
        max_size = rng.choice([1, 2, 4, 8, 16, 32])
        if addr + max_size > addr_space:
            addr = addr_space - max_size
            # Исключение ошибок
            if addr < 0:
                addr = 0
        
        # Выбираем размер доступа
        size = rng.choice([1, 2, 4, 8, 16, 32])
        if addr + size > addr_space:
            size = addr_space - addr
            if size <= 0:
                size = 4
        
        # Для разнообразия делаем невыровненные адреса
        if rng.random() < 0.2 and size > 1:
            # Смещаем адрес, чтобы он не был выровнен
            offset = rng.randint(1, min(3, size - 1))
            if addr + offset + size < addr_space:
                addr += offset
        
        # Генерируем запрос
        if is_write:
            data = random_bytes(size, rng)
            trace.append(W(addr, data))
            write_history[addr] = data
            if addr not in active_addresses:
                active_addresses.append(addr)
        else:
            trace.append(R(addr, size))
            if addr not in active_addresses:
                active_addresses.append(addr)
        
        # После записи часто добавляем чтение того же адреса
        if is_write and rng.random() < 0.6:
            # Читаем с тем же или другим размером
            read_size = rng.choice([size, size // 2, size * 2])
            if read_size <= 0:
                read_size = 1
            if addr + read_size <= addr_space:
                trace.append(R(addr, read_size))
        
        # Иногда делаем дополнительное обращение к активному адресу
        if active_addresses and rng.random() < 0.15:
            addr2 = rng.choice(active_addresses)
            size2 = rng.choice([1, 2, 4, 8])
            if addr2 + size2 <= addr_space:
                if rng.random() < 0.3:
                    # Запись в активный адрес
                    data = random_bytes(size2, rng)
                    trace.append(W(addr2, data))
                    write_history[addr2] = data
                else:
                    # Чтение активного адреса
                    trace.append(R(addr2, size2))
        
        # Периодически читаем из истории записей
        if write_history and rng.random() < 0.1:
            addr3 = rng.choice(list(write_history.keys()))
            size3 = rng.choice([1, 2, 4])
            if addr3 + size3 <= addr_space:
                trace.append(R(addr3, size3))
    
    trace.append('')
    trace.append('# End of trace')
    
    return trace

def main():
    import argparse
    
    # Парсер ключей при запуске
    parser = argparse.ArgumentParser(
        description='Input request generator',
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument('-o', '--output', default='-', 
                       help='Output file (by default: stdout)')
    parser.add_argument('--seed', type=int, default=42,
                       help='Seed for generator (by default: 42)')
    parser.add_argument('--ops', type=int, default=10,
                       help='Number of operations (by default: 10)')
    parser.add_argument('--block-size', type=int, default=64,
                       help='Block size (by default: 64)')
    parser.add_argument('--num-blocks', type=int, default=8,
                       help='Number of blocks (by default: 8)')
    
    args = parser.parse_args()
    
    trace = generate_trace(
        seed=args.seed,
        ops=args.ops,
        block_size=args.block_size,
        num_blocks=args.num_blocks
    )
    
    # формирование строки запроса
    output = '\n'.join(trace) + '\n'
    
    # Проверка наличия выходного файла
    if args.output == '-':
        sys.stdout.write(output)
    else:
        path = Path(args.output)
        path.parent.mkdir(parents=True, exist_ok=True)
        with open(path, 'w') as f:
            f.write(output)
        print(f'[OK] Generated {len(trace)} lines -> {path}', file=sys.stderr)

if __name__ == '__main__':
    main()
