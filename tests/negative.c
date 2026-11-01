#include <stdint.h>

// The pass must leave all of these loops alone.

// int16, accelerator is int8 only
int32_t dot_i16(const int16_t *a, const int16_t *b, int n)
{
    int32_t s = 0;
    for (int i = 0; i < n; i++)
        s += (int32_t)a[i] * (int32_t)b[i];
    return s;
}

// store inside the loop
int32_t dot_with_store(const int8_t *a, const int8_t *b, int32_t *partial, int n)
{
    int32_t s = 0;
    for (int i = 0; i < n; i++) {
        s += (int32_t)a[i] * (int32_t)b[i];
        partial[i] = s;
    }
    return s;
}

// different index for a and b
int32_t dot_shifted(const int8_t *a, const int8_t *b, int n)
{
    int32_t s = 0;
    for (int i = 0; i < n; i++)
        s += (int32_t)a[i] * (int32_t)b[i + 1];
    return s;
}
