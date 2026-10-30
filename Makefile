
OS := $(shell uname -s)

# Compiler & linker
# ASM           = nasm
# LIN           = ld
# CC            = gcc
# ISO           = genisoimage

# Directory
SOURCE_FOLDER = src
OUTPUT_FOLDER = bin
ISO_NAME      = OSkm

# Flags
WARNING_CFLAG = -Wall -Wextra -Werror
DEBUG_CFLAG   = -fshort-wchar -g
STRIP_CFLAG   = -nostdlib -fno-builtin -fno-stack-protector -nostartfiles -nodefaultlibs -ffreestanding -fno-delete-null-pointer-checks
ARCH_CFLAG    = -m32 -march=i386 -mno-sse -mno-sse2 -mno-mmx -msoft-float -mno-avx
AFLAGS        = -f elf32 -g -F dwarf
LFLAGS        = -T $(SOURCE_FOLDER)/linker.ld -melf_i386
DISK_NAME     = sample-image
SWAP_NAME = swap

ifeq ($(OS),Darwin)
    LLVM_PREFIX := $(shell brew --prefix llvm)
    LLD_PREFIX  := $(shell brew --prefix lld 2>/dev/null || brew --prefix llvm)
    ASM         := nasm
    CC          := clang
    LIN         := $(LLD_PREFIX)/bin/ld.lld
    ISO         := mkisofs
    CFLAGS      := --target=i386-elf $(DEBUG_CFLAG) $(WARNING_CFLAG) $(STRIP_CFLAG) $(ARCH_CFLAG) -Wno-error=unused-command-line-argument -c -I$(SOURCE_FOLDER)
	INSERTER_FLAGS := -g
else
    # Linux / Windows (WSL)
    ASM         := nasm
    CC          := gcc
    LIN         := ld
    ISO         := $(shell which genisoimage 2>/dev/null || which mkisofs 2>/dev/null)
    CFLAGS      := $(DEBUG_CFLAG) $(WARNING_CFLAG) $(STRIP_CFLAG) $(ARCH_CFLAG) -c -I$(SOURCE_FOLDER)
	    INSERTER_FLAGS := -g -Wno-builtin-declaration-mismatch
endif

run: all $(OUTPUT_FOLDER)/$(SWAP_NAME).bin
	qemu-system-i386 -drive file=$(OUTPUT_FOLDER)/$(DISK_NAME).bin,format=raw,if=ide,index=0,media=disk -drive file=$(OUTPUT_FOLDER)/$(SWAP_NAME).bin,format=raw,if=ide,index=1,media=disk -cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso

debug: all $(OUTPUT_FOLDER)/$(SWAP_NAME).bin
	qemu-system-i386 -s -S -drive file=$(OUTPUT_FOLDER)/$(DISK_NAME).bin,format=raw,if=ide,index=0,media=disk -drive file=$(OUTPUT_FOLDER)/$(SWAP_NAME).bin,format=raw,if=ide,index=1,media=disk -cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso

all: build
build: iso
clean:
	rm -rf $(OUTPUT_FOLDER)/*.o $(OUTPUT_FOLDER)/*.iso $(OUTPUT_FOLDER)/kernel

disk:
	@qemu-img create -f raw $(OUTPUT_FOLDER)/$(DISK_NAME).bin 4M

inserter:
	@mkdir -p $(OUTPUT_FOLDER)
	@$(CC) $(INSERTER_FLAGS) -I$(SOURCE_FOLDER) \
		$(SOURCE_FOLDER)/stdlib/string.c \
		$(SOURCE_FOLDER)/ext2.c \
		$(SOURCE_FOLDER)/external-inserter.c \
		-o $(OUTPUT_FOLDER)/inserter

user-shell:
	@mkdir -p $(OUTPUT_FOLDER)
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/crt0.s -o crt0.o
	@$(CC) $(CFLAGS) -fno-pie $(SOURCE_FOLDER)/user-shell.c -o user-shell.o
	@$(LIN) -T $(SOURCE_FOLDER)/user-linker.ld -melf_i386 --oformat=binary \
		crt0.o user-shell.o -o $(OUTPUT_FOLDER)/shell
	@echo Linking object shell object files and generate flat binary...
	@rm -f *.o

insert-shell: inserter user-shell
	@echo Inserting shell into root directory...
	@cd $(OUTPUT_FOLDER); ./inserter shell 1 $(DISK_NAME).bin

$(OUTPUT_FOLDER)/$(SWAP_NAME).bin:
	@mkdir -p $(OUTPUT_FOLDER)
	@qemu-img create -f raw $@ 4M

kernel:
	@mkdir -p $(OUTPUT_FOLDER)
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/kernel-entrypoint.s -o $(OUTPUT_FOLDER)/kernel-entrypoint.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/kernel.c -o $(OUTPUT_FOLDER)/kernel.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/paging.c -o $(OUTPUT_FOLDER)/paging.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/gdt.c -o $(OUTPUT_FOLDER)/gdt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/framebuffer.c -o $(OUTPUT_FOLDER)/framebuffer.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/stdlib/string.c -o $(OUTPUT_FOLDER)/string.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/portio.c -o $(OUTPUT_FOLDER)/portio.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/interrupt.c -o $(OUTPUT_FOLDER)/interrupt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/idt.c -o $(OUTPUT_FOLDER)/idt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/keyboard.c -o $(OUTPUT_FOLDER)/keyboard.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/disk.c -o $(OUTPUT_FOLDER)/disk.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/ext2.c -o $(OUTPUT_FOLDER)/ext2.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/swap.c -o $(OUTPUT_FOLDER)/swap.o
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/intsetup.s -o $(OUTPUT_FOLDER)/intsetup.o
	@echo Linking object files and generate elf32...
	@$(LIN) $(LFLAGS) $(OUTPUT_FOLDER)/*.o -o $(OUTPUT_FOLDER)/kernel
	@rm -f $(OUTPUT_FOLDER)/*.o

iso: kernel
	@mkdir -p $(OUTPUT_FOLDER)/iso/boot/grub
	@cp $(OUTPUT_FOLDER)/kernel $(OUTPUT_FOLDER)/iso/boot/
	@cp other/grub1 $(OUTPUT_FOLDER)/iso/boot/grub/
	@cp $(SOURCE_FOLDER)/menu.lst $(OUTPUT_FOLDER)/iso/boot/grub/
	@$(ISO) -R -b boot/grub/grub1 -no-emul-boot -boot-load-size 4 -A os -input-charset utf8 -quiet -boot-info-table -o $(OUTPUT_FOLDER)/$(ISO_NAME).iso $(OUTPUT_FOLDER)/iso
	@rm -r $(OUTPUT_FOLDER)/iso/
