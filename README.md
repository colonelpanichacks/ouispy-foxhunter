# OUI-SPY FOXHUNT

A real-time BLE proximity tracker for directional antenna foxhunting using the Seeed Studio Xiao ESP32 S3.

## Overview

OUI-SPY FOXHUNT is a specialized variant of the OUI-SPY project designed for radio direction finding (foxhunting) using Bluetooth Low Energy signals. It tracks a single target MAC address and provides real-time audio feedback based on signal strength (RSSI) to help locate the target device.

## Hardware Requirements

- **Seeed Studio Xiao ESP32 S3** - Main microcontroller
- **Buzzer** - Connected to GPIO3 for audio feedback
- **Directional BLE antenna** - For improved range and directionality
- **Power source** - USB-C or battery pack

## Features

### Core Functionality
- **Single MAC address tracking** - Focus on one specific target
- **Real-time RSSI-based proximity beeping** - Audio feedback indicates distance
- **Web-based configuration** - Easy target setup via WiFi AP
- **Stealth mode** - Randomized MAC address on boot
- **Aggressive BLE scanning** - Maximum sensitivity for weak signals

### Audio Feedback System
- **Startup beep** - Single beep on power-on
- **Ready signal** - Two ascending melodic beeps when scanning starts
- **Target acquired** - Three same-tone beeps on first detection
- **Proximity beeps** - Variable frequency based on signal strength:
  - 5 seconds: Very weak signal (-80 dBm and below)
  - 4.5-4 seconds: Weak signal (-80 to -70 dBm)
  - 4-3 seconds: Medium-weak signal (-70 to -60 dBm)
  - 3-2 seconds: Medium signal (-60 to -50 dBm)
  - 2-1 seconds: Strong signal (-50 to -40 dBm)
  - 1-0.1 seconds: Very strong signal (-40 to -30 dBm)

### Web Interface
- **Access Point**: `snoopuntothem`
- **Password**: `astheysnoopuntous`
- **Portal URL**: `http://192.168.4.1`
- **Features**:
  - Single MAC address input with validation
  - Random MAC placeholder generation
  - Clear configuration option
  - Device reset functionality
  - Lime green ASCII art background
  - Dark mode interface

## Installation

### PlatformIO Setup
1. Clone the repository
2. Navigate to the `foxhunt` directory
3. Install dependencies:
   ```bash
   platformio lib install
   ```
4. Build and upload:
   ```bash
   platformio run --target upload
   ```

### Dependencies
- NimBLE-Arduino ^1.4.0
- ESP Async WebServer ^3.0.6
- Preferences ^2.0.0

## Usage

### Initial Setup
1. Power on the device
2. Wait for the startup beep
3. Connect to WiFi network `snoopuntothem` (password: `astheysnoopuntous`)
4. Open browser to `http://192.168.4.1`
5. Enter target MAC address in format `XX:XX:XX:XX:XX:XX`
6. Click "Save Configuration"

### Foxhunting Operation
1. Device will play two ascending beeps when scanning starts
2. When target is first detected, three same-tone beeps will play
3. Proximity beeps will continue based on signal strength
4. Use directional antenna to triangulate target location
5. Beep frequency increases as you get closer to target

### Configuration Timeout
- Device stays in config mode while clients are connected to AP
- Automatically switches to tracking mode after 20 seconds if no activity and no connected clients
- Saved configurations persist across reboots

## Technical Details

### BLE Scanning Parameters
- **Scan Interval**: 16ms (maximum aggressive)
- **Scan Window**: 15ms (95% duty cycle)
- **Scan Type**: Active scanning
- **Continuous**: No timeout, runs indefinitely

### Real-time Proximity Logic
- Only beeps when target data is fresh (within 3 seconds)
- Stops beeping if target data becomes stale
- Target considered "lost" after 10 seconds of no detections
- Automatic first detection reset for re-acquisition

### Memory Management
- NVS storage for persistent configuration
- Efficient string handling for MAC addresses
- Minimal memory footprint for continuous operation

## Troubleshooting

### Target Not Detected
- Ensure target device is advertising (not in sleep mode)
- Check if target uses MAC address randomization
- Verify MAC address format is correct
- Some devices stop advertising after initial discovery

### Weak Signal Reception
- Use directional antenna for better range
- Check antenna connection and orientation
- Consider environmental interference (walls, metal objects)
- Target device may have low transmission power

### Configuration Issues
- Ensure correct WiFi credentials
- Check if device MAC randomization is working
- Verify web portal accessibility at 192.168.4.1
- Try device reset if configuration appears corrupted

## Development

### Build Configuration
- Platform: ESP32 (espressif32 ^6.3.0)
- Board: seeed_xiao_esp32s3
- Framework: Arduino
- Upload Speed: 115200 baud
- Monitor Speed: 115200 baud

### Key Components
- **BLE Scanner**: NimBLE-based continuous scanning
- **Web Server**: Async HTTP server for configuration
- **Audio System**: PWM-based buzzer control
- **Storage**: NVS for persistent settings

## License

This project is part of the OUI-SPY suite. Refer to the main project license for usage terms.

## Contributing

Contributions welcome! Please ensure:
- No emoji usage in code or documentation
- Maintain aggressive scanning performance
- Preserve real-time proximity feedback
- Test with various BLE devices
- Document any hardware modifications

## Acknowledgments

Built on the foundation of the original OUI-SPY project with specialized enhancements for radio direction finding applications. 