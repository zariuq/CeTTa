#include "digest.h"

#include <string.h>

/* FIPS 180-4, sections 4.2.2, 5.3.3 and 6.2. */
static const uint32_t rounds[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static uint32_t rotate(uint32_t word, unsigned count) {
    return (word >> count) | (word << (32-count));
}

static void compress(CeDigest *d) {
    uint32_t w[64];
    for (unsigned i = 0; i < 16; ++i)
        w[i] = (uint32_t)d->block[4*i] << 24 | (uint32_t)d->block[4*i+1] << 16 |
               (uint32_t)d->block[4*i+2] << 8 | d->block[4*i+3];
    for (unsigned i = 16; i < 64; ++i) {
        uint32_t a = w[i-15], b = w[i-2];
        w[i] = w[i-16] + (rotate(a,7)^rotate(a,18)^(a>>3)) + w[i-7] +
               (rotate(b,17)^rotate(b,19)^(b>>10));
    }
    uint32_t a=d->words[0],b=d->words[1],c=d->words[2],e=d->words[4],
             f=d->words[5],g=d->words[6],h=d->words[7],v=d->words[3];
    for (unsigned i = 0; i < 64; ++i) {
        uint32_t first = h + (rotate(e,6)^rotate(e,11)^rotate(e,25)) +
                         ((e&f)^(~e&g)) + rounds[i] + w[i];
        uint32_t second = (rotate(a,2)^rotate(a,13)^rotate(a,22)) +
                          ((a&b)^(a&c)^(b&c));
        h=g;g=f;f=e;e=v+first;v=c;c=b;b=a;a=first+second;
    }
    d->words[0]+=a;d->words[1]+=b;d->words[2]+=c;d->words[3]+=v;
    d->words[4]+=e;d->words[5]+=f;d->words[6]+=g;d->words[7]+=h;
}

void ce_digest_init(CeDigest *d) {
    *d = (CeDigest){.words={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                            0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};
}

bool ce_digest_update(CeDigest *d, const void *data, size_t length) {
    if (length > UINT64_MAX/8-d->bytes) return false;
    d->bytes += length;
    const unsigned char *bytes = data;
    while (length) {
        size_t count = length < 64-d->used ? length : 64-d->used;
        memcpy(d->block+d->used,bytes,count);
        bytes += count; length -= count; d->used += count;
        if (d->used == 64) { compress(d); d->used = 0; }
    }
    return true;
}

void ce_digest_finish(CeDigest *d, char hex[65]) {
    uint64_t bits = d->bytes*8;
    d->block[d->used++] = 0x80;
    if (d->used > 56) {
        memset(d->block+d->used,0,64-d->used); compress(d); d->used = 0;
    }
    memset(d->block+d->used,0,56-d->used);
    for (unsigned i = 0; i < 8; ++i) d->block[63-i] = (unsigned char)(bits>>(8*i));
    compress(d);
    static const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i) {
        unsigned byte = (d->words[i/4] >> (24-8*(i%4))) & 255;
        hex[2*i] = digits[byte>>4]; hex[2*i+1] = digits[byte&15];
    }
    hex[64] = 0;
}
