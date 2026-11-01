// Firmware for the SoC run. dot_i8() comes from tests/dot.c after the
// mac-offload pass, so it calls mac_accel_dot(). dot_ref() is compiled
// normally and runs on the CPU.

#include <stdint.h>

#define OUT_CHAR (*(volatile uint32_t *)0x10000000)
#define N 1024

int32_t dot_i8(const int8_t *a, const int8_t *b, int n);

int8_t a[N] __attribute__((aligned(4)));
int8_t b[N] __attribute__((aligned(4)));

static uint32_t cycles(void)
{
    uint32_t c;
    asm volatile ("rdcycle %0" : "=r"(c));
    return c;
}

static void print(const char *s)
{
    while (*s)
        OUT_CHAR = *s++;
}

static void print_num(int32_t v)
{
    char buf[12];
    int n = 0;
    uint32_t u = v;
    if (v < 0) {
        OUT_CHAR = '-';
        u = -u;
    }
    do {
        buf[n++] = '0' + u % 10;
        u /= 10;
    } while (u);
    while (n)
        OUT_CHAR = buf[--n];
}

__attribute__((noinline))
static int32_t dot_ref(const int8_t *x, const int8_t *y, int n)
{
    int32_t s = 0;
    for (int i = 0; i < n; i++)
        s += x[i] * y[i];
    return s;
}

int main(void)
{
    uint32_t r = 2026;
    for (int i = 0; i < N; i++) {
        r = r * 1664525u + 1013904223u;
        a[i] = r >> 24;
        r = r * 1664525u + 1013904223u;
        b[i] = r >> 24;
    }

    uint32_t t = cycles();
    int32_t sw = dot_ref(a, b, N);
    uint32_t t_sw = cycles() - t;

    t = cycles();
    int32_t hw = dot_i8(a, b, N);
    uint32_t t_hw = cycles() - t;

    print("software loop     : "); print_num(sw); print(" in "); print_num(t_sw); print(" cycles\n");
    print("compiler-offloaded: "); print_num(hw); print(" in "); print_num(t_hw); print(" cycles (incl. copy)\n");
    if (sw != hw) {
        print("FAIL: results differ\n");
        return 1;
    }
    print("PASS\n");
    return 0;
}
