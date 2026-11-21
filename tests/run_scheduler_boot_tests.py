"""Verify scheduler startup, PCB state transitions and empty-table idle in QEMU/GDB.

Uses an ordinary kernel build, no production test hooks or injected probe code.
All build artifacts are temporary and all guest disk writes use snapshots.
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


def checked(command, root, show_output=True):
    result = subprocess.run(command, cwd=root, capture_output=True, text=True,
                            timeout=40)
    if result.stdout and show_output:
        print(result.stdout.strip(), flush=True)
    if result.stderr:
        print(result.stderr.strip(), flush=True)
    result.check_returncode()
    return result


def verify_boot(root, build, disk, swap, mode):
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
        empty = mode == "empty"
        target = 1 if mode == "different target" else 0
        commands = [
            "set pagination off", "set confirm off",
            f"target remote 127.0.0.1:{port}",
            "hbreak *scheduler_switch_to_next_process", "continue",
            'python assert int(gdb.parse_and_eval("_process_list[0].metadata.state")) == 1, "Shell is not READY before scheduling"',
            'python assert not (int(gdb.parse_and_eval("$eflags")) & 0x200), "Startup switching is interruptible"',
            "set $kernel_cr3 = $cr3",
            "set $expected_cr3 = (unsigned int)_process_list[0].context.page_directory_virtual_addr - 0xc0000000",
            'python assert int(gdb.parse_and_eval("$kernel_cr3")) != int(gdb.parse_and_eval("$expected_cr3")), "User page directory was activated before scheduler"',
            "delete breakpoints",
        ]
        if mode == "same target":
            commands.append("set variable _process_list[0].metadata.state = PROCESS_STATE_RUNNING")
        elif mode == "different target":
            # Synthetic PCB-only fixture, not a second application: share the
            # already valid shell address space to isolate state transitions.
            commands.extend([
                "set variable _process_list[1] = _process_list[0]",
                "set variable _process_list[0].metadata.state = PROCESS_STATE_RUNNING",
                "set variable _process_list[1].metadata.pid = 2",
                "set variable _process_list[1].metadata.state = PROCESS_STATE_READY",
                "set variable process_manager_state.active_process_count = 2",
                # Represent the cursor immediately after slot 0's previous turn.
                "set variable next_search_index = 1",
            ])
        if empty:
            commands.extend([
                "set variable _process_list[0].metadata.state = PROCESS_STATE_INACTIVE",
                "set variable process_manager_state.active_process_count = 0",
                "hbreak *interrupt_handler_32", "continue",
                'python assert int(gdb.parse_and_eval("*(unsigned int*)($esp+4)")) == 8, "Idle timer did not interrupt kernel mode"',
                'python assert int(gdb.parse_and_eval("$cr3")) == int(gdb.parse_and_eval("$kernel_cr3")), "Empty selection changed address space"',
                'python assert int(gdb.parse_and_eval("_process_list[0].metadata.state")) == 0, "Empty selection marked a PCB RUNNING"',
                'printf "PASS: empty process table idles in kernel mode and accepts timer IRQ0\\n"',
            ])
        else:
            commands.extend([
                "hbreak *process_context_switch", "continue",
                f'python assert int(gdb.parse_and_eval("_process_list[{target}].metadata.state")) == 2, "Selected shell is not RUNNING"',
                'python assert int(gdb.parse_and_eval("$cr3")) == int(gdb.parse_and_eval("$expected_cr3")), "Scheduler did not activate the shell page directory"',
                'python assert not (int(gdb.parse_and_eval("$eflags")) & 0x200), "Interrupts enabled before context restoration"',
            ])
            if mode == "different target":
                commands.extend([
                    'python assert int(gdb.parse_and_eval("_process_list[0].metadata.state")) == 1, "Old RUNNING process did not become READY"',
                    'printf "PASS: old RUNNING -> READY while selected process becomes RUNNING\\n"',
                    # Retire the state-only fixture before iret can enable the
                    # timer: both test slots share the shell's stack and memory.
                    "set variable _process_list[0].metadata.state = PROCESS_STATE_INACTIVE",
                    "set variable process_manager_state.active_process_count = 1",
                ])
            commands.extend([
                "delete breakpoints", "hbreak *0x0", "continue",
                'python assert int(gdb.parse_and_eval("$cs")) == 0x1b, "User code segment not restored"',
                'python assert int(gdb.parse_and_eval("$ss")) == 0x23, "User stack segment not restored"',
                'python assert int(gdb.parse_and_eval("$esp")) == 0xbffffffc, "Initial user stack not restored"',
                'python assert int(gdb.parse_and_eval("$eflags")) & 0x200, "User interrupts not enabled by saved context"',
                f'python assert int(gdb.parse_and_eval("_process_list[{target}].metadata.state")) == 2, "Shell state changed during restoration"',
                'printf "PASS: selected process RUNNING, CR3, user entry and interrupt flag\\n"',
            ])
        commands.extend(["delete breakpoints", "detach"])
        # Stop the command file immediately if any assertion fails.
        script = build / "scheduler-boot.gdb"
        script.write_text("\n".join(commands) + "\n")
        gdb = ["gdb", "-q", "-nx", "--batch", str(build / "kernel"), "-x", str(script)]
        print("CASE:", mode, flush=True)
        result = checked(gdb, root)
        assert "PASS:" in result.stdout, "GDB did not complete boot assertions"
        if not empty:
            deadline = time.monotonic() + 10
            while "user@os:/$" not in qmp.screen():
                if time.monotonic() >= deadline:
                    raise AssertionError("Shell did not reach a prompt after scheduling")
                time.sleep(0.1)
            print("PASS: real shell prompt reached through scheduler startup", flush=True)
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
    with tempfile.TemporaryDirectory(prefix="oskm-boot-") as temporary:
        build = Path(temporary) / "build"
        checked(["make", "build", "OUTPUT_FOLDER=" + str(build)], root)
        symbols = checked(["nm", "--defined-only", str(build / "kernel")], root,
                          show_output=False)
        names = {line.split()[-1] for line in symbols.stdout.splitlines() if line.split()}
        assert "scheduler_switch_to_next_process" in names, "Scheduler switch implementation is missing"
        for mode in ("normal first process", "same target", "different target", "empty"):
            verify_boot(root, build, disk, swap, mode)


if __name__ == "__main__":
    main()
