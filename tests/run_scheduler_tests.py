"""Build and run scheduler logic tests using Linux/WSL and GCC's i386 target.

The test executable needs no 32-bit libc. It links the real scheduler, process
table, process lookup, and string functions; unused kernel code is discarded.
"""

from pathlib import Path
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory(prefix="oskm-scheduler-") as temporary:
        executable = Path(temporary) / "scheduler_test"
        command = [
            "gcc", "-m32", "-march=i386", "-mno-sse", "-mno-sse2",
            "-mno-mmx", "-msoft-float", "-mno-avx",
            "-Wall", "-Wextra", "-Werror", "-ffreestanding", "-fno-builtin",
            "-fno-stack-protector", "-fno-pie", "-fno-delete-null-pointer-checks",
            "-ffunction-sections", "-fdata-sections", "-nostdlib", "-static",
            "-no-pie", "-Wl,--gc-sections", "-Wl,-e,_start", "-Isrc",
            "tests/scheduler_test.c", "src/scheduler.c", "src/process.c",
            "src/stdlib/string.c", "-o", str(executable),
        ]
        print("Building real scheduler tests (i386, no libc)...", flush=True)
        subprocess.run(command, cwd=root, check=True)
        subprocess.run([str(executable)], cwd=root, check=True, timeout=10)


if __name__ == "__main__":
    main()
