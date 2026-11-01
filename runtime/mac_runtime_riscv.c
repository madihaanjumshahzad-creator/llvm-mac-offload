// mac_accel_dot() for the RISC-V SoC (RISC-V_near_memory_MAC, rtl/mac_accel.v).
// The mac-offload pass puts calls to this function in place of the loop.

#include <stdint.h>

#define ACC      0x20000000u
#define CTRL     (*(volatile uint32_t *)(ACC + 0x00))
#define STATUS   (*(volatile uint32_t *)(ACC + 0x04))
#define LEN      (*(volatile uint32_t *)(ACC + 0x08))
#define A_OFF    (*(volatile uint32_t *)(ACC + 0x0C))
#define B_OFF    (*(volatile uint32_t *)(ACC + 0x10))
#define RESULT   (*(volatile uint32_t *)(ACC + 0x14))
#define SPAD     ((volatile uint32_t *)(ACC + 0x10000))

#define SPAD_BYTES 4096     // A and B together

static int32_t dot_sw(const int8_t *a, const int8_t *b, int32_t n)
{
    int32_t s = 0;
    for (int32_t i = 0; i < n; i++)
        s += a[i] * b[i];
    return s;
}

// scratchpad takes 32-bit writes only
static void copy_to_spad(uint32_t word, const int8_t *src, int32_t n)
{
    if (((uintptr_t)src & 3) == 0) {
        const uint32_t *w = (const uint32_t *)src;
        for (int32_t i = 0; i < (n + 3) / 4; i++)
            SPAD[word + i] = w[i];
        return;
    }
    // not aligned, build each word from 4 bytes
    for (int32_t i = 0; i < n; i += 4) {
        uint32_t w = 0;
        for (int k = 0; k < 4 && i + k < n; k++)
            w |= (uint32_t)(uint8_t)src[i + k] << (8 * k);
        SPAD[word + i / 4] = w;
    }
}

int32_t mac_accel_dot(const int8_t *a, const int8_t *b, int32_t n)
{
    int32_t n4 = (n + 3) & ~3;

    if (n <= 0)
        return 0;
    if (2 * n4 > SPAD_BYTES)
        return dot_sw(a, b, n);     // does not fit

    copy_to_spad(0, a, n);
    copy_to_spad(n4 / 4, b, n);

    LEN = n;
    A_OFF = 0;
    B_OFF = n4;
    CTRL = 1;
    while ((STATUS & 2) == 0)
        ;
    if (STATUS & 4)
        return dot_sw(a, b, n);
    return RESULT;
}
