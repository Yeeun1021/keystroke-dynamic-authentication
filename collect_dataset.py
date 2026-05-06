import serial

PORT = "COM7"       # Windows: COM3, COM4 etc. Linux: /dev/ttyACM0
USER_ID = 6         # Change to 1 for second user

with serial.Serial(PORT, 115200, timeout=1) as ser, \
     open(f"data_user{USER_ID}.csv", "w") as f:
    
    header = "hold0,hold1,hold2,hold3,flight01,flight12,flight23,label\n"
    f.write(header)
    count = 0
    print(f"Recording user {USER_ID}... Ctrl+C to stop")
    
    while True:
        line = ser.readline().decode("utf-8", errors="ignore").strip()
        if not line or line.startswith("#"):
            continue
        print(f"[{count+1}] {line}")
        f.write(f"{line},{USER_ID}\n")
        count += 1

print(f"Done. {count} samples saved.")