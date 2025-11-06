/**
 * Stub implementations for AWS SDK ARM-optimized functions.
 * These are needed when compiling for musl/OpenWRT where the ARM
 * optimized implementations are excluded but generic code still
 * references them.
 */

#include <stdint.h>
#include <stddef.h>

// Stub implementations that return 0 to indicate unavailable
uint32_t aws_checksums_crc32_armv8(const uint8_t *input, int length, uint32_t previousCrc32) {
    (void)input; (void)length; (void)previousCrc32;
    return 0; // Indicate function unavailable
}

uint32_t aws_checksums_crc32c_armv8(const uint8_t *input, int length, uint32_t previousCrc32) {
    (void)input; (void)length; (void)previousCrc32;
    return 0; // Indicate function unavailable
}

uint64_t aws_checksums_crc64nvme_arm_pmull(const uint8_t *input, int length, uint64_t previousCrc64) {
    (void)input; (void)length; (void)previousCrc64;
    return 0; // Indicate function unavailable
}
