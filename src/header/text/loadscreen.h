#ifndef _LOADSCREEN_H
#define _LOADSCREEN_H

#include <stdint.h>

/**
 * Disable the VGA hardware blinking cursor while the splash screen is active.
 */
void splash_hide_cursor(void);

/**
 * Draw the centered NAIRO title banner and multi-color mascot
 * onto the 80x25 VGA text framebuffer.
 */
void splash_draw_nairo(void);

/**
 * Update the centered status message (row 20) and CP437 progress bar (row 21).
 *
 * @param percent Completion percentage (0 to 100)
 * @param status  Null-terminated status label to display above the bar
 */
void splash_draw_progress(uint8_t percent, const char *status);

/**
 * Busy-wait delay so boot milestones stay briefly visible on fast emulators.
 *
 * @param loops Number of NOP iterations to wait
 */
void splash_delay(uint32_t loops);

/**
 * Run the standalone animated loading screen from 0% to 100%,
 * then clear the framebuffer and restore the cursor.
 */
void splash_show_loading_screen(void);

#endif