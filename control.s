string:
        orr     w4, w1, 8
        mov     x7, -2314885530818453537
        ldp     x1, x10, [x0, 24]
        movk    x7, 0xdfe0, lsl 0
        ldp     x3, x2, [x0]
        cmp     x1, x10
        cset    x1, ne
        sub     x3, x2, x3
        stp     w3, w4, [x10]
        add     x1, x10, x1, lsl 3
        str     x1, [x0, 32]
        add     x4, x2, 1
        ldr     x1, [x2, 1]
        add     x2, x1, x7
        bic     x2, x2, x1
        tst     x2, -9187201950435737472
        bne     .L3
        mov     x9, 23644
        mov     x8, 41891
        mov     x5, -72340172838076674
        movk    x9, 0x5c5c, lsl 16
        movk    x8, 0xa3a3, lsl 16
        orr     x9, x9, x9, lsl 32
        orr     x8, x8, x8, lsl 32
        movk    x5, 0xfeff, lsl 0
        b       .L2
.L4:
        ldr     x1, [x4, 8]!
        add     x2, x1, x7
        bic     x2, x2, x1
        tst     x2, -9187201950435737472
        bne     .L3
.L2:
        eor     x2, x1, 2459565876494606882
        eor     x3, x1, x9
        eor     x6, x1, -2459565876494606883
        add     x2, x2, x5
        add     x3, x3, x5
        eor     x1, x1, x8
        and     x2, x2, x6
        and     x1, x3, x1
        orr     x1, x2, x1
        tst     x1, -9187201950435737472
        beq     .L4
.L3:
        adrp    x3, .LANCHOR0
        add     x3, x3, :lo12:.LANCHOR0
        mov     w1, 0
.L5:
        ldrb    w2, [x4], 1
        ubfiz   x1, x1, 8, 8
        add     x1, x3, x1
        ldrb    w1, [x1, w2, sxtw]
        cmp     w1, 5
        ble     .L5
        cmp     w1, 7
        str     x4, [x0, 8]
        csel    x0, x10, xzr, eq
        ret
