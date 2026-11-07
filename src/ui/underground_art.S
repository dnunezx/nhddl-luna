.section .rodata
.balign 128
.global undergroundBackgroundData
undergroundBackgroundData:
.incbin "background.t8z"
undergroundBackgroundEnd:
.balign 128
.global undergroundFrameData
undergroundFrameData:
.incbin "frame.t8z"
undergroundFrameEnd:
.balign 128
.global undergroundLogoData
undergroundLogoData:
.incbin "logo.t8z"
undergroundLogoEnd:
.balign 4
.global undergroundBackgroundSize
undergroundBackgroundSize:
.word undergroundBackgroundEnd-undergroundBackgroundData
.global undergroundFrameSize
undergroundFrameSize:
.word undergroundFrameEnd-undergroundFrameData
.global undergroundLogoSize
undergroundLogoSize:
.word undergroundLogoEnd-undergroundLogoData
