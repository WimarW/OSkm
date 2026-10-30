#include "header/driver/disk.h"
#include "header/cpu/portio.h"

static void ATA_busy_wait() {
    while (in(0x1F7) & ATA_STATUS_BSY);
}

static void ATA_DRQ_wait() {
    while (!(in(0x1F7) & ATA_STATUS_RDY));
}

static void ata_select_drive(uint8_t drive, uint32_t lba) {
    ATA_busy_wait();
    out(0x1F6, 0xE0 | ((drive & 1) << 4) | ((lba >> 24) & 0xF));
    for (int i = 0; i < 4; i++) in(0x3F6);
    ATA_busy_wait();
}

void read_blocks_drive(void *ptr, uint32_t lba, uint8_t block_count, uint8_t drive) {
    ata_select_drive(drive, lba);
    out(0x1F2, block_count);
    out(0x1F3, (uint8_t) lba);
    out(0x1F4, (uint8_t) (lba >> 8));
    out(0x1F5, (uint8_t) (lba >> 16));
    out(0x1F7, 0x20);

    uint16_t *target = (uint16_t*) ptr;
    for (uint32_t i = 0; i < block_count; i++) {
        ATA_busy_wait();
        ATA_DRQ_wait();
        for (uint32_t j = 0; j < HALF_BLOCK_SIZE; j++)
            target[j] = in16(0x1F0);
        target += HALF_BLOCK_SIZE;
    }
}

void write_blocks_drive(const void *ptr, uint32_t lba, uint8_t block_count, uint8_t drive) {
    ata_select_drive(drive, lba);
    out(0x1F2, block_count);
    out(0x1F3, (uint8_t) lba);
    out(0x1F4, (uint8_t) (lba >> 8));
    out(0x1F5, (uint8_t) (lba >> 16));
    out(0x1F7, 0x30);

    for (uint32_t i = 0; i < block_count; i++) {
        ATA_busy_wait();
        ATA_DRQ_wait();
        for (uint32_t j = 0; j < HALF_BLOCK_SIZE; j++)
            out16(0x1F0, ((uint16_t*) ptr)[HALF_BLOCK_SIZE*i + j]);
    }
}

void read_blocks(void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    read_blocks_drive(ptr, logical_block_address, block_count, 0);
}

void write_blocks(const void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    write_blocks_drive(ptr, logical_block_address, block_count, 0);
}