##################################################
Background
##################################################

This is a custom toy OS I am building to learn more about various low-level systems! 
Since building a simple data path on an FPGA, I was extremely interested in learning
more about low-level systems, I decided to take on this challenge. This OS is 
inspired by RISC-V operating systems and the xv6 OS! 

##################################################
Technical Details
##################################################

Software:

This OS will be written primarily in C and run on a vitual CPU simulated by QEMU.
As of now, it will be run using the risc-v architecture on a single core.

Current Plan:

Basics (console, panics, and trap handling)
Time interrupts
Virtual Memory (frame tables + page allocators, etc)
Process Abstraction and Context Switching
User mode + improve VM to include address spaces for users and processes
Syscall
Program Loading
Init and shell

Future:

Since I this is my first time trying to code an OS, there is no plan for 
multiple HARTS/Cores for the sake of simplicity. However, I would love
to enable multi core processing once I learn more about the topic. 
I also would love to integrate a more complex file manager in the future. 
