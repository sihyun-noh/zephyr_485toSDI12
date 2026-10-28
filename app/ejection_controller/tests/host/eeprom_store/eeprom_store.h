#ifndef TEST_EEPROM_H
#define TEST_EEPROM_H
#include <stddef.h>
#include <sys/types.h>
int eeprom_store_init(void);
int eeprom_store_read(off_t offset, void *data, size_t size);
int eeprom_store_write(off_t offset, const void *data, size_t size);
#endif
