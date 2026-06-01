// Fully self-contained bare-metal protoacc round-trip (NO accellib link).
// Inlines setup/parse/serialize because calling accellib.cpp's functions hangs
// on this bare-metal target while identical inline code runs.
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include "rocc.h"

// Raw HTIF exit: newlib-nano's exit/_Exit path hangs on this target, so signal
// the simulator directly via the tohost MMIO symbol (riscv-tests convention).
extern volatile uint64_t tohost;
static void htif_exit(int code) {
    tohost = ((uint64_t)code << 1) | 1;
    while (1) { }
}

#define P2 2            // PROTOACC_OPCODE (deserialize)
#define P3 3            // PROTOACC_SER_OPCODE (serialize)
// custom2 functs
#define F_SFENCE 0
#define F_PARSE_INFO 1
#define F_DO_PARSE 2
#define F_MEM_SETUP 3
#define F_CHECK 4
// custom3 functs
#define F_SER_SFENCE 0
#define F_HASBITS_INFO 1
#define F_DO_SER 2
#define F_SER_MEM_SETUP 3
#define F_SER_CHECK 4
// Small regions: the smoke messages are tiny, and bare-metal has all DRAM
// present (no demand paging), so the 1 MiB accellib regions are unnecessary.
// 64 KiB keeps the page-touch DRAM traffic small (DRAMSim is slow in Verilator).
#define RSZ ((size_t)(64 << 10))
#define PG 4096

#include DATA_HEADER

static char* touchalloc(size_t sz) {
    char* p = (char*)memalign(PG, sz);
    for (uint64_t i = 0; i < sz; i += PG) p[i] = 0;
    return p;
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("selfcontained round-trip: %s\n", TEST_NAME);

    // ---- parser setup (custom2) ----
    ROCC_INSTRUCTION(P2, F_SFENCE);
    char* fixed = touchalloc(RSZ);
    char* array = touchalloc(RSZ);
    ROCC_INSTRUCTION_SS(P2, (uint64_t)fixed, (uint64_t)array, F_MEM_SETUP);
    printf("[1] parser setup\n");

    // ---- serializer setup (custom3) ----
    ROCC_INSTRUCTION(P3, F_SER_SFENCE);
    char* salloc = touchalloc(RSZ);
    uint64_t tail = (uint64_t)salloc + RSZ;
    char** sp = (char**)touchalloc(2048 * sizeof(char*));
    sp[0] = (char*)tail; sp += 1;
    ROCC_INSTRUCTION_SS(P3, tail, (uint64_t)sp, F_SER_MEM_SETUP);
    volatile char** ptrs = (volatile char**)sp;
    printf("[2] serializer setup\n");

    patch_descriptors();
    void* obj = memalign(PG, PG); memset(obj, 0, PG);
    printf("[3] ready\n");

    // ---- parse (custom2) ----
    uint64_t* d = (uint64_t*)ROOT_DESCRIPTOR;
    uint64_t minf = d[3] >> 32;
    uint64_t minf_len = (minf << 32) | ((uint64_t)SERIALIZED_INPUT_LEN & 0xFFFFFFFFULL);
    ROCC_INSTRUCTION_SS(P2, (uint64_t)ROOT_DESCRIPTOR, (uint64_t)obj, F_PARSE_INFO);
    ROCC_INSTRUCTION_SS(P2, (uint64_t)serialized_input, minf_len, F_DO_PARSE);
    printf("[4] parse issued\n");
    { uint64_t rv; ROCC_INSTRUCTION_D(P2, rv, F_CHECK); asm volatile("fence"); }
    printf("[5] parse complete\n");

#ifdef PARSE_ONLY
    // The protoacc serializer RTL asserts "not yet implemented" on sub-message
    // fields (fieldhandler_serializer.scala), so nested messages can only be
    // round-tripped through the deserializer. Parse completing is the pass.
    printf("PASSED %s parse-only (serializer does not support this message type)\n", TEST_NAME);
    htif_exit(0);
    return 0;
#endif

    // ---- serialize (custom3) ----
    uint64_t hasbits_off = d[2];
    uint64_t minmax = d[3];
    ROCC_INSTRUCTION_SS(P3, hasbits_off, minmax, F_HASBITS_INFO);
    ROCC_INSTRUCTION_SS(P3, (uint64_t)ROOT_DESCRIPTOR, (uint64_t)obj, F_DO_SER);
    printf("[6] serialize issued\n");
    { uint64_t rv; ROCC_INSTRUCTION_D(P3, rv, F_SER_CHECK); asm volatile("fence"); }
    while (ptrs[0] == 0) { asm volatile("fence"); }
    volatile char* outp = ptrs[0];
    size_t outlen = (size_t)(ptrs[-1] - ptrs[0]);
    printf("[7] serialize complete, outlen=%lu inlen=%lu\n", (unsigned long)outlen, (unsigned long)SERIALIZED_INPUT_LEN);

    int fail = (outlen != SERIALIZED_INPUT_LEN);
    for (size_t i = 0; !fail && i < outlen; i++)
        if ((unsigned char)outp[i] != serialized_input[i]) fail = 1;
    printf(fail ? "FAILED %s round-trip\n" : "PASSED %s round-trip\n", TEST_NAME);
    htif_exit(fail);
    return fail;
}
