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
    int32_t retcode = -1;
    struct EXT2DriverRequest request = {
        .buf = buf,
        .name = name,
        .name_len = (uint8_t)strlen(name),
        .parent_inode = parent_inode,
        .buffer_size = size,
        .is_directory = false
    };
    syscall(0, (uint32_t)&request, (uint32_t)&retcode, 0);
    return (int8_t)retcode;
}

int8_t fs_read_directory(uint32_t parent_inode, void *buf, uint32_t size){
    int32_t retcode = -1;
    struct EXT2DriverRequest request = {
        .buf = buf,
        .name = ".",
        .name_len = 1,
        .parent_inode = parent_inode,
        .buffer_size = size,
        .is_directory = true
    };
    syscall(1, (uint32_t)&request, (uint32_t)&retcode, 0);
    return (int8_t)retcode;
}

int8_t fs_write_file(uint32_t parent_inode, char *name, void *buf, uint32_t size){
    int32_t retcode = -1;
    struct EXT2DriverRequest request = {
        .buf = buf,
        .name = name,
        .name_len = (uint8_t)strlen(name),
        .parent_inode = parent_inode,
        .buffer_size = size,
        .is_directory = false 
    };
    syscall(2, (uint32_t)&request, (uint32_t)&retcode, 0);
    return (int8_t)retcode;
}

int8_t fs_mkdir(uint32_t parent_inode, char *name){
    int32_t retcode = -1;
    struct EXT2DriverRequest request = {
        .buf = NULL,
        .name = name,
        .name_len = (uint8_t)strlen(name),
        .parent_inode = parent_inode,
        .buffer_size = 0,
        .is_directory = true 
    };
    syscall(2, (uint32_t)&request, (uint32_t)&retcode, 0);
    return (int8_t)retcode;
}

int8_t fs_delete(uint32_t parent_inode, char *name, bool is_dir){
    int32_t retcode = -1;
    struct EXT2DriverRequest request = {
        .buf = NULL,
        .name = name,
        .name_len = (uint8_t)strlen(name),
        .parent_inode = parent_inode,
        .buffer_size = 0,
        .is_directory = is_dir 
    };
    syscall(3, (uint32_t)&request, (uint32_t)&retcode, 0);
    return (int8_t)retcode;
}


// State and Buffer Management Functions
static char current_directory_path[MAX_PATH_LEN] = "/";
static uint32_t current_inode = ROOT_INODE;

static char pipe_buf_1[BUFFER_SIZE];
static char pipe_buf_2[BUFFER_SIZE];
static uint8_t temp_buf[BUFFER_SIZE];

void buf_append(char *buf, size_t *offset, size_t max, const char *str){
    
    size_t slen = strlen(str);
    if (*offset + slen >= max-1) return;
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
// RegEx Functions
static int matchhere(const char *regexp, const char *text);
static int matchstar(int c, const char *regexp, const char *text);

static int regex_match(const char *regexp, const char *text) {
    if (regexp[0] == '^') {
        return matchhere(regexp + 1, text);
    }
    do {
        if (matchhere(regexp, text)) {
            return 1;
        }
    } while (*text++ != '\0');
    return 0;
}

static int matchhere(const char *regexp, const char *text) {
    if (regexp[0] == '\0') {
        return 1;
    }
    if (regexp[1] == '*') {
        return matchstar(regexp[0], regexp + 2, text);
    }
    if (regexp[0] == '$' && regexp[1] == '\0') {
        return *text == '\0';
    }
    if (*text != '\0' && (regexp[0] == '.' || regexp[0] == *text)) {
        return matchhere(regexp + 1, text + 1);
    }
    return 0;
}

static int matchstar(int c, const char *regexp, const char *text) {
    do {
        if (matchhere(regexp, text)) {
            return 1;
        }
    } while (*text != '\0' && (*text++ == c || c == '.'));
    return 0;
}



// Command Handlers
typedef int (*cmd_handler)(int argc, char *argv[], const char *stdin_buf, char *stdout_buf, size_t max_out);
int cd_cmd(int argc, char *argv[], const char *stdin_buf, char *stdout_buf, size_t max_out){
    (void)stdin_buf;
    (void)stdout_buf;
    (void)max_out;

    // Goes to root if no args or arg is '/'
    if (argc < 2 || strcmp(argv[1], "/") == 0) {
        current_inode = ROOT_INODE;
        strcpy(current_directory_path, "/");
        return 0;
    }

    if (strcmp(argv[1], ".") == 0) return 0;

    //Goes to parent directory
    if (strcmp(argv[1], "..") == 0) {
        if (current_inode == ROOT_INODE) return 0;

        uint32_t parent = ROOT_INODE;
        lookup_entry(current_inode, "..", &parent, NULL);
        current_inode = parent;

        char *last_slash = NULL;
        for (char *p = current_directory_path; *p; p++){
            if (*p == '/') last_slash = p;
        }
        if (last_slash == current_directory_path){
            current_directory_path[1] = '\0';
        } else if (last_slash){
            *last_slash = '\0';
        }
        return 0;
    }

    // Move to sub directory
    uint32_t target_inode = 0;
    uint8_t file_type = 0;
    if (!lookup_entry(current_inode, argv[1], &target_inode, &file_type) || file_type != EXT2_FT_DIR){
        size_t offset = 0;
        buf_append(stdout_buf, &offset, max_out, "cd: directory not found: ");
        buf_append(stdout_buf, &offset, max_out, argv[1]);
        buf_append(stdout_buf, &offset, max_out, "\n");
        return -1;
    }

    size_t pathlen = strlen(current_directory_path);
    size_t arglen = strlen(argv[1]);
    if (pathlen + arglen + 2 > MAX_PATH_LEN) {
        size_t offset = 0;
        buf_append(stdout_buf, &offset, max_out, "cd: path too long\n");
        return -1;
    }

    current_inode = target_inode;
    if (strcmp(current_directory_path, "/") != 0) {
        strcat(current_directory_path, "/");
    }
    strcat(current_directory_path, argv[1]);
    return 0;

}

int ls_cmd(int argc, char *argv[], const char *stdin_buf, char *stdout_buf, size_t max_out){
    (void)stdin_buf;    
    (void) argc;
    (void) argv;
    size_t offset = 0;

    if (fs_read_directory(current_inode,temp_buf,sizeof(temp_buf)) != 0) {
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

    char name[MAX_PATH_LEN];
    strncpy(name, argv[1], MAX_PATH_LEN - 1);
    name[MAX_PATH_LEN - 1] = '\0';
    size_t len = strlen(name);
    while (len > 0 && name[len - 1] == '/') {
        name[--len] = '\0';
    }

    if (len == 0) {
        buf_append(stdout_buf, &offset, max_out, "mkdir: invalid directory name\n");
        return -1;
    }

    if (strchr(name, '/') != NULL) {
        buf_append(stdout_buf, &offset, max_out, "mkdir: nested paths are not supported\n");
        return -1;
    }

    if (fs_mkdir(current_inode, name) != 0){
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

    if (fs_read(current_inode, argv[1], stdout_buf, max_out-1) != 0){
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

    static char file_buf[BUFFER_SIZE];
    memset(file_buf, 0, sizeof(file_buf));

    if (fs_read(current_inode, argv[1], file_buf, sizeof(file_buf)-1) != 0){
        buf_append(stdout_buf, &offset, max_out, "cp: cannot read source file\n");
        return -1;
    }
    size_t len = strlen(file_buf);
    if (len == 0) {
        buf_append(stdout_buf, &offset, max_out, "cp: empty or binary source not supported\n");
        return -1;
    }

    const char *dst_name = argv[2];
    uint32_t dst_dir = current_inode;
    char dst_buf[MAX_PATH_LEN];
    strncpy(dst_buf, argv[2], MAX_PATH_LEN - 1);
    dst_buf[MAX_PATH_LEN - 1] = '\0';
    size_t dlen = strlen(dst_buf);
    if (dlen > 1 && dst_buf[dlen - 1] == '/') dst_buf[--dlen] = '\0';

    // If destination is an existing directory, copy into it keeping the source name
    uint32_t t_inode; uint8_t t_type;
    if (lookup_entry(current_inode, dst_buf, &t_inode, &t_type) && t_type == EXT2_FT_DIR) {
        dst_dir = t_inode;
        dst_name = argv[1];
    } else {
        dst_name = dst_buf;
    }
    if (fs_write_file(dst_dir, (char *)dst_name, file_buf, strlen(file_buf)) != 0) {
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

    if (strcmp(argv[1], ".") == 0 || strcmp(argv[1], "..") == 0) {
        buf_append(stdout_buf, &offset, max_out, "rm: cannot remove '.' or '..'\n");
        return -1;
    }

    uint32_t target_inode = 0;
    uint8_t file_type = 0;
    if (!lookup_entry(current_inode, argv[1], &target_inode, &file_type)){
        buf_append(stdout_buf, &offset, max_out, "rm: target not found\n");
        return -1;
    }

    bool is_dir = (file_type == EXT2_FT_DIR);
    if (fs_delete(current_inode, argv[1], is_dir) != 0) {
        buf_append(stdout_buf, &offset, max_out, "rm: cannot remove file/folder (folder must be empty)\n");
        return -1;
    }
    return 0;
}

int mv_cmd(int argc, char *argv[], const char *stdin_buf, char *stdout_buf, size_t max_out) {
    int res = cp_cmd(argc, argv, stdin_buf, stdout_buf, max_out);
    if (res == 0) {
        fs_delete(current_inode, argv[1], false);
    }
    return res;
}

static void recursive_find(uint32_t inode, const char *path_prefix, const char *target, char *stdout_buf, size_t *offset, size_t max_out) {
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

                if (strcmp(name, target) == 0) {
                    buf_append(stdout_buf, offset, max_out, full_path);
                    buf_append(stdout_buf, offset, max_out, "\n");
                }

                if (entry->file_type == EXT2_FT_DIR) {
                    recursive_find(entry->inode, full_path, target, stdout_buf, offset, max_out);
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
    recursive_find(ROOT_INODE, "/", argv[1], stdout_buf, &off, max_out);
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
    int arg_idx = 1;

    if (argc < 2) {
        buf_append(stdout_buf, &off, max_out, "grep: missing pattern\n");
        return -1;
    }
    if (strcmp(argv[1], "-E") == 0) {
        use_regex = true;
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
        if (fs_read(current_inode, (char*)filename, source_text, sizeof(source_text) - 1) != 0) {
            buf_append(stdout_buf, &off, max_out, "grep: file not found: ");
            buf_append(stdout_buf, &off, max_out, filename);
            buf_append(stdout_buf, &off, max_out, "\n");
            return -1;
        }
    } else if (stdin_buf && stdin_buf[0]) {
        strncpy(source_text, stdin_buf, sizeof(source_text) - 1);
        source_text[sizeof(source_text) - 1] = '\0';
    } else {
        return 0;
    }

    char *cursor = source_text;
    while (*cursor) {
        char *line_start = cursor;
        while (*cursor && *cursor != '\n') {
            cursor++;
        }

        char delim = *cursor;
        *cursor = '\0';
        bool matched = false;
        if (use_regex) {
            matched = (regex_match(pattern, line_start) == 1);
        } else {
            matched = (strstr(line_start, pattern) != NULL);
        }

        if (matched) {
            buf_append(stdout_buf, &off, max_out, line_start);
            buf_append(stdout_buf, &off, max_out, "\n");
        }

        if (delim == '\n') {
            cursor++;
        }
    }
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
    {NULL,    NULL}
};

int parse_args(char *line, char *argv[]) {
    int argc = 0;
    char *p = line;

    while (*p && argc < MAX_ARGS - 1) {
        while (*p == ' ' || *p == '\t') *p++ = '\0';
        if (*p == '\0') break;

        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
    }
    argv[argc] = NULL;
    return argc;
}

int dispatch(char *cmd_line, const char *stdin_buf, char *stdout_buf, size_t max_out) {
    char *argv[MAX_ARGS];
    int argc = parse_args(cmd_line, argv);
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

void eval_line(char *line) {
    char *stage = line;
    const char *in = NULL;
    char *out = pipe_buf_1;
    char *other = pipe_buf_2;
    int rc = 0;

    while (stage != NULL) {
        char *next = strchr(stage, '|');
        if (next) { *next = '\0'; next++; }

        memset(out, 0, BUFFER_SIZE);
        rc = dispatch(stage, in, out, BUFFER_SIZE);

        in = out;                        // this stage's output feeds the next
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
