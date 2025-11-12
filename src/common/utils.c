#include "utils.h"

#include <stdint.h>


/* Little-endian helpers (portable, indépendants de l'endian du CPU) */
static int32_t read_int32_le(const uint8_t *buf, size_t offset) {
    return (int32_t) (
        (uint32_t) buf[offset + 0]
        | ((uint32_t) buf[offset + 1] << 8)
        | ((uint32_t) buf[offset + 2] << 16)
        | ((uint32_t) buf[offset + 3] << 24)
    );
}

static void write_int32_le(uint8_t *buf, size_t offset, int32_t value) {
    buf[offset + 0] = (uint8_t) (value & 0xFF);
    buf[offset + 1] = (uint8_t) ((value >> 8) & 0xFF);
    buf[offset + 2] = (uint8_t) ((value >> 16) & 0xFF);
    buf[offset + 3] = (uint8_t) ((value >> 24) & 0xFF);
}
