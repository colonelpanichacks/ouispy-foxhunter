# OUI-SPY FOXHUNTER

![OUI-SPY](ouispy.png)

Precision BLE proximity tracker for radio direction finding with real-time audio feedback.

## Hardware

**OUI-SPY Board** - Available on [ColonelPanic.tech](https://colonelpanic.tech)
- ESP32-S3 based tracking system
- Integrated buzzer and power management
- Ready-to-use, no additional components required

**Alternative:** Standard ESP32-S3 with external buzzer on GPIO3

**Xiao ESP32-S3:** Onboard LED on GPIO21 (inverted logic)

## Quick Start

1. **Power on device** - Creates WiFi AP `snoopuntothem` (password: `astheysnoopuntous`)
2. **Connect and configure** - Navigate to `http://192.168.4.1`
3. **Enter target MAC** - Format: `XX:XX:XX:XX:XX:XX`
4. **Save configuration** - Device switches to tracking mode

## Features

### Tracking System
- Single MAC address targeting
- Real-time RSSI-based proximity beeping
- Persistent configuration storage
- Automatic mode switching

### Audio & Visual Feedback
- **Buzzer Control:** Independent enable/disable toggle
- **LED Control:** Orange LED blinks with same cadence as buzzer
- **Startup beep:** Power-on confirmation with LED flash
- **Ready signal:** Two ascending beeps with LED synchronization
- **Target acquired:** Three same-tone beeps with LED flashing
- **Proximity feedback:** Variable frequency based on signal strength
- **Persistent Settings:** Buzzer/LED preferences survive reboots

### Ultra-Reactive Proximity Indicators
- **10-25ms intervals:** INSANE SPEED (-35 to -25 dBm) - MACHINE GUN
- **25-75ms intervals:** ULTRA FAST (-45 to -35 dBm) - LIGHTNING
- **75-150ms intervals:** VERY FAST (-55 to -45 dBm) - RAPID FIRE
- **150-250ms intervals:** FAST (-65 to -55 dBm) - QUICK
- **250-400ms intervals:** MEDIUM (-75 to -65 dBm) - MODERATE
- **400-600ms intervals:** SLOW (-85 to -75 dBm) - STEADY
- **800ms intervals:** VERY SLOW (-85+ dBm) - PAINFULLY SLOW

## Installation

### PlatformIO
```bash
cd ouibuzzer-main/foxhunt
python3 -m platformio run --target upload
```

### Dependencies
- NimBLE-Arduino ^1.4.0
- ESP Async WebServer ^3.0.6
- Preferences ^2.0.0

## Operation

### Setup Process
1. Device starts in configuration mode
2. Connect to `snoopuntothem` WiFi network
3. Access web portal at `http://192.168.4.1`
4. Enter target MAC address
5. Configure audio/visual settings (buzzer & LED toggles)
6. Configuration saves automatically with persistent settings

### Tracking Mode
1. BLE scanning starts with ready signal (audio + LED)
2. Target acquisition triggers three beeps with LED flashing
3. Proximity feedback indicates distance (buzzer + LED sync)
4. Use directional antenna for triangulation
5. LED turns off instantly when target lost

### Technical Details
- **Scan parameters:** 16ms intervals, 95% duty cycle
- **Detection timeout:** Target lost after 5 seconds
- **Range:** Varies with antenna and environment
- **Power:** Maximum BLE transmission power

## Web Interface

Clean, professional configuration portal with:
- MAC address validation
- **Audio & Visual Settings:** Independent buzzer and LED toggles
- Configuration confirmation screen
- 5-second automatic mode switch
- Device reset functionality
- **Persistent Settings:** Preferences survive reboots

## Serial Output

```
==============================
=== STARTING FOXHUNT TRACKING MODE ===
Target MAC: 5e:9f:f9:eb:2e:23
Buzzer enabled: Yes
LED enabled: Yes
==============================

FOXHUNT REALTIME tracking started!
TARGET ACQUIRED!
RSSI: -45 dBm
```

## Troubleshooting

**No WiFi AP:** Wait 30 seconds after power-on
**No web portal:** Ensure connected to `snoopuntothem`, disable mobile data
**No target detection:** Verify device is advertising BLE
**Intermittent beeping:** Target may use MAC randomization

## Applications

- Radio direction finding competitions
- Asset tracking and recovery
- Security device location
- RF signal analysis and mapping

## Technical Specifications

- **Platform:** ESP32-S3
- **BLE scanning:** Continuous, aggressive parameters
- **Audio system:** PWM-based buzzer control (GPIO3)
- **Visual system:** Onboard LED control (GPIO21, inverted logic)
- **Storage:** NVS flash memory with persistent settings
- **Power optimization:** Dual-core processing
- **Response time:** Ultra-reactive with instant LED feedback

## OUI-SPY Firmware Ecosystem

OUI-SPY Foxhunter is part of the OUI-SPY firmware family:

| Firmware | Description | Board |
|----------|-------------|-------|
| **[OUI-SPY Unified](https://github.com/colonelpanichacks/oui-spy-unified-blue)** | Multi-mode BLE + WiFi detector | ESP32-S3 / ESP32-C5 |
| **[OUI-SPY Detector](https://github.com/colonelpanichacks/ouispy-detector)** | Targeted BLE scanner with OUI filtering | ESP32-S3 |
| **[OUI-SPY Foxhunter](https://github.com/colonelpanichacks/ouispy-foxhunter)** | RSSI-based proximity tracker (this project) | ESP32-S3 |
| **[Flock You](https://github.com/colonelpanichacks/flock-you)** | Flock Safety / Raven surveillance detection | ESP32-S3 |
| **[Sky-Spy](https://github.com/colonelpanichacks/Sky-Spy)** | Drone Remote ID detection | ESP32-S3 / ESP32-C5 |
| **[Remote-ID-Spoofer](https://github.com/colonelpanichacks/Remote-ID-Spoofer)** | WiFi Remote ID spoofer & simulator with swarm mode | ESP32-S3 |
| **[OUI-SPY UniPwn](https://github.com/colonelpanichacks/Oui-Spy-UniPwn)** | Unitree robot exploitation system | ESP32-S3 |

## License

Open source project. Modifications welcome.
