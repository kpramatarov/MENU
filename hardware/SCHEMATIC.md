# ESP8266 Relay & Sensor Controller: Schematic

| File | Format |
|---|---|
| [`schematic.tex`](schematic.tex) | LaTeX / CircuiTikZ source (`pdflatex schematic.tex`) |
| [`schematic.pdf`](schematic.pdf) | Vector schematic |
| [`schematic.png`](schematic.png) | Raster preview |

![Schematic](schematic.png)

## Block diagram (Mermaid)

```mermaid
flowchart LR
    USB["J1 USB 5 V"] -->|VBUS| U1

    subgraph PWR["POWER RAILS"]
        direction TB
        R5V["+5V RAIL (Right)"]
        R3V3["+3V3 RAIL (Left)"]
        GND["COMMON GND (both − rails)"]
        C1["C1 1000 µF / 16 V<br/>+ → 5V, − → GND"]
        R5V --- C1 --- GND
    end

    subgraph U1["U1 ESP8266 (NodeMCU)"]
        VIN; V33["3V3"]; G["GND"]; A0
        D0; D1; D2; D3; D4; D5; D6; D7
    end

    VIN --> R5V
    V33 --> R3V3
    G --> GND

    subgraph DIV["VIN MONITOR"]
        R1["R1 47 kΩ"]; R2["R2 20 kΩ"]
    end
    R5V --- R1 --- A0
    A0 --- R2 --- GND

    subgraph RLY["RELAY BLOCK (5 V)"]
        K1["Relay 1"]; K2["Relay 2"]; K3["Relay 3"]; K4["Relay 4"]
    end
    D0 -->|IN| K1
    D1 -->|IN| K2
    D2 -->|IN| K3
    D3 -->|IN| K4
    R5V -->|VCC| RLY
    RLY -->|GND| GND

    subgraph SEN["SENSOR BLOCK (3.3 V)"]
        DHT["U2 DHT11"]; HR["U3 HR202 (DO)"]
    end
    DHT -->|DATA| D4
    HR -->|DO| D7
    R3V3 -->|VCC| SEN
    SEN -->|GND| GND

    subgraph BTN["INPUTS (active low, INPUT_PULLUP)"]
        SW1["SW1 Relay 1 ON"]; SW2["SW2 All OFF"]
    end
    SW1 --> D5
    SW2 --> D6
    BTN -->|T2| GND

    classDef p5 stroke:#c81e1e,stroke-width:2px
    classDef p3 stroke:#e67800,stroke-width:2px
    class R5V,RLY,C1 p5
    class R3V3,SEN p3
```

## Netlist

| Net | Connections |
|---|---|
| **+5V** | J1 VBUS → U1 VIN, C1(+), R1 pin 1, K1–K4 VCC |
| **+3V3** | U1 3V3, U2 VCC, U3 VCC |
| **GND** | U1 GND (both), C1(−), R2 pin 2, K1–K4 GND, U2 GND, U3 GND, SW1 T2, SW2 T2 |
| VSENSE | R1 pin 2, R2 pin 1, U1 A0 |
| RLY1…RLY4 | U1 D0 / D1 / D2 / D3 (GPIO16 / 5 / 4 / 0) → K1…K4 IN |
| DHT_DATA | U1 D4 (GPIO2) ↔ U2 DATA |
| HUM_DO | U3 DO → U1 D7 (GPIO13) |
| BTN_ON / BTN_OFF | SW1 T1 → U1 D5 (GPIO14), SW2 T1 → U1 D6 (GPIO12) |

## Design review notes

1. **A0 divider.** The drawing assumes a NodeMCU‑style board, which has its own 220 k / 100 k divider behind A0, so the pin accepts 0–3.2 V. With R1/R2, 5.00 V gives 1.49 V unloaded and about 1.43 V loaded by the onboard 320 kΩ. That reads as roughly 457 counts, so `Vin ≈ raw × 0.01094`. On a **bare ESP‑12 module** the ADC maxes out at **1.0 V**, and 1.49 V would be out of range. In that case use 47 k / 10 k (0.88 V).
2. **The 5 V rail is not quite 5 V.** On most NodeMCU boards VIN sits behind a Schottky diode from USB VBUS, so the rail measures about 4.6–4.8 V. This is the value the divider reports.
3. **Power budget.** Each relay coil draws about 70–90 mA. With all four on, plus the ESP8266 (up to about 300 mA peaks on Wi‑Fi TX), you get close to the 500 mA USB limit. C1 absorbs coil inrush; use a 1–2 A USB supply.
4. **Boot strapping pins.** D3 (GPIO0) and D4 (GPIO2) must be HIGH at reset. Active‑low opto relay modules and the DHT11 pull-up both keep them high, which is fine. In firmware, `digitalWrite(pin, HIGH)` **before** `pinMode(pin, OUTPUT)` so the relays don't click at boot.
5. **3.3 V GPIO → 5 V relay input.** On many opto-isolated active‑low modules, a 3.3 V "HIGH" leaves about 1.7 V across the opto LED, and the relay may not release reliably. If that happens, switch "off" by setting the pin to `INPUT` (Hi‑Z) instead of HIGH. Other options are a module with a transistor or level-shifted input, or powering the module's logic side (VCC) from 3.3 V while keeping JD‑VCC on 5 V. ESP8266 GPIOs are not officially 5 V tolerant.
6. **HR202.** HR202 is a bare resistive element. The "DO" pin implies the LM393 comparator board, which gives only a threshold (set by its trimpot). AO is left unconnected, since A0 is taken by the divider.
7. **DHT11.** A bare 4‑pin DHT11 needs a 4.7–10 kΩ pull‑up from DATA to 3V3. The 3‑pin modules already include one.
8. **Buttons.** No external resistors are needed because of INPUT_PULLUP. Debounce in software (about 30–50 ms).
