.section .text
.global swtch
.align 2
.option norvc

/*

    switch(old ocntext, new context) {
        a0 = old
        a1 = new
    
*/  

swtch:
    sd ra, 0(a0)
    sd sp,8(a0)
    sd s0,16(a0)
    sd s1,24(a0)
    sd s2,32(a0)
    sd s3,40(a0)
    sd s4,48(a0)
    sd s5,56(a0)
    sd s6,64(a0)
    sd s7,72(a0)
    sd s8,80(a0)
    sd s9,88(a0)
    sd s10,96(a0)
    sd s11,104(a0)

    # Restore new context
    ld ra,0(a1)
    ld sp,8(a1)
    ld s0,16(a1)
    ld s1,24(a1)
    ld s2,32(a1)
    ld s3,40(a1)
    ld s4,48(a1)
    ld s5,56(a1)
    ld s6,64(a1)
    ld s7,72(a1)
    ld s8,80(a1)
    ld s9,88(a1)
    ld s10,96(a1)
    ld s11,104(a1)

    ret