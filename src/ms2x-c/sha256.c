/* Portable SHA-256 (FIPS 180-4 algorithm). Independently checked with hashlib.
 * No OS or crypto-library dependency. All arithmetic is explicitly unsigned.
 */
#include "ms2x.h"
#include <string.h>

static uint32_t rotr(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32U - n));
}

static void compress(uint32_t h[8], const uint8_t block[64])
{
    static const uint32_t k[64] = {
        0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
        0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
        0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
        0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
        0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
        0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
        0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
        0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U
    };
    uint32_t w[64], a=h[0], b=h[1], c=h[2], d=h[3], e=h[4], f=h[5], g=h[6], v=h[7];
    size_t i;
    for (i=0; i<16U; ++i) {
        size_t j=i*4U;
        w[i]=((uint32_t)block[j]<<24)|((uint32_t)block[j+1U]<<16)|
             ((uint32_t)block[j+2U]<<8)|(uint32_t)block[j+3U];
    }
    for (i=16U; i<64U; ++i) {
        uint32_t s0=rotr(w[i-15U],7U)^rotr(w[i-15U],18U)^(w[i-15U]>>3);
        uint32_t s1=rotr(w[i-2U],17U)^rotr(w[i-2U],19U)^(w[i-2U]>>10);
        w[i]=w[i-16U]+s0+w[i-7U]+s1;
    }
    for (i=0; i<64U; ++i) {
        uint32_t s1=rotr(e,6U)^rotr(e,11U)^rotr(e,25U);
        uint32_t ch=(e&f)^((~e)&g);
        uint32_t t1=v+s1+ch+k[i]+w[i];
        uint32_t s0=rotr(a,2U)^rotr(a,13U)^rotr(a,22U);
        uint32_t maj=(a&b)^(a&c)^(b&c);
        uint32_t t2=s0+maj;
        v=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=v;
}

int ms2x_sha256(const uint8_t *bytes, size_t length, uint8_t out[32])
{
    uint32_t h[8]={0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,
                   0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};
    uint8_t tail[128]={0};
    size_t offset=0, remaining, padded, i;
    uint64_t bits;
    if (out==NULL || (bytes==NULL && length!=0U)) return -1;
    /* On 32-bit size_t every length fits in the 64-bit bit count. */
#if SIZE_MAX > UINT64_MAX / 8U
    if (length>UINT64_MAX/8U) return -1;
#endif
    bits=(uint64_t)length*8U;
    while (length-offset>=64U) {
        compress(h,bytes+offset); offset+=64U;
    }
    remaining=length-offset;
    if (remaining!=0U) memcpy(tail,bytes+offset,remaining);
    tail[remaining]=0x80U;
    padded=remaining<56U ? 64U : 128U;
    for (i=0; i<8U; ++i) tail[padded-1U-i]=(uint8_t)(bits>>(8U*(unsigned)i));
    compress(h,tail);
    if (padded==128U) compress(h,tail+64U);
    for (i=0; i<8U; ++i) {
        out[i*4U]=(uint8_t)(h[i]>>24); out[i*4U+1U]=(uint8_t)(h[i]>>16);
        out[i*4U+2U]=(uint8_t)(h[i]>>8); out[i*4U+3U]=(uint8_t)h[i];
    }
    return 0;
}