#!/usr/bin/env python3
"""
MediBot Display Simulator & Hardware Mock
-----------------------------------------
Simulates the Sunton 7" LCD Touch Display over UART.
Communicates with the ESP-IDF MediBot Main Controller using the line-delimited
JSON protocol defined in display_comm.c / health_main_control.

Usage:
  python3 tools/display_simulator.py --port /dev/ttyUSB0 --baud 115200
  python3 tools/display_simulator.py --virtual    # Creates a virtual PTY pair
"""

import sys
import os
import time
import json
import threading
import argparse

try:
    import serial
except ImportError:
    print("Installing pyserial for UART communication...")
    os.system(f"{sys.executable} -m pip install pyserial")
    import serial

# ANSI Colors
GREEN = "\033[92m"
CYAN = "\033[96m"
YELLOW = "\033[93m"
RED = "\033[91m"
BOLD = "\033[1m"
RESET = "\033[0m"

class DisplaySimulator:
    def __init__(self, ser):
        self.ser = ser
        self.running = True
        self.current_user = None
        self.latest_vitals = None

    def listen_loop(self):
        """Continuously reads incoming JSON lines from the ESP32 controller."""
        while self.running:
            try:
                line = self.ser.readline()
                if not line:
                    continue
                line_str = line.decode('utf-8', errors='replace').strip()
                if not line_str:
                    continue
                self.handle_incoming(line_str)
            except Exception as e:
                if self.running:
                    print(f"{RED}[Error reading serial]: {e}{RESET}")
                break

    def handle_incoming(self, raw_str):
        """Parses and pretty-prints incoming JSON messages from the controller."""
        try:
            msg = json.loads(raw_str)
            msg_type = msg.get("type", "UNKNOWN")

            print(f"\n{CYAN}┌── [ESP32 → DISPLAY] ({msg_type}){RESET}")
            
            if msg_type == "SYSTEM_STATUS":
                msg_text = msg.get("message", "")
                wifi = "Connected" if msg.get("wifi_connected") else "Disconnected"
                cloud = "Ready" if msg.get("ready") or msg.get("cloud_ready") else "Offline"
                ip = msg.get("ip", "0.0.0.0")
                print(f"{CYAN}│{RESET}  Status: {BOLD}{msg_text}{RESET}")
                print(f"{CYAN}│{RESET}  WiFi:   [{wifi}] | IP: {ip} | Cloud: [{cloud}]")

            elif msg_type == "PROMPT":
                prompt_text = msg.get("message", "")
                print(f"{YELLOW}│  📢 PROMPT: {BOLD}{prompt_text}{RESET}")

            elif msg_type == "USER_DATA":
                self.current_user = msg
                name = f"{msg.get('first_name', '')} {msg.get('last_name', '')}".strip() or msg.get('user_name', 'Unknown')
                med_id = msg.get("user_medical_id", "N/A")
                age = msg.get("user_age", "N/A")
                gender = msg.get("user_gender", "N/A")
                print(f"{GREEN}│  👤 USER LOGGED IN:{RESET}")
                print(f"{GREEN}│     Name: {BOLD}{name}{RESET} | ID: {med_id} | Age: {age} | Gender: {gender}")

            elif msg_type == "SENSOR_DATA":
                self.latest_vitals = msg
                hr = msg.get("heart_rate", 0.0)
                spo2 = msg.get("spo2", 0.0)
                temp = msg.get("temperature", 0.0)
                wt = msg.get("weight", 0.0)
                ht = msg.get("height", 0.0)
                bmi = msg.get("bmi", 0.0)
                print(f"{GREEN}│  📊 VITALS RECEIVED:{RESET}")
                print(f"{GREEN}│     Heart Rate:  {BOLD}{hr:.1f} BPM{RESET}")
                print(f"{GREEN}│     SpO2:        {BOLD}{spo2:.1f} %{RESET}")
                print(f"{GREEN}│     Temperature: {BOLD}{temp:.1f} °C{RESET}")
                print(f"{GREEN}│     Weight:      {BOLD}{wt:.1f} kg{RESET}")
                print(f"{GREEN}│     Height:      {BOLD}{ht:.2f} m{RESET}")
                print(f"{GREEN}│     BMI:         {BOLD}{bmi:.1f}{RESET}")

            elif "SUCCESS" in msg_type:
                print(f"{GREEN}│  ✅ SUCCESS: {msg.get('message', '')}{RESET}")

            elif "ERROR" in msg_type or "FAILED" in msg_type:
                print(f"{RED}│  ❌ ERROR: {msg.get('message', '')}{RESET}")

            else:
                print(f"{CYAN}│{RESET}  Raw: {json.dumps(msg, indent=2)}")

            print(f"{CYAN}└────────────────────────────────────{RESET}")
            print(f"{BOLD}Command [1-8] > {RESET}", end="", flush=True)

        except json.JSONDecodeError:
            print(f"\n{YELLOW}[ESP32 Log]: {raw_str}{RESET}")
            print(f"{BOLD}Command [1-8] > {RESET}", end="", flush=True)

    def send_cmd(self, cmd_text):
        """Sends a newline-terminated command string to the ESP32."""
        payload = (cmd_text.strip() + "\n").encode('utf-8')
        self.ser.write(payload)
        self.ser.flush()
        print(f"{BOLD}Sent → {cmd_text}{RESET}")

    def menu(self):
        print(f"\n{BOLD}============================================{RESET}")
        print(f"{BOLD}     MediBot Virtual Touchscreen Display    {RESET}")
        print(f"{BOLD}============================================{RESET}")
        print("  [1] Send DISPLAY_READY (Boot handshake)")
        print("  [2] Send START_LOGIN   (Touch 'Login' button)")
        print("  [3] Send START_ENROLLMENT (Touch 'Enroll' button)")
        print("  [4] Send READ_OXIMETER (Trigger pulse/SpO2 reading)")
        print("  [5] Send SAVE_READINGS (Upload latest vitals to cloud & SD)")
        print("  [6] Send LOGOUT        (Return to idle screen)")
        print("  [7] Send BACK          (Cancel current operation)")
        print("  [8] Send Custom String")
        print("  [q] Quit simulator")
        print(f"{BOLD}============================================{RESET}\n")

        while self.running:
            try:
                choice = input(f"{BOLD}Command [1-8] > {RESET}").strip()
            except (KeyboardInterrupt, EOFError):
                break

            if choice == '1':
                self.send_cmd("DISPLAY_READY")
            elif choice == '2':
                self.send_cmd("START_LOGIN")
            elif choice == '3':
                self.send_cmd("START_ENROLLMENT")
            elif choice == '4':
                self.send_cmd("READ_OXIMETER")
            elif choice == '5':
                self.send_cmd("SAVE_READINGS")
            elif choice == '6':
                self.send_cmd("LOGOUT")
            elif choice == '7':
                self.send_cmd("BACK")
            elif choice == '8':
                custom = input("Enter custom command: ")
                if custom:
                    self.send_cmd(custom)
            elif choice.lower() in ('q', 'exit'):
                break

        self.running = False

def main():
    parser = argparse.ArgumentParser(description="MediBot Virtual Touchscreen Display Simulator")
    parser.add_argument("--port", default="/dev/ttyUSB0", help="Serial port to ESP32 (e.g. /dev/ttyUSB0, COM3)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("--virtual", action="store_true", help="Create virtual pseudo-terminal (PTY) for testing without hardware")
    args = parser.parse_args()

    if args.virtual:
        import pty
        master, slave = pty.openpty()
        slave_name = os.ttyname(slave)
        print(f"{GREEN}Virtual PTY created!{RESET}")
        print(f"Connect your test runner / loopback to: {BOLD}{slave_name}{RESET}")
        ser = serial.Serial(os.ttyname(master), baudrate=args.baud, timeout=0.1)
    else:
        print(f"Opening serial port {BOLD}{args.port}{RESET} at {args.baud} baud...")
        try:
            ser = serial.Serial(args.port, baudrate=args.baud, timeout=0.1)
        except serial.SerialException as e:
            print(f"{RED}Error opening port {args.port}: {e}{RESET}")
            print("\nTip: Use --virtual to run a local virtual loopback test:")
            print(f"     python3 {sys.argv[0]} --virtual")
            sys.exit(1)

    sim = DisplaySimulator(ser)
    rx_thread = threading.Thread(target=sim.listen_loop, daemon=True)
    rx_thread.start()

    time.sleep(0.5)
    # Automatically send DISPLAY_READY upon launch
    sim.send_cmd("DISPLAY_READY")
    sim.menu()

    print("\nShutting down simulator.")
    ser.close()

if __name__ == "__main__":
    main()
