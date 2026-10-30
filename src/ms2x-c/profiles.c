/* Fixed production catalog. Tests may link a separate synthetic catalog;
 * no runtime catalog selection or validation bypass exists. */
#include "ms2x.h"

const ms2x_profile ms2x_ms2x_profile = {
    "051-ms2.bin", MS2X_SHA256_HEX, 0U, MS2X_HARDWARE,
    0U, MS2X_BLOCK_SIZE, 4U, MS2X_FIRST_BLOCK, MS2X_LAST_BLOCK, MS2X_IMAGE_SIZE
};
static const ms2x_profile other_profiles[] = {
    {"050-ms2.bin", "af26cfdc9f747379f5a7bd7e62df384b641e21c4007dbdf9a19c69b3e808bb69",
     0U, 0x32U, 252U, 1024U, 4U, 95, 4, 93500U},
    {"052-ms2.bin", "a56765bb56626795f792839272a3621298955c8831cc6ad35e290afa027f33c7",
     0U, 0x34U, 0U, 1024U, 4U, 127, 4, 126092U}
};
const ms2x_profile *ms2x_profile_for(uint16_t hardware)
{
    if (hardware==0x33U) return &ms2x_ms2x_profile;
    if (hardware==0x32U) return &other_profiles[0];
    if (hardware==0x34U) return &other_profiles[1];
    return NULL;
}
