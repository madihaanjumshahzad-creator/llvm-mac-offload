#include <stdint.h>

// this loop should be offloaded by the pass
int32_t dot_i8(const int8_t *a, const int8_t *b, int n)
{
    int32_t s = 0;
    for (int i = 0; i < n; i++)
        s += (int32_t)a[i] * (int32_t)b[i];
    return s;
}
