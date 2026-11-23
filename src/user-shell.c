#include <stdint.h>
#include "header/filesystem/ext2.h"
#include "header/stdlib/string.h"

#define BLOCK_COUNT 16
#define MAX_PATH_LEN 256
#define MAX_ARGS 32
#define ROOT_INODE 1
#define BUFFER_SIZE (BLOCK_SIZE*BLOCK_COUNT) // 8 KiB I/O buffer
#define MAX_LINE_LEN 512

#define DEFAULT_COLOR 0x0F // White text, black bg
#define PROMPT_COLOR 0x0A // Green text, black bg
#define DIR_COLOR 0x0B // Cyan text, black bg
#define ERR_COLOR 0x0C // Red text, black bg

#define REGEX_MAX_STEPS 5000

void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile(
        "int $0x30"
        :
        : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx)
        : "memory"
    );
}

// Utility Functions (Mainly String related)
char *get_entry_name(void *entry) {
    return (char *)entry + sizeof(struct EXT2DirectoryEntry);
}

size_t strlen(const char *str){
    size_t len = 0;
    while(str[len]) len++;
    return len;
}

int strcmp(const char *s1, const char *s2){
    while (*s1 && (*s1 == *s2)){
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*) s2;
}

int strncmp(const char *s1, const char *s2, size_t n){
    while (n && *s1 && *s1 == *s2) { 
        s1++; 
        s2++; 
        n--; 
    }
    return n ? *(const unsigned char*)s1 - *(const unsigned char*)s2 : 0;
}

char *strcpy(char *dest, const char *src){
    char *ret = dest;
    while((*dest++ = *src++));
    return ret;
}

char *strncpy(char *dest, const char *src, size_t n) {
    char *ret = dest;
    while (n && (*dest++ = *src++)) n--;
    while (n--) *dest++ = '\0';
    return ret;
}

char *strcat(char *dest, const char *src) {
    char *ret = dest;
    while (*dest) dest++;
    while ((*dest++ = *src++));
    return ret;
}

char *strchr(const char *s, int c) {
    while (*s != (char)c) {
        if (!*s++) return NULL;
    }
    return (char*)s;
}

char *strstr(const char *haystack, const char *needle) {
    size_t nlen = strlen(needle);
    if (!nlen) return (char*)haystack;
    while (*haystack) {
        if (strncmp(haystack, needle, nlen) == 0) {
            return (char*)haystack;
        }
        haystack++;
    }
    return NULL;
}

// System Call Wrappers
void sys_print(const char *str, uint32_t color){
    syscall(6, (uint32_t)str, strlen(str), color);
}

void sys_putc(char c, uint32_t color) {
    syscall(5, (uint32_t)(uint8_t)c, color, 0);
}

char sys_getc(void){
    char c = 0;
    syscall(4, (uint32_t)&c, 0, 0);
    return c;
}

void sys_clear_screen(void) {
    syscall(8, 0, 0, 0);
}

int8_t fs_read(uint32_t parent_inode, char *name, void *buf, uint32_t size){
    int8_t retcode = -1;
    struct EXT2DriverRequest request = {
        .buf = buf,
        .name = name,
        .name_len = (uint8_t)strlen(name),
        .parent_inode = parent_inode,
        .buffer_size = size,
        .is_directory = false
    };
    syscall(0, (uint32_t)&request, (uint32_t)&retcode, 0);
    return retcode;
}

int8_t fs_read_directory(uint32_t parent_inode, void *buf, uint32_t size){
    int8_t retcode = -1;
    struct EXT2DriverRequest request = {
        .buf = buf,
        .name = ".",
        .name_len = 1,
        .parent_inode = parent_inode,
        .buffer_size = size,
        .is_directory = true
    };
    syscall(1, (uint32_t)&request, (uint32_t)&retcode, 0);
    return retcode;
}

int8_t fs_write_file(uint32_t parent_inode, char *name, void *buf, uint32_t size){
    int8_t retcode = -1;
    struct EXT2DriverRequest request = {
        .buf = buf,
        .name = name,
        .name_len = (uint8_t)strlen(name),
        .parent_inode = parent_inode,
        .buffer_size = size,
        .is_directory = false 
    };
    syscall(2, (uint32_t)&request, (uint32_t)&retcode, 0);
    return retcode;
}

int8_t fs_mkdir(uint32_t parent_inode, char *name){
    int8_t retcode = -1;
    struct EXT2DriverRequest request = {
        .buf = NULL,
        .name = name,
        .name_len = (uint8_t)strlen(name),
        .parent_inode = parent_inode,
        .buffer_size = 0,
        .is_directory = true 
    };
    syscall(2, (uint32_t)&request, (uint32_t)&retcode, 0);
    return retcode;
}

int8_t fs_delete(uint32_t parent_inode, char *name, bool is_dir){
    int8_t retcode = -1;
    struct EXT2DriverRequest request = {
        .buf = NULL,
        .name = name,
        .name_len = (uint8_t)strlen(name),
        .parent_inode = parent_inode,
        .buffer_size = 0,
        .is_directory = is_dir 
    };
    syscall(3, (uint32_t)&request, (uint32_t)&retcode, 0);
    return retcode;
}


// State and Buffer Management Functions
static char current_directory_path[MAX_PATH_LEN] = "/";
static uint32_t current_inode = ROOT_INODE;

static char pipe_buf_1[BUFFER_SIZE];
static char pipe_buf_2[BUFFER_SIZE];
static uint8_t temp_buf[BUFFER_SIZE];

void buf_append(char *buf, size_t *offset, size_t max, const char *str) {
    if (max == 0 || *offset >= max - 1) return;
    size_t slen = strlen(str);
    size_t avail = (max - 1) - *offset;
    if (slen > avail) slen = avail;
    memcpy(buf + *offset, str, slen);
    *offset += slen;
    buf[*offset] = '\0';
}

bool lookup_entry(uint32_t dir_inode, const char *name, uint32_t *out_inode, uint8_t *out_type) {
    if (fs_read_directory(dir_inode, temp_buf, sizeof(temp_buf)) != 0) return false;

    uint32_t offset = 0;
    while(offset + sizeof(struct EXT2DirectoryEntry) <= sizeof(temp_buf)){
        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)(temp_buf + offset);
        if(entry->rec_len == 0) break;
        
        if (entry->inode != 0 && entry->name_len == strlen(name)){
            char *entry_name = get_entry_name(entry);
            if(strncmp(entry_name, name, entry->name_len) == 0){
                if (out_inode) *out_inode = entry->inode;
                if (out_type) *out_type = entry->file_type;
                return true;
            }
        }
        offset += entry->rec_len;
    }
    return false;
}
// RegEx & Case-Insensitive Helpers
static char to_lower_ascii(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static bool char_eq(char a, char b, bool ignore_case) {
    if (ignore_case) return to_lower_ascii(a) == to_lower_ascii(b);
    return a == b;
}

static const char *strstr_ci(const char *haystack, const char *needle) {
    size_t nlen = strlen(needle);
    if (!nlen) return haystack;
    while (*haystack) {
        size_t i = 0;
        while (i < nlen && haystack[i] &&
               to_lower_ascii(haystack[i]) == to_lower_ascii(needle[i])) {
            i++;
        }
        if (i == nlen) return haystack;
        haystack++;
    }
    return NULL;
}

static const char *next_atom(const char *re, const char *re_end) {
    if (re >= re_end) return re_end;
    if (*re == '\\' && re + 1 < re_end) {
        return re + 2;
    }
    if (*re == '[') {
        const char *p = re + 1;
        if (p < re_end && *p == '^') p++;
        if (p < re_end && *p == ']') p++;
        while (p < re_end && *p != ']') {
            if (*p == '\\' && p + 1 < re_end) p += 2;
            else p++;
        }
        if (p < re_end && *p == ']') {
            return p + 1;
        }
    }
    return re + 1;
}

static bool atom_matches(const char *atom, const char *atom_end, char ch, bool ignore_case) {
    if (ch == '\0') return false;
    if (*atom == '\\' && atom + 1 < atom_end) {
        return char_eq(atom[1], ch, ignore_case);
    }
    if (*atom == '.' && atom + 1 == atom_end) {
        return true;
    }
    if (*atom == '[' && atom_end - atom >= 2 && *(atom_end - 1) == ']') {
        const char *p = atom + 1;
        const char *close = atom_end - 1;
        bool negated = false;
        if (p < close && *p == '^') {
            negated = true;
            p++;
        }
        bool hit = false;
        char c_cmp = ignore_case ? to_lower_ascii(ch) : ch;
        while (p < close) {
            char start = *p++;
            if (start == '\\' && p < close) start = *p++;
            if (p + 1 < close && *p == '-') {
                p++; // skip '-'
                char end = *p++;
                if (end == '\\' && p < close) end = *p++;
                char s_cmp = ignore_case ? to_lower_ascii(start) : start;
                char e_cmp = ignore_case ? to_lower_ascii(end) : end;
                if ((unsigned char)c_cmp >= (unsigned char)s_cmp &&
                    (unsigned char)c_cmp <= (unsigned char)e_cmp) {
                    hit = true;
                }
            } else {
                char s_cmp = ignore_case ? to_lower_ascii(start) : start;
                if (c_cmp == s_cmp) hit = true;
            }
        }
        return negated ? !hit : hit;
    }
    return char_eq(*atom, ch, ignore_case);
}

static int matchhere_range(const char *re, const char *re_end, const char *text, bool ignore_case, int *steps) {
    if (++(*steps) > REGEX_MAX_STEPS) return 0;
    if (re >= re_end) return 1;
    if (*re == '$' && re + 1 == re_end) return *text == '\0';
    const char *atom_end = next_atom(re, re_end);
    char quant = (atom_end < re_end && (*atom_end == '*' || *atom_end == '+' || *atom_end == '?'))
                 ? *atom_end : '\0';
    const char *rest = quant ? (atom_end + 1) : atom_end;
    if (quant == '*') {
        do {
            if (matchhere_range(rest, re_end, text, ignore_case, steps)) return 1;
            if (*steps > REGEX_MAX_STEPS) return 0;
        } while (*text != '\0' && atom_matches(re, atom_end, *text++, ignore_case));
        return 0;
    }
    if (quant == '+') {
        while (*text != '\0' && atom_matches(re, atom_end, *text++, ignore_case)) {
            if (matchhere_range(rest, re_end, text, ignore_case, steps)) return 1;
            if (*steps > REGEX_MAX_STEPS) return 0;
        }
        return 0;
    }
    if (quant == '?') {
        if (*text != '\0' && atom_matches(re, atom_end, *text, ignore_case)) {
            if (matchhere_range(rest, re_end, text + 1, ignore_case, steps)) return 1;
        }
        return matchhere_range(rest, re_end, text, ignore_case, steps);
    }
    if (*text != '\0' && atom_matches(re, atom_end, *text, ignore_case)) {
        return matchhere_range(rest, re_end, text + 1, ignore_case, steps);
    }
    return 0;
}

static int match_branch(const char *re, const char *re_end, const char *text, bool ignore_case) {
    int steps = 0;
    if (re < re_end && *re == '^') {
        return matchhere_range(re + 1, re_end, text, ignore_case, &steps);
    }
    do {
        if (matchhere_range(re, re_end, text, ignore_case, &steps)) return 1;
        if (steps > REGEX_MAX_STEPS) return 0;
    } while (*text++ != '\0');
    return 0;
}

static bool has_closing_bracket(const char *p) {
    if (*p == '^') p++;
    if (*p == ']') p++;
    while (*p) {
        if (*p == '\\' && p[1] != '\0') { p += 2; continue; }
        if (*p == ']') return true;
        p++;
    }
    return false;
}

static int regex_match(const char *regexp, const char *text, bool ignore_case) {
    const char *p = regexp;
    const char *branch_start = regexp;
    bool in_class = false;
    while (true) {
        if (*p == '\0') return match_branch(branch_start, p, text, ignore_case);
        if (*p == '\\' && p[1] != '\0') { p += 2; continue; }
        if (*p == '[' && !in_class && has_closing_bracket(p + 1)) {
            in_class = true;
            p++;
            if (*p == '^') p++;
            if (*p == ']') p++;
            continue;
        }
        if (*p == ']' && in_class) { in_class = false; p++; continue; }
        if (*p == '|' && !in_class) {
            if (match_branch(branch_start, p, text, ignore_case)) return 1;
            p++;
            branch_start = p;
            continue;
        }
        p++;
    }
}

static void buf_append_uint(char *buf, size_t *offset, size_t max, uint32_t val) {
    char num[12];
    int i = 0;
    if (val == 0) {
        num[i++] = '0';
    } else {
        char rev[12];
        int r = 0;
        while (val > 0) {
            rev[r++] = (char)('0' + (val % 10));
            val /= 10;
        }
        while (r > 0) {
            num[i++] = rev[--r];
        }
    }
    num[i] = '\0';
    buf_append(buf, offset, max, num);
}

static size_t strip_trailing_slashes(const char *arg, char *out, size_t max_len, bool *had_trailing_slash) {
    strncpy(out, arg, max_len - 1);
    out[max_len - 1] = '\0';
    size_t len = strlen(out);
    bool stripped = false;
    while (len > 0 && out[len - 1] == '/') {
        out[--len] = '\0';
        stripped = true;
    }
    if (had_trailing_slash) *had_trailing_slash = stripped;
    return len;
}

static bool resolve_dir(const char *path, uint32_t *out_inode, char *out_canonical, size_t max_len) {
    uint32_t cur = (path[0] == '/') ? ROOT_INODE : current_inode;
    char canon[MAX_PATH_LEN];
    strcpy(canon, (path[0] == '/') ? "/" : current_directory_path);

    const char *p = path;
    while (*p) {
        while (*p == '/') p++;
        if (*p == '\0') break;

        char seg[MAX_PATH_LEN];
        size_t slen = 0;
        while (*p && *p != '/') {
            if (slen >= sizeof(seg) - 1) return false;
            seg[slen++] = *p++;
        }
        seg[slen] = '\0';

        if (strcmp(seg, ".") == 0) continue;

        if (strcmp(seg, "..") == 0) {
            if (cur == ROOT_INODE) continue;
            uint32_t parent = 0;
            if (!lookup_entry(cur, "..", &parent, NULL)) return false;
            cur = parent;

            char *last_slash = NULL;
            for (char *q = canon; *q; q++) {
                if (*q == '/') last_slash = q;
            }
            if (last_slash == canon) {
                canon[1] = '\0';
            } else if (last_slash) {
                *last_slash = '\0';
            }
            continue;
        }

        uint32_t next_inode = 0;
        uint8_t file_type = 0;
        if (!lookup_entry(cur, seg, &next_inode, &file_type) || file_type != EXT2_FT_DIR) {
            return false;
        }

        size_t clen = strlen(canon);
        if (clen + slen + 2 > sizeof(canon)) return false;
        if (strcmp(canon, "/") != 0) {
            strcat(canon, "/");
        }
        strcat(canon, seg);
        cur = next_inode;
    }

    *out_inode = cur;
    if (out_canonical && max_len > 0) {
        strncpy(out_canonical, canon, max_len - 1);
        out_canonical[max_len - 1] = '\0';
    }
    return true;
}

static bool resolve_parent(const char *path, uint32_t *out_parent_inode, char *out_name, size_t max_name, bool *out_had_trailing_slash) {
    char clean[MAX_PATH_LEN];
    size_t len = strip_trailing_slashes(path, clean, sizeof(clean), out_had_trailing_slash);
    if (len == 0) return false;

    char *last_slash = NULL;
    for (char *p = clean; *p; p++) {
        if (*p == '/') last_slash = p;
    }

    const char *leaf = clean;
    uint32_t parent_inode = current_inode;

    if (last_slash != NULL) {
        leaf = last_slash + 1;
        if (last_slash == clean) {
            parent_inode = ROOT_INODE;
        } else {
            *last_slash = '\0';
            if (!resolve_dir(clean, &parent_inode, NULL, 0)) {
                return false;
            }
        }
    }

    size_t leaf_len = strlen(leaf);
    if (leaf_len == 0 || leaf_len >= max_name) return false;

    *out_parent_inode = parent_inode;
    strcpy(out_name, leaf);
    return true;
}

static bool resolve_copy_dest(const char *dst_arg, const char *src_name, uint32_t *out_dst_dir, char *out_dst_name, size_t max_name) {
    uint32_t dir_inode = 0;
    if (resolve_dir(dst_arg, &dir_inode, NULL, 0)) {
        if (strlen(src_name) >= max_name) return false;
        *out_dst_dir = dir_inode;
        strcpy(out_dst_name, src_name);
        return true;
    }

    bool dst_had_slash = false;
    if (!resolve_parent(dst_arg, out_dst_dir, out_dst_name, max_name, &dst_had_slash)) {
        return false;
    }
    if (dst_had_slash || strcmp(out_dst_name, ".") == 0 || strcmp(out_dst_name, "..") == 0) {
        return false;
    }
    return true;
}

// Command Handlers
typedef int (*cmd_handler)(int argc, char *argv[], const char *stdin_buf, char *stdout_buf, size_t max_out);
int cd_cmd(int argc, char *argv[], const char *stdin_buf, char *stdout_buf, size_t max_out){
    (void)stdin_buf;

    // Goes to root if no args
    if (argc < 2) {
        current_inode = ROOT_INODE;
        strcpy(current_directory_path, "/");
        return 0;
    }

    uint32_t target_inode = 0;
    char new_path[MAX_PATH_LEN];
    if (!resolve_dir(argv[1], &target_inode, new_path, sizeof(new_path))) {
        size_t offset = 0;
        buf_append(stdout_buf, &offset, max_out, "cd: directory not found: ");
        buf_append(stdout_buf, &offset, max_out, argv[1]);
        buf_append(stdout_buf, &offset, max_out, "\n");
        return -1;
    }

    current_inode = target_inode;
    strcpy(current_directory_path, new_path);
    return 0;
}

int ls_cmd(int argc, char *argv[], const char *stdin_buf, char *stdout_buf, size_t max_out){
    (void)stdin_buf;
    size_t offset = 0;
    uint32_t target_inode = current_inode;

    if (argc >= 2) {
        if (!resolve_dir(argv[1], &target_inode, NULL, 0)) {
            buf_append(stdout_buf, &offset, max_out, "ls: directory not found: ");
            buf_append(stdout_buf, &offset, max_out, argv[1]);
            buf_append(stdout_buf, &offset, max_out, "\n");
            return -1;
        }
    }

    if (fs_read_directory(target_inode, temp_buf, sizeof(temp_buf)) != 0) {
        buf_append(stdout_buf, &offset, max_out, "ls: failed to read directory\n");
        return -1;
    }

    uint32_t fs_offset = 0;
    while (fs_offset + sizeof(struct EXT2DirectoryEntry) <= sizeof(temp_buf)){
        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *) (temp_buf + fs_offset);
        if (entry->rec_len == 0) break;

        if (entry->inode != 0){
            char name_buf[256];
            char *entry_name = get_entry_name(entry);
            memcpy(name_buf, entry_name, entry->name_len);
            name_buf[entry->name_len] = '\0';

            if (strcmp(name_buf, ".") == 0 || strcmp(name_buf, "..") == 0) {
                fs_offset += entry->rec_len;
                continue;
            }

            buf_append(stdout_buf, &offset, max_out, name_buf);
            if (entry->file_type == EXT2_FT_DIR){
                buf_append(stdout_buf, &offset, max_out, "/");
            }
            buf_append(stdout_buf, &offset, max_out, "\n");
        }
        fs_offset += entry->rec_len;
    }
    return 0;
}

int mkdir_cmd(int argc, char *argv[], const char *stdin_buf, char *stdout_buf, size_t max_out) {
    (void) stdin_buf;
    size_t offset = 0;
    if (argc < 2){
        buf_append(stdout_buf, &offset, max_out, "mkdir: missing directory operand\n");
        return -1;
    }

    uint32_t parent_inode = 0;
    char name[MAX_PATH_LEN];
    if (!resolve_parent(argv[1], &parent_inode, name, sizeof(name), NULL) ||
        strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        buf_append(stdout_buf, &offset, max_out, "mkdir: invalid directory path\n");
        return -1;
    }

    if (fs_mkdir(parent_inode, name) != 0){
        buf_append(stdout_buf, &offset, max_out, "mkdir: failed to create directory\n");
        return -1;
    }
    return 0;
}

int cat_cmd(int argc, char *argv[], const char *stdin_buf, char *stdout_buf, size_t max_out){
    size_t offset = 0;
    if (argc < 2){
        // pipeline mode
        if (stdin_buf && stdin_buf[0]){
            buf_append(stdout_buf, &offset, max_out, stdin_buf);
            return 0;
        }
        buf_append(stdout_buf, &offset, max_out, "cat: missing file operand\n");
        return -1;
    }

    uint32_t parent_inode = 0;
    char name[MAX_PATH_LEN];
    bool had_slash = false;
    if (!resolve_parent(argv[1], &parent_inode, name, sizeof(name), &had_slash) ||
        had_slash || strcmp(name, ".") == 0 || strcmp(name, "..") == 0 ||
        fs_read(parent_inode, name, stdout_buf, max_out - 1) != 0) {
        buf_append(stdout_buf, &offset, max_out, "cat: file not found or read error\n");
        return -1;
    }
    stdout_buf[max_out-1] = '\0';
    return 0;
}

int cp_cmd(int argc, char *argv[], const char *stdin_buf, char *stdout_buf, size_t max_out){
    (void) stdin_buf;
    size_t offset = 0;
    if (argc < 3){
        buf_append(stdout_buf, &offset, max_out, "cp: missing file operand (cp <src> <dst>)\n");
        return -1;
    }

    uint32_t src_parent = 0;
    char src_name[MAX_PATH_LEN];
    bool src_had_slash = false;
    if (!resolve_parent(argv[1], &src_parent, src_name, sizeof(src_name), &src_had_slash) ||
        src_had_slash || strcmp(src_name, ".") == 0 || strcmp(src_name, "..") == 0) {
        buf_append(stdout_buf, &offset, max_out, "cp: cannot read source file\n");
        return -1;
    }

    uint32_t dst_dir = 0;
    char dst_name[MAX_PATH_LEN];
    if (!resolve_copy_dest(argv[2], src_name, &dst_dir, dst_name, sizeof(dst_name))) {
        buf_append(stdout_buf, &offset, max_out, "cp: destination directory not found\n");
        return -1;
    }

    if (src_parent == dst_dir && strcmp(src_name, dst_name) == 0) {
        buf_append(stdout_buf, &offset, max_out, "cp: source and destination are the same file\n");
        return -1;
    }

    static char file_buf[BUFFER_SIZE];
    memset(file_buf, 0, sizeof(file_buf));

    if (fs_read(src_parent, src_name, file_buf, sizeof(file_buf)-1) != 0){
        buf_append(stdout_buf, &offset, max_out, "cp: cannot read source file\n");
        return -1;
    }
    size_t len = strlen(file_buf);
    if (len == 0) {
        buf_append(stdout_buf, &offset, max_out, "cp: empty or binary source not supported\n");
        return -1;
    }

    if (fs_write_file(dst_dir, dst_name, file_buf, len) != 0) {
        buf_append(stdout_buf, &offset, max_out, "cp: failed to write destination file\n");
        return -1;
    }
    return 0;
}

int rm_cmd(int argc, char *argv[], const char *stdin_buf, char *stdout_buf, size_t max_out){
    (void) stdin_buf;
    size_t offset = 0;

    if (argc < 2) {
        buf_append(stdout_buf, &offset, max_out, "rm: missing operand\n");
        return -1;
    }

    char clean[MAX_PATH_LEN];
    if (strip_trailing_slashes(argv[1], clean, sizeof(clean), NULL) == 0) {
        buf_append(stdout_buf, &offset, max_out, "rm: cannot remove root directory\n");
        return -1;
    }

    uint32_t parent_inode = 0;
    char target[MAX_PATH_LEN];
    bool had_slash = false;
    if (!resolve_parent(argv[1], &parent_inode, target, sizeof(target), &had_slash)) {
        buf_append(stdout_buf, &offset, max_out, "rm: target not found\n");
        return -1;
    }

    if (strcmp(target, ".") == 0 || strcmp(target, "..") == 0) {
        buf_append(stdout_buf, &offset, max_out, "rm: cannot remove '.' or '..'\n");
        return -1;
    }

    uint32_t target_inode = 0;
    uint8_t file_type = 0;
    if (!lookup_entry(parent_inode, target, &target_inode, &file_type)){
        buf_append(stdout_buf, &offset, max_out, "rm: target not found\n");
        return -1;
    }

    bool is_dir = (file_type == EXT2_FT_DIR);
    if (had_slash && !is_dir) {
        buf_append(stdout_buf, &offset, max_out, "rm: target is not a directory\n");
        return -1;
    }

    if (fs_delete(parent_inode, target, is_dir) != 0) {
        buf_append(stdout_buf, &offset, max_out, "rm: cannot remove file/folder (folder must be empty)\n");
        return -1;
    }
    return 0;
}

int mv_cmd(int argc, char *argv[], const char *stdin_buf, char *stdout_buf, size_t max_out) {
    size_t offset = 0;
    if (argc < 3) {
        buf_append(stdout_buf, &offset, max_out, "mv: missing operand (mv <src> <dst>)\n");
        return -1;
    }

    char clean[MAX_PATH_LEN];
    if (strip_trailing_slashes(argv[1], clean, sizeof(clean), NULL) == 0) {
        buf_append(stdout_buf, &offset, max_out, "mv: moving directories is not supported\n");
        return -1;
    }

    uint32_t src_parent = 0;
    char src_target[MAX_PATH_LEN];
    bool src_had_slash = false;
    if (!resolve_parent(argv[1], &src_parent, src_target, sizeof(src_target), &src_had_slash)) {
        buf_append(stdout_buf, &offset, max_out, "mv: source not found\n");
        return -1;
    }

    if (strcmp(src_target, ".") == 0 || strcmp(src_target, "..") == 0) {
        buf_append(stdout_buf, &offset, max_out, "mv: moving directories is not supported\n");
        return -1;
    }

    uint32_t src_inode; uint8_t src_type;
    if (!lookup_entry(src_parent, src_target, &src_inode, &src_type)) {
        buf_append(stdout_buf, &offset, max_out, "mv: source not found\n");
        return -1;
    }
    if (src_type == EXT2_FT_DIR) {
        buf_append(stdout_buf, &offset, max_out, "mv: moving directories is not supported\n");
        return -1;
    }
    if (src_had_slash) {
        buf_append(stdout_buf, &offset, max_out, "mv: source is not a directory\n");
        return -1;
    }

    uint32_t dst_dir = 0;
    char dst_name[MAX_PATH_LEN];
    if (!resolve_copy_dest(argv[2], src_target, &dst_dir, dst_name, sizeof(dst_name))) {
        buf_append(stdout_buf, &offset, max_out, "mv: destination directory not found\n");
        return -1;
    }
    if (src_parent == dst_dir && strcmp(src_target, dst_name) == 0) {
        return 0; // moving onto itself is a no-op
    }

    int res = cp_cmd(argc, argv, stdin_buf, stdout_buf, max_out);
    if (res != 0) return res;

    if (fs_delete(src_parent, src_target, false) != 0) {
        size_t off2 = 0;
        buf_append(stdout_buf, &off2, max_out, "mv: copied but failed to remove source\n");
        return -1;
    }
    return 0;
}
static void recursive_find(uint32_t inode, const char *path_prefix, const char *target, bool dir_only, bool *found, char *stdout_buf, size_t *offset, size_t max_out) {
    uint8_t local_buf[BUFFER_SIZE];
    if (fs_read_directory(inode, local_buf, sizeof(local_buf)) != 0) return;
    uint32_t off = 0;
    while (off + sizeof(struct EXT2DirectoryEntry) <= sizeof(local_buf)) {
        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)(local_buf + off);
        if (entry->rec_len == 0) break;
        if (entry->inode != 0) {
            char name[256];
            char *entry_name = get_entry_name(entry);
            memcpy(name, entry_name, entry->name_len);
            name[entry->name_len] = '\0';
            if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
                char full_path[MAX_PATH_LEN];
                strcpy(full_path, path_prefix);
                if (full_path[strlen(full_path) - 1] != '/') {
                    strcat(full_path, "/");
                }
                strcat(full_path, name);
                if (strcmp(name, target) == 0 && (!dir_only || entry->file_type == EXT2_FT_DIR)) {
                    buf_append(stdout_buf, offset, max_out, full_path);
                    buf_append(stdout_buf, offset, max_out, "\n");
                    *found = true;
                }
                if (entry->file_type == EXT2_FT_DIR) {
                    recursive_find(entry->inode, full_path, target, dir_only, found, stdout_buf, offset, max_out);
                }
            }
        }
        off += entry->rec_len;
    }
}
int find_cmd(int argc, char *argv[], const char *stdin_buf, char *stdout_buf, size_t max_out) {
    (void)stdin_buf;
    size_t off = 0;
    if (argc < 2) {
        buf_append(stdout_buf, &off, max_out, "find: missing search target name\n");
        return -1;
    }
    char target[MAX_PATH_LEN];
    bool had_slash = false;
    size_t len = strip_trailing_slashes(argv[1], target, sizeof(target), &had_slash);
    if (len == 0) {
        buf_append(stdout_buf, &off, max_out, "/\n");
        return 0;
    }
    bool found = false;
    recursive_find(ROOT_INODE, "/", target, had_slash, &found, stdout_buf, &off, max_out);
    if (!found) {
        buf_append(stdout_buf, &off, max_out, "File/Folder not found\n");
        return -1;
    }
    return 0;
}

int pwd_cmd(int argc, char *argv[], const char *stdin_buf, char *stdout_buf, size_t max_out){
    (void)argc;
    (void)argv;
    (void)stdin_buf;

    size_t off = 0;
    buf_append(stdout_buf, &off, max_out, current_directory_path);
    buf_append(stdout_buf, &off, max_out, "\n");
    return 0;
}

int clear_cmd(int argc, char *argv[], const char *stdin_buf, char *stdout_buf, size_t max_out){
    (void)argc;
    (void)argv;
    (void)stdin_buf;
    (void)stdout_buf;
    (void)max_out;
    sys_clear_screen();
    return 0;
}

int grep_cmd(int argc, char *argv[], const char *stdin_buf, char *stdout_buf, size_t max_out) {
    size_t off = 0;
    bool use_regex = false;
    bool ignore_case = false;
    bool invert_match = false;
    bool show_line_num = false;
    int arg_idx = 1;

    while (arg_idx < argc && argv[arg_idx][0] == '-' && argv[arg_idx][1] != '\0') {
        if (strcmp(argv[arg_idx], "--") == 0) {
            arg_idx++;
            break;
        }
        for (const char *f = argv[arg_idx] + 1; *f; f++) {
            if (*f == 'E') use_regex = true;
            else if (*f == 'i') ignore_case = true;
            else if (*f == 'v') invert_match = true;
            else if (*f == 'n') show_line_num = true;
            else {
                buf_append(stdout_buf, &off, max_out, "grep: invalid option: -");
                char opt[2] = {*f, '\0'};
                buf_append(stdout_buf, &off, max_out, opt);
                buf_append(stdout_buf, &off, max_out, "\n");
                return -1;
            }
        }
        arg_idx++;
    }

    if (arg_idx >= argc) {
        buf_append(stdout_buf, &off, max_out, "grep: missing pattern\n");
        return -1;
    }
    const char *pattern = argv[arg_idx++];
    const char *filename = (arg_idx < argc) ? argv[arg_idx] : NULL;

    static char source_text[BUFFER_SIZE];
    memset(source_text, 0, sizeof(source_text));

    if (filename != NULL) {
        uint32_t file_parent = 0;
        char file_name[MAX_PATH_LEN];
        bool file_had_slash = false;
        if (!resolve_parent(filename, &file_parent, file_name, sizeof(file_name), &file_had_slash) ||
            file_had_slash || strcmp(file_name, ".") == 0 || strcmp(file_name, "..") == 0 ||
            fs_read(file_parent, file_name, source_text, sizeof(source_text) - 1) != 0) {
            buf_append(stdout_buf, &off, max_out, "grep: file not found: ");
            buf_append(stdout_buf, &off, max_out, filename);
            buf_append(stdout_buf, &off, max_out, "\n");
            return -1;
        }
    } else if (stdin_buf != NULL) {
        if (!stdin_buf[0]) return 0;
        strncpy(source_text, stdin_buf, sizeof(source_text) - 1);
        source_text[sizeof(source_text) - 1] = '\0';
    } else {
        buf_append(stdout_buf, &off, max_out, "grep: missing file operand\n");
        return -1;
    }

    uint32_t line_num = 1;
    char *cursor = source_text;
    while (*cursor) {
        char *line_start = cursor;
        while (*cursor && *cursor != '\n') {
            cursor++;
        }

        char delim = *cursor;
        *cursor = '\0';
        if (cursor > line_start && *(cursor - 1) == '\r') {
            *(cursor - 1) = '\0';
        }

        bool matched = false;
        if (use_regex) {
            matched = (regex_match(pattern, line_start, ignore_case) == 1);
        } else if (ignore_case) {
            matched = (strstr_ci(line_start, pattern) != NULL);
        } else {
            matched = (strstr(line_start, pattern) != NULL);
        }

        if (invert_match) {
            matched = !matched;
        }

        if (matched) {
            if (show_line_num) {
                buf_append_uint(stdout_buf, &off, max_out, line_num);
                buf_append(stdout_buf, &off, max_out, ":");
            }
            buf_append(stdout_buf, &off, max_out, line_start);
            buf_append(stdout_buf, &off, max_out, "\n");
        }

        if (delim == '\n') {
            cursor++;
        }
        line_num++;
    }
    return 0;
}

int help_cmd(int argc, char *argv[], const char *stdin_buf, char *stdout_buf, size_t max_out) {
    (void)stdin_buf;
    size_t off = 0;
    if (argc >= 2) {
        const char *cmd = argv[1];
        if (strcmp(cmd, "cd") == 0) {
            buf_append(stdout_buf, &off, max_out, "Usage: cd [dir]\n  Change working directory (defaults to /).\n");
        } else if (strcmp(cmd, "ls") == 0) {
            buf_append(stdout_buf, &off, max_out, "Usage: ls [dir]\n  List entries in current or target directory.\n");
        } else if (strcmp(cmd, "pwd") == 0) {
            buf_append(stdout_buf, &off, max_out, "Usage: pwd\n  Print current working directory path.\n");
        } else if (strcmp(cmd, "clear") == 0) {
            buf_append(stdout_buf, &off, max_out, "Usage: clear\n  Clear the screen.\n");
        } else if (strcmp(cmd, "mkdir") == 0) {
            buf_append(stdout_buf, &off, max_out, "Usage: mkdir <path>\n  Create a new directory.\n");
        } else if (strcmp(cmd, "cat") == 0) {
            buf_append(stdout_buf, &off, max_out, "Usage: cat [file]\n  Print file contents (or pass through piped input).\n");
        } else if (strcmp(cmd, "cp") == 0) {
            buf_append(stdout_buf, &off, max_out, "Usage: cp <src> <dst>\n  Copy a file to a new path or into a directory.\n");
        } else if (strcmp(cmd, "rm") == 0) {
            buf_append(stdout_buf, &off, max_out, "Usage: rm <path>\n  Remove a file or an empty directory.\n");
        } else if (strcmp(cmd, "mv") == 0) {
            buf_append(stdout_buf, &off, max_out, "Usage: mv <src> <dst>\n  Move or rename a file.\n");
        } else if (strcmp(cmd, "find") == 0) {
            buf_append(stdout_buf, &off, max_out, "Usage: find <name>[/]\n  Search from / for matching file or dir (trailing / = dir only).\n");
        } else if (strcmp(cmd, "grep") == 0) {
            buf_append(stdout_buf, &off, max_out,
                "Usage: grep [-Eivn] [--] <pattern> [file]\n"
                "  -E  RegEx (^ $ . * + ? [...] [^...] | \\)\n"
                "  -i  Case-insensitive match\n"
                "  -v  Invert match\n"
                "  -n  Show line numbers\n");
        } else if (strcmp(cmd, "help") == 0) {
            buf_append(stdout_buf, &off, max_out, "Usage: help [command]\n  Show available commands or details for one command.\n");
        } else {
            buf_append(stdout_buf, &off, max_out, "help: unknown command: ");
            buf_append(stdout_buf, &off, max_out, cmd);
            buf_append(stdout_buf, &off, max_out, "\n");
            return -1;
        }
        return 0;
    }
    buf_append(stdout_buf, &off, max_out,
        "Available commands:\n"
        "  cd [dir]                 Change directory\n"
        "  ls [dir]                 List directory contents\n"
        "  pwd                      Print working directory\n"
        "  clear                    Clear screen\n"
        "  mkdir <path>             Create directory\n"
        "  cat [file]               Display file or stdin\n"
        "  cp <src> <dst>           Copy file\n"
        "  rm <path>                Remove file or empty directory\n"
        "  mv <src> <dst>           Move or rename file\n"
        "  find <name>[/]           Find file or directory from /\n"
        "  grep [-Eivn] <pat> [f]   Search lines (supports RegEx & pipes)\n"
        "  help [command]           Show this help or command details\n"
        "Pipelines: cmd1 | cmd2     Quote patterns containing | or spaces.\n");
    return 0;
}

// REPL Shell 

struct command_entry {
    const char   *name;
    cmd_handler handler;
};

static const struct command_entry COMMAND_TABLE[] = {
    {"cd",    cd_cmd},
    {"ls",    ls_cmd},
    {"pwd",   pwd_cmd},
    {"clear", clear_cmd},
    {"mkdir", mkdir_cmd},
    {"cat",   cat_cmd},
    {"cp",    cp_cmd},
    {"rm",    rm_cmd},
    {"mv",    mv_cmd},
    {"find",  find_cmd},
    {"grep",  grep_cmd},
    {"help", help_cmd},
    {NULL,    NULL}
};

int parse_args(char *line, char *argv[]) {
    int argc = 0;
    char *p = line;

    while (*p && argc < MAX_ARGS - 1) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        char *dst = p;
        argv[argc++] = dst;
        char quote = 0;

        while (*p) {
            if (quote == 0) {
                if (*p == ' ' || *p == '\t') {
                    p++;
                    break;
                }
                if (*p == '\'' || *p == '"') {
                    quote = *p++;
                    continue;
                }
                *dst++ = *p++;
            } else {
                if (quote == '"' && *p == '\\' && (p[1] == '"' || p[1] == '\\')) {
                    p++;
                    *dst++ = *p++;
                    continue;
                }
                if (*p == quote) {
                    quote = 0;
                    p++;
                    continue;
                }
                *dst++ = *p++;
            }
        }
        if (quote != 0) {
            return -1;
        }
        *dst = '\0';
    }
    argv[argc] = NULL;
    return argc;
}

int dispatch(char *cmd_line, const char *stdin_buf, char *stdout_buf, size_t max_out) {
    char *argv[MAX_ARGS];
    int argc = parse_args(cmd_line, argv);
    if (argc < 0) {
        size_t off = 0;
        buf_append(stdout_buf, &off, max_out, "shell: unclosed quote\n");
        return -1;
    }
    if (argc == 0) return 0;

    for (int i = 0; COMMAND_TABLE[i].name != NULL; i++) {
        if (strcmp(argv[0], COMMAND_TABLE[i].name) == 0) {
            return COMMAND_TABLE[i].handler(argc, argv, stdin_buf, stdout_buf, max_out);
        }
    }

    size_t off = 0;
    buf_append(stdout_buf, &off, max_out, argv[0]);
    buf_append(stdout_buf, &off, max_out, ": command not found\n");
    return -1;
}

static char *find_unquoted_pipe(char *s) {
    char quote = 0;
    while (*s) {
        if (quote == 0) {
            if (*s == '\'' || *s == '"') {
                quote = *s;
            } else if (*s == '|') {
                return s;
            }
        } else {
            if (quote == '"' && *s == '\\' && (s[1] == '"' || s[1] == '\\')) {
                s += 2;
                continue;
            }
            if (*s == quote) {
                quote = 0;
            }
        }
        s++;
    }
    return NULL;
}

static bool is_blank(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return *s == '\0';
}

void eval_line(char *line) {
    char *stage = line;
    const char *in = NULL;
    char *out = pipe_buf_1;
    char *other = pipe_buf_2;
    int rc = 0;
    bool has_pipe = (find_unquoted_pipe(line) != NULL);

    while (stage != NULL) {
        char *next = find_unquoted_pipe(stage);
        if (next) { *next = '\0'; next++; }

        if (has_pipe && is_blank(stage)) {
            sys_print("shell: syntax error near '|'\n", ERR_COLOR);
            return;
        }

        memset(out, 0, BUFFER_SIZE);
        rc = dispatch(stage, in, out, BUFFER_SIZE);
        in = out;                        // this stage's output feeds the next
        if (rc != 0) break;

        char *tmp = out; out = other; other = tmp;
        stage = next;
    }

    if (in && in[0])
        sys_print(in, rc == 0 ? DEFAULT_COLOR : ERR_COLOR);
}

void shell_readline(char *buffer, size_t max_len) {
    size_t idx = 0;
    while (true) {
        char c = sys_getc();

        if (c == '\r' || c == '\n') {
            buffer[idx] = '\0';
            sys_putc('\n', DEFAULT_COLOR);
            break;
        }

        if (c == '\b' || c == 0x7F) {
            if (idx > 0) {
                idx--;
                sys_putc('\b', DEFAULT_COLOR);
                sys_putc(' ', DEFAULT_COLOR);
                sys_putc('\b', DEFAULT_COLOR);
            }
            continue;
        }

        if (idx < max_len - 1 && c >= 32 && c <= 126) {
            buffer[idx++] = c;
            sys_putc(c, DEFAULT_COLOR);
        }
    }
}

int main(void) {
    syscall(7, 0, 0, 0);

    sys_print("OSkm Interactive Shell (Ring 3 User Mode)\n", PROMPT_COLOR);

    char input_line[MAX_LINE_LEN];

    while (true) {
        sys_print("user@os:", PROMPT_COLOR);
        sys_print(current_directory_path, DIR_COLOR);
        sys_print("$ ", PROMPT_COLOR);

        memset(input_line, 0, sizeof(input_line));
        shell_readline(input_line, sizeof(input_line));

        if (input_line[0] == '\0') {
            continue;
        }

        eval_line(input_line);
    }

    return 0;
}
