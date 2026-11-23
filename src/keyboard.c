#include "header/driver/keyboard.h"
#include "header/cpu/portio.h"
#include "header/stdlib/string.h"
#include "header/text/framebuffer.h"

static struct KeyboardDriverState keyboard_state = {
    .read_extended_mode = false,
    .keyboard_input_on = false,
    .left_shift_pressed = false,
    .right_shift_pressed = false,
    .left_ctrl_pressed = false,
    .right_ctrl_pressed = false,
    .left_alt_pressed = false,
    .right_alt_pressed = false,
    .caps_lock_on = false,
    .caps_lock_pressed = false,
    .num_lock_on = true,
    .num_lock_pressed = false,
    .scroll_lock_on = false,
    .scroll_lock_pressed = false,
    .e1_skip_bytes = 0,
    .keyboard_buffer = 0,
    .buffer = {0},
    .buffer_head = 0,
    .buffer_tail = 0,
};

const char keyboard_scancode_1_to_ascii_shift_map[256] = {
      0, 0x1B, '!', '@', '#', '$', '%', '^',  '&', '*', '(',  ')',  '_', '+', '\b', '\t',
    'Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I',  'O', 'P', '{',  '}', '\n',   0,  'A',  'S',
    'D',  'F', 'G', 'H', 'J', 'K', 'L', ':',  '"', '~',   0,  '|',  'Z', 'X',  'C',  'V',
    'B',  'N', 'M', '<', '>', '?',   0, '*',    0, ' ',   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0, '-',    0,    0,   0,  '+',    0,
      0,    0,   0,   0,   0,   0, '|',   0,    0,   0,   0,    0,    0,   0,    0,    0,
};

const char keyboard_scancode_1_to_ascii_map[256] = {
      0, 0x1B, '1', '2', '3', '4', '5', '6',  '7', '8', '9',  '0',  '-', '=', '\b', '\t',
    'q',  'w', 'e', 'r', 't', 'y', 'u', 'i',  'o', 'p', '[',  ']', '\n',   0,  'a',  's',
    'd',  'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0, '\\',  'z', 'x',  'c',  'v',
    'b',  'n', 'm', ',', '.', '/',   0, '*',    0, ' ',   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0, '-',    0,    0,   0,  '+',    0,
      0,    0,   0,   0,   0,   0, '\\',  0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,

      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
};

static void keyboard_buffer_push(char c) {
    if (c == 0) {
        return;
    }
    uint8_t head = keyboard_state.buffer_head;
    uint8_t next_head = (uint8_t)(head + 1u);
    if (next_head == keyboard_state.buffer_tail) {
        return;
    }
    keyboard_state.buffer[head] = c;
    __asm__ volatile("" : : : "memory");
    keyboard_state.buffer_head = next_head;
    keyboard_state.keyboard_buffer = c;
}

static char translate_numpad_scancode(uint8_t scancode, bool num_lock, bool shift, bool ctrl, bool alt) {
    if (num_lock && !shift) {
        if (ctrl || alt) {
            return 0;
        }
        switch (scancode) {
            case 0x47: return '7';
            case 0x48: return '8';
            case 0x49: return '9';
            case 0x4B: return '4';
            case 0x4C: return '5';
            case 0x4D: return '6';
            case 0x4F: return '1';
            case 0x50: return '2';
            case 0x51: return '3';
            case 0x52: return '0';
            case 0x53: return '.';
            default:   return 0;
        }
    }

    switch (scancode) {
        case 0x47: return (char)KEY_HOME;
        case 0x48: return (char)KEY_UP;
        case 0x49: return (char)KEY_PAGE_UP;
        case 0x4B: return (char)KEY_LEFT;
        case 0x4D: return (char)KEY_RIGHT;
        case 0x4F: return (char)KEY_END;
        case 0x50: return (char)KEY_DOWN;
        case 0x51: return (char)KEY_PAGE_DOWN;
        case 0x52: return (char)KEY_INSERT;
        case 0x53: return (char)KEY_DELETE;
        default:   return 0;
    }
}

static char translate_ctrl_char(uint8_t scancode, char base_char) {
    if (base_char >= 'a' && base_char <= 'z') {
        return (char)(base_char - 'a' + 1);
    }
    switch (base_char) {
        case '[':  return 0x1B;
        case '\\': return 0x1C;
        case ']':  return 0x1D;
        case '6':  return 0x1E;
        case '\b': return '\b';
        case '\n': return '\n';
        case 0x1B: return 0x1B;
        default:
            if (scancode == 0x0C) {
                return 0x1F;
            }
            return 0;
    }
}

void keyboard_state_activate(void) {
    keyboard_state.keyboard_input_on = true;
}

void keyboard_state_deactivate(void) {
    keyboard_state.keyboard_input_on = false;
}

void get_keyboard_buffer(char *buf) {
    if (buf == NULL) {
        return;
    }
    uint8_t tail = keyboard_state.buffer_tail;
    uint8_t head = keyboard_state.buffer_head;
    if (tail == head) {
        *buf = 0;
        return;
    }
    char c = keyboard_state.buffer[tail];
    __asm__ volatile("" : : : "memory");
    keyboard_state.buffer_tail = (uint8_t)(tail + 1u);
    *buf = c;
}

void keyboard_isr(void) {
    uint8_t scancode = in(KEYBOARD_DATA_PORT);

    if (keyboard_state.e1_skip_bytes > 0) {
        keyboard_state.e1_skip_bytes--;
        return;
    }
    if (scancode == EXTENDED_E1_SCANCODE_BYTE) {
        keyboard_state.read_extended_mode = false;
        keyboard_state.e1_skip_bytes = 2;
        return;
    }

    if (scancode == EXTENDED_SCANCODE_BYTE) {
        keyboard_state.read_extended_mode = true;
        return;
    }

    if (keyboard_state.read_extended_mode) {
        keyboard_state.read_extended_mode = false;

        if (scancode == EXT_SCANCODE_RCTRL_MAKE) {
            keyboard_state.right_ctrl_pressed = true;
            return;
        } else if (scancode == EXT_SCANCODE_RCTRL_BREAK) {
            keyboard_state.right_ctrl_pressed = false;
            return;
        } else if (scancode == EXT_SCANCODE_RALT_MAKE) {
            keyboard_state.right_alt_pressed = true;
            return;
        } else if (scancode == EXT_SCANCODE_RALT_BREAK) {
            keyboard_state.right_alt_pressed = false;
            return;
        }

        if ((scancode & 0x80u) != 0 || !keyboard_state.keyboard_input_on) {
            return;
        }

        bool ctrl = keyboard_state.left_ctrl_pressed || keyboard_state.right_ctrl_pressed;
        bool alt  = keyboard_state.left_alt_pressed  || keyboard_state.right_alt_pressed;
        char mapped = 0;

        switch (scancode) {
            case EXT_SCANCODE_KP_ENTER:
                if (!alt) {
                    mapped = '\n';
                }
                break;
            case EXT_SCANCODE_KP_SLASH:
                if (!ctrl && !alt) {
                    mapped = '/';
                }
                break;
            case EXT_SCANCODE_UP:        mapped = (char)KEY_UP;        break;
            case EXT_SCANCODE_DOWN:      mapped = (char)KEY_DOWN;      break;
            case EXT_SCANCODE_LEFT:      mapped = (char)KEY_LEFT;      break;
            case EXT_SCANCODE_RIGHT:     mapped = (char)KEY_RIGHT;     break;
            case EXT_SCANCODE_HOME:      mapped = (char)KEY_HOME;      break;
            case EXT_SCANCODE_END:       mapped = (char)KEY_END;       break;
            case EXT_SCANCODE_PAGE_UP:   mapped = (char)KEY_PAGE_UP;   break;
            case EXT_SCANCODE_PAGE_DOWN: mapped = (char)KEY_PAGE_DOWN; break;
            case EXT_SCANCODE_INSERT:    mapped = (char)KEY_INSERT;    break;
            case EXT_SCANCODE_DELETE:    mapped = (char)KEY_DELETE;    break;
            default:
                break;
        }
        keyboard_buffer_push(mapped);
        return;
    }

    switch (scancode) {
        case SCANCODE_LSHIFT_MAKE:
            keyboard_state.left_shift_pressed = true;
            return;
        case SCANCODE_RSHIFT_MAKE:
            keyboard_state.right_shift_pressed = true;
            return;
        case SCANCODE_LSHIFT_BREAK:
            keyboard_state.left_shift_pressed = false;
            return;
        case SCANCODE_RSHIFT_BREAK:
            keyboard_state.right_shift_pressed = false;
            return;
        case SCANCODE_LCTRL_MAKE:
            keyboard_state.left_ctrl_pressed = true;
            return;
        case SCANCODE_LCTRL_BREAK:
            keyboard_state.left_ctrl_pressed = false;
            return;
        case SCANCODE_LALT_MAKE:
            keyboard_state.left_alt_pressed = true;
            return;
        case SCANCODE_LALT_BREAK:
            keyboard_state.left_alt_pressed = false;
            return;
        case SCANCODE_CAPSLOCK_MAKE:
            if (!keyboard_state.caps_lock_pressed) {
                keyboard_state.caps_lock_on = !keyboard_state.caps_lock_on;
                keyboard_state.caps_lock_pressed = true;
            }
            return;
        case SCANCODE_CAPSLOCK_BREAK:
            keyboard_state.caps_lock_pressed = false;
            return;
        case SCANCODE_NUMLOCK_MAKE:
            if (!keyboard_state.num_lock_pressed) {
                keyboard_state.num_lock_on = !keyboard_state.num_lock_on;
                keyboard_state.num_lock_pressed = true;
            }
            return;
        case SCANCODE_NUMLOCK_BREAK:
            keyboard_state.num_lock_pressed = false;
            return;
        case SCANCODE_SCROLLLOCK_MAKE:
            if (!keyboard_state.scroll_lock_pressed) {
                keyboard_state.scroll_lock_on = !keyboard_state.scroll_lock_on;
                keyboard_state.scroll_lock_pressed = true;
            }
            return;
        case SCANCODE_SCROLLLOCK_BREAK:
            keyboard_state.scroll_lock_pressed = false;
            return;
        default:
            break;
    }

    if ((scancode & 0x80u) != 0 || !keyboard_state.keyboard_input_on) {
        return;
    }

    bool shift = keyboard_state.left_shift_pressed || keyboard_state.right_shift_pressed;
    bool ctrl  = keyboard_state.left_ctrl_pressed  || keyboard_state.right_ctrl_pressed;
    bool alt   = keyboard_state.left_alt_pressed   || keyboard_state.right_alt_pressed;

    if (scancode >= SCANCODE_F1_MAKE && scancode <= SCANCODE_F10_MAKE) {
        keyboard_buffer_push((char)(KEY_F1 + (scancode - SCANCODE_F1_MAKE)));
        return;
    }
    if (scancode == SCANCODE_F11_MAKE) {
        keyboard_buffer_push((char)KEY_F11);
        return;
    }
    if (scancode == SCANCODE_F12_MAKE) {
        keyboard_buffer_push((char)KEY_F12);
        return;
    }

    if (scancode >= 0x47 && scancode <= 0x53 && scancode != 0x4A && scancode != 0x4E) {
        keyboard_buffer_push(translate_numpad_scancode(
            scancode, keyboard_state.num_lock_on, shift, ctrl, alt
        ));
        return;
    }

    if (alt) {
        return;
    }

    char base_char = keyboard_scancode_1_to_ascii_map[scancode];
    if (base_char == 0) {
        return;
    }

    if (ctrl) {
        keyboard_buffer_push(translate_ctrl_char(scancode, base_char));
        return;
    }

    char mapped_char;
    if (base_char >= 'a' && base_char <= 'z') {
        bool upper = shift ^ keyboard_state.caps_lock_on;
        mapped_char = upper ? keyboard_scancode_1_to_ascii_shift_map[scancode] : base_char;
    } else {
        mapped_char = shift ? keyboard_scancode_1_to_ascii_shift_map[scancode] : base_char;
    }
    keyboard_buffer_push(mapped_char);
}

static void insert_editor_char(int *row, int *col, int *max_row, int row_lengths[FRAMEBUFFER_HEIGHT], char c) {
    if (*col < row_lengths[*row]) {
        int shift_limit = row_lengths[*row];
        if (shift_limit >= FRAMEBUFFER_WIDTH) {
            shift_limit = FRAMEBUFFER_WIDTH - 1;
        }
        for (int c_idx = shift_limit; c_idx > *col; c_idx--) {
            char prev_c = framebuffer_read(*row, c_idx - 1);
            framebuffer_write(*row, c_idx, prev_c, 0xF, 0);
        }
        if (row_lengths[*row] < FRAMEBUFFER_WIDTH) {
            row_lengths[*row]++;
        }
    }

    framebuffer_write(*row, *col, c, 0xF, 0);
    (*col)++;

    if (*col > row_lengths[*row]) {
        row_lengths[*row] = *col;
    }

    if (*col >= FRAMEBUFFER_WIDTH) {
        if (*row < FRAMEBUFFER_HEIGHT - 1) {
            *col = 0;
            (*row)++;
            if (*row > *max_row) {
                *max_row = *row;
            }
        } else {
            *col = FRAMEBUFFER_WIDTH - 1;
        }
    }
}

void keyboard_configuration(void) {
    int row = 0, col = 0;
    int row_lengths[FRAMEBUFFER_HEIGHT] = {0};
    int max_row = 0;

    while (true) {
        char c;
        get_keyboard_buffer(&c);
        if (c) {
            uint8_t key = (uint8_t)c;
            if (c == '\b') {
                if (col > 0) {
                    col--;
                    for (int c_idx = col; c_idx < row_lengths[row] - 1; c_idx++) {
                        char next_c = framebuffer_read(row, c_idx + 1);
                        framebuffer_write(row, c_idx, next_c, 0xF, 0);
                    }
                    framebuffer_write(row, row_lengths[row] - 1, ' ', 0xF, 0);
                    row_lengths[row]--;
                } else if (row > 0) {
                    int target_row = row - 1;
                    int target_col = row_lengths[target_row];
                    int curr_len = row_lengths[row];

                    if (target_col + curr_len <= FRAMEBUFFER_WIDTH) {
                        for (int i = 0; i < curr_len; i++) {
                            char ch = framebuffer_read(row, i);
                            framebuffer_write(target_row, target_col + i, ch, 0xF, 0);
                        }
                        row_lengths[target_row] += curr_len;

                        for (int r = row; r < FRAMEBUFFER_HEIGHT - 1; r++) {
                            int len_below = row_lengths[r + 1];
                            for (int c_idx = 0; c_idx < len_below; c_idx++) {
                                char char_below = framebuffer_read(r + 1, c_idx);
                                framebuffer_write(r, c_idx, char_below, 0xF, 0);
                            }
                            for (int c_idx = len_below; c_idx < FRAMEBUFFER_WIDTH; c_idx++) {
                                framebuffer_write(r, c_idx, ' ', 0xF, 0);
                            }
                            row_lengths[r] = len_below;
                        }

                        for (int c_idx = 0; c_idx < FRAMEBUFFER_WIDTH; c_idx++) {
                            framebuffer_write(FRAMEBUFFER_HEIGHT - 1, c_idx, ' ', 0xF, 0);
                        }
                        row_lengths[FRAMEBUFFER_HEIGHT - 1] = 0;

                        row = target_row;
                        col = target_col;
                        if (col >= FRAMEBUFFER_WIDTH) {
                            col = FRAMEBUFFER_WIDTH - 1;
                        }

                        if (max_row > 0) max_row--;
                    }
                }
            } else if (key == KEY_DELETE) {
                if (col < row_lengths[row]) {
                    for (int c_idx = col; c_idx < row_lengths[row] - 1; c_idx++) {
                        char next_c = framebuffer_read(row, c_idx + 1);
                        framebuffer_write(row, c_idx, next_c, 0xF, 0);
                    }
                    framebuffer_write(row, row_lengths[row] - 1, ' ', 0xF, 0);
                    row_lengths[row]--;
                } else if (row < max_row) {
                    int curr_len = row_lengths[row];
                    int next_len = row_lengths[row + 1];
                    if (curr_len + next_len <= FRAMEBUFFER_WIDTH) {
                        for (int i = 0; i < next_len; i++) {
                            char ch = framebuffer_read(row + 1, i);
                            framebuffer_write(row, curr_len + i, ch, 0xF, 0);
                        }
                        row_lengths[row] += next_len;

                        for (int r = row + 1; r < FRAMEBUFFER_HEIGHT - 1; r++) {
                            int len_below = row_lengths[r + 1];
                            for (int c_idx = 0; c_idx < len_below; c_idx++) {
                                char char_below = framebuffer_read(r + 1, c_idx);
                                framebuffer_write(r, c_idx, char_below, 0xF, 0);
                            }
                            for (int c_idx = len_below; c_idx < FRAMEBUFFER_WIDTH; c_idx++) {
                                framebuffer_write(r, c_idx, ' ', 0xF, 0);
                            }
                            row_lengths[r] = len_below;
                        }

                        for (int c_idx = 0; c_idx < FRAMEBUFFER_WIDTH; c_idx++) {
                            framebuffer_write(FRAMEBUFFER_HEIGHT - 1, c_idx, ' ', 0xF, 0);
                        }
                        row_lengths[FRAMEBUFFER_HEIGHT - 1] = 0;

                        if (max_row > 0) max_row--;
                    }
                }
            } else if (c == '\n') {
                if (row < FRAMEBUFFER_HEIGHT - 1) {
                    for (int r = FRAMEBUFFER_HEIGHT - 1; r > row + 1; r--) {
                        int len_above = row_lengths[r - 1];
                        for (int c_idx = 0; c_idx < len_above; c_idx++) {
                            char char_above = framebuffer_read(r - 1, c_idx);
                            framebuffer_write(r, c_idx, char_above, 0xF, 0);
                        }
                        for (int c_idx = len_above; c_idx < FRAMEBUFFER_WIDTH; c_idx++) {
                            framebuffer_write(r, c_idx, ' ', 0xF, 0);
                        }
                        row_lengths[r] = len_above;
                    }

                    int dest_c = 0;
                    for (int src_c = col; src_c < row_lengths[row]; src_c++) {
                        char char_to_move = framebuffer_read(row, src_c);
                        framebuffer_write(row + 1, dest_c, char_to_move, 0xF, 0);
                        dest_c++;
                    }
                    for (int c_clear = dest_c; c_clear < FRAMEBUFFER_WIDTH; c_clear++) {
                        framebuffer_write(row + 1, c_clear, ' ', 0xF, 0);
                    }
                    row_lengths[row + 1] = dest_c;

                    for (int c_clear = col; c_clear < row_lengths[row]; c_clear++) {
                        framebuffer_write(row, c_clear, ' ', 0xF, 0);
                    }
                    row_lengths[row] = col;

                    row++;
                    col = 0;

                    if (max_row < FRAMEBUFFER_HEIGHT - 1) max_row++;
                } else {
                    for (int c_clear = col; c_clear < row_lengths[row]; c_clear++) {
                        framebuffer_write(row, c_clear, ' ', 0xF, 0);
                    }
                    row_lengths[row] = col;
                    col = 0;
                }
            } else if (key == KEY_LEFT) {
                if (col > 0) {
                    col--;
                } else if (row > 0) {
                    row--;
                    col = row_lengths[row];
                    if (col >= FRAMEBUFFER_WIDTH) {
                        col = FRAMEBUFFER_WIDTH - 1;
                    }
                }
            } else if (key == KEY_RIGHT) {
                if (col < row_lengths[row] && col < FRAMEBUFFER_WIDTH - 1) {
                    col++;
                } else if (row < max_row) {
                    row++;
                    col = 0;
                }
            } else if (key == KEY_UP) {
                if (row > 0) {
                    row--;
                    if (col > row_lengths[row]) {
                        col = row_lengths[row];
                    }
                    if (col >= FRAMEBUFFER_WIDTH) {
                        col = FRAMEBUFFER_WIDTH - 1;
                    }
                }
            } else if (key == KEY_DOWN) {
                if (row < max_row) {
                    row++;
                    if (col > row_lengths[row]) {
                        col = row_lengths[row];
                    }
                    if (col >= FRAMEBUFFER_WIDTH) {
                        col = FRAMEBUFFER_WIDTH - 1;
                    }
                }
            } else if (key == KEY_HOME) {
                col = 0;
            } else if (key == KEY_END) {
                col = row_lengths[row];
                if (col >= FRAMEBUFFER_WIDTH) {
                    col = FRAMEBUFFER_WIDTH - 1;
                }
            } else if (c == '\t') {
                int spaces = 4 - (col % 4);
                for (int i = 0; i < spaces; i++) {
                    insert_editor_char(&row, &col, &max_row, row_lengths, ' ');
                }
            } else if (c >= 32 && c <= 126) {
                insert_editor_char(&row, &col, &max_row, row_lengths, c);
            }
            framebuffer_set_cursor(row, col);
        }
    }
}
