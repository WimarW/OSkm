import struct
import subprocess
import sys
import tempfile
import os
from PIL import Image

COLS, ROWS = 80, 50

def main():
    if len(sys.argv) < 4:
        print("Usage: video2frames.py <video> <fps> <output.bin> [max_seconds]")
        sys.exit(1)

    video, fps, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    max_seconds = float(sys.argv[4]) if len(sys.argv) > 4 else None

    with tempfile.TemporaryDirectory() as tmp:
        cmd = ["ffmpeg", "-loglevel", "error", "-i", video,
               "-vf", f"fps={fps},scale={COLS}:{ROWS}:flags=area",
               "-pix_fmt", "gray"]
        if max_seconds:
            cmd += ["-t", str(max_seconds)]
        cmd += [os.path.join(tmp, "f%05d.png")]
        subprocess.run(cmd, check=True)

        names = sorted(n for n in os.listdir(tmp) if n.endswith(".png"))
        frames = []
        for n in names:
            img = Image.open(os.path.join(tmp, n)).convert("L")
            px = img.load()
            data = bytearray(500)
            for y in range(ROWS):
                for x in range(COLS):
                    if px[x, y] > 127:
                        bit = y * COLS + x
                        data[bit // 8] |= 1 << (bit % 8)
            frames.append(bytes(data))

    with open(out, "wb") as f:
        f.write(struct.pack("<II", len(frames), fps))
        for fr in frames:
            f.write(fr)

    print(f"{len(frames)} frames, {fps} fps, {8 + 500 * len(frames)} bytes")

main()