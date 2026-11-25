#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <libgen.h>
#include "header/filesystem/ext2.h"

#define IMG_SIZE (4 * 1024 * 1024)

uint8_t *image_storage;

void read_blocks(void *ptr, uint32_t lba, uint8_t count) {
    for (int i = 0; i < count; i++)
        memcpy((uint8_t *)ptr + BLOCK_SIZE * i, image_storage + BLOCK_SIZE * (lba + i), BLOCK_SIZE);
}

void write_blocks(const void *ptr, uint32_t lba, uint8_t count) {
    for (int i = 0; i < count; i++)
        memcpy(image_storage + BLOCK_SIZE * (lba + i), (const uint8_t *)ptr + BLOCK_SIZE * i, BLOCK_SIZE);
}

static uint32_t free_blocks(void) {
    struct EXT2Superblock *s = (struct EXT2Superblock *)(image_storage + BLOCK_SIZE);
    return s->s_free_blocks_count;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: ext2_roundtrip <file> <storage.bin>\n");
        return 1;
    }

    image_storage = malloc(IMG_SIZE);
    uint8_t *file_buf = malloc(IMG_SIZE);
    uint8_t *read_buf = malloc(IMG_SIZE);

    FILE *f = fopen(argv[2], "rb");
    if (f == NULL || fread(image_storage, IMG_SIZE, 1, f) != 1) {
        fprintf(stderr, "cannot read storage\n");
        return 1;
    }
    fclose(f);

    FILE *t = fopen(argv[1], "rb");
    if (t == NULL) { fprintf(stderr, "cannot open file\n"); return 1; }
    size_t size = fread(file_buf, 1, IMG_SIZE, t);
    fclose(t);

    char path[256];
    strncpy(path, argv[1], sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    char *name = basename(path);

    initialize_filesystem_ext2();
    uint32_t free_before = free_blocks();

    struct EXT2DriverRequest req = {
        .buf          = file_buf,
        .name         = name,
        .name_len     = (uint8_t)strlen(name),
        .parent_inode = 1,
        .buffer_size  = (uint32_t)size,
        .is_directory = false,
    };

    int rc = write(&req);
    printf("size %zu: write rc=%d, blocks used=%u\n", size, rc, free_before - free_blocks());
    if (rc != 0) { puts("FAIL"); return 1; }

    memset(read_buf, 0, IMG_SIZE);
    struct EXT2DriverRequest rr = req;
    rr.buf = read_buf;
    rc = read(rr);
    bool same = (rc == 0) && memcmp(read_buf, file_buf, size) == 0;
    printf("read rc=%d, content %s\n", rc, same ? "IDENTICAL" : "DIFFERENT");

    rc = delete(req);
    bool freed = (rc == 0) && (free_blocks() == free_before);
    printf("delete rc=%d, blocks %s\n", rc, freed ? "RESTORED" : "LEAKED");

    puts((same && freed) ? "PASS" : "FAIL");
    return (same && freed) ? 0 : 1;
}