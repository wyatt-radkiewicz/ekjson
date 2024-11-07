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

print('// Round down tests')
for i in range(1, 10, 2):
    print(f'{round_test(i, 0.3)} -> {round_test(i, 0.0)}')
print('\n// Round up tests')
for i in range(1, 10, 2):
    print(f'{round_test(i, 0.85)} -> {round_test(i, 1.0)}')

