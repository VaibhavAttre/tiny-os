#pragma once
#include "kernel/trapframe.h"

void trap_init(void);
void trap_handler(struct trapframe * tpfrm);