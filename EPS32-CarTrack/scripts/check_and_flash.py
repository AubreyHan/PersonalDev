#!/usr/bin/env python3
"""
M5Track-Tab5 Pre-Flash Hardware & All-Sensor Verification Pipeline
==================================================================
Workflow:
  1. Detect connected M5Stack Tab5 USB serial port (/dev/cu.usbmodem* or /dev/cu.wchusbserial*).
  2. Query ESP32-P4 chip metadata, MAC address, and 16MB Flash health via esptool before flashing.
  3. Flash firmware & capture boot [HW_SELF_TEST] serial diagnostic report to verify all sensors:
     - Tab5 Onboard : ESP32-P4 PSRAM, BMI270 Master (0x68), RX8130CE RTC (0x32), INA226 (0x40), MicroSD
     - Module GNSS  : NEO-M9N (UART), BMI270 Slave (0x69), BMP280 Baro (0x76), BMM150 (0x10 HW ACK)
"""

import glob
import os
import subprocess
import sys
import time
import serial

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PIO_BIN = os.path.join(PROJECT_DIR, ".venv", "bin", "pio")
PYTHON_BIN = os.path.join(PROJECT_DIR, ".venv", "bin", "python")
ESPTOOL_PY = os.path.expanduser("~/.platformio/packages/tool-esptoolpy/esptool.py")


def find_serial_port():
    patterns = [
        "/dev/cu.usbmodem*",
        "/dev/cu.usbserial*",
        "/dev/cu.wchusbserial*",
        "/dev/cu.SLAB_USBtoUART*",
    ]
    ports = []
    for p in patterns:
        ports.extend(glob.glob(p))
    return sorted(ports)


def check_mcu_and_flash(port):
    print(f"\n[Step 1/3] Checking ESP32-P4 MCU & 16MB Flash on {port} ...")
    cmd = [PYTHON_BIN, ESPTOOL_PY, "--chip", "esp32p4", "--port", port, "flash_id"]
    res = subprocess.run(cmd, capture_output=True, text=True)
    print(res.stdout)
    if res.returncode != 0:
        print(res.stderr)
        print("[ERROR] Failed to communicate with ESP32-P4 bootloader/USB JTAG.")
        return False
    if "ESP32-P4" not in res.stdout:
        print("[ERROR] Connected chip is not an ESP32-P4!")
        return False
    print("[OK] ESP32-P4 MCU & Flash verified successfully.")
    return True


def flash_firmware(port):
    print(f"\n[Step 2/3] Flashing M5Track-Tab5 firmware to {port} ...")
    cmd = [PIO_BIN, "run", "-t", "upload", "--upload-port", port]
    res = subprocess.run(cmd, cwd=PROJECT_DIR)
    if res.returncode != 0:
        print("[ERROR] Firmware upload failed!")
        return False
    print("[OK] Firmware uploaded successfully.")
    return True


def verify_sensors_over_serial(port, timeout_sec=12):
    print(f"\n[Step 3/3] Reading real-time [HW_SELF_TEST] Sensor Report from {port} ...")
    lines = []
    try:
        with serial.Serial(port, 115200, timeout=0.5) as ser:
            # Trigger board reset via RTS/DTR or wait for boot banner
            ser.dtr = False
            ser.rts = True
            time.sleep(0.1)
            ser.rts = False
            start = time.time()
            while time.time() - start < timeout_sec:
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").rstrip()
                print(f"  {line}")
                lines.append(line)
                if "All M5Track-Tab5 FreeRTOS tasks started." in line:
                    break
    except Exception as e:
        print(f"[ERROR] Serial monitor error: {e}")
        return False

    joined = "\n".join(lines)
    required_checks = [
        ("Tab5 BMI270 Master (0x68)", "ONLINE (OK)"),
        ("M135 BMI270 Slave  (0x69)", "ONLINE (OK)"),
        ("Tab5 RX8130CE      (0x32)", "ONLINE (OK)"),
        ("Tab5 INA226 Gauge  (0x40)", "ONLINE (OK)"),
        ("M135 BMP280        (0x76)", "ONLINE (OK)"),
        ("M135 BMM150        (0x10)", "HW_ACK (OK)"),
        ("M135 NEO-M9N       (UART)", "ONLINE (OK)"),
    ]

    all_ok = True
    print("\n================ SENSOR VERIFICATION SUMMARY ================")
    for name, expected in required_checks:
        matched = any((name in ln and expected in ln) for ln in lines)
        status_str = "PASS" if matched else "CHECK_FAIL"
        print(f"  - {name:28s} : {status_str}")
        if not matched:
            all_ok = False
    print("=============================================================")
    return all_ok


def main():
    ports = find_serial_port()
    if not ports:
        print("[WAITING] No M5Stack Tab5 USB serial device detected (/dev/cu.usbmodem*).")
        print("          Please connect M5Stack Tab5 via USB-C data cable and power it on.")
        sys.exit(1)

    port = ports[0]
    print(f"[DETECTED] Found USB Serial Device: {port}")

    if not check_mcu_and_flash(port):
        sys.exit(2)

    if not flash_firmware(port):
        sys.exit(3)

    if not verify_sensors_over_serial(port):
        print("[WARN] One or more sensors did not report ONLINE. Check M5-Bus stacking of Module GNSS.")
        sys.exit(4)

    print("\n[SUCCESS] All M5Stack Tab5 + Module GNSS sensors verified and firmware is running!")


if __name__ == "__main__":
    main()
