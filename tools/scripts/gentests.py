# This file will autogenerate float test cases since there are many
# edge cases in a float parser

def msb(x: int):
    n = -1
    while x:
        x >>= 1
        n += 1
    return n

def round_test(i: int, slop_percent: float):
    n = i << 54
    ptn = (msb(n) - 52)
    n += int(slop_percent * (2.0 ** ptn))
    return n

def print_round_test(name_postfix: str, i: int, read: float, written: float):
    print(f'PASS_SETUP(float_{name_postfix}, "{round_test(i, read)}.0", 64)')
    print(f'\tCHECK_FLOAT(0, {round_test(i, written)}.0)')
    print(f'PASS_END')

print('// Round down tests')
for i in range(1, 10):
    print_round_test(f'round_down{i}', i*2, 0.3, 0.0)
print('\n// Round up tests')
for i in range(1, 10):
    print_round_test(f'round_up{i}', i*2, 0.85, 1.0)

