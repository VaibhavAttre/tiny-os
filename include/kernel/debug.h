#pragma once

extern int kdebug;

static inline int kdebug_on(void) {
    return kdebug != 0;
}
