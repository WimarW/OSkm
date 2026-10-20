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

bool is_directory_empty(uint32_t inode) {
    if (inode == 0 || inode > sb.s_inodes_count) return false;

    struct BlockBuffer b;
    struct EXT2Inode dir;
    uint32_t g     = inode_to_bgd(inode);
    uint32_t local = inode_to_local(inode);
    read_blocks(&b, bgd_table.table[g].bg_inode_table + local / INODES_PER_TABLE, 1);
    memcpy(&dir, b.buf + (local % INODES_PER_TABLE) * INODE_SIZE, INODE_SIZE);

    if ((dir.i_mode & EXT2_S_IFDIR) == 0) return false;
    if (dir.i_block[0] == 0) return true;

    read_blocks(&b, dir.i_block[0], 1);

    uint32_t off = 0;
    while (off < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *e = (struct EXT2DirectoryEntry *)(b.buf + off);
        if (e->rec_len == 0) break;

        if (e->inode != 0) {
            char *name = get_entry_name(e);
            bool is_dot    = (e->name_len == 1 && name[0] == '.');
            bool is_dotdot = (e->name_len == 2 && name[0] == '.' && name[1] == '.');

            if (!is_dot && !is_dotdot) return false;
        }
        off += e->rec_len;
    }
    return true;
}

void deallocate_blocks(void *loc, uint32_t blocks) {
    if (loc == NULL || blocks == 0) return;

    uint32_t *i_block = (uint32_t *)loc;

    uint32_t has_indirect = (blocks > 12) ? 1 : 0;
    uint32_t data_blocks  = has_indirect ? blocks - 1 : blocks;
    if (data_blocks > 12 + (BLOCK_SIZE / sizeof(uint32_t))) return;   

    uint32_t list[12 + (BLOCK_SIZE / sizeof(uint32_t)) + 1];
    uint32_t count = 0;
    struct BlockBuffer b;

    for (uint32_t i = 0; i < data_blocks && i < 12; i++) {
        if (i_block[i] != 0) list[count++] = i_block[i]; 
    }

    if (has_indirect && i_block[12] != 0) {
        read_blocks(&b, i_block[12], 1);
        uint32_t *ptrs = (uint32_t *)b.buf;
        for (uint32_t i = 12; i < data_blocks; i++) {
            if (ptrs[i - 12] != 0) list[count++] = ptrs[i - 12];
        }
        list[count++] = i_block[12];
    }

    uint32_t groups = sb.s_inodes_count / sb.s_inodes_per_group;

    for (uint32_t k = 0; k < count; k++) {
        uint32_t g = list[k] / sb.s_blocks_per_group;
        uint32_t i = list[k] % sb.s_blocks_per_group;
        if (g >= groups) continue;

        read_blocks(&b, bgd_table.table[g].bg_block_bitmap, 1);

        if (b.buf[i / 8] & (1u << (i % 8))) {
            b.buf[i / 8] &= (uint8_t)~(1u << (i % 8));
            write_blocks(&b, bgd_table.table[g].bg_block_bitmap, 1);
            bgd_table.table[g].bg_free_blocks_count++;
            sb.s_free_blocks_count++;
        }
    }

}



void deallocate_node (uint32_t inode){
    if (inode == 0 || inode > sb.s_inodes_count) return;
    
    uint32_t g     = inode_to_bgd(inode);
    uint32_t local = inode_to_local(inode);
    uint32_t block = bgd_table.table[g].bg_inode_table + local / INODES_PER_TABLE;
    uint32_t index = local % INODES_PER_TABLE;

    struct EXT2Inode node;
    struct BlockBuffer b;
    read_blocks(&b, block, 1);
    // Copies Inode information to node var.
    memcpy(&node, (b.buf + index * INODE_SIZE), INODE_SIZE);

    // Deallocates Memory blocks
    if (node.i_blocks > 0) deallocate_blocks(node.i_block, node.i_blocks);

    if ((node.i_mode & EXT2_S_IFDIR) != 0 && bgd_table.table[g].bg_used_dirs_count > 0) {
        bgd_table.table[g].bg_used_dirs_count--;
    }


    struct BlockBuffer bitmap;
    read_blocks(&bitmap, bgd_table.table[g].bg_inode_bitmap, 1);

    // Constructs a mask where everything but the allocation bit is 1, so if local % 8 == 2
    // ~ (00000100) --> (11111011)
    // The mask is used to change the inode's allocation bit to 0
    bitmap.buf[local / 8] &= (uint8_t)~(1u << (local % 8));
    write_blocks(&bitmap, bgd_table.table[g].bg_inode_bitmap, 1);

    // zeroes out inode data on disk
    memset(&node, 0, sizeof(struct EXT2Inode));
    sync_node(&node, inode);

    bgd_table.table[g].bg_free_inodes_count++;
    sb.s_free_inodes_count++;
    sync_metadata();
}

int8_t write(struct EXT2DriverRequest *request) {
    if (request == NULL || request->name == NULL || request->name_len == 0) return -1;

    bool is_dir = (request->buffer_size == 0);

    if (request->parent_inode == 0 || request->parent_inode > sb.s_inodes_count) return 2;

    struct BlockBuffer b;
    struct EXT2Inode parent;
    {
        uint32_t pg    = inode_to_bgd(request->parent_inode);
        uint32_t plc   = inode_to_local(request->parent_inode);
        read_blocks(&b, bgd_table.table[pg].bg_inode_table + plc / INODES_PER_TABLE, 1);
        memcpy(&parent, b.buf + (plc % INODES_PER_TABLE) * INODE_SIZE, INODE_SIZE);
    }
    if ((parent.i_mode & EXT2_S_IFDIR) == 0 || parent.i_block[0] == 0) return 2;


    struct BlockBuffer dir_buf;
    read_blocks(&dir_buf, parent.i_block[0], 1);

    uint32_t need = (sizeof(struct EXT2DirectoryEntry) + request->name_len + 3u) & ~3u;
    struct EXT2DirectoryEntry *slot = NULL;
    uint32_t slot_used = 0;

    for (uint32_t off = 0; off + sizeof(struct EXT2DirectoryEntry) <= BLOCK_SIZE; ) {
        struct EXT2DirectoryEntry *e = (struct EXT2DirectoryEntry *)(dir_buf.buf + off);
        if (e->rec_len < sizeof(struct EXT2DirectoryEntry) || (off + e->rec_len) > BLOCK_SIZE) {
            break;
        }

        if (e->inode != 0 && e->name_len == request->name_len &&
            memcmp(get_entry_name(e), request->name, request->name_len) == 0) {
            if ((e->file_type == EXT2_FT_DIR) == is_dir) return 1;
        }

        if (slot == NULL) {
            uint32_t used = (e->inode == 0)
                ? 0
                : ((sizeof(struct EXT2DirectoryEntry) + e->name_len + 3u) & ~3u);
            if (e->rec_len >= used + need) { 
                slot = e; 
                slot_used = used; 
            }
        }
        off += e->rec_len;
    }
    if (slot == NULL) return -1;

    // Check if buffer_size + BLOCK_SIZE - 1 overflows uint32_t
    if (!is_dir && request->buffer_size > (12u + BLOCK_SIZE / sizeof(uint32_t)) * BLOCK_SIZE) return -1; 
    uint32_t data_blocks = is_dir ? 1 : (request->buffer_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (data_blocks > 12 + (BLOCK_SIZE / sizeof(uint32_t))) return -1;
    uint32_t has_indirect  = (data_blocks > 12) ? 1 : 0;
    uint32_t total_blocks  = data_blocks + has_indirect;

    if (sb.s_free_inodes_count < 1 || sb.s_free_blocks_count < total_blocks) return -1;

    uint32_t groups = sb.s_inodes_count / sb.s_inodes_per_group;

    
    uint32_t ino = 0;
    for (uint32_t g = 0; g < groups && ino == 0; g++) {
        if (bgd_table.table[g].bg_free_inodes_count == 0) continue;
        struct BlockBuffer ibm;
        read_blocks(&ibm, bgd_table.table[g].bg_inode_bitmap, 1);
        for (uint32_t i = 0; i < sb.s_inodes_per_group; i++) {
            if (!(ibm.buf[i / 8] & (1u << (i % 8)))) {
                ibm.buf[i / 8] |= (uint8_t)(1u << (i % 8));
                write_blocks(&ibm, bgd_table.table[g].bg_inode_bitmap, 1);
                bgd_table.table[g].bg_free_inodes_count--;
                sb.s_free_inodes_count--;
                ino = g * sb.s_inodes_per_group + i + 1;
                break;
            }
        }
    }
    if (ino == 0) return -1;

    uint32_t blocks[12 + (BLOCK_SIZE / sizeof(uint32_t)) + 1];
    for (uint32_t k = 0; k < total_blocks; k++) {
        uint32_t found = 0;
        for (uint32_t g = 0; g < groups && !found; g++) {
            if (bgd_table.table[g].bg_free_blocks_count == 0) continue;
            struct BlockBuffer bbm;
            read_blocks(&bbm, bgd_table.table[g].bg_block_bitmap, 1);
            for (uint32_t i = 0; i < sb.s_blocks_per_group; i++) {
                if (!(bbm.buf[i / 8] & (1u << (i % 8)))) {
                    bbm.buf[i / 8] |= (uint8_t)(1u << (i % 8));
                    write_blocks(&bbm, bgd_table.table[g].bg_block_bitmap, 1);
                    bgd_table.table[g].bg_free_blocks_count--;
                    sb.s_free_blocks_count--;
                    blocks[k] = g * sb.s_blocks_per_group + i;
                    found = 1;
                    break;
                }
            }
        }
        if (!found) return -1;
    }


    struct EXT2Inode node;
    memset(&node, 0, sizeof(node));
    node.i_mode   = is_dir ? EXT2_S_IFDIR : EXT2_S_IFREG;
    node.i_size   = is_dir ? BLOCK_SIZE : request->buffer_size;
    node.i_blocks = total_blocks;
    for (uint32_t i = 0; i < data_blocks && i < 12; i++) node.i_block[i] = blocks[i];

    if (has_indirect) {
        uint32_t indirect = blocks[data_blocks];
        node.i_block[12] = indirect;

        memset(&b, 0, BLOCK_SIZE);
        uint32_t *ptrs = (uint32_t *)b.buf;
        for (uint32_t i = 12; i < data_blocks; i++) ptrs[i - 12] = blocks[i];
        write_blocks(&b, indirect, 1);
    }


    if (is_dir) {
        memset(&b, 0, BLOCK_SIZE);

        uint32_t dot_len = (sizeof(struct EXT2DirectoryEntry) + 1u + 3u) & ~3u;
        struct EXT2DirectoryEntry *dot = (struct EXT2DirectoryEntry *)b.buf;
        dot->inode     = ino;
        dot->name_len  = 1;
        dot->file_type = EXT2_FT_DIR;
        dot->rec_len   = dot_len;
        memcpy(get_entry_name(dot), ".", 1);

        struct EXT2DirectoryEntry *dd = (struct EXT2DirectoryEntry *)(b.buf + dot_len);
        dd->inode     = request->parent_inode;
        dd->name_len  = 2;
        dd->file_type = EXT2_FT_DIR;
        dd->rec_len   = BLOCK_SIZE - dot_len;
        memcpy(get_entry_name(dd), "..", 2);

        write_blocks(&b, blocks[0], 1);
        bgd_table.table[inode_to_bgd(ino)].bg_used_dirs_count++;
    } else {
        for (uint32_t i = 0; i < data_blocks; i++) {
            memset(&b, 0, BLOCK_SIZE);
            uint32_t remaining = request->buffer_size - i * BLOCK_SIZE;
            uint32_t n = remaining < BLOCK_SIZE ? remaining : BLOCK_SIZE;
            memcpy(b.buf, (uint8_t *)request->buf + i * BLOCK_SIZE, n);
            write_blocks(&b, blocks[i], 1);
        }
    }

    sync_node(&node, ino);

    struct EXT2DirectoryEntry *ne;
    if (slot_used == 0) {
        ne = slot;
    } else {
        ne = (struct EXT2DirectoryEntry *)((uint8_t *)slot + slot_used);
        ne->rec_len   = slot->rec_len - slot_used;
        slot->rec_len = slot_used;
    }
    ne->inode     = ino;
    ne->name_len  = request->name_len;
    ne->file_type = is_dir ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
    memcpy(get_entry_name(ne), request->name, request->name_len);
    write_blocks(&dir_buf, parent.i_block[0], 1);

    sync_metadata();
    return 0;
}


int8_t delete(struct EXT2DriverRequest request){

    if (request.name == NULL || request.name_len == 0) {
        return -1; // Invalid request
    }

    if (request.parent_inode == 0 || request.parent_inode > sb.s_inodes_count) {
        return 3; // Parent Folder invalid
    }

    if ((request.name_len == 1 && request.name[0] == '.') || (request.name_len == 2 && request.name[0] == '.' && request.name[1] == '.')) return -1;
    

    uint32_t g     = inode_to_bgd(request.parent_inode);
    uint32_t local = inode_to_local(request.parent_inode);
    uint32_t block = bgd_table.table[g].bg_inode_table + local / INODES_PER_TABLE;
    uint32_t index = local % INODES_PER_TABLE;
    struct EXT2Inode parent;
    struct BlockBuffer b;
    read_blocks(&b, block, 1);
    // Copies Inode information to node var.
    memcpy(&parent, (b.buf + index * INODE_SIZE), INODE_SIZE);

    if ((parent.i_mode & EXT2_S_IFDIR) == 0 || parent.i_block[0] == 0) {
        return 3; // Parent folder invalid
    }

    struct BlockBuffer dir_buf;
    read_blocks(&dir_buf, parent.i_block[0], 1);

    struct EXT2DirectoryEntry *prev = NULL;
    struct EXT2DirectoryEntry *curr = (struct EXT2DirectoryEntry *)dir_buf.buf;
    uint32_t offset = 0;
    bool found = false;

    // Find target to be deleted
    while (offset < BLOCK_SIZE && curr->rec_len != 0) {
        if (curr->inode != 0 && curr->name_len == request.name_len &&
            memcmp(get_entry_name(curr), request.name, request.name_len) == 0) {
            bool entry_is_dir = (curr->file_type == EXT2_FT_DIR);
            if (entry_is_dir == request.is_directory) {
                found = true;
                break;
            }
        }
        offset += curr->rec_len;
        prev = curr;
        curr = get_next_directory_entry(curr);
    }

    if (!found) return 1;
    uint32_t target_inode = curr->inode;
    if(target_inode == 0 || target_inode > sb.s_inodes_count) return 1;

    struct EXT2Inode target;
    g = inode_to_bgd(target_inode);
    local = inode_to_local(target_inode);
    block = bgd_table.table[g].bg_inode_table + local / INODES_PER_TABLE;
    index = local % INODES_PER_TABLE;
    
    read_blocks(&b, block, 1);
    memcpy(&target, (b.buf + index * INODE_SIZE), INODE_SIZE);

    if ((target.i_mode & EXT2_S_IFDIR) && !is_directory_empty(target_inode)) return 2; // directory not empty

    uint16_t freed_len = curr->rec_len;
    curr->inode = 0;
    if (prev != NULL) prev->rec_len += freed_len;
    write_blocks(&dir_buf, parent.i_block[0], 1);
    deallocate_node(target_inode);

    return 0;
}



