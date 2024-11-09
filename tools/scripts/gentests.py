from decimal import Decimal
import gentbl as tbl
import math

# This file will autogenerate float test cases since there are many
# edge cases in a float parser

def msb(x: int):
    n = -1
    while x:
        x >>= 1
        n += 1
    return n

def print_test(postfix: str, read, written):
    print(f'PASS_SETUP(float_{postfix}, "{read}.0", 64)')
    print(f'\tCHECK_FLOAT(0, {float.hex(written)})')
    print(f'PASS_END')

def round_test(i: int, slop_percent: float):
    n = i << 54
    ptn = (msb(n) - 52)
    n += int(slop_percent * (2.0 ** ptn))
    return n

def print_round_test(name_postfix: str, i: int, read: float, written: float):
    print_test(name_postfix,
               round_test(i, read),
               float(round_test(i, written)))

def tie_test(i: int, add_one: bool, round_down: bool):
    n = i << 54
    ptn = (msb(n) - 52)
    if not round_down: n += 1 << ptn
    if add_one and not round_down:
        n += 1 << ptn
    else:
        n += 1 << (ptn - 1)
    return n

def print_tie_test(round_down: bool, i: int, j: int):
    print_test('tie_' + ('down' if round_down else 'up') + str(i),
               tie_test(j, False, round_down),
               float(tie_test(j, True, round_down)))

print('// Round down tests')
for i in range(1, 10):
    print_round_test(f'round_down{i}', i*2, 0.3, 0.0)
print('\n// Round up tests')
for i in range(1, 10):
    print_round_test(f'round_up{i}', i*2, 0.85, 1.0)
print('\n// Tie to even (down) tests')
for i in range(1, 10):
    print_tie_test(True, i, i)
print('\n// Tie to even (up) tests')
for i in range(1, 10):
    print_tie_test(False, i, i)

