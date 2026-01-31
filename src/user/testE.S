.section .text
.globl _start
.option norvc
.align 2

.equ SYS_EXIT, 6
.equ SYS_WRITE, 9
.equ SYS_OPEN, 11
.equ SYS_READ, 8
.equ SYS_CLOSE, 10
.equ SYS_MKDIR, 15
.equ SYS_UNLINK, 18
.equ SYS_READDIR, 22
.equ SYS_RENAME, 23
.equ SYS_SNAPSHOT, 24
.equ SYS_SUBVOL_SET, 25
.equ SYS_CLONE, 12

.equ O_RDONLY, 0x000
.equ O_WRONLY, 0x001
.equ O_CREATE, 0x200
.equ O_TREE,   0x800

_start:
    # init gp for small-data references (prevent relaxation)
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    # start banner
    li a0, 1
    la a1, msg_start
    li a2, 13
    li a7, SYS_WRITE
    ecall

    # mkdir /tdir (tree)
    la a0, dir_path
    li a1, O_TREE
    li a7, SYS_MKDIR
    ecall

    # create /tdir/a and /tdir/b
    la a0, file_a
    li a1, (O_WRONLY | O_CREATE | O_TREE)
    li a7, SYS_OPEN
    ecall
    bltz a0, fail
    mv s0, a0
    mv a0, s0
    li a7, SYS_CLOSE
    ecall

    la a0, file_b
    li a1, (O_WRONLY | O_CREATE | O_TREE)
    li a7, SYS_OPEN
    ecall
    bltz a0, fail
    mv s0, a0
    mv a0, s0
    li a7, SYS_CLOSE
    ecall

    # rename /tdir/a -> /tdir/c
    la a0, file_a
    la a1, file_c
    li a2, O_TREE
    li a7, SYS_RENAME
    ecall
    bltz a0, fail

    # readdir /tdir using cookie
    li t0, 0
    li t1, 4
readdir_loop:
    la a0, dir_path
    la a1, cookie
    la a2, name_buf
    li a3, 32
    li a4, O_TREE
    li a7, SYS_READDIR
    ecall
    bltz a0, readdir_done

    li a0, 1
    la a1, msg_entry
    li a2, 7
    li a7, SYS_WRITE
    ecall

    li a0, 1
    la a1, name_buf
    li a2, 16
    li a7, SYS_WRITE
    ecall

    li a0, 1
    la a1, msg_nl
    li a2, 1
    li a7, SYS_WRITE
    ecall

    addi t0, t0, 1
    blt t0, t1, readdir_loop
readdir_done:

    # write /snap.txt = "base"
    la a0, snap_path
    li a1, (O_WRONLY | O_CREATE | O_TREE)
    li a7, SYS_OPEN
    ecall
    bltz a0, fail
    mv s0, a0
    mv a0, s0
    la a1, snap_base
    li a2, 5
    li a7, SYS_WRITE
    ecall
    mv a0, s0
    li a7, SYS_CLOSE
    ecall

    # snapshot current FS
    li a7, SYS_SNAPSHOT
    ecall
    mv s1, a0

    # overwrite /snap.txt = "new!"
    la a0, snap_path
    li a1, (O_WRONLY | O_TREE)
    li a7, SYS_OPEN
    ecall
    bltz a0, fail
    mv s0, a0
    mv a0, s0
    la a1, snap_new
    li a2, 4
    li a7, SYS_WRITE
    ecall
    mv a0, s0
    li a7, SYS_CLOSE
    ecall

    # switch to snapshot subvol
    mv a0, s1
    li a7, SYS_SUBVOL_SET
    ecall
    bltz a0, fail

    # read /snap.txt (should be "base")
    la a0, snap_path
    li a1, (O_RDONLY | O_TREE)
    li a7, SYS_OPEN
    ecall
    bltz a0, fail
    mv s0, a0
    mv a0, s0
    la a1, read_buf
    li a2, 8
    li a7, SYS_READ
    ecall
    mv s2, a0
    mv a0, s0
    li a7, SYS_CLOSE
    ecall

    li a0, 1
    la a1, msg_snap
    li a2, 6
    li a7, SYS_WRITE
    ecall

    li a0, 1
    la a1, read_buf
    mv a2, s2
    li a7, SYS_WRITE
    ecall

    li a0, 1
    la a1, msg_nl
    li a2, 1
    li a7, SYS_WRITE
    ecall

    # reflink test: create src, clone to dst, modify src, read dst
    la a0, reflink_src
    li a1, (O_WRONLY | O_CREATE | O_TREE)
    li a7, SYS_OPEN
    ecall
    bltz a0, fail
    mv s0, a0
    mv a0, s0
    la a1, reflink_a
    li a2, 5
    li a7, SYS_WRITE
    ecall
    mv a0, s0
    li a7, SYS_CLOSE
    ecall

    la a0, reflink_src
    la a1, reflink_dst
    li a7, SYS_CLONE
    ecall
    bltz a0, fail

    li a0, 1
    la a1, msg_reflink
    li a2, 11
    li a7, SYS_WRITE
    ecall

    la a0, reflink_src
    li a1, (O_WRONLY | O_TREE)
    li a7, SYS_OPEN
    ecall
    bltz a0, fail
    mv s0, a0
    mv a0, s0
    la a1, reflink_b
    li a2, 5
    li a7, SYS_WRITE
    ecall
    mv a0, s0
    li a7, SYS_CLOSE
    ecall

    la a0, reflink_dst
    li a1, (O_RDONLY | O_TREE)
    li a7, SYS_OPEN
    ecall
    bltz a0, fail
    mv s0, a0
    mv a0, s0
    la a1, read_buf
    li a2, 5
    li a7, SYS_READ
    ecall
    mv a0, s0
    li a7, SYS_CLOSE
    ecall

    lb t0, read_buf
    li t1, 'A'
    bne t0, t1, fail

    li a0, 1
    la a1, msg_pass
    li a2, 12
    li a7, SYS_WRITE
    ecall

    li a0, 0
    li a7, SYS_EXIT
    ecall

fail:
    li a0, 1
    la a1, msg_fail
    li a2, 12
    li a7, SYS_WRITE
    ecall
    li a0, 1
    li a7, SYS_EXIT
    ecall

.section .rodata
msg_start:
    .ascii "testE: start\n"
msg_entry:
    .ascii "entry: "
msg_snap:
    .ascii "snap: "
msg_reflink:
    .ascii "reflink ok\n"
msg_pass:
    .ascii "testE: PASS\n"
msg_fail:
    .ascii "testE: FAIL\n"
msg_nl:
    .ascii "\n"
dir_path:
    .asciz "/tdir"
file_a:
    .asciz "/tdir/a"
file_b:
    .asciz "/tdir/b"
file_c:
    .asciz "/tdir/c"
snap_path:
    .asciz "/snap.txt"
snap_base:
    .ascii "base"
snap_new:
    .ascii "new!"
reflink_src:
    .asciz "/reflink_src"
reflink_dst:
    .asciz "/reflink_dst"
reflink_a:
    .ascii "AAAAA"
reflink_b:
    .ascii "BBBBB"

.section .bss
.align 3
cookie:
    .quad 0
name_buf:
    .space 32
read_buf:
    .space 16
