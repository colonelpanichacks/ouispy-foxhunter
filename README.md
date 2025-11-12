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

1. **Power on device** - Creates WiFi AP `foxhunter` (password: `foxhunter`)
2. **Connect and configure** - Navigate to `http://192.168.4.1`
3. **Scan or enter target MAC** - Use built-in BLE scanner or manual entry
4. **Optional: Alias devices** - Name your targets for easy identification
5. **Save configuration** - Device switches to tracking mode

## Features

### Tracking System
- Single MAC address targeting
- **Built-in BLE Scanner:** Discover nearby devices instantly
- **MAC Address Aliasing:** Name your devices with persistent labels
- Real-time RSSI-based proximity beeping
- Persistent configuration storage (NVS)
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
2. Connect to `foxhunter` WiFi network (password: `foxhunter`)
3. Access web portal at `http://192.168.4.1`
4. **Use BLE Scanner** or manually enter target MAC address
5. **Optional:** Assign alias to target device (persists across reboots)
6. Configure audio/visual settings (buzzer & LED toggles)
7. Configuration saves automatically with persistent settings

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
- **BLE Device Scanner:** 3-second scan with RSSI display
- **Device Organization:** Results sorted by signal strength
- **MAC Address Aliasing:** Persistent device naming with NVS storage
- **One-Click Target Selection:** Click any MAC to populate target field
- **Alias Display:** Shows `ALIAS (MAC)` format in target field
- MAC address validation
- **Audio & Visual Settings:** Independent buzzer and LED toggles
- Configuration confirmation screen
- 5-second automatic mode switch
- Device reset functionality
- **Persistent Settings:** Preferences and aliases survive reboots

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
**No web portal:** Ensure connected to `foxhunter`, disable mobile data
**No target detection:** Verify device is advertising BLE, use scanner to confirm
**Scanner shows no devices:** Ensure BLE devices are nearby and advertising
**Alias not persisting:** NVS storage saves automatically, survives reboots
**Tracking not working:** Ensure target field shows correct MAC format (extracted from alias automatically)
**Intermittent beeping:** Target may use MAC randomization

## Applications

- Radio direction finding competitions
- Asset tracking and recovery
- Security device location
- RF signal analysis and mapping

## Technical Specifications

- **Platform:** ESP32-S3
- **BLE scanning:** Continuous, aggressive parameters (3-second discovery scans)
- **Audio system:** PWM-based buzzer control (GPIO3)
- **Visual system:** Onboard LED control (GPIO21, inverted logic)
- **Storage:** NVS flash memory with persistent settings and device aliases
- **Web UI:** BLE scanner with RSSI-sorted results and one-click targeting
- **Alias System:** NVS-based persistent device naming across reboots
- **Power optimization:** Dual-core processing
- **Response time:** Ultra-reactive with instant LED feedback

## License

Open source project. Modifications welcome. 
