// Host test. dot_i8() is tests/dot.c after the pass, dot_ref() is plain C.
// Both must give the same result, and dot_i8() must call mac_accel_dot()
// once for every n > 0.
#include <stdint.h>
#include <stdio.h>

int32_t dot_i8(const int8_t *a, const int8_t *b, int n);
extern int mac_accel_calls;

static int32_t dot_ref(const int8_t *a, const int8_t *b, int n)
{
    int32_t s = 0;
    for (int i = 0; i < n; i++)
        s += (int32_t)a[i] * (int32_t)b[i];
    return s;
}

int main(void)
{
    static int8_t a[2048], b[2048];
    uint32_t x = 2026;
    for (int i = 0; i < 2048; i++) {
        x = x * 1664525u + 1013904223u;
        a[i] = x >> 24;
        x = x * 1664525u + 1013904223u;
        b[i] = x >> 24;
    }

    const int lens[] = {0, 1, 3, 4, 7, 64, 1000, 1024, 2048, -5};
    int fails = 0, expected_calls = 0;
    for (unsigned k = 0; k < sizeof lens / sizeof lens[0]; k++) {
        int n = lens[k];
        int32_t got = dot_i8(a, b, n), ref = dot_ref(a, b, n);
        if (n > 0)
            expected_calls++;
        printf("n=%5d  offloaded=%8d  reference=%8d  %s\n", n, got, ref,
               got == ref ? "ok" : "MISMATCH");
        fails += got != ref;
    }
    printf("mac_accel_dot calls: %d (expected %d)\n", mac_accel_calls, expected_calls);
    fails += mac_accel_calls != expected_calls;
    puts(fails ? "FAIL" : "PASS");
    return fails != 0;
}
