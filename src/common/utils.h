#pragma once
#include <stdio.h>
#include <stdint.h>

int32_t read_int32_le(const uint8_t *buf, size_t offset);
void write_int32_le(uint8_t *buf, size_t offset, int32_t value);
