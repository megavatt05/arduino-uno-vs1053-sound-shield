import sys, time, serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM30"
cmd = sys.argv[2].encode() if len(sys.argv) > 2 else b"M"
hold = float(sys.argv[3]) if len(sys.argv) > 3 else 135.0

p = serial.Serial(port, 115200, timeout=0.2)
time.sleep(2.5)          # плата перезагружается при открытии порта (DTR)
p.reset_input_buffer()
p.write(cmd)
t0 = time.time()
buf = b""
while time.time() - t0 < hold:
    data = p.read(4096)
    if data:
        buf += data
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            sys.stdout.write(line.decode("utf-8", "replace") + "\n")
            sys.stdout.flush()
p.close()
print("--- порт закрыт, прошло %.0f c ---" % (time.time() - t0))
