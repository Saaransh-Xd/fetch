"""Animated Python presentation for the native sfetch data bridge."""

from __future__ import annotations

import argparse
import code
import math
import os
import re
import sys
import time
from pathlib import Path


RESET = "\033[0m"
DIM = "\033[2m"
RED = "\033[31m"
BLUE = "\033[34m"
CYAN = "\033[36m"
GREEN = "\033[32m"
YELLOW = "\033[33m"
MAGENTA = "\033[35m"
WHITE = "\033[37m"
BRIGHT_CYAN = "\033[96m"
BRIGHT_WHITE = "\033[97m"
RAMP = ".,-~:;=!*#$@"
LOGO_COLORS = (GREEN, CYAN, YELLOW, BLUE, MAGENTA, RED, WHITE, BRIGHT_CYAN, BRIGHT_WHITE)
BLOCK_RAMP = " ░▒▓█"


def logo_path(os_id: str, requested: str | None = None) -> Path:
    root = Path(__file__).resolve().parent.parent / "assets" / "ascii"
    if requested:
        custom_path = Path(requested).expanduser()
        candidates = [
            custom_path,
            root / requested[0:1] / f"{requested}.txt",
        ]
    else:
        system_logo = Path("/etc/sfetch/logo")
        candidates = [system_logo] if system_logo.is_file() and system_logo.stat().st_size else []
        candidates.append(root / os_id[0:1] / f"{os_id}.txt")
    candidates.append(root / "_" / "unknown.txt")
    return next((path for path in candidates if path.is_file()), candidates[-1])


def load_logo(os_id: str, requested: str | None = None) -> list[str]:
    try:
        return logo_path(os_id, requested).read_text(encoding="utf-8").splitlines()
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
    parsed_lines: list[list[tuple[str, str | None]]] = []
    for line in lines:
        parsed: list[tuple[str, str | None]] = []
        active_color: str | None = None
        index = 0
        while index < len(line):
            if line[index] == "$" and index + 1 < len(line) and line[index + 1] in "123456789":
                active_color = LOGO_COLORS[int(line[index + 1]) - 1]
                index += 2
                continue
            parsed.append((line[index], active_color))
            index += 1
        parsed_lines.append(parsed)

    width = max((len(line) for line in parsed_lines), default=1)
    center = (width - 1) / 2
    cosine = math.cos(angle)
    sine = math.sin(angle)
    rendered: list[str] = []
    for line in parsed_lines:
        canvas = [" "] * max(1, width + 4)
        for x, (character, active_color) in enumerate(line):
            weight = density(character)
            if not weight:
                continue
            rotated_x = int(round((x - center) * cosine + center + 2))
            if not 0 <= rotated_x < len(canvas):
                continue
            light = max(0.0, min(1.0, 0.35 + 0.5 * weight + 0.25 * sine * (x - center) / max(1, width)))
            glyph = ramp[min(len(ramp) - 1, int(light * (len(ramp) - 1)))]
            if color and active_color:
                glyph = f"{active_color}{glyph}{RESET}"
            canvas[rotated_x] = glyph
        rendered.append("".join(canvas).rstrip())
    return rendered


def format_uptime(seconds: int) -> str:
    days, remainder = divmod(seconds, 86400)
    hours, minutes = divmod(remainder, 3600)[0], remainder % 3600 // 60
    return f"{days}d {hours:02d}h {minutes:02d}m"


def info_lines(info: dict[str, object]) -> list[str]:
    memory = info["memory"]
    cpu_data = info["cpu"]
    uptime = info["uptime"]
    total = int(memory["total_mb"]) * 1024 * 1024
    used = int(memory["used_mb"]) * 1024 * 1024
    cpu = str(cpu_data["model"])
    mhz = info.get("cpu_mhz")
    cpu_speed = f" @ {float(mhz) / 1000:.2f} GHz" if mhz else ""
    temperature = info.get("cpu_temperature")
    cpu_temp = f" | {float(temperature):.1f} C" if temperature is not None else " | N/A"
    lines = [
        f"{GREEN}{info['user']}{RESET}@{CYAN}{info['hostname']}{RESET}",
        f"{DIM}{'-' * (len(str(info['user'])) + len(str(info['hostname'])) + 1)}{RESET}",
        f"{CYAN}OS{RESET}        {info['os']}",
        f"{CYAN}Kernel{RESET}    {info['kernel']}",
        f"{CYAN}Uptime{RESET}    {format_uptime(int(uptime['seconds']))}",
        f"{CYAN}CPU{RESET}       {cpu} ({cpu_data['cores']}){cpu_speed}{cpu_temp}",
        *[f"{CYAN}GPU{RESET}       {gpu}" for gpu in info.get("gpu", [])],
        f"{CYAN}Memory{RESET}    {used // 1024 // 1024} MB / {total // 1024 // 1024} MB ({memory['percentage']}%)",
        f"{CYAN}Processes{RESET} {info['process_count']}",
        f"{CYAN}Shell{RESET}     {info['shell']}",
        f"{CYAN}Arch{RESET}      {info['arch']}",
        f"{CYAN}Swap{RESET}      {info['swap']['used_mb']} MB / {info['swap']['total_mb']} MB",
        f"{CYAN}Packages{RESET}  {info['packages']['count'] or 'Unknown'} ({info['packages']['manager'] or 'N/A'})",
        f"{CYAN}Terminal{RESET}  {info['terminal'] or 'Unknown'}",
        f"{CYAN}Local IP{RESET}  {info['local_ip'] or 'Unknown'}",
        f"{CYAN}Display{RESET}   {info['display']['width']}x{info['display']['height']} @ {info['display']['refresh_hz']:.0f} Hz",
        f"{CYAN}Chassis{RESET}   {info['chassis'] or 'Unknown'}",
    ]
    for disk in info["disks"]:
        lines.append(f"{CYAN}Disk{RESET}      {disk['mountpoint']}: {disk['used_gib']:.2f} GiB / {disk['total_gib']:.2f} GiB ({disk['percent']}%) - {disk['filesystem']}")
    batteries = info["battery"]
    if batteries:
        for battery in batteries:
            lines.append(f"{CYAN}Battery{RESET}   {battery['name']}: {battery['capacity']}% ({battery['status']})")
    else:
        lines.append(f"{CYAN}Battery{RESET}   None detected")
    return lines


def render(info: dict[str, object], lines: list[str], angle: float, ramp: str, color: bool, boxed: bool) -> str:
    logo = shaded_logo(lines, angle, ramp, color)
    details = info_lines(info)
    phase = (1.0 - math.cos(angle)) / 2.0
    x_offset = int(phase * 16)
    source_width = max((len(re.sub(r"\$[1-9]", "", row)) for row in lines), default=1)
    # Include the logo's four-cell render margin and its maximum 16-cell shift
    # so the data column remains fixed for every frame.
    stage_width = max(58, source_width + 20)
    gap = " " * 14
    # Keep the animation vertically anchored; only the horizontal perspective
    # shift should change as the logo spins.
    staged_logo = [(" " * x_offset) + row for row in logo]
    height = max(len(staged_logo), len(details))
    output: list[str] = []
    for row in range(height):
        left = staged_logo[row] if row < len(staged_logo) else ""
        right = details[row] if row < len(details) else ""
        visible_left = re.sub(r"\033\[[0-9;]*m", "", left)
        output.append(f"  {left}{' ' * max(0, stage_width - len(visible_left))}{gap}{right}")
    if boxed:
        width = max(map(len, output), default=0)
        border = "  +" + "-" * width + "+"
        output = [border, *[f"  |{row}|" for row in output], border]
    return "\n".join(output)


def positive_float(value: str) -> float:
    parsed = float(value)
    if not math.isfinite(parsed) or parsed <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return parsed


def main() -> int:
    parser = argparse.ArgumentParser(description="Animated CPython presentation for sfetch")
    parser.add_argument("--version", action="version", version="LARP 1.0")
    parser.add_argument("--logo", metavar="NAME_OR_PATH", help="use a bundled logo name or custom logo file")
    parser.add_argument("--eval", metavar="EXPR", help="evaluate a Python expression")
    parser.add_argument("--interactive", action="store_true", help="open a Python console after rendering")
    parser.add_argument("--frames", type=int, default=None, help="render a finite number of frames with --no-infinite")
    infinite_group = parser.add_mutually_exclusive_group()
    infinite_group.add_argument("--infinite", dest="infinite", action="store_true", help="spin until Ctrl-C (default)")
    infinite_group.add_argument("--no-infinite", dest="infinite", action="store_false", help="stop after the configured number of frames")
    parser.set_defaults(infinite=True)
    parser.add_argument("--speed", type=float, default=1.0, help="rotation speed")
    parser.add_argument("--fps", type=positive_float, default=12.5, help="maximum animation frames per second")
    parser.add_argument("--shading-chars", default=RAMP, help="brightness ramp")
    parser.add_argument("--blocks", action="store_true", help="use block shading")
    parser.add_argument("--no-color", action="store_true")
    parser.add_argument("--box", action="store_true")
    args = parser.parse_args()

    if args.infinite and args.frames is not None:
        parser.error("--frames requires --no-infinite")

    info = dict(sfetch_info)
    if args.eval is not None:
        print(repr(eval(args.eval, {"sfetch_info": info, "math": math})))
        return 0
    color = sys.stdout.isatty() and not args.no_color
    ramp = BLOCK_RAMP if args.blocks else args.shading_chars
    if not ramp:
        parser.error("--shading-chars cannot be empty")
    if not color:
        global RESET, DIM, RED, BLUE, CYAN, GREEN, YELLOW, MAGENTA, WHITE, BRIGHT_CYAN, BRIGHT_WHITE
        RESET = DIM = RED = BLUE = CYAN = GREEN = YELLOW = MAGENTA = WHITE = BRIGHT_CYAN = BRIGHT_WHITE = ""

    frames = max(1, args.frames) if args.frames is not None else 48
    cycle_frames = frames or 48
    logo = load_logo(str(info.get("os_id", "unknown")), args.logo)
    try:
        frame = 0
        while args.infinite or frame < frames:
            frame_start = time.monotonic()
            if color:
                sys.stdout.write("\033[H\033[2J")
            angle = (frame / cycle_frames) * math.tau * args.speed
            sys.stdout.write(render(info, logo, angle, ramp, color, args.box))
            sys.stdout.write("\n")
            sys.stdout.flush()
            frame += 1
            if not sys.stdout.isatty():
                break
            time.sleep(max(0.0, (1.0 / args.fps) - (time.monotonic() - frame_start)))
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
