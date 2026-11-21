"""Check timer-driven saving and restoration against real CPU interrupt frames.

The controlled user loop is placed in an already allocated test page. No user
application, syscall, production logging, or test hook is added to the kernel.
"""

import argparse
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True
from irq0_smoke import QMP
from run_scheduler_boot_tests import checked


def verify_preemption(root, build, disk, swap, null_ds, direction_flag=False):
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    socket_path = build / "qmp.sock"
    command = [
        "qemu-system-i386", "-accel", "tcg", "-m", "128",
        "-display", "none", "-serial", "none", "-monitor", "none",
        "-no-reboot", "-no-shutdown", "-snapshot", "-boot", "d", "-S",
        "-gdb", f"tcp:127.0.0.1:{port}",
        "-drive", f"file={disk},format=raw,if=ide,index=0,media=disk",
        "-drive", f"file={swap},format=raw,if=ide,index=1,media=disk",
        "-cdrom", str(build / "OSkm.iso"),
        "-qmp", f"unix:{socket_path},server=on,wait=off",
    ]
    process = subprocess.Popen(command, stdout=subprocess.DEVNULL,
                               stderr=subprocess.PIPE)
    qmp = None
    try:
        qmp = QMP(socket_path, process)
        commands = [
            "set pagination off", "set confirm off",
            f"target remote 127.0.0.1:{port}",
            "hbreak *scheduler_switch_to_next_process", "continue",
        ]
        initial = {
            "cpu.general.eax": 0x11111111, "cpu.general.ebx": 0x22222222,
            "cpu.general.ecx": 0x33333333, "cpu.general.edx": 0x44444444,
            "cpu.stack.ebp": 0x55555555, "cpu.index.esi": 0x66666666,
            "cpu.index.edi": 0x77777777, "cpu.stack.esp": 0xBFFFFEFC,
            "cpu.segment.ds": 0 if null_ds else 0x23,
            "eip": 0x80000, "eflags": 0x602 if direction_flag else 0x202,
        }
        for field, value in initial.items():
            commands.append(f"set variable _process_list[0].context.{field} = {value}")
        commands.extend([
            "delete breakpoints", "hbreak *process_context_switch", "continue",
            # INC EAX; JMP back to INC EAX. This loop never yields voluntarily.
            "set {unsigned char}0x80000 = 0x40",
            "set {unsigned char}0x80001 = 0xeb",
            "set {unsigned char}0x80002 = 0xfd",
            "delete breakpoints",
        ])
        fields = [
            "index.edi", "index.esi", "stack.ebp", "general.ebx",
            "general.edx", "general.ecx", "general.eax",
            "segment.gs", "segment.fs", "segment.es", "segment.ds",
        ]
        for sample in range(3):
            commands.extend([
                "hbreak main_interrupt_handler", "continue",
                'python assert int(gdb.parse_and_eval("frame.int_number")) == 32, "Expected timer interrupt"',
                'python assert int(gdb.parse_and_eval("frame.int_stack.cs")) == 0x1b, "Expected interrupted user code"',
                'python assert not (int(gdb.parse_and_eval("$eflags")) & 0x400), "C handler entered with DF set"',
                "set $snapshot = frame",
                # Read the hardware-pushed user ESP independently of the stub's
                # saved PUSHAD ESP slot. Static layout checks pin this offset.
                "set $hardware_esp = *(unsigned int*)((char*)&frame + 68)",
                "set $resume_eip = frame.int_stack.eip",
            ])
            if sample == 0:
                commands.append("set $handler_esp = $esp")
            else:
                commands.append('python assert int(gdb.parse_and_eval("$esp")) == int(gdb.parse_and_eval("$handler_esp")), "Kernel stack grew across interrupts"')
            commands.extend([
                'python assert int(gdb.parse_and_eval("$hardware_esp")) == 0xbffffefc, "Interrupted user stack changed"',
                'python assert int(gdb.parse_and_eval("$resume_eip")) in (0x80000, 0x80001), "User loop restarted elsewhere"',
                "delete breakpoints", "hbreak *scheduler_switch_to_next_process", "continue",
            ])
            for field in fields:
                commands.append(
                    f'python assert int(gdb.parse_and_eval("_process_list[0].context.cpu.{field}"))'
                    f' == int(gdb.parse_and_eval("$snapshot.cpu.{field}")), "Incorrect saved {field}"')
            commands.extend([
                'python assert int(gdb.parse_and_eval("_process_list[0].context.cpu.stack.esp")) == int(gdb.parse_and_eval("$hardware_esp")), "Wrong saved user ESP"',
                'python assert int(gdb.parse_and_eval("_process_list[0].context.eip")) == int(gdb.parse_and_eval("$resume_eip")), "Wrong saved EIP"',
                'python assert int(gdb.parse_and_eval("_process_list[0].context.eflags")) == int(gdb.parse_and_eval("$snapshot.int_stack.eflags")), "Wrong saved EFLAGS"',
                'python assert int(gdb.parse_and_eval("_process_list[0].context.page_directory_virtual_addr")) == int(gdb.parse_and_eval("$cr3")) + 0xc0000000, "Wrong saved page directory"',
                "monitor info pic",
                "delete breakpoints", "hbreak *$resume_eip", "continue",
            ])
            restored = {
                "eax": "cpu.general.eax", "ebx": "cpu.general.ebx",
                "ecx": "cpu.general.ecx", "edx": "cpu.general.edx",
                "esi": "cpu.index.esi", "edi": "cpu.index.edi",
                "ebp": "cpu.stack.ebp", "eip": "int_stack.eip",
            }
            for register, field in restored.items():
                commands.append(
                    f'python assert (int(gdb.parse_and_eval("${register}")) & 0xffffffff)'
                    f' == (int(gdb.parse_and_eval("$snapshot.{field}")) & 0xffffffff), "Incorrect resumed {register}"')
            commands.extend([
                'python assert int(gdb.parse_and_eval("$esp")) == int(gdb.parse_and_eval("$hardware_esp")), "Incorrect resumed ESP"',
                'python assert (int(gdb.parse_and_eval("$eflags")) & 0xed5) == (int(gdb.parse_and_eval("$snapshot.int_stack.eflags")) & 0xed5), "Incorrect resumed flags"',
                f'python assert int(gdb.parse_and_eval("$ds")) == {0 if null_ds else 0x23}, "Incorrect resumed DS"',
                f'printf "PASS: timer sample {sample + 1} saved and resumed the exact user context\\n"',
                "delete breakpoints",
                # A stopped debugger can leave the next timer pending before
                # the loop gets one instruction. Step the loop once explicitly
                # between samples; unattended progress is checked by smoke tests.
                "if $eip == 0x80001", "stepi", "end", "stepi",
            ])
        commands.extend([
            'python assert (int(gdb.parse_and_eval("$eax")) & 0xffffffff) > 0x11111111, "User loop made no progress"',
            # Return to the real shell after all saved/restored-state assertions.
            "set $ds = 0x23", "set $eflags = $eflags & ~0x400",
            "set $eip = 0", "detach",
        ])
        # A command file stops at its first error. Separate -ex arguments can
        # continue after a failed Python assertion and later return exit code 0.
        script = build / "preemption.gdb"
        script.write_text("\n".join(commands) + "\n")
        gdb = ["gdb", "-q", "-nx", "--batch", str(build / "kernel"), "-x", str(script)]
        print(f"CASE: timer preemption, null_DS={null_ds}, user_DF={int(direction_flag)}",
              flush=True)
        try:
            result = subprocess.run(gdb, cwd=root, capture_output=True, text=True,
                                    timeout=15)
        except subprocess.TimeoutExpired as error:
            for output in (error.stdout, error.stderr):
                if output:
                    text = output.decode(errors="replace") if isinstance(output, bytes) else output
                    print(text[-3500:], flush=True)
            raise AssertionError("Timer did not reach scheduler and resume the user context") from None
        if result.returncode:
            print(result.stdout, flush=True)
            print(result.stderr, flush=True)
        else:
            for line in (result.stdout + result.stderr).splitlines():
                if line.startswith(("PASS:", "pic0:")):
                    print(line, flush=True)
        result.check_returncode()
        pic_states = re.findall(r"pic0:.*?\bisr=([0-9a-fA-F]+)",
                                result.stdout + result.stderr)
        assert len(pic_states) == 3, "Could not verify PIC state at each switch"
        assert all(not (int(value, 16) & 1) for value in pic_states), "IRQ0 was not ACKed before scheduling"
        print("PASS: IRQ0 ACK precedes each switch; no kernel stack growth", flush=True)
        deadline = time.monotonic() + 10
        while "user@os:/$" not in qmp.screen():
            if time.monotonic() >= deadline:
                raise AssertionError("Shell did not execute after preemption checks")
            time.sleep(0.1)
        print("PASS: shell executes after the preemption checks", flush=True)
    finally:
        try:
            if qmp is not None and process.poll() is None:
                qmp.command("quit")
        finally:
            if qmp is not None:
                qmp.close()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            errors = process.stderr.read().decode(errors="replace")
            process.stderr.close()
            if errors:
                print("QEMU:", errors.strip(), flush=True)
            if socket_path.exists():
                socket_path.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disk", default="bin/sample-image.bin")
    parser.add_argument("--swap", default="bin/swap.bin")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    disk = (root / args.disk).resolve(strict=True)
    swap = (root / args.swap).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="oskm-preempt-") as temporary:
        build = Path(temporary) / "build"
        checked(["make", "build", "OUTPUT_FOLDER=" + str(build)], root)
        verify_preemption(root, build, disk, swap, null_ds=False)
        verify_preemption(root, build, disk, swap, null_ds=True)
        verify_preemption(root, build, disk, swap, null_ds=False, direction_flag=True)


if __name__ == "__main__":
    main()
