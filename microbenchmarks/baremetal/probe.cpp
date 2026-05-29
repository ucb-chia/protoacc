// Minimal bare-metal probe: replicate AccelSetupFixedAllocRegion step by step
// with markers, to isolate where the hang occurs (sfence / memalign / page
// touch / MEM_SETUP).
#include <stdio.h>
#include <stdint.h>
#include <malloc.h>
#include "rocc.h"

#define PROTOACC_OPCODE 2
#define FUNCT_SFENCE 0
#define FUNCT_MEM_SETUP 3
#define PAGESIZE_BYTES 4096

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("a: before sfence\n");
    ROCC_INSTRUCTION(PROTOACC_OPCODE, FUNCT_SFENCE);
    printf("b: after sfence\n");

    size_t regionsize = (size_t)(128 << 13);   // 1 MiB, same as accellib
    char *r1 = (char*)memalign(PAGESIZE_BYTES, regionsize);
    printf("c: memalign1 = %p\n", (void*)r1);
    for (uint64_t i = 0; i < regionsize; i += PAGESIZE_BYTES) r1[i] = 0;
    printf("d: touched r1\n");

    char *r2 = (char*)memalign(PAGESIZE_BYTES, regionsize);
    printf("e: memalign2 = %p\n", (void*)r2);
    for (uint64_t i = 0; i < regionsize; i += PAGESIZE_BYTES) r2[i] = 0;
    printf("f: touched r2\n");

    uint64_t fixed_ptr = (uint64_t)r1, array_ptr = (uint64_t)r2;
    ROCC_INSTRUCTION_SS(PROTOACC_OPCODE, fixed_ptr, array_ptr, FUNCT_MEM_SETUP);
    printf("g: after mem_setup -- ALL OK\n");
    return 0;
}
