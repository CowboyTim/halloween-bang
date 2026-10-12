import time
import serial

# Open the serial port
ser = serial.Serial("/dev/ttyUSB2", 9600, timeout=1)

try:
  # 1. Enter AT Command Mode (DTR = True -> pin goes LOW)
  print("Entering AT Command Mode...")
  ser.dtr = True
  time.sleep(0.1)

  # 2. Send configuration commands
  commands = [
      b"AT+BAUD=4\r\n",
      b"AT+RFID0017\r\n",
      b"AT+DVID0017\r\n",
      b"AT+RFC017\r\n",
      b"AT+POWE9\r\n",
      b"AT+CLSSC1\r\n",
      b"AT+CLSS\r\n",
  ]

  for cmd in commands:
    print(cmd)
    ser.write(cmd)
    time.sleep(0.2)
    response = ser.read_all()
    if response:
      print(response.decode(), end="")

  # 3. Explicitly Exit AT Mode (DTR = False -> pin goes HIGH)
  print("Switching back to Transparent Mode...")
  ser.dtr = False
  time.sleep(0.1)  # Give the module a moment to process the mode change

except KeyboardInterrupt:
  print("\nExiting...")
finally:
  # Ensure DTR is released and port is closed cleanly on exit
  ser.dtr = False
  ser.close()
