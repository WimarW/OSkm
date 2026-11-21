"""Verify the real C-to-assembly context switch in an isolated QEMU test kernel.

Run from WSL with GCC, NASM, Make, QEMU, and GDB installed. Test kernels and
probe objects live in a temporary directory; guest writes use disk snapshots.
"""

import argparse
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True
from irq0_smoke import QMP


def checked(command, root):
    result = subprocess.run(command, cwd=root, capture_output=True, text=True,
                            timeout=40)
    if result.stdout:
        print(result.stdout.strip(), flush=True)
    if result.stderr:
        print(result.stderr.strip(), flush=True)
    result.check_returncode()
    return result


def verify_case(root, build, disk, swap, flags, null_segments, entry):
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
        registers = {
            "eax": 0x11111111, "ebx": 0x22222222,
            "ecx": 0x33333333, "edx": 0x44444444,
            "ebp": 0x55555555, "esi": 0x66666666, "edi": 0x77777777,
            "esp": 0xBFFFFEFC, "eip": entry, "cs": 0x1B, "ss": 0x23,
        }
        registers.update({name: 0 if null_segments else 0x23
                          for name in ("ds", "es", "fs", "gs")})
        commands = [
            "set pagination off", "set confirm off",
            f"target remote 127.0.0.1:{port}",
            "hbreak *process_context_switch", "continue",
            "set $expected_cr3 = $cr3",
            f"set variable context_switch_test_flags = {flags}",
            f"set variable context_switch_test_null_segments = {int(null_segments)}",
            f"set variable context_switch_test_eip = {entry}",
            # A two-byte infinite loop in an allocated user page provides a
            # controlled nonzero resume address without another application.
            "set {unsigned short}0x80000 = 0xfeeb",
            "delete breakpoints", f"hbreak *{entry}",
            "set $eip = context_switch_test_enter", "continue",
        ]
        for name, expected in registers.items():
            commands.append(
                f'python assert (int(gdb.parse_and_eval("${name}")) & 0xffffffff)'
                f' == {expected}, "Incorrect restored {name}"')
        commands.extend([
            # Arithmetic flags, DF and IF; ignore debugger-induced RF.
            f'python assert (int(gdb.parse_and_eval("$eflags")) & 0xed5)'
            f' == ({flags} & 0xed5), "Incorrect restored EFLAGS"',
            'python assert int(gdb.parse_and_eval("$cr3"))'
            ' == int(gdb.parse_and_eval("$expected_cr3")), "Unexpected CR3 change"',
            'printf "PASS: C ABI, registers, user stack, segments, EFLAGS and iret\\n"',
        ])
        if null_segments:
            # Null data selectors are valid saved state, but the shell needs its
            # normal flat selectors and interrupts enabled to execute C code.
            commands.extend([f"set ${name} = 0x23" for name in ("ds", "es", "fs", "gs")])
            commands.append("set $eflags = $eflags | 0x200")
        if entry != 0:
            # The resume-address assertion is complete. Start the real shell
            # afterward to verify that the recovered stack/registers are usable.
            commands.append("set $eip = 0")
        commands.extend(["delete breakpoints", "detach"])
        # Stop the command file immediately if any assertion fails.
        script = build / "context-switch.gdb"
        script.write_text("\n".join(commands) + "\n")
        gdb = ["gdb", "-q", "-nx", "--batch", str(build / "kernel"), "-x", str(script)]
        print(f"CASE: EIP=0x{entry:x}, flags=0x{flags:x}, null_segments={null_segments}",
              flush=True)
        result = checked(gdb, root)
        assert "PASS: C ABI" in result.stdout, "GDB did not verify restored state"
        deadline = time.monotonic() + 10
        while "user@os:/$" not in qmp.screen():
            if time.monotonic() >= deadline:
                raise AssertionError("Shell did not run after context restoration")
            time.sleep(0.1)
        print("PASS: shell executes after the restored user entry", flush=True)
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
    with tempfile.TemporaryDirectory(prefix="oskm-context-") as temporary:
        build = Path(temporary) / "build"
        build.mkdir()
        checked([
            "gcc", "-m32", "-march=i386", "-mno-sse", "-mno-sse2", "-mno-mmx",
            "-msoft-float", "-mno-avx", "-fshort-wchar", "-g", "-Wall", "-Wextra",
            "-Werror", "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
            "-fno-pie", "-fno-delete-null-pointer-checks", "-Isrc", "-c",
            "tests/context_switch_probe.c", "-o", str(build / "context_switch_probe.o"),
        ], root)
        checked(["make", "build", "OUTPUT_FOLDER=" + str(build)], root)
        verify_case(root, build, disk, swap, flags=0x247, null_segments=False, entry=0)
        verify_case(root, build, disk, swap, flags=0x43, null_segments=True, entry=0x80000)


if __name__ == "__main__":
    main()
