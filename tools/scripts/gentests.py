# This file will autogenerate float test cases since there are many
# edge cases in a float parser

def gen_rounded_off(seed, offs):
    seed <<= 52
    while seed < (1 << 64):
        seed <<= 1
    seed &= (1 << 64) - 1;
    seed += offs
    return seed

print('// Round down tests')
for i in range(1, 100, 10):
    for j in range(1, 0x800, 694):
        a = gen_rounded_off(i, j)
        b = gen_rounded_off(i, 0)
        print(f'{a} -> {b}')
print('// Round up tests')
for i in range(1, 100, 10):
    for j in range(0x800, 0x1000, 694):
        a = gen_rounded_off(i, j)
        b = gen_rounded_off(i + 1, 0)
        print(f'{a} -> {b}')

