# Copyright 2026 XMOS LIMITED.
# This Software is subject to the terms of the XMOS Public Licence: Version 1.
"""Simulate every build config of the test apps under xsim and check the VGA
sync timing and RGB test pattern in the trace with vga_tb.py.

test_vga       - static frame buffer, every resolution / colour depth, plus
                 stress configs (controller at minimum MIPS share) and a full
                 standard-timing config.
test_vga_swap  - double buffering: frame_ptr_base swapped between two buffers.
"""

import re
import subprocess
import sys
from pathlib import Path
from shutil import which

import pytest
from filelock import FileLock

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "logs"
VGA_HEADER = ROOT.parent / "lib_vga" / "api" / "vga.h"

# Delete traces this large once their checks pass (the full-timing trace is ~600 MB)
VCD_KEEP_LIMIT = 100 * 1024 * 1024

# xsim and the build require XTC tools
pytestmark = pytest.mark.skipif(
    which("xsim") is None,
    reason="Test requires XTC tools",
)


def parse_configs(app):
    """{config: {macro: value}} from APP_COMPILER_FLAGS_<config> in the app's CMakeLists.txt."""
    text = (ROOT / app / "CMakeLists.txt").read_text()
    variables = {m.group(1): m.group(2)
                 for m in re.finditer(r"set\((\w+)\s+([^)]*)\)", text)}

    def expand(value, depth=0):
        if depth > 5:
            return value
        return re.sub(r"\$\{(\w+)\}",
                      lambda m: expand(variables.get(m.group(1), ""), depth + 1), value)

    return {name[len("APP_COMPILER_FLAGS_"):]: dict(re.findall(r"-D(\w+)=(\S+)", expand(flags)))
            for name, flags in variables.items() if name.startswith("APP_COMPILER_FLAGS_")}


CONFIGS = parse_configs("test_vga")
SWAP_CONFIGS = parse_configs("test_vga_swap")


def binary(app, config):
    return ROOT / app / "bin" / config / f"{app}_{config}.xe"


def max_cycles(defines):
    """xsim core cycles covering ~1.3 frames: vga_tb.py extrapolates backwards from
    the first vsync, so the frame displayed before it plus the whole vsync pulse and
    back porch is enough. 1M cycles of slack covers startup and buffer filling."""
    get = lambda name, default: int(defines.get(name, default))
    frame_pixels = ((get("VGA_H_ACTIVE", 640) + get("VGA_H_FRONT", 16) +
                     get("VGA_H_SYNC", 96) + get("VGA_H_BACK", 48)) *
                    (get("VGA_V_ACTIVE", 480) + get("VGA_V_FRONT", 10) +
                     get("VGA_V_SYNC", 2) + get("VGA_V_BACK", 33)))
    # 24 core cycles (600 MHz) per 25 MHz pixel clock
    return frame_pixels * 24 * 13 // 10 + 1_000_000


@pytest.fixture(scope="session")
def binaries():
    """Build all test app configs unless prebuilt (e.g. by CI). The lock
    serialises the build across pytest-xdist workers."""
    needed = [binary(app, cfg)
              for app, cfgs in (("test_vga", CONFIGS), ("test_vga_swap", SWAP_CONFIGS))
              for cfg in cfgs]
    with FileLock(ROOT / "build.lock"):
        if not all(path.exists() for path in needed):
            for cmd in (["cmake", "-G", "Unix Makefiles", "-B", "build"],
                        ["cmake", "--build", "build", "-j"]):
                result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
                assert result.returncode == 0, \
                    f"'{' '.join(cmd)}' failed:\n{result.stdout}\n{result.stderr}"


def simulate_and_check(app, config, defines, src):
    OUT.mkdir(exist_ok=True)
    name = f"{app}_{config}"
    vcd = OUT / f"trace_{name}.vcd"
    sim_log = OUT / f"sim_{name}.log"
    sim = ["xsim", "--max-cycles", str(max_cycles(defines)), "--vcd-tracing",
           f"-o {vcd} -tile tile[0] -clock-blocks -ports -ports-detailed -cores",
           str(binary(app, config))]
    with open(sim_log, "w") as f:
        returncode = subprocess.run(sim, stdout=f, stderr=subprocess.STDOUT).returncode
    # The firmware never exits, so hitting --max-cycles is the normal end of a run
    assert returncode == 0 or "maximum cycles reached" in sim_log.read_text(), \
        f"xsim failed for {name}:\n{sim_log.read_text()}"

    tb = [sys.executable, str(ROOT / "vga_tb.py"), "--vcd", str(vcd),
          "--src", str(src), str(VGA_HEADER)]
    for macro, value in defines.items():
        tb += ["-D", f"{macro}={value}"]
    result = subprocess.run(tb, capture_output=True, text=True)
    print(result.stdout)
    assert result.returncode == 0, f"vga_tb.py checks failed for {name}:\n{result.stdout}\n{result.stderr}"
    if vcd.stat().st_size > VCD_KEEP_LIMIT:
        vcd.unlink()


@pytest.mark.parametrize("config", sorted(CONFIGS))
def test_vga_output(config, binaries):
    simulate_and_check("test_vga", config, CONFIGS[config],
                       ROOT / "test_vga" / "src" / "test_vga.c")


@pytest.mark.parametrize("config", sorted(SWAP_CONFIGS))
def test_vga_buffer_swap(config, binaries):
    simulate_and_check("test_vga_swap", config, SWAP_CONFIGS[config],
                       ROOT / "test_vga_swap" / "src" / "test_vga_swap.c")
