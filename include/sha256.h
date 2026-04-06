#ifndef SHA256_H
#define SHA256_H

/*
 * Self-contained SHA-256 implementation (FIPS 180-4).
 * No external dependencies (no OpenSSL required).
 * Supports streaming (init/update/final) and one-shot file hashing.
 */

#include <cstdint>
#include <cstddef>
#include <string>

struct SHA256_CTX {
    uint32_t state[8];
    uint64_t bitcount;
    uint8_t  buffer[64];
    uint32_t buflen;
};

void sha256_init(SHA256_CTX* ctx);
void sha256_update(SHA256_CTX* ctx, const void* data, size_t len);
void sha256_final(SHA256_CTX* ctx, uint8_t hash[32]);

// Convenience: compute SHA-256 of a file, return 64-char hex string.
// Returns empty string on error.
std::string sha256_file(const std::string& filepath);

// Convenience: compute SHA-256 of a memory buffer, return 64-char hex string.
std::string sha256_hex(const void* data, size_t len);

#endif /* SHA256_H */
