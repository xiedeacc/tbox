// Stub implementations for Intel-specific functions when building for ARM
// These functions are referenced by common code but won't be called at runtime on ARM

#include <stddef.h>
#include <stdint.h>

// Stub for AVX2 detection
int aws_common_private_has_avx2(void) {
    return 0;
}

// Stubs for Intel CRC32 functions
uint32_t aws_checksums_crc32c_intel_avx512_with_sse_fallback(
    const uint8_t *input, int length, uint32_t previousCrc32) {
    (void)input;
    (void)length;
    (void)previousCrc32;
    return 0;
}

uint64_t aws_checksums_crc64nvme_intel_clmul(
    const uint8_t *input, int length, uint64_t previousCrc64) {
    (void)input;
    (void)length;
    (void)previousCrc64;
    return 0;
}

// Stubs for Intel base64 encoding/decoding functions
size_t aws_common_private_base64_encode_sse41(
    const uint8_t *input, size_t len, uint8_t *output) {
    (void)input;
    (void)len;
    (void)output;
    return 0;
}

int aws_common_private_base64_decode_sse41(
    const uint8_t *input, size_t len, uint8_t *output, size_t *output_len) {
    (void)input;
    (void)len;
    (void)output;
    (void)output_len;
    return -1;
}
