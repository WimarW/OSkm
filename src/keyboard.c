#include "header/driver/keyboard.h"
#include "header/cpu/portio.h"
#include "header/stdlib/string.h"


static struct KeyboardDriverState keyboard_state = {
    .read_extended_mode = false,
    .keyboard_buffer = 0,
    .keyboard_input_on = false,
};


const char keyboard_scancode_1_to_ascii_map[256] = {
      0, 0x1B, '1', '2', '3', '4', '5', '6',  '7', '8', '9',  '0',  '-', '=', '\b', '\t',
    'q',  'w', 'e', 'r', 't', 'y', 'u', 'i',  'o', 'p', '[',  ']', '\n',   0,  'a',  's',
    'd',  'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0, '\\',  'z', 'x',  'c',  'v',
    'b',  'n', 'm', ',', '.', '/',   0, '*',    0, ' ',   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0, '-',    0,    0,   0,  '+',    0,
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
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
};

void keyboard_state_activate(void){
    keyboard_state.keyboard_input_on = true;
}

void keyboard_state_deactivate(void){
    keyboard_state.keyboard_input_on = false;
}

void get_keyboard_buffer(char *buf){
    *buf = keyboard_state.keyboard_buffer;
    keyboard_state.keyboard_buffer = 0;
}


void keyboard_isr(void) {
    uint8_t scancode = in(KEYBOARD_DATA_PORT);

    if(!keyboard_state.keyboard_input_on){
        return;
    }

    if(scancode == EXTENDED_SCANCODE_BYTE){
        keyboard_state.read_extended_mode = true;
        return;
    }

    if(keyboard_state.read_extended_mode){
        keyboard_state.read_extended_mode= false;

        if(scancode == EXT_SCANCODE_DOWN    || scancode == EXT_SCANCODE_RIGHT  || scancode == EXT_SCANCODE_UP     || scancode == EXT_SCANCODE_LEFT){
                keyboard_state.keyboard_buffer = (char) scancode;
            }
        return;
    }

    if(!(scancode & 0x80)){
        char mapped_char = keyboard_scancode_1_to_ascii_map[scancode];
        if(mapped_char != 0){
            keyboard_state.keyboard_buffer = mapped_char;
        }
    }
}
