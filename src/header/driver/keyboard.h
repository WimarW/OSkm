#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/cpu/interrupt.h"

/* -- PS/2 Scancode Set 1 Constants -- */
#define EXT_SCANCODE_KP_ENTER    0x1C
#define EXT_SCANCODE_RCTRL_MAKE  0x1D
#define EXT_SCANCODE_RCTRL_BREAK 0x9D
#define EXT_SCANCODE_KP_SLASH    0x35
#define EXT_SCANCODE_RALT_MAKE   0x38
#define EXT_SCANCODE_RALT_BREAK  0xB8
#define EXT_SCANCODE_HOME        0x47
#define EXT_SCANCODE_UP          0x48
#define EXT_SCANCODE_PAGE_UP     0x49
#define EXT_SCANCODE_LEFT        0x4B
#define EXT_SCANCODE_RIGHT       0x4D
#define EXT_SCANCODE_END         0x4F
#define EXT_SCANCODE_DOWN        0x50
#define EXT_SCANCODE_PAGE_DOWN   0x51
#define EXT_SCANCODE_INSERT      0x52
#define EXT_SCANCODE_DELETE      0x53

#define SCANCODE_LCTRL_MAKE       0x1D
#define SCANCODE_LCTRL_BREAK      0x9D // 0x1D | 0x80
#define SCANCODE_LSHIFT_MAKE      0x2A
#define SCANCODE_RSHIFT_MAKE      0x36
#define SCANCODE_LSHIFT_BREAK     0xAA // 0x2A | 0x80
#define SCANCODE_RSHIFT_BREAK     0xB6 // 0x36 | 0x80
#define SCANCODE_LALT_MAKE        0x38
#define SCANCODE_LALT_BREAK       0xB8 // 0x38 | 0x80
#define SCANCODE_CAPSLOCK_MAKE    0x3A
#define SCANCODE_CAPSLOCK_BREAK   0xBA // 0x3A | 0x80
#define SCANCODE_F1_MAKE          0x3B
#define SCANCODE_F10_MAKE         0x44
#define SCANCODE_NUMLOCK_MAKE     0x45
#define SCANCODE_NUMLOCK_BREAK    0xC5 // 0x45 | 0x80
#define SCANCODE_SCROLLLOCK_MAKE  0x46
#define SCANCODE_SCROLLLOCK_BREAK 0xC6 // 0x46 | 0x80
#define SCANCODE_F11_MAKE         0x57
#define SCANCODE_F12_MAKE         0x58

/* -- Non-ASCII special key codes returned in keyboard buffer (0x80..0xFF) -- */
#define KEY_UP                 0x80
#define KEY_DOWN               0x81
#define KEY_LEFT               0x82
#define KEY_RIGHT              0x83
#define KEY_HOME               0x84
#define KEY_END                0x85
#define KEY_PAGE_UP            0x86
#define KEY_PAGE_DOWN          0x87
#define KEY_INSERT             0x88
#define KEY_DELETE             0x89
#define KEY_F1                 0x91
#define KEY_F2                 0x92
#define KEY_F3                 0x93
#define KEY_F4                 0x94
#define KEY_F5                 0x95
#define KEY_F6                 0x96
#define KEY_F7                 0x97
#define KEY_F8                 0x98
#define KEY_F9                 0x99
#define KEY_F10                0x9A
#define KEY_F11                0x9B
#define KEY_F12                0x9C

#define KEYBOARD_DATA_PORT        0x60
#define EXTENDED_SCANCODE_BYTE    0xE0
#define EXTENDED_E1_SCANCODE_BYTE 0xE1

#define KEYBOARD_BUFFER_SIZE      256

/**
 * keyboard_scancode_1_to_ascii_map[256], Convert scancode values that correspond to ASCII printables
 * How to use this array: ascii_char = k[scancode]
 * 
 * By default, QEMU using scancode set 1 (from empirical testing)
 */
extern const char keyboard_scancode_1_to_ascii_map[256];
extern const char keyboard_scancode_1_to_ascii_shift_map[256];

/**
 * KeyboardDriverState - Contain all driver states
 * 
 * @param read_extended_mode Indicate next scancode belongs to an 0xE0 extended sequence
 * @param keyboard_input_on  Indicate whether keyboard ISR enqueues decoded input
 * @param keyboard_buffer    Most recently queued/peeked key (legacy field)
 * @param buffer             Circular FIFO buffer storing decoded keys
 * @param buffer_head        Producer write index into buffer (mod 256)
 * @param buffer_tail        Consumer read index into buffer (mod 256)
 */
struct KeyboardDriverState {
    bool             read_extended_mode;
    bool             keyboard_input_on;
    bool             left_shift_pressed;
    bool             right_shift_pressed;
    bool             left_ctrl_pressed;
    bool             right_ctrl_pressed;
    bool             left_alt_pressed;
    bool             right_alt_pressed;
    bool             caps_lock_on;
    bool             caps_lock_pressed;
    bool             num_lock_on;
    bool             num_lock_pressed;
    bool             scroll_lock_on;
    bool             scroll_lock_pressed;
    uint8_t          e1_skip_bytes;
    char             keyboard_buffer;
    char             buffer[KEYBOARD_BUFFER_SIZE];
    volatile uint8_t buffer_head;
    volatile uint8_t buffer_tail;
} __attribute__((packed));


/* -- Driver Interfaces -- */

// Activate keyboard ISR / start listen keyboard & save to buffer
void keyboard_state_activate(void);

// Deactivate keyboard ISR / stop listening keyboard interrupt
void keyboard_state_deactivate(void);

// Get next buffered keyboard value (or 0 if empty) - @param buf Pointer to char buffer
void get_keyboard_buffer(char *buf);

/* -- Keyboard Interrupt Service Routine -- */

/**
 * Handling keyboard interrupt & process scancodes into ASCII or special key codes.
 * Tracks modifiers continuously and enqueues decoded keys when keyboard_input_on.
 */
void keyboard_isr(void);

void keyboard_configuration(void);

#endif