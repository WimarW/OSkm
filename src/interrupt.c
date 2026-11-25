#include "header/cpu/interrupt.h"
#include "header/cpu/portio.h"
#include "header/cpu/gdt.h"
#include "header/driver/keyboard.h"
#include "header/filesystem/ext2.h"
#include "header/memory/swap.h"
#include "header/text/framebuffer.h"
#include "header/process/scheduler.h"
#include "header/process/process.h"
#include "header/driver/cmos.h"
#include "header/stdlib/string.h"

void io_wait(void)
{
    out(0x80, 0);
}

void pic_ack(uint8_t irq)
{
    if (irq >= 8)
        out(PIC2_COMMAND, PIC_ACK);
    out(PIC1_COMMAND, PIC_ACK);
}

void pic_remap(void)
{
    // Starts the initialization sequence in cascade mode
    out(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    out(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    out(PIC1_DATA, PIC1_OFFSET); // ICW2: Master PIC vector offset
    io_wait();
    out(PIC2_DATA, PIC2_OFFSET); // ICW2: Slave PIC vector offset
    io_wait();
    out(PIC1_DATA, 0b0100); // ICW3: tell Master PIC, slave PIC at IRQ2 (0000 0100)
    io_wait();
    out(PIC2_DATA, 0b0010); // ICW3: tell Slave PIC its cascade identity (0000 0010)
    io_wait();

    out(PIC1_DATA, ICW4_8086);
    io_wait();
    out(PIC2_DATA, ICW4_8086);
    io_wait();

    // Disable all interrupts
    out(PIC1_DATA, PIC_DISABLE_ALL_MASK);
    out(PIC2_DATA, PIC_DISABLE_ALL_MASK);
}

void syscall(struct InterruptFrame frame) {
    switch (frame.cpu.general.eax) {
        case 0:
            *((int8_t*) frame.cpu.general.ecx) = read(
                *(struct EXT2DriverRequest*) frame.cpu.general.ebx
            );
            break;
        case 1:
            *((int8_t*) frame.cpu.general.ecx) = read_directory(
                (struct EXT2DriverRequest*) frame.cpu.general.ebx
            );
            break;
        case 2: 
            *((int8_t*) frame.cpu.general.ecx) = write(
                (struct EXT2DriverRequest*) frame.cpu.general.ebx
            );
            break;
        case 3:
            *((int8_t*) frame.cpu.general.ecx) = delete(
                *(struct EXT2DriverRequest*) frame.cpu.general.ebx
            );
            break;
        case 4:
            get_keyboard_buffer((char*) frame.cpu.general.ebx);
            break;
        case 5:
            framebuffer_putchar(
                (char) frame.cpu.general.ebx, 
                frame.cpu.general.ecx
            );
            break;
        case 6:
            framebuffer_puts(
                (const char*) frame.cpu.general.ebx,
                frame.cpu.general.ecx,
                frame.cpu.general.edx
            ); 
            break;
        case 7: 
            keyboard_state_activate();
            break;
        case 8:
            framebuffer_clear();
            framebuffer_set_cursor(0, 0);
            break;
        case 9:
            cmos_read_time((struct CMOSTime*) frame.cpu.general.ebx);
            break;
        case 10: {
            struct ProcessControlBlock *running = process_get_current_running_pcb_pointer();
            if (running != NULL) {
                process_destroy(running->metadata.pid);
            }
            scheduler_switch_to_next_process();
            break;
        }
        case 11: {
            int32_t ret = PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT;
            if (frame.cpu.general.ebx != 0) {
                ret = process_create_user_process(
                    *(struct EXT2DriverRequest*) frame.cpu.general.ebx
                );
            }
            if (frame.cpu.general.ecx != 0) {
                *((int32_t*) frame.cpu.general.ecx) = ret;
            }
            break;
        }
        case 12: {
            uint32_t target_pid = frame.cpu.general.ebx;
            struct ProcessControlBlock *running = process_get_current_running_pcb_pointer();
            bool is_self = (running != NULL && running->metadata.pid == target_pid);
            bool destroyed = process_destroy(target_pid);
            if (is_self && destroyed) {
                scheduler_switch_to_next_process();
            }
            if (frame.cpu.general.ecx != 0) {
                *((bool*) frame.cpu.general.ecx) = destroyed;
            }
            break;
        }
        case 13: {
            uint32_t count = process_get_active_list(
                (struct ProcessInfo*) frame.cpu.general.ebx,
                frame.cpu.general.ecx
            );
            if (frame.cpu.general.edx != 0) {
                *((uint32_t*) frame.cpu.general.edx) = count;
            }
            break;
        }
        case 14:
            framebuffer_write(
                (frame.cpu.general.ebx >> 16) & 0xFFFF,
                frame.cpu.general.ebx & 0xFFFF,
                (char) frame.cpu.general.ecx,
                frame.cpu.general.edx,
                0
            );
            break;
        case 15:
            framebuffer_clear();
            framebuffer_puts("Shutting down OSkm...\n", 22, 0x0F);
            
            // ACPI poweroff ports (QEMU, Bochs/older QEMU, VirtualBox)
            out16(0x604, 0x2000);   // QEMU (modern PIIX4)
            out16(0xB004, 0x2000);  // Bochs / older QEMU
            out16(0x4004, 0x3400);  // VirtualBox
            
            // Fallback halt if emulator didn't power off
            __asm__ volatile("cli");
            while (true) {
                __asm__ volatile("hlt");
            }
            break;
    }
}


void main_interrupt_handler(struct InterruptFrame frame)
{
    switch (frame.int_number)
    {
    case 14:
        handle_page_fault(frame.int_stack.error_code);
        break;
    case PIC1_OFFSET + IRQ_TIMER:
        // Only user execution can be preempted by this scheduler. A kernel IRQ
        // (including idle) has no hardware user-stack tail and returns normally.
        if ((frame.int_stack.cs & 0x3u) == 0x3u &&
            process_get_current_running_pcb_pointer() != NULL) {
            struct Context ctx = {
                .cpu = frame.cpu,
                .eip = frame.int_stack.eip,
                .eflags = frame.int_stack.eflags,
                .page_directory_virtual_addr = paging_get_current_page_directory_addr(),
            };
            ctx.cpu.stack.esp = frame.user_esp;
            scheduler_save_context_to_current_running_pcb(ctx);

            // The switch never returns here. ACK before leaving this handler;
            // kernel-mode IRQ0 still uses the shared ACK at the bottom instead.
            pic_ack(IRQ_TIMER);
            scheduler_switch_to_next_process();
        }
        break;
    case PIC1_OFFSET + IRQ_KEYBOARD:
        keyboard_isr();
        break;
    case 0x30:
        syscall(frame);
        break;
    default:
        break;
    }

    if (frame.int_number >= PIC1_OFFSET && frame.int_number < PIC1_OFFSET + 16)
    {
        pic_ack(frame.int_number - PIC1_OFFSET);
    }
}

void activate_keyboard_interrupt(void)
{
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_KEYBOARD));
}

void activate_timer_interrupt(void)
{
    uint32_t saved_eflags;
    // Prevent interrupts while writing the two-byte divisor and updating PIC mask.
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(saved_eflags) : : "memory");

    uint16_t counter = PIT_TIMER_COUNTER;
    out(PIT_COMMAND_REGISTER_PIO, PIT_COMMAND_VALUE);
    out(PIT_CHANNEL_0_DATA_PIO, (uint8_t)(counter & 0xFF));
    out(PIT_CHANNEL_0_DATA_PIO, (uint8_t)(counter >> 8));

    // Clear only IRQ0's mask bit, preserving IRQ1 (keyboard) and all other masks.
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_TIMER));

    // IF is EFLAGS bit 9. A caller that disabled interrupts keeps them disabled.
    if (saved_eflags & (1u << 9)) {
        __asm__ volatile("sti" : : : "memory");
    }
}


struct TSSEntry _interrupt_tss_entry = {
    .ss0  = GDT_KERNEL_DATA_SEGMENT_SELECTOR,
};

void set_tss_kernel_current_stack(void) {
    uint32_t stack_ptr;
    // Reading base stack frame instead esp
    __asm__ volatile ("mov %%ebp, %0": "=r"(stack_ptr) : /* <Empty> */);
    // Add 8 because 4 for ret address and other 4 is for stack_ptr variable
    _interrupt_tss_entry.esp0 = stack_ptr + 8; 
}

