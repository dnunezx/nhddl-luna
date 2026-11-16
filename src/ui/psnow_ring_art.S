    .section .rodata
    .balign 16
    .global lunaPsnowRingData
lunaPsnowRingData:
    .incbin "assets/psnow-ring.t8z"
lunaPsnowRingEnd:
    .balign 4
    .global lunaPsnowRingSize
lunaPsnowRingSize:
    .word lunaPsnowRingEnd-lunaPsnowRingData
    .balign 16
    .global lunaPsnowSmallRingData
lunaPsnowSmallRingData:
    .incbin "assets/psnow-small-ring.t8z"
lunaPsnowSmallRingEnd:
    .balign 4
    .global lunaPsnowSmallRingSize
lunaPsnowSmallRingSize:
    .word lunaPsnowSmallRingEnd-lunaPsnowSmallRingData
