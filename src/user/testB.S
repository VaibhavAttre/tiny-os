.section .text
.globl _start
.option norvc
.align 2

.equ SYS_EXIT, 6
.equ SYS_WRITE, 9
.equ SYS_OPEN, 11
.equ SYS_READ, 8
.equ SYS_CLOSE, 10
.equ SYS_TRUNCATE, 21

.equ O_RDONLY, 0x000
.equ O_WRONLY, 0x001
.equ O_RDWR,   0x002
.equ O_CREATE, 0x200

_start:
    # init gp for small-data references (prevent relaxation)
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    # Print start message
    li a0, 1
    la a1, msg_start
    li a2, 20
    li a7, SYS_WRITE
    ecall

    # Open a file for writing (create it)
    # fd = open("/userfile.txt", O_WRONLY | O_CREATE)
    la a0, filename
    li a1, (O_WRONLY | O_CREATE)
    li a7, SYS_OPEN
    ecall
    
    # Check if open succeeded (fd in a0)
    bltz a0, open_failed
    mv s0, a0           # Save fd in s0
    
    # Print success message
    li a0, 1
    la a1, msg_opened
    li a2, 14
    li a7, SYS_WRITE
    ecall
    
    # Write to the file
    # write(fd, "User wrote this!", 17)
    mv a0, s0           # fd
    la a1, file_content
    li a2, 17
    li a7, SYS_WRITE
    ecall
    
    # Close the file
    mv a0, s0
    li a7, SYS_CLOSE
    ecall

    # Truncate the file to 0 bytes
    la a0, filename
    li a1, 0
    li a7, SYS_TRUNCATE
    ecall

    # Reopen file for reading
    la a0, filename
    li a1, O_RDONLY
    li a7, SYS_OPEN
    ecall
    
    bltz a0, open_failed
    mv s0, a0           # Save fd
    
    # Read from the file
    mv a0, s0           # fd
    la a1, read_buf     # buffer
    li a2, 32           # max bytes
    li a7, SYS_READ
    ecall
    
    # a0 = bytes read
    mv s1, a0           # save bytes read
    
    # Close the file
    mv a0, s0
    li a7, SYS_CLOSE
    ecall
    
    # Print what we read
    li a0, 1
    la a1, msg_read
    li a2, 6
    li a7, SYS_WRITE
    ecall

    # Print the buffer content
    li a0, 1
    la a1, read_buf
    mv a2, s1           # bytes we read
    li a7, SYS_WRITE
    ecall

    # Print truncate result
    li a0, 1
    la a1, msg_trunc
    li a2, 17
    li a7, SYS_WRITE
    ecall
    
    # Print newline
    li a0, 1
    la a1, newline
    li a2, 1
    li a7, SYS_WRITE
    ecall

    # Print done message
    li a0, 1
    la a1, msg_done
    li a2, 6
    li a7, SYS_WRITE
    ecall
    
    # Exit successfully
    li a0, 0
    li a7, SYS_EXIT
    ecall

open_failed:
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
    .ascii "testB: starting...\n"
msg_opened:
    .ascii "File opened!\n"
msg_read:
    .ascii "Read: "
msg_trunc:
    .ascii " (truncated)\n"
msg_fail:
    .ascii "Open failed!\n"
msg_done:
    .ascii "Done!\n"
newline:
    .ascii "\n"
filename:
    .asciz "/userfile.txt"
file_content:
    .ascii "User wrote this!\0"

.section .bss
.align 3
read_buf:
    .space 64
