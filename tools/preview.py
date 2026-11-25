import struct
import sys
import time

COLS, ROWS = 80, 25

def main():
    path = sys.argv[1]
    mode = sys.argv[2] if len(sys.argv) > 2 else "play"

    with open(path, "rb") as f:
        count, fps = struct.unpack("<II", f.read(8))
        frames = [f.read(250) for _ in range(count)]

    def render(fr):
        lines = []
        for y in range(ROWS):
            row = ""
            for x in range(COLS):
                bit = y * COLS + x
                row += "#" if fr[bit // 8] & (1 << (bit % 8)) else " "
            lines.append(row)
        return "\n".join(lines)

    if mode == "play":
        for fr in frames:
            print("\033[H\033[2J" + render(fr))
            time.sleep(1 / fps)
    else:
        print(render(frames[int(mode)]))

main()