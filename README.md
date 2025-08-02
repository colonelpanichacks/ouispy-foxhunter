# OUI-SPY FOXHUNT

![OUI-SPY](ouispy.png)

Precision BLE proximity tracker for radio direction finding with real-time audio feedback.

## Hardware

**OUI-SPY Board** - Available on [Tindie](https://www.tindie.com)
- ESP32-S3 based tracking system
- Integrated buzzer and power management
- Ready-to-use, no additional components required

**Alternative:** Standard ESP32-S3 with external buzzer on GPIO3

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

### Audio Feedback
- Startup beep: Power-on confirmation
- Ready signal: Two ascending beeps
- Target acquired: Three same-tone beeps
- Proximity beeps: Variable frequency based on signal strength

### Proximity Indicators
- **100ms intervals:** Very close (-30 to -40 dBm)
- **1-2 second intervals:** Close (-40 to -60 dBm)
- **3-4 second intervals:** Medium (-60 to -80 dBm)
- **5 second intervals:** Far (-80+ dBm)

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
5. Configuration saves automatically

### Tracking Mode
1. BLE scanning starts with ready signal
2. Target acquisition triggers three beeps
3. Proximity beeps indicate distance
4. Use directional antenna for triangulation

### Technical Details
- **Scan parameters:** 16ms intervals, 95% duty cycle
- **Detection timeout:** Target lost after 5 seconds
- **Range:** Varies with antenna and environment
- **Power:** Maximum BLE transmission power

## Web Interface

Clean, professional configuration portal with:
- MAC address validation
- Configuration confirmation screen
- 5-second automatic mode switch
- Device reset functionality

## Serial Output

```
==============================
=== STARTING FOXHUNT TRACKING MODE ===
Target MAC: 5e:9f:f9:eb:2e:23
==============================

FOXHUNT REALTIME tracking started!
TARGET ACQUIRED!
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
- **Audio system:** PWM-based buzzer control
- **Storage:** NVS flash memory
- **Power optimization:** Dual-core processing

## License

Open source project. Modifications welcome. 
