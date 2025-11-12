#pragma once
#include <stdio.h>
#include <stdint.h>

static int32_t read_int32_le(const uint8_t *buf, size_t offset);
static void write_int32_le(uint8_t *buf, size_t offset, int32_t value);
