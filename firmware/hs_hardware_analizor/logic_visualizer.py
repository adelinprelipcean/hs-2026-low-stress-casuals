import serial
import time
import sys

# Configurări
PORT = "COM11"
BAUD = 115200
SAMPLES = 80

def capture():
    try:
        # Deschidem portul fără control de flux pentru a evita reset-ul în bootloader
        ser = serial.Serial(PORT, BAUD, timeout=3)
        ser.dtr = False
        ser.rts = False
        print(f"Conectat la {PORT}. Aștept răspuns (Resetează placa manual dacă nu pornește)...")
        
        # Așteptăm orice date (pentru debug-ul pe care l-am adăugat în firmware)
        start_time = time.time()
        while time.time() - start_time < 5:
            line = ser.readline().decode(errors='ignore')
            if "DEBUG" in line:
                print(f"Mesaj boot detectat: {line.strip()}")
                break
        
        # Sincronizare SUMP (trimitere 0x00 repetată)
        ser.write(b'\x00\x00\x00\x00\x00')
        time.sleep(0.1)
        ser.reset_input_buffer()

        # ID Check (Încercare repetată)
        print("Trimit cerere ID (SUMP)...")
        for _ in range(3):
            ser.write(b'\x02')
            response = ser.read(4)
            if response == b'1ALS':
                print("Dispozitiv SUMP identificat: 1ALS")
                break
            time.sleep(0.5)
        else:
            print("Eroare: Nu am primit răspuns de la Analizorul Logic.")
            ser.close()
            return

        # Scurtăm achiziția la 80 eșantioane pentru visual local
        ser.write(b'\x81\x13\x00\x00\x00') # 0x13 = 19 -> (19+1)*4 = 80 samples
        ser.write(b'\x01') # Start
        
        data = ser.read(SAMPLES)
        ser.close()

        if len(data) < SAMPLES:
            print(f"Eroare: Am primit doar {len(data)} eșantioane.")
            return

        print("Achiziție finalizată. Randare grafic...")
        render_logic(data)

    except Exception as e:
        print(f"Eroare Serial: {e}")

def render_logic(data):
    # Canale
    channels = ["GPIO 5 (SDA)", "GPIO 6 (SCL)", "GPIO 7", "GPIO 8"]
    output = []
    
    output.append("# [Rezultat Captură] Analizor Logic ESP32-C3\n")
    output.append("| Canal | Waveform |")
    output.append("| :--- | :--- |")

    for bit in range(4):
        line = f"| **{channels[bit]}** | `"
        for byte in data:
            # Bitul canalului
            is_high = (byte >> bit) & 0x01
            line += "¯" if is_high else "_"
        line += "` |"
        output.append(line)

    with open("logic_capture.md", "w", encoding="utf-8") as f:
        f.write("\n".join(output))
    
    print("Raport generat: logic_capture.md")

if __name__ == "__main__":
    capture()
