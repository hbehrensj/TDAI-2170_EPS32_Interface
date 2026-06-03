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
| Mikrocontroller| ESP32-S2 Mini (Lolin/WeMos)              |
| Tilslutning    | UART (TX/RX) til mål-enheden             |
| Forbindelse    | WiFi (2.4 GHz)                           |

> ⚠️ Pinout og konkrete forbindelser tilføjes når hardwaren er fastlagt.

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

🚧 Under udvikling — projektet er i opstartsfasen.

## Licens

Endnu ikke fastlagt.
