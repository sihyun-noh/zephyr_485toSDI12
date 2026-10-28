#ifndef TEST_CRC_H
#define TEST_CRC_H
#include <stdint.h>
#include <stddef.h>
uint32_t crc32_ieee(const uint8_t *p, size_t n);
#endif
