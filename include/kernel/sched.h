#pragma once
#include <stdint.h>

extern volatile int need_switch;
void yield();
void sched_tick();