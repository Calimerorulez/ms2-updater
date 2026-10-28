#include "ms2x.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    static const struct { unsigned hardware, blocks; const char *filename; } cases[] = {
        {0x32U, 92U, "050-ms2.bin"},
        {0x33U, 156U, "051-ms2.bin"},
        {0x34U, 124U, "052-ms2.bin"}
    };
    unsigned i;
    for (i=0U; i<3U; ++i) {
        const ms2x_profile *p=ms2x_profile_for((uint16_t)cases[i].hardware);
        if (p==NULL || p->uid!=0U || p->hardware!=cases[i].hardware ||
            ms2x_profile_blocks(p)!=cases[i].blocks ||
            strcmp(p->filename,cases[i].filename)!=0) {
            (void)fprintf(stderr, "Profile validation failed for hardware %04X\n", cases[i].hardware);
            return 1;
        }
        (void)printf("%04X => %s (%u blocks), dynamic UID\n",
                     cases[i].hardware, p->filename, cases[i].blocks);
    }
    if (ms2x_profile_for(0x35U)!=NULL || ms2x_profile_for(0x40U)!=NULL ||
        ms2x_profile_for(0x10U)!=NULL) {
        (void)fprintf(stderr, "Unsupported hardware was accepted\n");
        return 1;
    }
    (void)puts("Unsupported hardware correctly rejected");
    return 0;
}
