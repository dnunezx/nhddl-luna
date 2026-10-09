/* Original relocatable transfer. No external code/data/stack after entry.
 * Offsets match LunaPsxTransfer and are asserted by the C caller.
 * SDK _ExecPS2 and FlushCache use EE syscalls 7 and 100 respectively. */
    .text
    .set noreorder
    .set noat
    .balign 16
    .globl luna_psx_stub_start
luna_psx_stub_start:
    lui $sp,0x0141
    addiu $sp,$sp,-64
    move $s0,$a0
    /* Erase only frontend user RAM; preserve stage, plan, args and this stub. */
    lui $t0,0x0010
    lui $t1,0x0100
1:
    sq $zero,0($t0)
    sq $zero,16($t0)
    sq $zero,32($t0)
    sq $zero,48($t0)
    addiu $t0,$t0,64
    bne $t0,$t1,1b
    nop
    lw $s1,4($s0)
    addiu $s2,$s0,8
2:
    lw $t0,0($s2)
    lui $t1,0x0100
    addu $t0,$t0,$t1
    lw $t2,4($s2)
    lw $t3,8($s2)
    lw $8,12($s2)
    subu $8,$8,$t3
3:
    beqz $t3,4f
    nop
    lbu $9,0($t0)
    sb $9,0($t2)
    addiu $t0,$t0,1
    addiu $t2,$t2,1
    addiu $t3,$t3,-1
    b 3b
    nop
4:
    beqz $8,5f
    nop
    sb $zero,0($t2)
    addiu $t2,$t2,1
    addiu $8,$8,-1
    b 4b
    nop
5:
    addiu $s2,$s2,16
    addiu $s1,$s1,-1
    bnez $s1,2b
    nop
    move $a0,$zero
    li $v1,100
    syscall
    nop
    li $a0,2
    li $v1,100
    syscall
    nop
    lw $a0,0($s0)
    move $a1,$zero /* Child crt0 initializes gp. */
    lw $a2,136($s0)
    addiu $a3,$s0,140
    li $v1,7
    syscall
    nop
6:
    b 6b
    nop
    .globl luna_psx_stub_end
luna_psx_stub_end:
