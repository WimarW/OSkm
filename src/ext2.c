#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/stdlib/string.h"
#include "header/filesystem/ext2.h"
#define FS_HEADER_BLOCKS      3u
#define META_BLOCKS_PER_GROUP (2u + INODES_TABLE_BLOCK_COUNT)

const uint8_t fs_signature[BLOCK_SIZE] = {
    'C', 'o', 'u', 'r', 's', 'e', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',  ' ',
    'D', 'e', 's', 'i', 'g', 'n', 'e', 'd', ' ', 'b', 'y', ' ', ' ', ' ', ' ',  ' ',
    'L', 'a', 'b', ' ', 'S', 'i', 's', 't', 'e', 'r', ' ', 'I', 'T', 'B', ' ',  ' ',
    'M', 'a', 'd', 'e', ' ', 'w', 'i', 't', 'h', ' ', '<', '3', ' ', ' ', ' ',  ' ',
    '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '2', '0', '2', '5', '\n',
    [BLOCK_SIZE-2] = 'O',
    [BLOCK_SIZE-1] = 'k',
};

static struct EXT2Superblock sb;
static struct EXT2BlockGroupDescriptorTable bgd_table;

bool is_empty_storage(void) {
    struct BlockBuffer boot_sector;
    read_blocks(&boot_sector, BOOT_SECTOR, 1);
    return memcmp(boot_sector.buf, fs_signature, BLOCK_SIZE) != 0;
}

void initialize_filesystem_ext2(void) {
    if (is_empty_storage()) {
        create_ext2();
    } else {
        struct BlockBuffer b;
        read_blocks(&b, 1, 1);
        memcpy(&sb, b.buf, sizeof(sb));
        read_blocks(&b, 2, 1);
        memcpy(&bgd_table, b.buf, sizeof(bgd_table));
    }
}


static uint32_t group_meta_start(uint32_t g) {
    return (g == 0) ? FS_HEADER_BLOCKS : g * BLOCKS_PER_GROUP;
}

static void sync_metadata(void) {
    struct BlockBuffer b;
    memset(&b, 0, BLOCK_SIZE);
    memcpy(b.buf, &sb, sizeof(sb));
    write_blocks(&b, 1, 1);
    memset(&b, 0, BLOCK_SIZE);
    memcpy(b.buf, &bgd_table, sizeof(bgd_table));
    write_blocks(&b, 2, 1);
}

uint32_t inode_to_bgd(uint32_t inode) {
    return (inode - 1) / INODES_PER_GROUP;
}

uint32_t inode_to_local(uint32_t inode) {
    return (inode - 1) % INODES_PER_GROUP;
}

uint16_t get_entry_record_len(uint8_t name_len) {
    uint32_t len = sizeof(struct EXT2DirectoryEntry) + name_len;
    return (uint16_t)((len + 3u) & ~3u);
}

char *get_entry_name(void *entry) {
    return (char *)entry + sizeof(struct EXT2DirectoryEntry);
}

struct EXT2DirectoryEntry *get_next_directory_entry(struct EXT2DirectoryEntry *entry) {
    return (struct EXT2DirectoryEntry *)((uint8_t *)entry + entry->rec_len);
}

static uint32_t allocate_block(uint32_t prefered_bgd) {
    struct BlockBuffer bitmap;
    for (uint32_t n = 0; n < GROUPS_COUNT; n++) {
        uint32_t g = (prefered_bgd + n) % GROUPS_COUNT;
        if (bgd_table.table[g].bg_free_blocks_count == 0) continue;

        read_blocks(&bitmap, bgd_table.table[g].bg_block_bitmap, 1);
        for (uint32_t i = 0; i < BLOCKS_PER_GROUP; i++) {
            if (!(bitmap.buf[i / 8] & (1u << (i % 8)))) {
                bitmap.buf[i / 8] |= (uint8_t)(1u << (i % 8));
                write_blocks(&bitmap, bgd_table.table[g].bg_block_bitmap, 1);
                bgd_table.table[g].bg_free_blocks_count--;
                sb.s_free_blocks_count--;
                sync_metadata();
                return g * BLOCKS_PER_GROUP + i;
            }
        }
    }
    return 0;
}

uint32_t allocate_node(void) {
    struct BlockBuffer bitmap;
    for (uint32_t g = 0; g < GROUPS_COUNT; g++) {
        if (bgd_table.table[g].bg_free_inodes_count == 0) continue;

        read_blocks(&bitmap, bgd_table.table[g].bg_inode_bitmap, 1);
        for (uint32_t i = 0; i < INODES_PER_GROUP; i++) {
            if (!(bitmap.buf[i / 8] & (1u << (i % 8)))) {
                bitmap.buf[i / 8] |= (uint8_t)(1u << (i % 8));
                write_blocks(&bitmap, bgd_table.table[g].bg_inode_bitmap, 1);
                bgd_table.table[g].bg_free_inodes_count--;
                sb.s_free_inodes_count--;
                sync_metadata();
                return g * INODES_PER_GROUP + i + 1;
            }
        }
    }
    return 0;
}

void sync_node(struct EXT2Inode *node, uint32_t inode) {
    uint32_t g     = inode_to_bgd(inode);
    uint32_t local = inode_to_local(inode);
    uint32_t block = bgd_table.table[g].bg_inode_table + local / INODES_PER_TABLE;
    uint32_t index = local % INODES_PER_TABLE;

    struct BlockBuffer b;
    read_blocks(&b, block, 1);
    memcpy(b.buf + index * INODE_SIZE, node, INODE_SIZE);
    write_blocks(&b, block, 1);
}

void init_directory_table(struct EXT2Inode *node, uint32_t inode, uint32_t parent_inode) {
    struct BlockBuffer table;
    memset(&table, 0, BLOCK_SIZE);

    uint32_t block = allocate_block(inode_to_bgd(inode));

    struct EXT2DirectoryEntry *dot = (struct EXT2DirectoryEntry *)table.buf;
    dot->inode     = inode;
    dot->name_len  = 1;
    dot->file_type = EXT2_FT_DIR;
    dot->rec_len   = get_entry_record_len(1);
    memcpy(get_entry_name(dot), ".", 1);

    struct EXT2DirectoryEntry *dotdot = get_next_directory_entry(dot);
    dotdot->inode     = parent_inode;
    dotdot->name_len  = 2;
    dotdot->file_type = EXT2_FT_DIR;
    dotdot->rec_len   = get_entry_record_len(2);
    memcpy(get_entry_name(dotdot), "..", 2);

    struct EXT2DirectoryEntry *first_child = get_next_directory_entry(dotdot);
    first_child->inode   = 0;
    first_child->rec_len = (uint16_t)(BLOCK_SIZE - ((uint8_t *)first_child - table.buf));

    write_blocks(&table, block, 1);

    memset(node, 0, sizeof(*node));
    node->i_mode     = EXT2_S_IFDIR;
    node->i_size     = BLOCK_SIZE;
    node->i_blocks   = 1;
    node->i_block[0] = block;

    bgd_table.table[inode_to_bgd(inode)].bg_used_dirs_count++;
}

void create_ext2(void) {
    struct BlockBuffer buf;
    uint32_t total_free_blocks = 0;

    memset(&sb, 0, sizeof(sb));
    memset(&bgd_table, 0, sizeof(bgd_table));

    for (uint32_t g = 0; g < GROUPS_COUNT; g++) {
        uint32_t group_start = g * BLOCKS_PER_GROUP;
        uint32_t meta        = group_meta_start(g);
        uint32_t used        = (meta - group_start) + META_BLOCKS_PER_GROUP;

        bgd_table.table[g].bg_block_bitmap      = meta;
        bgd_table.table[g].bg_inode_bitmap      = meta + 1;
        bgd_table.table[g].bg_inode_table       = meta + 2;
        bgd_table.table[g].bg_free_blocks_count = BLOCKS_PER_GROUP - used;
        bgd_table.table[g].bg_free_inodes_count = INODES_PER_GROUP;
        bgd_table.table[g].bg_used_dirs_count   = 0;
        total_free_blocks += BLOCKS_PER_GROUP - used;

        memset(&buf, 0, BLOCK_SIZE);
        for (uint32_t i = 0; i < used; i++) {
            buf.buf[i / 8] |= (uint8_t)(1u << (i % 8));
        }
        write_blocks(&buf, meta, 1);

        memset(&buf, 0, BLOCK_SIZE);
        write_blocks(&buf, meta + 1, 1);

        for (uint32_t b = 0; b < INODES_TABLE_BLOCK_COUNT; b++) {
            write_blocks(&buf, meta + 2 + b, 1);
        }
    }

    sb.s_inodes_count      = INODES_PER_GROUP * GROUPS_COUNT;
    sb.s_blocks_count      = DISK_SPACE / BLOCK_SIZE;
    sb.s_free_blocks_count = total_free_blocks;
    sb.s_free_inodes_count = INODES_PER_GROUP * GROUPS_COUNT;
    sb.s_first_data_block  = 1;
    sb.s_first_ino         = 1;
    sb.s_blocks_per_group  = BLOCKS_PER_GROUP;
    sb.s_frags_per_group   = BLOCKS_PER_GROUP;
    sb.s_inodes_per_group  = INODES_PER_GROUP;
    sb.s_magic             = EXT2_SUPER_MAGIC;

    sync_metadata();

    uint32_t root_inode = allocate_node();
    struct EXT2Inode root;
    init_directory_table(&root, root_inode, root_inode);
    sync_node(&root, root_inode);
    sync_metadata();

    write_blocks(fs_signature, BOOT_SECTOR, 1);
}

static bool load_inode(uint32_t inode_number, struct EXT2Inode *result) {
    // MENERIMA NOMOR INODE DAN POINTER KE RAM TEMPAT NYIMPAN
    // CEK APAKAH OUT OF RANGE
    if (result == NULL || inode_number == 0 ||
        inode_number > GROUPS_COUNT * INODES_PER_GROUP)
        return false;

    // MENENTUKAN BLOCK GROUP (112 INODES PER GROUP)
    uint32_t group = inode_to_bgd(inode_number);
    uint32_t local = inode_to_local(inode_number);

    // AMBIL DATA BGD
    struct EXT2BlockGroupDescriptor *bgd = &bgd_table.table[group];

    
    if (bgd->bg_inode_bitmap == 0 ||
        bgd->bg_inode_bitmap >= DISK_SPACE / BLOCK_SIZE)
        return false;

    // MENGECEK APAKAH BITMAP DIGUNAKAN
    struct BlockBuffer bitmap;
    read_blocks(&bitmap, bgd->bg_inode_bitmap, 1);
    if ((bitmap.buf[local / 8u] & (1u << (local % 8u))) == 0)
        return false;

    // CARI TEMPAT INODE DISIMPAN
    uint32_t table_block = bgd->bg_inode_table + local / INODES_PER_TABLE;
    if (table_block == 0 || table_block >= DISK_SPACE / BLOCK_SIZE)
        return false;

    // TENTUUKAN OFFSET
    struct BlockBuffer block;
    read_blocks(&block, table_block, 1);
    uint32_t offset = (local % INODES_PER_TABLE) * INODE_SIZE;
    memcpy(result, block.buf + offset, sizeof(*result));
    return true;
}

static uint32_t read_indirect_pointer(uint32_t block_number, uint32_t index) {
    const uint32_t pointers_per_block = BLOCK_SIZE / sizeof(uint32_t);
    if (block_number == 0 || block_number >= DISK_SPACE / BLOCK_SIZE ||
        index >= pointers_per_block)
        return 0;

    struct BlockBuffer block;
    uint32_t pointer;
    read_blocks(&block, block_number, 1);
    memcpy(&pointer, block.buf + index * sizeof(pointer), sizeof(pointer));
    return pointer;
}

static uint32_t inode_data_block(const struct EXT2Inode *inode, uint32_t index) {
    const uint32_t N = BLOCK_SIZE / sizeof(uint32_t);
    if (index < 12u)
        return inode->i_block[index];

    index -= 12u;
    if (index < N)
        return read_indirect_pointer(inode->i_block[12], index);

    index -= N;
    if (index < N * N) {
        uint32_t indirect = read_indirect_pointer(inode->i_block[13], index / N);
        return read_indirect_pointer(indirect, index % N);
    }

    index -= N * N;
    if (index < N * N * N) {
        uint32_t double_indirect = read_indirect_pointer(
            inode->i_block[14], index / (N * N));
        uint32_t indirect = read_indirect_pointer(
            double_indirect, (index / N) % N);
        return read_indirect_pointer(indirect, index % N);
    }

    return 0;
}

static int8_t find_child_inode(
    uint32_t parent_inode,
    const char *name,
    uint8_t name_len,
    bool want_directory,
    struct EXT2Inode *result
) {
    if (name == NULL || name_len == 0 || result == NULL)
        return -1;

    struct EXT2Inode parent;
    if (!load_inode(parent_inode, &parent) ||
        (parent.i_mode & 0xF000u) != EXT2_S_IFDIR)
        return 3;

    if (parent.i_size == 0 || parent.i_size > DISK_SPACE)
        return 3;

    bool found_other_type = false;
    uint32_t dir_block_count = (parent.i_size + BLOCK_SIZE - 1u) / BLOCK_SIZE;

    for (uint32_t i = 0; i < dir_block_count; i++) {
        uint32_t disk_block = inode_data_block(&parent, i);
        if (disk_block == 0 || disk_block >= DISK_SPACE / BLOCK_SIZE)
            return 3;

        struct BlockBuffer entries;
        read_blocks(&entries, disk_block, 1);

        uint32_t start = i * BLOCK_SIZE;
        uint32_t bytes_in_block = parent.i_size - start;
        if (bytes_in_block > BLOCK_SIZE)
            bytes_in_block = BLOCK_SIZE;

        uint32_t offset = 0;
        while (offset + sizeof(struct EXT2DirectoryEntry) <= bytes_in_block) {
            struct EXT2DirectoryEntry *entry =
                (struct EXT2DirectoryEntry *)(entries.buf + offset);

            if (entry->rec_len < sizeof(*entry) ||
                (entry->rec_len & 3u) != 0 ||
                entry->rec_len > bytes_in_block - offset ||
                entry->name_len > entry->rec_len - sizeof(*entry))
                return -1;

            if (entry->inode != 0 && entry->name_len == name_len &&
                memcmp(get_entry_name(entry), name, name_len) == 0) {
                bool is_directory = entry->file_type == EXT2_FT_DIR;
                bool is_file = entry->file_type == EXT2_FT_REG_FILE;
                bool type_matches = want_directory ? is_directory : is_file;

                if (!type_matches) {
                    found_other_type = true;
                } else {
                    if (!load_inode(entry->inode, result))
                        return -1;
                    uint16_t expected_mode =
                        want_directory ? EXT2_S_IFDIR : EXT2_S_IFREG;
                    if ((result->i_mode & 0xF000u) != expected_mode)
                        return -1;
                    return 0;
                }
            }

            offset += entry->rec_len;
        }
    }

    return found_other_type ? 1 : 2;
}

int8_t read(struct EXT2DriverRequest request) {
    struct EXT2Inode file;
    int8_t result = find_child_inode(
        request.parent_inode, request.name, request.name_len,
        false, &file
    );

    if (result == 3) return 4;
    if (result == 2) return 3;
    if (result != 0) return result;

    if (request.buffer_size < file.i_size)
        return 2;
    if (file.i_size != 0 && request.buf == NULL)
        return -1;
    if (file.i_size > DISK_SPACE)
        return -1;

    uint32_t block_count = (file.i_size + BLOCK_SIZE - 1u) / BLOCK_SIZE;
    for (uint32_t i = 0; i < block_count; i++) {
        uint32_t disk_block = inode_data_block(&file, i);
        if (disk_block == 0 || disk_block >= DISK_SPACE / BLOCK_SIZE)
            return -1;

        struct BlockBuffer block;
        read_blocks(&block, disk_block, 1);

        uint32_t remaining = file.i_size - i * BLOCK_SIZE;
        uint32_t bytes_to_copy = remaining < BLOCK_SIZE ? remaining : BLOCK_SIZE;
        memcpy((uint8_t *)request.buf + i * BLOCK_SIZE,
               block.buf, bytes_to_copy);
    }
    return 0;
}

int8_t read_directory(struct EXT2DriverRequest *prequest) {
    if (prequest == NULL)
        return -1;

    struct EXT2Inode directory;
    int8_t result = find_child_inode(
        prequest->parent_inode, prequest->name,
        prequest->name_len, true, &directory
    );

    if (result != 0)
        return result;

    if (prequest->buf == NULL || prequest->buffer_size < sizeof(directory))
        return -1;

    memcpy(prequest->buf, &directory, sizeof(directory));
    return 0;
}