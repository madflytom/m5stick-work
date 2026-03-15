#!/usr/bin/env python3

from __future__ import annotations

import argparse
import pathlib
import time

import serial


ROOT = pathlib.Path("/sys/class/hwmon")


def read_text(path: pathlib.Path) -> str | None:
    try:
        return path.read_text().strip()
    except OSError:
        return None


def find_hwmon(name: str) -> pathlib.Path | None:
    for candidate in ROOT.glob("hwmon*"):
        if read_text(candidate / "name") == name:
            return candidate
    return None


def read_temp_c(path: pathlib.Path | None, filename: str) -> float:
    if path is None:
        return 0.0
    raw = read_text(path / filename)
    if not raw:
        return 0.0
    return int(raw) / 1000.0


def read_int(path: pathlib.Path | None, filename: str) -> int:
    if path is None:
        return 0
    raw = read_text(path / filename)
    if not raw:
        return 0
    return int(raw)


def read_battery_percent(name: str) -> float:
    base = pathlib.Path("/sys/class/power_supply") / name
    raw = read_text(base / "capacity")
    if raw is None:
        return -1.0
    return float(raw)


def read_uptime_hours() -> float:
    raw = read_text(pathlib.Path("/proc/uptime"))
    if not raw:
        return 0.0
    seconds = float(raw.split()[0])
    return seconds / 3600.0


def read_mem_used_pct() -> float:
    meminfo = pathlib.Path("/proc/meminfo").read_text().splitlines()
    values: dict[str, int] = {}
    for line in meminfo:
        key, raw = line.split(":", 1)
        values[key] = int(raw.strip().split()[0])
    total = values.get("MemTotal", 1)
    available = values.get("MemAvailable", 0)
    used = total - available
    return used * 100.0 / total


def read_cpu_times() -> tuple[int, int]:
    with pathlib.Path("/proc/stat").open() as handle:
        cpu = handle.readline().split()[1:]
    values = [int(part) for part in cpu]
    idle = values[3] + values[4]
    total = sum(values)
    return idle, total


def read_fan_rpm(thinkpad_hwmon: pathlib.Path | None) -> int:
    proc_path = pathlib.Path("/proc/acpi/ibm/fan")
    raw = read_text(proc_path)
    if raw:
      for line in raw.splitlines():
          if line.startswith("speed:"):
              value = line.split(":", 1)[1].strip()
              return int(value) if value.isdigit() else 0
    return read_int(thinkpad_hwmon, "fan1_input")


def format_payload(seq: int, load_pct: float, cpu_temp: float, wifi_temp: float,
                   pch_temp: float, fan_rpm: int, mem_used_pct: float,
                   batt0_pct: float, batt1_pct: float, uptime_hours: float) -> str:
    return (
        f"seq={seq};cpu={cpu_temp:.1f};wifi={wifi_temp:.1f};pch={pch_temp:.1f};"
        f"fan={fan_rpm};load={load_pct:.1f};mem={mem_used_pct:.1f};"
        f"bat0={batt0_pct:.0f};bat1={batt1_pct:.0f};up={uptime_hours:.1f}\n"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Send Lenovo T470 stats to the M5StickC over serial")
    parser.add_argument("--port", default="/dev/ttyUSB0", help="Serial port for the M5StickC")
    parser.add_argument("--baud", default=115200, type=int, help="Serial baud rate")
    parser.add_argument("--interval", default=1.0, type=float, help="Refresh interval in seconds")
    args = parser.parse_args()

    thinkpad_hwmon = find_hwmon("thinkpad")
    coretemp_hwmon = find_hwmon("coretemp")
    wifi_hwmon = find_hwmon("iwlwifi_1")
    pch_hwmon = find_hwmon("pch_skylake")

    idle_prev, total_prev = read_cpu_times()
    seq = 0

    with serial.Serial(args.port, args.baud, timeout=1) as device:
        device.dtr = False
        device.rts = False
        time.sleep(2.0)

        while True:
            idle_now, total_now = read_cpu_times()
            idle_delta = idle_now - idle_prev
            total_delta = total_now - total_prev
            idle_prev, total_prev = idle_now, total_now
            load_pct = 0.0 if total_delta <= 0 else 100.0 * (1.0 - idle_delta / total_delta)

            payload = format_payload(
                seq=seq,
                load_pct=load_pct,
                cpu_temp=read_temp_c(coretemp_hwmon, "temp1_input"),
                wifi_temp=read_temp_c(wifi_hwmon, "temp1_input"),
                pch_temp=read_temp_c(pch_hwmon, "temp1_input"),
                fan_rpm=read_fan_rpm(thinkpad_hwmon),
                mem_used_pct=read_mem_used_pct(),
                batt0_pct=read_battery_percent("BAT0"),
                batt1_pct=read_battery_percent("BAT1"),
                uptime_hours=read_uptime_hours(),
            )
            device.write(payload.encode("utf-8"))
            device.flush()
            print(payload.strip())
            seq += 1
            time.sleep(args.interval)


if __name__ == "__main__":
    raise SystemExit(main())