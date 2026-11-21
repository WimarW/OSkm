"""Boot a built OS image in QEMU and check timer/keyboard behavior via QMP.

Run with WSL Python. All guest disk writes use QEMU's temporary snapshots.
Omit --expect-timer to check the baseline before IRQ0 is implemented.
"""

import argparse
import json
from pathlib import Path
import re
import socket
import subprocess
import tempfile
import time


class QMP:
    def __init__(self, path, process):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(3)
        deadline = time.monotonic() + 10
        while True:
            try:
                self.socket.connect(str(path))
                break
            except (FileNotFoundError, ConnectionRefusedError):
                if process.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError("QEMU did not start its QMP socket")
                time.sleep(0.05)
        self.stream = self.socket.makefile("rwb")
        assert "QMP" in self.receive(), "Missing QMP greeting"
        self.command("qmp_capabilities")

    def receive(self):
        line = self.stream.readline()
        if not line:
            raise RuntimeError("QEMU closed its QMP connection")
        return json.loads(line)

    def command(self, name, arguments=None):
        request = {"execute": name}
        if arguments is not None:
            request["arguments"] = arguments
        self.stream.write(json.dumps(request).encode() + b"\n")
        self.stream.flush()
        while True:
            response = self.receive()
            if "error" in response:
                raise RuntimeError(str(response["error"]))
            if "return" in response:
                return response["return"]

    def monitor(self, command):
        return self.command("human-monitor-command", {"command-line": command})

    def screen(self):
        dump = self.monitor("xp /2000hx 0xb8000")
        cells = []
        for line in dump.splitlines():
            if ":" in line:
                cells.extend(int(word, 16) for word in re.findall(
                    r"0x[0-9a-fA-F]+", line.split(":", 1)[1]))
        assert len(cells) == 2000, "Could not read the VGA text screen"
        return "\n".join(
            # framebuffer_clear() uses NUL cells, which appear blank on VGA.
            "".join(chr((cell & 0xFF) or 0x20) for cell in cells[row:row + 80])
            for row in range(0, 2000, 80)
        )

    def timer_count(self):
        stats = self.monitor("info irq")
        counts = re.findall(r"^\s*0:\s+(\d+)", stats, re.MULTILINE)
        assert counts, "QEMU did not provide IRQ0 statistics"
        return int(counts[-1])

    def close(self):
        self.stream.close()
        self.socket.close()


def run(args):
    images = [Path(name).resolve(strict=True) for name in
              (args.iso, args.disk, args.swap)]
    with tempfile.TemporaryDirectory(prefix="oskm-qmp-") as temporary:
        qmp_path = Path(temporary) / "monitor.sock"
        command = [
            "qemu-system-i386", "-accel", "tcg", "-m", "128",
            "-display", "none", "-serial", "none", "-monitor", "none",
            "-no-reboot", "-no-shutdown", "-snapshot", "-boot", "d",
            "-drive", f"file={images[1]},format=raw,if=ide,index=0,media=disk",
            "-drive", f"file={images[2]},format=raw,if=ide,index=1,media=disk",
            "-cdrom", str(images[0]),
            "-qmp", f"unix:{qmp_path},server=on,wait=off",
        ]
        process = subprocess.Popen(command, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.PIPE)
        qmp = None
        try:
            qmp = QMP(qmp_path, process)
            deadline = time.monotonic() + 15
            while "user@os:/$" not in qmp.screen():
                if time.monotonic() >= deadline:
                    raise AssertionError("Shell did not reach its prompt")
                time.sleep(0.1)
            print("PASS: shell reached its prompt", flush=True)
            before = qmp.timer_count()
            time.sleep(args.seconds)
            after = qmp.timer_count()
            assert qmp.command("query-status")["running"], "Guest stopped"

            # Exercise keyboard IRQ1 and a user syscall after the idle interval.
            for key in ("p", "w", "d", "ret"):
                qmp.monitor("sendkey " + key + " 40")
                time.sleep(0.15)
            deadline = time.monotonic() + 5
            while True:
                screen = qmp.screen()
                if (screen.count("user@os:/$") >= 2
                        and any(line.strip() == "/" for line in screen.splitlines())):
                    break
                if time.monotonic() >= deadline:
                    raise AssertionError(
                        "Keyboard/pwd did not produce a new prompt; VGA="
                        + repr(screen[:640]))
                time.sleep(0.1)
            print(f"PASS: keyboard and pwd work after {args.seconds:g} seconds",
                  flush=True)
            print(f"IRQ0 source count: {before} -> {after}", flush=True)
            if args.expect_timer:
                pic = qmp.monitor("info pic")
                master = re.search(r"pic0:.*?\bimr=([0-9a-fA-F]+)", pic)
                assert master, "Could not inspect the master PIC interrupt mask"
                mask = int(master.group(1), 16)
                assert not (mask & 1), f"IRQ0 is masked by the PIC (IMR=0x{mask:02x})"
                # Source counters also increase for masked IRQs. Check the mask,
                # configured rate, and working lower-priority keyboard together.
                rate = (after - before) / args.seconds
                assert 500 <= rate <= 1500, f"PIT rate {rate:.1f} Hz is outside 500-1500 Hz"
                print(f"PASS: IRQ0 unmasked (IMR=0x{mask:02x}), PIT {rate:.1f} Hz and keyboard",
                      flush=True)
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
                    process.terminate()
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                errors = process.stderr.read().decode(errors="replace")
                process.stderr.close()
                if errors:
                    print("QEMU stderr:", errors.strip(), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", required=True)
    parser.add_argument("--disk", required=True)
    parser.add_argument("--swap", required=True)
    parser.add_argument("--seconds", type=float, default=12)
    parser.add_argument("--expect-timer", action="store_true")
    options = parser.parse_args()
    if options.seconds < 1:
        parser.error("--seconds must be at least 1")
    run(options)
