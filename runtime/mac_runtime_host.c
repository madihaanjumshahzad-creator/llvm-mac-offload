// Software model of mac_accel_dot() for the host test.
// Counts the calls so the test can check the pass really inserted them.

#include <stdint.h>

int mac_accel_calls = 0;

int32_t mac_accel_dot(const int8_t *a, const int8_t *b, int32_t n)
{
    int32_t s = 0;
    mac_accel_calls++;
    for (int32_t i = 0; i < n; i++)
        s += a[i] * b[i];
    return s;
}
