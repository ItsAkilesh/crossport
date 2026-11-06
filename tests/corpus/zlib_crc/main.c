/*
 * File: tests/corpus/zlib_crc/main.c
 * Purpose: Check zlib crc32 and adler32 against a bitwise reference over many lengths and alignments, and a PNG-style chunk CRC
 * Author: Akilesh
 * Date: 2026-11-06
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

unsigned long crc32(unsigned long crc, const unsigned char *buf, unsigned int len);
unsigned long adler32(unsigned long adler, const unsigned char *buf, unsigned int len);

#define MAX_LENGTH 600
#define MAX_OFFSET 16
#define POLYNOMIAL 0xEDB88320u
#define ADLER_BASE 65521u
#define PNG_IHDR_LENGTH 13

static uint32_t reference_crc32(uint32_t crc, const unsigned char *buf, size_t len)
{
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (POLYNOMIAL & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static uint32_t reference_adler32(const unsigned char *buf, size_t len)
{
    uint32_t a = 1;
    uint32_t b = 0;
    for (size_t i = 0; i < len; i++) {
        a = (a + buf[i]) % ADLER_BASE;
        b = (b + a) % ADLER_BASE;
    }
    return (b << 16) | a;
}

static int check_sweep(void)
{
    static unsigned char storage[MAX_LENGTH + MAX_OFFSET];
    int failures = 0;
    for (size_t i = 0; i < sizeof storage; i++) {
        storage[i] = (unsigned char)(i * 131u + 7u);
    }
    for (size_t offset = 0; offset < MAX_OFFSET; offset++) {
        for (size_t len = 0; len <= MAX_LENGTH; len++) {
            uint32_t expected = reference_crc32(0, storage + offset, len);
            uint32_t actual = (uint32_t)crc32(0, storage + offset, (unsigned int)len);
            if (expected != actual && failures++ < 10) {
                printf("crc32 mismatch offset=%zu len=%zu expected=%08x actual=%08x\n", offset, len, expected, actual);
            }
            expected = reference_adler32(storage + offset, len);
            actual = (uint32_t)adler32(1, storage + offset, (unsigned int)len);
            if (expected != actual && failures++ < 10) {
                printf("adler32 mismatch offset=%zu len=%zu expected=%08x actual=%08x\n", offset, len, expected, actual);
            }
        }
    }
    return failures;
}

static int check_png_header(void)
{
    static const unsigned char ihdr[4 + PNG_IHDR_LENGTH] = {
        'I', 'H', 'D', 'R', 0, 0, 0, 16, 0, 0, 0, 16, 8, 6, 0, 0, 0};
    uint32_t expected = reference_crc32(0, ihdr, sizeof ihdr);
    uint32_t actual = (uint32_t)crc32(crc32(0, NULL, 0), ihdr, sizeof ihdr);
    printf("png ihdr crc %08x\n", actual);
    return expected != actual;
}

static int check_incremental(void)
{
    static unsigned char data[MAX_LENGTH];
    for (size_t i = 0; i < sizeof data; i++) {
        data[i] = (unsigned char)(i ^ (i >> 3));
    }
    uint32_t whole = (uint32_t)crc32(0, data, sizeof data);
    uint32_t pieces = 0;
    for (size_t at = 0; at < sizeof data; at += 37) {
        size_t chunk = sizeof data - at < 37 ? sizeof data - at : 37;
        pieces = (uint32_t)crc32(pieces, data + at, (unsigned int)chunk);
    }
    printf("incremental crc %08x\n", whole);
    return whole != pieces || whole != reference_crc32(0, data, sizeof data);
}

int main(void)
{
    int failures = check_sweep();
    failures += check_png_header();
    failures += check_incremental();
    printf("failures %d\n", failures);
    return failures != 0;
}
