"""Animated Python presentation for the native sfetch data bridge."""

from __future__ import annotations

import argparse
import code
import math
import os
import sys
import time
from pathlib import Path


RESET = "\033[0m"
DIM = "\033[2m"
CYAN = "\033[36m"
GREEN = "\033[32m"
MAGENTA = "\033[35m"
RAMP = ".,-~:;=!*#$@"
BLOCK_RAMP = " ░▒▓█"


def logo_path(os_id: str) -> Path:
    root = Path(__file__).resolve().parent.parent / "assets" / "ascii"
    candidates = [root / os_id[0:1] / f"{os_id}.txt", root / "_" / "unknown.txt"]
    return next((path for path in candidates if path.is_file()), candidates[-1])


def load_logo(os_id: str) -> list[str]:
    try:
        return logo_path(os_id).read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeError):
        return ["  LARP"]


def density(character: str) -> float:
    if character in "@#▓█":
        return 1.0
    if character in "$&%O0*+=":
        return 0.8
    if character in ":;~-_.,'`":
        return 0.25
    return 0.6 if not character.isspace() else 0.0


def shaded_logo(lines: list[str], angle: float, ramp: str, color: bool) -> list[str]:
    width = max((len(line) for line in lines), default=1)
    center = (width - 1) / 2
    cosine = math.cos(angle)
    sine = math.sin(angle)
    rendered: list[str] = []
    for line in lines:
        canvas = [" "] * max(1, width + 4)
        for x, character in enumerate(line):
            weight = density(character)
            if not weight:
                continue
            rotated_x = int(round((x - center) * cosine + center + 2))
            if not 0 <= rotated_x < len(canvas):
                continue
            light = max(0.0, min(1.0, 0.35 + 0.5 * weight + 0.25 * sine * (x - center) / max(1, width)))
            canvas[rotated_x] = ramp[min(len(ramp) - 1, int(light * (len(ramp) - 1)))]
        rendered.append("".join(canvas).rstrip())
    return rendered


def format_uptime(seconds: int) -> str:
    days, remainder = divmod(seconds, 86400)
    hours, minutes = divmod(remainder, 3600)[0], remainder % 3600 // 60
    return f"{days}d {hours:02d}h {minutes:02d}m"


def info_lines(info: dict[str, object]) -> list[str]:
    total = int(info["total_ram"])
    free = int(info["free_ram"])
    used = max(0, total - free)
    percent = round(used * 100 / total) if total else 0
    cpu = str(info["cpu_model"])
    if len(cpu) > 34:
        cpu = cpu[:31] + "..."
    mhz = info.get("cpu_mhz")
    cpu_speed = f" @ {float(mhz) / 1000:.2f} GHz" if mhz else ""
    temperature = info.get("cpu_temperature")
    cpu_temp = f" | {float(temperature):.1f} C" if temperature else ""
    return [
        f"{GREEN}{info['user']}{RESET}@{CYAN}{info['hostname']}{RESET}",
        f"{DIM}{'-' * (len(str(info['user'])) + len(str(info['hostname'])) + 1)}{RESET}",
        f"{CYAN}OS{RESET}        {info['os']}",
        f"{CYAN}Kernel{RESET}    {info['kernel']}",
        f"{CYAN}Uptime{RESET}    {format_uptime(int(info['uptime']))}",
        f"{CYAN}CPU{RESET}       {cpu} ({info['cpu_cores']}){cpu_speed}{cpu_temp}",
        f"{CYAN}Memory{RESET}    {used // 1024 // 1024} MB / {total // 1024 // 1024} MB ({percent}%)",
        f"{CYAN}Processes{RESET} {info['process_count']}",
        f"{CYAN}Arch{RESET}      {info['arch']}",
        f"{CYAN}Shell{RESET}     {info['shell']}",
        f"{MAGENTA}C → Python bridge{RESET}",
    ]


def render(info: dict[str, object], lines: list[str], angle: float, ramp: str, color: bool, boxed: bool) -> str:
    logo = shaded_logo(lines, angle, ramp, color)
    details = info_lines(info)
    height = max(len(logo), len(details))
    output: list[str] = []
    for row in range(height):
        left = logo[row] if row < len(logo) else ""
        right = details[row] if row < len(details) else ""
        output.append(f"  {left:<34}  {right}")
    if boxed:
        width = max(map(len, output), default=0)
        border = "  +" + "-" * width + "+"
        output = [border, *[f"  |{row}|" for row in output], border]
    return "\n".join(output)


def main() -> int:
    parser = argparse.ArgumentParser(description="Animated CPython presentation for sfetch")
    parser.add_argument("--version", action="version", version="LARP 1.0")
    parser.add_argument("--eval", metavar="EXPR", help="evaluate a Python expression")
    parser.add_argument("--interactive", action="store_true", help="open a Python console after rendering")
    parser.add_argument("--frames", type=int, default=48, help="frames to render (default: 48)")
    parser.add_argument("--infinite", action="store_true", help="spin until Ctrl-C")
    parser.add_argument("--speed", type=float, default=1.0, help="rotation speed")
    parser.add_argument("--shading-chars", default=RAMP, help="brightness ramp")
    parser.add_argument("--blocks", action="store_true", help="use block shading")
    parser.add_argument("--no-color", action="store_true")
    parser.add_argument("--box", action="store_true")
    args = parser.parse_args()

    info = dict(sfetch_info)
    if args.eval is not None:
        print(repr(eval(args.eval, {"sfetch_info": info, "math": math})))
        return 0
    color = sys.stdout.isatty() and not args.no_color
    ramp = BLOCK_RAMP if args.blocks else args.shading_chars
    if not ramp:
        parser.error("--shading-chars cannot be empty")
    if not color:
        global RESET, DIM, CYAN, GREEN, MAGENTA
        RESET = DIM = CYAN = GREEN = MAGENTA = ""

    frames = max(1, args.frames)
    logo = load_logo(str(info.get("os_id", "unknown")))
    try:
        frame = 0
        while args.infinite or frame < frames:
            if color:
                sys.stdout.write("\033[H\033[2J")
            sys.stdout.write(render(info, logo, frame * 0.18 * args.speed, ramp, color, args.box))
            sys.stdout.write("\n")
            sys.stdout.flush()
            frame += 1
            if not sys.stdout.isatty():
                break
            time.sleep(0.08)
    except KeyboardInterrupt:
        pass
    finally:
        if color:
            sys.stdout.write(RESET + "\n")
    if args.interactive:
        code.interact(banner="LARP console (Ctrl-D to exit)", local={"sfetch_info": info})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
