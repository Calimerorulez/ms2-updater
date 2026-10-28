#include "ms2x.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cases;
#define REQUIRE(test) do { ++cases; if (!(test)) { \
    (void)fprintf(stderr, "negative firmware test failed at line %d: %s\\n", __LINE__, #test); \
    return 1; } } while (0)

int main(void)
{
    static const uint16_t types[] = {0x0032U, 0x0033U, 0x0034U};
    size_t i;
    for (i=0U; i<sizeof(types)/sizeof(types[0]); ++i) {
        const ms2x_profile *p=ms2x_profile_for(types[i]);
        ms2x_error err=MS2X_OK;
        uint8_t *image;
        ms2x_firmware *fw;
        REQUIRE(p!=NULL && p->image_size>p->version_offset+3U);
        image=calloc(p->image_size+1U,1U);
        REQUIRE(image!=NULL);
        image[p->version_offset]=5U;
        image[p->version_offset+1U]=6U;
        image[p->version_offset+2U]=(uint8_t)(types[i]>>8);
        image[p->version_offset+3U]=(uint8_t)types[i];
        /* Correct header but entirely fabricated contents: SHA256 must reject. */
        fw=ms2x_firmware_from_bytes_for(image,p->image_size,types[i],&err);
        REQUIRE(fw==NULL);
        fw=ms2x_firmware_from_bytes_for(image,p->image_size-1U,types[i],&err);
        REQUIRE(fw==NULL);
        fw=ms2x_firmware_from_bytes_for(image,p->image_size+1U,types[i],&err);
        REQUIRE(fw==NULL);
        image[p->version_offset]=4U;
        fw=ms2x_firmware_from_bytes_for(image,p->image_size,types[i],&err);
        REQUIRE(fw==NULL);
        image[p->version_offset]=5U;
        image[p->version_offset+3U]^=1U;
        fw=ms2x_firmware_from_bytes_for(image,p->image_size,types[i],&err);
        REQUIRE(fw==NULL);
        fw=ms2x_firmware_from_bytes_for(image,p->image_size,0x0035U,&err);
        REQUIRE(fw==NULL);
        free(image);
        (void)printf("hardware %04X: fabricated, truncated, oversized, wrong version/type rejected\\n", types[i]);
    }
    REQUIRE(ms2x_firmware_from_bytes_for(NULL,100U,0x0033U,NULL)==NULL);
    (void)printf("Passed %d offline negative firmware checks; no CAN used\\n",cases);
    return 0;
}
