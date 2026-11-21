/* Resident ELF transfer; all code, control data, and stack remain below 1 MiB.
 * Offsets match HomebrewControl; file copies precede every BSS clear. */
    .section ._launch_elf,"ax",@progbits
    .set noreorder
    .set noat
    .balign 16
    .globl luna_homebrew_transfer
luna_homebrew_transfer:
    lui $sp,0x0010
    addiu $sp,$sp,-64
    move $s0,$a0
    lw $s1,8($s0)
    addiu $s2,$s0,12
    lw $s3,524($s0)
1:
    lw $t0,0($s2)
    addu $t0,$t0,$s3
    lw $t1,4($s2)
    lw $t2,8($s2)
    beqz $t2,5f
    nop
    sltu $t3,$t0,$t1
    beqz $t3,3f
    nop
    addu $t3,$t0,$t2
    sltu $t3,$t1,$t3
    beqz $t3,3f
    nop
    /* memmove backwards when this segment overlaps its own staged bytes. */
    addu $t0,$t0,$t2
    addu $t1,$t1,$t2
2:
    addiu $t0,$t0,-1
    addiu $t1,$t1,-1
    lbu $t3,0($t0)
    sb $t3,0($t1)
    addiu $t2,$t2,-1
    bnez $t2,2b
    nop
    b 5f
    nop
3:
    lbu $t3,0($t0)
    sb $t3,0($t1)
    addiu $t0,$t0,1
    addiu $t1,$t1,1
    addiu $t2,$t2,-1
    bnez $t2,3b
    nop
5:
    addiu $s2,$s2,16
    addiu $s1,$s1,-1
    bnez $s1,1b
    nop
    lw $s1,8($s0)
    addiu $s2,$s0,12
6:
    lw $t0,4($s2)
    lw $t1,8($s2)
    addu $t0,$t0,$t1
    lw $t2,12($s2)
    subu $t2,$t2,$t1
    beqz $t2,8f
    nop
7:
    sb $zero,0($t0)
    addiu $t0,$t0,1
    addiu $t2,$t2,-1
    bnez $t2,7b
    nop
8:
    addiu $s2,$s2,16
    addiu $s1,$s1,-1
    bnez $s1,6b
    nop
    move $a0,$zero
    li $v1,100 /* FlushCache(0) */
    syscall
    nop
    li $a0,2
    li $v1,100 /* FlushCache(2) */
    syscall
    nop
    lw $a0,0($s0)
    lw $a1,4($s0)
    lw $a2,540($s0)
    addiu $a3,$s0,528
    li $v1,7 /* ExecPS2(entry, gp, argc, argv) */
    syscall
    nop
9:
    b 9b
    nop
