#!/usr/bin/env bash
#
# Build and flash Plane Radar to the CYD over USB serial (no sudo).
#
# One-time setup (so the serial port is accessible without root):
#     sudo usermod -aG dialout "$USER"
#     # then log out and back in (or reboot) for the group to take effect
#
# Port selection: by default the script auto-detects the CYD by its USB-serial
# bridge (WCH CH340/CH341, USB vendor 1a86) and asks you to confirm before
# flashing -- it prints the device's model and kernel driver so you don't write
# firmware to the wrong dongle. Override with PORT=/dev/ttyXXX if needed.
#
# Usage:
#     scripts/flash.sh              # build + upload firmware (auto-detect + confirm)
#     scripts/flash.sh monitor      # open the serial monitor
#     PORT=/dev/ttyUSB0 scripts/flash.sh        # force a specific port
#     scripts/flash.sh -y           # skip the confirmation prompt
#     ENV=cyd scripts/flash.sh                  # override PlatformIO env
#
# Note: unlike the System-Monitor build, Plane Radar embeds its font into the
# firmware (board_build.embed_files), so there is NO separate SPIFFS/uploadfs
# step -- a single firmware upload is all that is needed.
set -euo pipefail

ENV="${PIOENV:-${ENV:-cyd}}"
export PLATFORMIO_CORE_DIR="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"

# WCH CH340/CH341 USB vendor id -- the USB-serial bridge on the CYD.
CYD_VENDOR_ID="1a86"
ASSUME_YES="${ASSUME_YES:-0}"

# Parse args: an optional -y/--yes anywhere, plus one target (fw|all|monitor).
target=""
for arg in "$@"; do
  case "$arg" in
    -y | --yes) ASSUME_YES=1 ;;
    fw | firmware | all | monitor) target="$arg" ;;
    *) echo "Usage: $0 [-y] [fw|all|monitor]" >&2; exit 1 ;;
  esac
done
target="${target:-all}"

# Locate pio: prefer one on PATH, then the VSCode PlatformIO extension venv.
if [ -n "${PIO:-}" ]; then
  :
elif command -v pio >/dev/null 2>&1; then
  PIO=pio
elif [ -x "$HOME/.platformio/penv/bin/pio" ]; then
  PIO="$HOME/.platformio/penv/bin/pio"
else
  echo "Error: pio not found. Set PIO=/path/to/pio." >&2
  exit 1
fi

cd "$(dirname "$0")/.."

# --- USB metadata helpers (udevadm; degrade gracefully if absent) ----------
port_prop() {  # port_prop <device> <PROPERTY>
  udevadm info -q property -n "$1" 2>/dev/null | sed -n "s/^$2=//p"
}
port_model() { local m; m="$(port_prop "$1" ID_MODEL)"; echo "${m:-unknown}"; }
port_driver() { local d; d="$(port_prop "$1" ID_USB_DRIVER)"; echo "${d:-unknown}"; }

list_ports() { ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || true; }

print_port_line() {  # "    /dev/ttyUSB0  (model, driver)"
  printf '    %s  (%s, %s)\n' "$1" "$(port_model "$1")" "$(port_driver "$1")"
}

# --- Choose the port -------------------------------------------------------
if [ -n "${PORT:-}" ]; then
  : # explicit override, use as-is
else
  matches=()
  for p in $(list_ports); do
    [ "$(port_prop "$p" ID_VENDOR_ID)" = "$CYD_VENDOR_ID" ] && matches+=("$p")
  done

  if [ "${#matches[@]}" -eq 1 ]; then
    PORT="${matches[0]}"
  elif [ "${#matches[@]}" -gt 1 ]; then
    echo "Multiple CH340/CH341 devices found -- pick one with PORT=...:" >&2
    for p in "${matches[@]}"; do print_port_line "$p" >&2; done
    exit 1
  else
    echo "Error: no CYD (CH340/CH341, USB vendor $CYD_VENDOR_ID) serial port found." >&2
    echo "Available serial ports:" >&2
    found="$(list_ports)"
    if [ -n "$found" ]; then
      for p in $found; do print_port_line "$p" >&2; done
    else
      echo "    (none found)" >&2
    fi
    echo "Plug in the CYD, or force one with: PORT=/dev/ttyXXX $0" >&2
    exit 1
  fi
fi

# --- Validate the chosen port ----------------------------------------------
if [ ! -e "$PORT" ]; then
  echo "Error: serial port $PORT not found. Is the device plugged in?" >&2
  echo "Available serial ports:" >&2
  found="$(list_ports)"
  if [ -n "$found" ]; then
    for p in $found; do print_port_line "$p" >&2; done
  else
    echo "    (none found)" >&2
  fi
  exit 1
fi
if [ ! -w "$PORT" ]; then
  echo "Error: $PORT is not writable by $(whoami) (needs the 'dialout' group)." >&2
  echo "Run once, then log out/in:" >&2
  echo "    sudo usermod -aG dialout $USER" >&2
  exit 1
fi

# --- Confirm before touching the device ------------------------------------
echo "Target port: $PORT"
echo "  Model:  $(port_model "$PORT")"
echo "  Driver: $(port_driver "$PORT")"
if [ "$ASSUME_YES" != "1" ]; then
  if [ ! -t 0 ]; then
    echo "Refusing to proceed without confirmation (no TTY). Re-run with -y to skip." >&2
    exit 1
  fi
  read -r -p "Flash/monitor this device? [y/N] " ans
  case "$ans" in
    y | Y | yes | YES) ;;
    *) echo "Aborted."; exit 1 ;;
  esac
fi

case "$target" in
  fw | firmware | all)
    "$PIO" run -e "$ENV" -t upload --upload-port "$PORT"
    ;;
  monitor)
    exec "$PIO" device monitor -p "$PORT" -b 115200
    ;;
esac

echo "Done."
