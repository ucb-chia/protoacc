// Bare-metal end-to-end serialize -> deserialize pipeline (no protobuf).
//
// Stage 1: accelerator-serialize GOLDEN_OBJ; bytes == serialized_golden
// Stage 2: accelerator-deserialize serialized_golden into a fresh object;
//          descriptor-driven compare against GOLDEN_OBJ
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include "rocc.h"

extern volatile uint64_t tohost;
static void htif_exit(int code) {
    tohost = ((uint64_t)code << 1) | 1;
    while (1) { }
}

#define P2 2
#define P3 3
#define F_SFENCE 0
#define F_SER_SFENCE 0
#define F_PARSE_INFO 1
#define F_DO_PARSE 2
#define F_MEM_SETUP 3
#define F_CHECK 4
#define F_HASBITS_INFO 1
#define F_DO_SER 2
#define F_SER_MEM_SETUP 3
#define F_SER_CHECK 4
#ifndef ITERS
#define ITERS 1
#endif
#define RSZ ((size_t)(64 << 10))
#define PG 4096

#include DATA_HEADER

static inline uint64_t rdcycle(void) {
    uint64_t c; asm volatile("rdcycle %0" : "=r"(c)); return c;
}

static char* touchalloc(size_t sz) {
    char* p = (char*)memalign(PG, sz);
    for (uint64_t i = 0; i < sz; i += PG) p[i] = 0;
    return p;
}

// protobuf FieldDescriptor::TYPE_* codes
#define T_STRING 9
#define T_MESSAGE 11
#define T_BYTES 12
#define IS_STR(t) ((t) == T_STRING || (t) == T_BYTES)

static size_t elem_size(int t) {
    switch (t) {
        case 2: case 5: case 7: case 13: case 14: case 15: case 17: return 4;
        case 8: return 1;
        default: return 8;
    }
}

// std::string image: {data,size}; gold side has cap too; accel writes {data,size}.
static int cmp_str(const uint8_t* sa, const uint8_t* sb, const char* what) {
    if (!sa || !sb) { printf("FAILCMP %s: null block %p %p\n", what, sa, sb); return 1; }
    uint64_t la = ((const uint64_t*)sa)[1], lb = ((const uint64_t*)sb)[1];
    if (la != lb) { printf("FAILCMP %s: len %lu != %lu\n", what, la, lb); return 1; }
    const uint8_t* pa = (const uint8_t*)((const uint64_t*)sa)[0];
    const uint8_t* pb = (const uint8_t*)((const uint64_t*)sb)[0];
    if (memcmp(pa, pb, la)) { printf("FAILCMP %s: data differs\n", what); return 1; }
    return 0;
}

static int cmp_msgs(const uint64_t* d, const uint8_t* a, const uint8_t* b) {
    uint32_t minf = (uint32_t)(d[3] >> 32), maxf = (uint32_t)d[3];
    size_t nrel = maxf - minf + 1;
    uint64_t hb_off = d[2];

    for (size_t i = 0; i < nrel; i++) {
        uint64_t w = d[4 + 2 * i];
        int rep = (int)(w >> 63);
        int ty = (int)((w >> 58) & 0x1f);
        size_t off = (size_t)(w & ((1ULL << 58) - 1));
        uint32_t hba = ((const uint32_t*)(a + hb_off))[i / 32] >> (i % 32) & 1;
        uint32_t hbb = ((const uint32_t*)(b + hb_off))[i / 32] >> (i % 32) & 1;

        if (rep) {
            size_t cnt_off = IS_STR(ty) ? off : off - 8;          // RTL: string count @off, numeric @off-8
            uint32_t na = *(const uint32_t*)(a + cnt_off), nb = *(const uint32_t*)(b + cnt_off);
            if (na != nb) { printf("FAILCMP field %zu: count %u != %u\n", i, na, nb); return 1; }
            if (!na) continue;
            // numeric & ptr reps: object pointer -> Rep base {arena/alloc, elems[]}
            size_t pl = IS_STR(ty) ? off + 8 : off;                // string: Rep* @off+8; numeric: elems @off
            const uint8_t* ra = *(const uint8_t* const*)(a + pl) + (IS_STR(ty) ? 8 : 0);
            const uint8_t* rb = *(const uint8_t* const*)(b + pl) + (IS_STR(ty) ? 8 : 0);
            if (IS_STR(ty)) {
                
                for (uint32_t e = 0; e < na; e++)
                    if (cmp_str(((const uint8_t* const*)ra)[e], ((const uint8_t* const*)rb)[e], "rep-str")) return 1;
            } else if (memcmp(ra, rb, na * elem_size(ty))) {
                printf("FAILCMP field %zu: repeated data differs\n", i); return 1;
            }
        } else {
            if (hba != hbb) { printf("FAILCMP field %zu: hasbit %u != %u\n", i, hba, hbb); return 1; }
            if (!hba) continue;
            if (IS_STR(ty)) {
                if (cmp_str(*(const uint8_t* const*)(a + off), *(const uint8_t* const*)(b + off), "str")) return 1;
            } else if (ty == T_MESSAGE) {
                const uint8_t* ca = *(const uint8_t* const*)(a + off);
                const uint8_t* cb = *(const uint8_t* const*)(b + off);
                if (!ca || !cb) { printf("FAILCMP field %zu: null submsg\n", i); return 1; }
                if (cmp_msgs((const uint64_t*)d[4 + 2 * i + 1], ca, cb)) return 1;
            } else if (memcmp(a + off, b + off, elem_size(ty))) {
                printf("FAILCMP field %zu: scalar differs\n", i); return 1;
            }
        }
    }
    return 0;
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("pipeline test: %s\n", TEST_NAME);

    ROCC_INSTRUCTION(P2, F_SFENCE);
    char* fixed = touchalloc(RSZ);
    char* array = touchalloc(RSZ);
    ROCC_INSTRUCTION_SS(P2, (uint64_t)fixed, (uint64_t)array, F_MEM_SETUP);

    ROCC_INSTRUCTION(P3, F_SER_SFENCE);
    char* salloc = touchalloc(RSZ);
    uint64_t tail = (uint64_t)salloc + RSZ;
    char** sp = (char**)touchalloc(2048 * sizeof(char*));
    sp[0] = (char*)tail; sp += 1;
    ROCC_INSTRUCTION_SS(P3, tail, (uint64_t)sp, F_SER_MEM_SETUP);
    volatile char** ptrs = (volatile char**)sp;

    patch_descriptors();
    patch_objects();
    uint64_t* d = (uint64_t*)ROOT_DESCRIPTOR;
    printf("[1] setup done\n");

    // ---- stage 1: serialize GOLDEN_OBJ, compare to golden bytes ----
    int serfail = 0;
    for (int it = 0; it < ITERS && !serfail; it++) {
        uint64_t c0 = rdcycle();
        ROCC_INSTRUCTION_SS(P3, d[2], d[3], F_HASBITS_INFO);
        ROCC_INSTRUCTION_SS(P3, (uint64_t)d, (uint64_t)GOLDEN_OBJ, F_DO_SER);
        { uint64_t rv; ROCC_INSTRUCTION_D(P3, rv, F_SER_CHECK); asm volatile("fence"); }
        while (ptrs[it] == 0) { asm volatile("fence"); }
        uint64_t cycles = rdcycle() - c0;
        volatile char* outp = ptrs[it];
        size_t outlen = (size_t)(ptrs[it - 1] - ptrs[it]);
        if (outlen != SERIALIZED_GOLDEN_LEN) serfail = 1;
        for (size_t i = 0; !serfail && i < outlen; i++)
            if ((unsigned char)outp[i] != serialized_golden[i]) serfail = 1;
        if (serfail) printf("s%d, %lu: FAILED\n", it, (unsigned long)cycles);
    }

    // ---- stage 2: deserialize golden bytes, compare to GOLDEN_OBJ ----
    void* obj = memalign(PG, PG); memset(obj, 0, PG);
    uint64_t minf_len = ((d[3] >> 32) << 32) | ((uint64_t)SERIALIZED_GOLDEN_LEN & 0xFFFFFFFFULL);
    int desfail = 0;
    for (int it = 0; it < ITERS && !desfail; it++) {
        memset(obj, 0, PG);
        uint64_t c0 = rdcycle();
        ROCC_INSTRUCTION_SS(P2, (uint64_t)d, (uint64_t)obj, F_PARSE_INFO);
        ROCC_INSTRUCTION_SS(P2, (uint64_t)serialized_golden, minf_len, F_DO_PARSE);
        { uint64_t rv; ROCC_INSTRUCTION_D(P2, rv, F_CHECK); asm volatile("fence"); }
        uint64_t cycles = rdcycle() - c0;
        desfail = cmp_msgs(d, (const uint8_t*)obj, (const uint8_t*)GOLDEN_OBJ);
        if (desfail) printf("d%d, %lu: FAILED\n", it, (unsigned long)cycles);
    }


    int fail = serfail || desfail;
    printf(fail ? "FAILED %s pipeline\n" : "PASSED %s pipeline\n", TEST_NAME);
    htif_exit(fail);
    return fail;
}
