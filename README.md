# TDAI-2170 — ESP32-S2 WiFi-til-Serial Interface

WiFi-til-Serial adapter bygget på en **ESP32-S2 Mini**. Projektet gør det muligt at
tilgå en seriel (UART) enhed trådløst over WiFi — og udvides på sigt med en
**embedded MCP-server**, så enheden kan styres direkte fra en MCP-klient.

## Mål

- 📡 **WiFi-til-Serial bridge** — transparent bridge mellem en TCP/WiFi-forbindelse og UART.
- 🔌 **ESP32-S2 Mini** som hardwareplatform.
- 🤖 **Embedded MCP-server** — eksponér enhedens funktioner som MCP-tools, så de kan
  bruges af AI-agenter og MCP-klienter.

## Hardware

| Komponent      | Detalje                                  |
| -------------- | ---------------------------------------- |
| Mikrocontroller | ESP32-S2 Mini (Lolin/WeMos)             |
| Niveau-konverter | TTL ↔ RS232 (MAX3232-type)             |
| Stik            | RJ12 til mål-enheden                     |
| Forbindelse     | WiFi (2.4 GHz)                           |

### Pinout / forbindelser

ESP32-S2 Mini ←→ TTL↔RS232-konverter ←→ RJ12-stik:

| Board-label   | Forbindes til         |
| ------------- | --------------------- |
| VCC           | ESP32 3.3V            |
| GND (TTL)     | ESP32 GND             |
| TXD (TTL)     | ESP32 GPIO 17 (TX)    |
| RXD (TTL)     | ESP32 GPIO 18 (RX)    |
| TXD (RS232)   | RJ12 Pin 6            |
| RXD (RS232)   | RJ12 Pin 5            |
| GND (RS232)   | RJ12 Pin 4            |

> Hardwaren er bygget. ESP32 kommunikerer over UART (GPIO 17/18) til konverteren,
> som omsætter til RS232 og føres ud på RJ12.

## Funktioner (planlagt)

- [ ] WiFi-opsætning (station / access point)
- [ ] TCP-server der bridger til UART
- [ ] Konfigurerbar baudrate
- [ ] Embedded MCP-server med tools til serielt I/O
- [ ] Status-LED / fejlhåndtering

## Kom i gang

> Build-instruktioner tilføjes når toolchain er valgt (Arduino IDE / PlatformIO / ESP-IDF).

```bash
# Eksempel (PlatformIO)
pio run --target upload
```

## Status

🚧 Under udvikling.

- ✅ Hardware bygget (ESP32-S2 + TTL↔RS232 + RJ12)
- ⏳ Firmware (WiFi-bridge + MCP-server)

## Licens

Endnu ikke fastlagt.
