#pragma once
#include "kernel/trapframe.h"

void usertrapret() __attribute__((noreturn));

void trap_init(void);
void trap_handler(struct trapframe * tpfrm);
void kerneltrap();
void usertrap();