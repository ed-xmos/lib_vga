#!/usr/bin/env python3
"""Check VGA sync timing and RGB output in an xsim VCD trace against the settings in the test app.

Expected (standard VGA) line layout, starting at the first active pixel:
    [active][front porch][sync][back porch]
Frame line 0 is the line in which vsync falls (at line start or at hsync), followed by
    [sync][back porch][active][front porch] lines.
Alignment is extrapolated backwards from the first vsync so that the frame displayed
before it is checked too (reported as frame -1).
RGB must be zero everywhere outside the active area and follow the client's test pattern inside it.
If the client defines several frame buffer patterns, each frame must be displayed entirely
from one of them (no tearing) and every buffer must appear in at least one frame.
Signals are sampled a fixed delay after each falling edge of the port clock block.
"""
import argparse
import bisect
import re
import sys
from dataclasses import dataclass
from pathlib import Path

VGA_PIXEL_CLOCK_MHZ = 25.0  # Exactly 25.000, not the nominal 25.175
CLOCK_SCALE = 1.0  # Running at full speed for now
CLOCK_TOLERANCE = 0.001  # Only needs to cover VCD timescale rounding (1666 ps)

TIME_UNITS_PS = {"fs": 1e-3, "ps": 1.0, "ns": 1e3, "us": 1e6, "ms": 1e9, "s": 1e12}


@dataclass
class Config:
    resolution: str
    bpp: int
    pixels_per_word: int
    port_cycles_per_word: int
    h_output_divider: int
    v_repeat: int
    h_sync: int
    h_back: int
    h_active: int
    h_front: int
    v_sync: int
    v_back: int
    v_active: int
    v_front: int
    words_per_line: int
    pattern_bases: list
    p_hsync: str
    p_vsync: str
    p_rgb: str
    clk_blk: str

    @property
    def h_total(self):
        return self.h_sync + self.h_back + self.h_active + self.h_front

    @property
    def v_total(self):
        return self.v_sync + self.v_back + self.v_active + self.v_front

    def pixel(self, row, col, base):
        word = (base + row * self.words_per_line + col // self.pixels_per_word) & 0xFFFFFFFF
        return (word >> ((col % self.pixels_per_word) * self.bpp)) & ((1 << self.bpp) - 1)


_HERE = Path(__file__).resolve().parent
SRC_FILES = [str(_HERE / "test_vga" / "src" / "test_vga.c"),
             str(_HERE / ".." / "lib_vga" / "api" / "vga.h")]


def parse_c(paths=SRC_FILES, overrides=None):
    if isinstance(paths, str):
        paths = [paths]
    text = "\n".join(open(p).read() for p in paths)
    path = ", ".join(paths)
    defines = {m.group(1): int(m.group(2))
               for m in re.finditer(r"^[ \t]*#define[ \t]+(VGA_[HV]_\w+)[ \t]+(\d+)\b", text, re.M)}
    macros = {m.group(1): m.group(2)
              for m in re.finditer(r"^[ \t]*#define[ \t]+(\w+)[ \t]+(\w+)[ \t]*$", text, re.M)}
    macros.update(overrides or {})
    # Numeric -D overrides (e.g. the shortened test frame timings) replace the header defaults
    for name, value in (overrides or {}).items():
        if re.fullmatch(r"\d+", value):
            defines[name] = int(value)

    def field(name):
        m = re.search(rf"^[ \t]*\.{name}\s*=\s*(\w+)", text, re.M)
        if not m:
            sys.exit(f"Could not find '.{name} = ...' in {path}")
        value = m.group(1)
        seen = set()
        while value in macros and value not in seen:
            seen.add(value)
            value = macros[value]
        return value

    def define(name):
        if name not in defines:
            sys.exit(f"Could not find '#define {name}' in {path}")
        return defines[name]

    pattern_bases = sorted({int(m.group(1), 0) for m in re.finditer(
        r"\w+\[i\]\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*\+\s*i\s*;", text)})
    if not pattern_bases:
        sys.exit(f"Could not find test pattern '<buffer>[i] = <base> + i;' in {path}")

    resolution = field("resolution")
    rgb_m = re.fullmatch(r"XS1_PORT_(4|8|16)[A-Z]", field("p_rgb"))
    if not rgb_m:
        sys.exit(f"RGB port {field('p_rgb')} must be 4, 8 or 16 bits wide")
    bpp = int(rgb_m.group(1))
    pixels_per_word = 32 // bpp
    port_cycles_per_word = 32 // bpp  # app.c asserts port width == bpp
    output_scale = pixels_per_word // 2
    h_div = (2 if resolution == "VGA_640_480" else 4) * output_scale
    v_repeat = 1 if resolution == "VGA_640_480" else 2

    def h_ticks(name):
        value = define(name) * port_cycles_per_word
        if value % h_div:
            print(f"WARNING: {name}={define(name)} is not a multiple of the horizontal output divider")
        return value // h_div

    v_active = define("VGA_V_ACTIVE")
    if v_active % v_repeat:
        print(f"WARNING: VGA_V_ACTIVE={v_active} is not a multiple of the vertical repeat {v_repeat}")
    clk = field("clk_bit")
    clk_m = re.fullmatch(r"XS1_CLKBLK_(\d+)", clk)
    if not clk_m:
        sys.exit(f"Unexpected clock block name {clk}")

    return Config(
        resolution=resolution,
        bpp=bpp,
        pixels_per_word=pixels_per_word,
        port_cycles_per_word=port_cycles_per_word,
        h_output_divider=h_div,
        v_repeat=v_repeat,
        h_sync=h_ticks("VGA_H_SYNC"),
        h_back=h_ticks("VGA_H_BACK"),
        h_active=h_ticks("VGA_H_ACTIVE"),
        h_front=h_ticks("VGA_H_FRONT"),
        v_sync=define("VGA_V_SYNC"),
        v_back=define("VGA_V_BACK"),
        v_active=(v_active // v_repeat) * v_repeat,
        v_front=define("VGA_V_FRONT"),
        words_per_line=-(-define("VGA_H_ACTIVE") // pixels_per_word),
        pattern_bases=pattern_bases,
        p_hsync=field("p_hsync"),
        p_vsync=field("p_vsync"),
        p_rgb=field("p_rgb"),
        clk_blk=f"ClkBlk{clk_m.group(1)}",
    )


def parse_vcd(path, wanted):
    """Return (timescale_ps, {name: ([times], [values])}) for the wanted signal names."""
    id_to_name = {}
    signals = {name: ([], []) for name in wanted}
    timescale_ps = None
    t = 0
    in_header = True
    timescale_tokens = None

    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if in_header:
                if timescale_tokens is not None:
                    if line.startswith("$end"):
                        m = re.fullmatch(r"(\d+)\s*(\w+)", " ".join(timescale_tokens))
                        timescale_ps = int(m.group(1)) * TIME_UNITS_PS[m.group(2)]
                        timescale_tokens = None
                    else:
                        timescale_tokens.append(line)
                elif line.startswith("$timescale"):
                    rest = line[len("$timescale"):].replace("$end", "").strip()
                    timescale_tokens = [rest] if rest else []
                    if "$end" in line:
                        m = re.fullmatch(r"(\d+)\s*(\w+)", rest)
                        timescale_ps = int(m.group(1)) * TIME_UNITS_PS[m.group(2)]
                        timescale_tokens = None
                elif line.startswith("$var"):
                    parts = line.split()
                    if parts[4] in signals:
                        id_to_name[parts[3]] = parts[4]
                elif line.startswith(("$enddefinitions", "$dumpvars")):
                    in_header = False
                continue

            c = line[0]
            if c == "#":
                t = int(line[1:])
                continue
            if c in "bB":
                bits, ident = line[1:].split()
            elif c in "01xXzZ":
                bits, ident = c, line[1:]
            else:
                continue
            name = id_to_name.get(ident)
            if name is None:
                continue
            value = int(bits, 2) if set(bits) <= {"0", "1"} else None
            times, values = signals[name]
            if values and values[-1] == value:
                continue
            times.append(t)
            values.append(value)

    if timescale_ps is None:
        sys.exit("No $timescale found in VCD")
    missing = [n for n in wanted if n not in id_to_name.values()]
    if missing:
        sys.exit(f"Signals not found in VCD: {', '.join(missing)}")
    return timescale_ps, signals


class Report:
    def __init__(self, max_errors):
        self.max_errors = max_errors
        self.counts = {}

    def error(self, category, msg):
        n = self.counts.get(category, 0)
        if n < self.max_errors:
            print(f"  FAIL [{category}] {msg}")
        elif n == self.max_errors:
            print(f"  FAIL [{category}] ... further errors suppressed")
        self.counts[category] = n + 1

    @property
    def failed(self):
        return bool(self.counts)


def h_region(cfg, tick):
    edges = (("active", cfg.h_active), ("front", cfg.h_front), ("sync", cfg.h_sync), ("back", cfg.h_back))
    for name, length in edges:
        if tick < length:
            return name
        tick -= length
    return "?"


def v_region(cfg, line):
    edges = (("sync", cfg.v_sync), ("back", cfg.v_back), ("active", cfg.v_active), ("front", cfg.v_front))
    for name, length in edges:
        if line < length:
            return name
        line -= length
    return "?"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--vcd", default="trace.vcd")
    ap.add_argument("--src", nargs="+", default=SRC_FILES, help="C sources holding the config and timing")
    ap.add_argument("--tile", default="tile[0]")
    ap.add_argument("--sample-delay-ns", type=float, default=10.0,
                    help="Sample point after the clock block falling edge")
    ap.add_argument("--max-errors", type=int, default=10, help="Errors printed per category")
    ap.add_argument("-D", dest="defines", action="append", default=[], metavar="NAME=VALUE",
                    help="Override a macro used in the config, as passed to the compiler")
    args = ap.parse_args()

    cfg = parse_c(args.src, dict(d.split("=", 1) for d in args.defines))
    print(f"Config from {', '.join(args.src)}: {cfg.resolution}, {cfg.bpp}bpp, vertical repeat {cfg.v_repeat}")
    print(f"  H (port ticks): sync={cfg.h_sync} back={cfg.h_back} active={cfg.h_active} "
          f"front={cfg.h_front} total={cfg.h_total}")
    print(f"  V (lines):      sync={cfg.v_sync} back={cfg.v_back} active={cfg.v_active} "
          f"front={cfg.v_front} total={cfg.v_total}")

    sig_hs = f"{args.tile}_{cfg.p_hsync}"
    sig_vs = f"{args.tile}_{cfg.p_vsync}"
    sig_rgb = f"{args.tile}_{cfg.p_rgb}"
    sig_clk = f"{args.tile}_{cfg.clk_blk}_fallEdge"
    sig_rgb_wait = f"{sig_rgb}_tWaiting"
    sync_waits = [f"{sig_hs}_tWaiting", f"{sig_vs}_tWaiting"]
    ts_ps, sigs = parse_vcd(args.vcd, [sig_hs, sig_vs, sig_rgb, sig_clk, sig_rgb_wait] + sync_waits)
    delay = args.sample_delay_ns * 1000.0 / ts_ps  # in VCD ticks

    clk_t, clk_v = sigs[sig_clk]
    falls = [t for t, v in zip(clk_t, clk_v) if v == 1]
    if len(falls) < 2:
        sys.exit(f"Not enough falling edges on {sig_clk}")
    periods = sorted(b - a for a, b in zip(falls, falls[1:]))
    clk_period_ns = periods[len(periods) // 2] * ts_ps / 1000.0
    print(f"VCD: timescale {ts_ps:g} ps, {len(falls)} port clock falling edges, "
          f"period {clk_period_ns:g} ns ({1000.0 / clk_period_ns:g} MHz)")
    print(f"  Line period {cfg.h_total * clk_period_ns / 1000:g} us, "
          f"frame period {cfg.h_total * cfg.v_total * clk_period_ns / 1000:g} us")

    rep = Report(args.max_errors)

    # Port clock = VGA pixel clock scaled by port ticks per (640-wide) VGA pixel
    expected_mhz = VGA_PIXEL_CLOCK_MHZ * CLOCK_SCALE * cfg.port_cycles_per_word / cfg.h_output_divider
    measured_mhz = 1000.0 / clk_period_ns
    print(f"  Expected port clock {expected_mhz:g} MHz "
          f"(VGA {VGA_PIXEL_CLOCK_MHZ:g} MHz x {CLOCK_SCALE:g})")
    if abs(measured_mhz - expected_mhz) > expected_mhz * CLOCK_TOLERANCE:
        rep.error("clock frequency", f"port clock {measured_mhz:g} MHz, expected {expected_mhz:g} MHz")

    def us(t):
        return t * ts_ps / 1e6

    # Data must be stable at the sample point: flag changes between the sample point and the next edge.
    print("Checking output change timing...")
    for name in (sig_hs, sig_vs, sig_rgb):
        for t in sigs[name][0]:
            k = bisect.bisect_right(falls, t) - 1
            if k < 0:
                continue
            if t - falls[k] > delay:
                rep.error("late change", f"{name} changed at {us(t):.4f} us, "
                          f"{(t - falls[k]) * ts_ps / 1000.0:.3f} ns after clock fall")

    def sample(name, t):
        times, values = sigs[name]
        i = bisect.bisect_right(times, t) - 1
        return values[i] if i >= 0 else None

    samples = [(f, sample(sig_rgb, f + delay), sample(sig_hs, f + delay), sample(sig_vs, f + delay))
               for f in falls]
    rgb = [s[1] for s in samples]
    hs = [s[2] for s in samples]
    vs = [s[3] for s in samples]

    def edges(values, frm, to):
        return [i for i in range(1, len(values)) if values[i - 1] == frm and values[i] == to]

    hs_fall, hs_rise = edges(hs, 1, 0), edges(hs, 0, 1)
    vs_fall, vs_rise = edges(vs, 1, 0), edges(vs, 0, 1)
    print(f"  {len(hs_fall)} hsync pulses, {len(vs_fall)} vsync pulses")

    print("Checking hsync timing...")
    if not hs_fall:
        rep.error("hsync", "no hsync pulses found")
    for a, b in zip(hs_fall, hs_fall[1:]):
        if b - a != cfg.h_total:
            rep.error("hsync period", f"at {us(falls[a]):.3f} us: {b - a} ticks, expected {cfg.h_total}")
    for a in hs_fall:
        r = bisect.bisect_right(hs_rise, a)
        if r < len(hs_rise) and hs_rise[r] - a != cfg.h_sync:
            rep.error("hsync width", f"at {us(falls[a]):.3f} us: {hs_rise[r] - a} ticks, expected {cfg.h_sync}")

    print("Checking vsync timing...")
    frame_ticks = cfg.h_total * cfg.v_total
    if not vs_fall:
        rep.error("vsync", "no vsync pulses found")
    for a, b in zip(vs_fall, vs_fall[1:]):
        if b - a != frame_ticks:
            rep.error("vsync period", f"at {us(falls[a]):.3f} us: {b - a} ticks "
                      f"({(b - a) / cfg.h_total:g} lines), expected {frame_ticks} ({cfg.v_total} lines)")
    for a in vs_fall:
        r = bisect.bisect_right(vs_rise, a)
        if r < len(vs_rise) and vs_rise[r] - a != cfg.v_sync * cfg.h_total:
            w = vs_rise[r] - a
            rep.error("vsync width", f"at {us(falls[a]):.3f} us: {w} ticks ({w / cfg.h_total:g} lines), "
                      f"expected {cfg.v_sync * cfg.h_total} ({cfg.v_sync} lines)")

    # Lines start at active video, i.e. hsync fall minus (active + front).
    line_starts = [i - cfg.h_active - cfg.h_front for i in hs_fall]
    complete_lines = sum(1 for s in line_starts if s + cfg.h_total <= len(samples))

    # Frame line 0 is the line in which vsync first falls.
    anchor = None
    if vs_fall:
        k = bisect.bisect_right(line_starts, vs_fall[0]) - 1
        if k >= 0:
            anchor = k
            print(f"  INFO: vsync falls {vs_fall[0] - line_starts[k]} ticks after the start of the line "
                  f"(0 = start of active, {cfg.h_active + cfg.h_front} = hsync)")
    bad_frames = set()
    if anchor is None:
        whole_frames = 0
        rep.error("vsync", "no frame start found")
    else:
        whole_frames = max(0, (complete_lines - anchor) // cfg.v_total)
        # Alignment extrapolates backwards from the first vsync, so the frame
        # displayed before it is checked too (as frame -1)
        for k, i in enumerate(hs_fall):
            line = (k - anchor) % cfg.v_total
            expect_low = line < cfg.v_sync
            if vs[i] != (0 if expect_low else 1):
                bad_frames.add((k - anchor) // cfg.v_total)
                rep.error("vsync line alignment", f"line {line} at {us(falls[i]):.3f} us: vsync={vs[i]}, "
                          f"expected {0 if expect_low else 1}")

    print("Checking RGB data...")
    checked_active = 0
    skipped = 0
    # Buffers (pattern bases) consistent with each frame's active pixels so far.
    # Not every pixel distinguishes the buffers, so a set is narrowed as pixels
    # arrive; tearing or corruption leaves no consistent buffer and is reported.
    frame_candidates = {}
    for i, (t, value, _, _) in enumerate(samples):
        k = bisect.bisect_right(line_starts, i) - 1
        if k < 0:
            skipped += 1  # Before the first line of output
            continue
        tick = i - line_starts[k]
        if tick >= cfg.h_total:
            continue  # Broken line length (already reported) or past the last hsync
        in_active = False
        frame = None
        col = tick
        h_active = col < cfg.h_active
        if anchor is None:
            where = f"tick {tick} (h {h_region(cfg, tick)})"
            if h_active:
                continue  # Can't know the expected pixel without vertical position
            expected = 0
        else:
            line = (k - anchor) % cfg.v_total
            frame = (k - anchor) // cfg.v_total
            row = line - cfg.v_sync - cfg.v_back
            v_active = 0 <= row < cfg.v_active
            where = f"frame {frame} line {line} (v {v_region(cfg, line)}) tick {tick} (h {h_region(cfg, tick)})"
            in_active = h_active and v_active
            if in_active:
                checked_active += 1
                cands = frame_candidates.setdefault(frame, set(cfg.pattern_bases))
                expected = 0  # Unused when value is None (reported as undefined below)
                if value is not None:
                    matching = {b for b in cands
                                if value == cfg.pixel(row // cfg.v_repeat, col, b)}
                    if matching:
                        frame_candidates[frame] = matching
                        expected = value
                    else:
                        expected = cfg.pixel(row // cfg.v_repeat, col, min(cands))
            else:
                expected = 0

        if value is None:
            bad_frames.add(frame)
            rep.error("rgb undefined", f"at {us(t):.3f} us, {where}")
        elif value != expected:
            bad_frames.add(frame)
            category = "rgb pixel mismatch" if in_active else "rgb blanking not zero"
            rep.error(category, f"at {us(t):.3f} us, {where}: got 0x{value:0{cfg.bpp // 4}x}, "
                      f"expected 0x{expected:0{cfg.bpp // 4}x}")

    print(f"  {checked_active} active pixels checked, {skipped} samples before the first line not checked")
    good_frames = sum(1 for n in range(whole_frames) if n not in bad_frames)
    print(f"  {whole_frames} whole frames in trace after the first vsync, {good_frames} passed")
    # The frame before the first vsync is checked too, so one whole frame's
    # worth of active samples is enough coverage even in a short trace
    full_frame_samples = cfg.h_active * cfg.v_active
    if checked_active < full_frame_samples:
        rep.error("coverage", f"checked {checked_active} active samples, need at least "
                  f"one whole frame's worth ({full_frame_samples})")
    if len(cfg.pattern_bases) > 1:
        seen = {next(iter(cands)) for n, cands in frame_candidates.items()
                if n < whole_frames and n not in bad_frames and len(cands) == 1}
        print(f"  Buffers displayed in whole good frames: {[hex(b) for b in sorted(seen)]} "
              f"of {[hex(b) for b in cfg.pattern_bases]}")
        if whole_frames >= 2 and len(seen) < 2:
            rep.error("buffer swap", f"only {len(seen)} of {len(cfg.pattern_bases)} buffers "
                      f"displayed across {whole_frames} whole frames")

    print("Checking RGB thread headroom...")
    # Thread compute time between RGB OUTs: from resuming after one blocking OUT to blocking on the next.
    # A gap where the thread waited on a sync port is blanking; the next line restarts with a timed OUT.
    sync_wait_starts = sorted(t for name in sync_waits for t, v in zip(*sigs[name]) if v)
    busy = []
    resume = None
    for t, v in zip(*sigs[sig_rgb_wait]):
        if t < falls[0]:
            continue
        if not v:
            resume = t
        elif resume is not None:
            restart = bisect.bisect_left(sync_wait_starts, resume) < bisect.bisect_left(sync_wait_starts, t)
            busy.append((resume, t - resume, restart))
            resume = None
    streamed = [(r, b) for r, b, restart in busy if not restart]

    def position(t):
        i = bisect.bisect_right(falls, t) - 1
        k = bisect.bisect_right(line_starts, i) - 1
        if anchor is None or k < 0:
            return f"at {us(t):.3f} us"
        line = (k - anchor) % cfg.v_total
        tick = i - line_starts[k]
        return f"at {us(t):.3f} us, port at line {line} (v {v_region(cfg, line)}) tick {tick} (h {h_region(cfg, tick)})"

    word_ticks = cfg.port_cycles_per_word * periods[len(periods) // 2]
    for label, scale in (("trace speed", 1.0), ("full speed", CLOCK_SCALE)):
        if label == "full speed" and CLOCK_SCALE == 1.0:
            break
        word = word_ticks * scale
        # Replay compute times against the port: the transfer register frees when the previous word starts
        # shifting, and must be refilled before that word finishes.
        done = start = 0.0
        margins = []
        for resume, b, restart in busy:
            if restart:
                # Timed OUT: the next word is queued as this one starts, as if just blocked
                done, start = 0.0, word
                continue
            done = max(done + b, start)
            margin = start + word - done
            start = max(start + word, done)
            margins.append((margin, resume))
        if not margins:
            rep.error("headroom", f"no blocking OUTs on {sig_rgb} found")
            break
        worst = min(margins)
        print(f"  {label}: word time {word * ts_ps / 1000:.0f} ns, min margin {worst[0] * ts_ps / 1000:.1f} ns "
              f"{position(worst[1])}")
        underruns = [m for m in margins if m[0] < 0]
        for m, t in underruns:
            rep.error(f"rgb underrun ({label})", f"late by {-m * ts_ps / 1000:.1f} ns {position(t)}")
    print(f"  Longest thread compute between RGB OUTs: "
          f"{max(b for _, b in streamed) * ts_ps / 1000:.1f} ns {position(max(streamed, key=lambda x: x[1])[0])}"
          if streamed else "  No RGB OUTs found")

    print()
    if rep.failed:
        print("FAILED:")
        for category, n in rep.counts.items():
            print(f"  {category}: {n}")
        return 1
    print("PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
