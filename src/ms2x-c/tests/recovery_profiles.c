/* Linked ONLY by test-recovery, in place of profiles.o. These hashes describe
 * synthetic_image.h's patterns, not executable firmware. No production API
 * or runtime switch accepts them. Geometry matches the production profiles. */
#include "ms2x.h"

const ms2x_profile ms2x_ms2x_profile = {
    "synthetic-0033", "48a663111bf4c5b6c1f07d08858070a2a3c97a1d1703785e5b551dfabdf97b6a",
    0U, 0x33U, 0U, 1024U, 4U, 159, 4, 159219U
};
static const ms2x_profile others[] = {
    {"synthetic-0032", "2d178f6be8fc82bbea2ff3f75e2e35a37c625d83b6b6671a9637d0d895d62a8c",
     0U, 0x32U, 252U, 1024U, 4U, 95, 4, 93500U},
    {"synthetic-0034", "3a80d64f4c6e8f92d5ef49d31e6b8fcacc2b39e575d26949a0e90f1e40c2fc1d",
     0U, 0x34U, 0U, 1024U, 4U, 127, 4, 126092U}
};
const ms2x_profile *ms2x_profile_for(uint16_t hardware)
{
    if (hardware==0x33U) return &ms2x_ms2x_profile;
    if (hardware==0x32U) return &others[0];
    if (hardware==0x34U) return &others[1];
    return NULL;
}
