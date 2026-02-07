#include <stdint.h>
#include "kernel/fdt.h"

static inline uint32_t be32(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

static inline uintptr_t align4(uintptr_t x) { return (x + 3) & ~(uintptr_t)3; }

const char *dtb_bootargs(const void *dtb) {
    if (!dtb) return 0;
    const uint8_t *base = (const uint8_t *)dtb;

    if (be32(base + 0x00) != 0xd00dfeed) return 0;

    uint32_t off_struct  = be32(base + 0x08);
    uint32_t off_strings = be32(base + 0x0c);

    const uint8_t *sp = base + off_struct;
    const uint8_t *st = base + off_strings;

    int in_chosen = 0;
    int depth = 0;

    for (;;) {
        uint32_t tok = be32(sp);
        sp += 4;

        if (tok == 1) {
            const char *name = (const char *)sp;
            uintptr_t n = 0;
            while (sp[n]) n++;
            sp = (const uint8_t *)align4((uintptr_t)sp + n + 1);

            depth++;
            if (depth == 1 && name[0] == 0) in_chosen = 0;
            if (depth == 2 && in_chosen == 0) {
                if (name[0]=='c'&&name[1]=='h'&&name[2]=='o'&&name[3]=='s'&&name[4]=='e'&&name[5]=='n'&&name[6]==0)
                    in_chosen = 1;
            }
            continue;
        }

        if (tok == 2) {
            if (depth > 0) depth--;
            if (depth < 2) in_chosen = 0;
            continue;
        }

        if (tok == 3) {
            uint32_t len = be32(sp); sp += 4;
            uint32_t nameoff = be32(sp); sp += 4;

            const char *pname = (const char *)(st + nameoff);
            const char *pval = (const char *)sp;

            sp = (const uint8_t *)align4((uintptr_t)sp + len);

            if (in_chosen) {
                if (pname[0]=='b'&&pname[1]=='o'&&pname[2]=='o'&&pname[3]=='t'&&pname[4]=='a'&&pname[5]=='r'&&pname[6]=='g'&&pname[7]=='s'&&pname[8]==0)
                    return pval;
            }
            continue;
        }

        if (tok == 4) continue;
        if (tok == 9) break;

        break;
    }

    return 0;
}
