# Wemos D1 Mini Ultrasonic Distance Sensor

A configurable IoT water level monitoring system using a Wemos D1 Mini (ESP8266) and HC-SR04 ultrasonic sensor. The device provides web-based configuration, real-time water level monitoring, and ESP-NOW communication capabilities.

## Features

- **Web-based Configuration**: Easy setup through WiFi Access Point
- **Water Level Calculation**: Automatic percentage calculation based on barrel height
- **ESP-NOW Communication**: Wireless data transmission to parent devices
- **Persistent Storage**: Settings saved in EEPROM
- **Real-time Monitoring**: Live sensor readings with auto-refresh
- **LED Status Indicator**: Visual feedback for device operation
- **Configurable Parameters**: Customizable refresh rates, barrel heights, and network settings

## Hardware Requirements

### Components
- **Wemos D1 Mini** (ESP8266-based development board)
- **HC-SR04 Ultrasonic Distance Sensor**
- **Breadboard and jumper wires** (for prototyping)
- **USB cable** (for programming and power)

### Pin Connections
| Wemos D1 Mini | HC-SR04 Sensor | Description |
|---------------|----------------|-------------|
| D5 (GPIO14)   | TRIG           | Trigger pin |
| D6 (GPIO12)   | ECHO           | Echo pin    |
| 5V            | VCC            | Power supply|
| GND           | GND            | Ground      |

## Installation & Setup

### 1. Software Requirements
- **PlatformIO** (recommended) or Arduino IDE
- **ESP8266 Board Package**

### 2. Building and Uploading
```bash
# Navigate to the project directory
cd "Distanse Sensor"

# Build the project
platformio run

# Upload to device
platformio run --target upload

# Monitor serial output
platformio device monitor
```

### 3. Initial Configuration

1. **Power on the device** - it will create a WiFi Access Point
2. **Connect to WiFi** - Look for network named `WATER_SENSOR_XXXXXX` (password: `HardPassword1234`)
3. **Open web browser** - Navigate to `http://192.168.4.1`
4. **Configure settings**:
   - **Parent MAC Address**: Target device for ESP-NOW communication (default: FF:FF:FF:FF:FF:FF)
   - **Refresh Rate**: How often to read sensor (default: 5 seconds)
   - **Barrel Height**: Total height of water container in cm (default: 50 cm)
   - **LED Blinking**: Enable/disable status LED (default: enabled)
   - **WiFi SSID Prefix**: Custom prefix for Access Point name
   - **WiFi Password**: Custom password for Access Point

5. **Save configuration** - Device will reboot and start monitoring

## Configuration Options

### Web Interface Pages

#### Main Configuration Page (`/`)
- **First-time setup**: Shows configuration form
- **Configured device**: Shows current settings and sensor readings
- **Update Settings**: Modify existing configuration
- **Reset to Default**: Clear all settings

#### Sensor Monitoring Page (`/sensor`)
- **Live sensor readings** with auto-refresh
- **Water level percentage** calculation
- **Manual refresh button** for immediate readings
- **Distance and barrel height** display

#### Debug Page (`/debug`)
- **MAC address information** for ESP-NOW configuration
- **Test ESP-NOW transmission** button
- **Detailed device information**

### Configuration Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| Parent MAC | FF:FF:FF:FF:FF:FF | Target device for ESP-NOW |
| Refresh Rate | 5 seconds | Sensor reading interval |
| Barrel Height | 50 cm | Total container height |
| LED Blinking | Enabled | Status indicator |
| WiFi SSID Prefix | WATER_SENSOR_ | Access Point name prefix |
| WiFi Password | HardPassword1234 | Access Point password |

## Water Level Calculation

The system calculates water level percentage using the following formula:

```
Water Level % = ((Barrel Height - (Distance - 20cm)) / Barrel Height) × 100
```

Where:
- **Distance**: Raw sensor reading in cm
- **20cm**: Sensor mounting offset (minimum reliable measurement)
- **Barrel Height**: Total height of water container

### Example Calculation
- Barrel Height: 50 cm
- Sensor Reading: 25 cm
- Adjusted Distance: 25 - 20 = 5 cm
- Water Level: ((50 - 5) / 50) × 100 = 90%

## ESP-NOW Communication

### Data Structure
```cpp
struct Payload {
    float distanceCm;      // Raw sensor distance
    float waterLevel;      // Calculated water percentage
    float barrelHeightCm;  // Configured barrel height
};
```

### Configuration
- **Channel**: Fixed to WiFi channel 1
- **Role**: Controller (sends data to parent devices)
- **Retry Logic**: Automatic retry on transmission failure
- **MAC Address**: Uses configured parent MAC (skips broadcast FF:FF:FF:FF:FF:FF)

### Debugging ESP-NOW
- Check serial monitor for transmission status
- Use debug page to view all available MAC addresses
- Verify parent device MAC address configuration

## Troubleshooting

### Common Issues

#### 1. Sensor Reading Stuck at 20cm
**Symptoms**: Consistent readings around 20.1-20.2 cm
**Solutions**:
- Check sensor connections (TRIG/ECHO pins)
- Verify sensor mounting (no obstacles in front)
- Ensure proper power supply (5V)
- Check for loose connections

#### 2. WiFi Access Point Not Appearing
**Symptoms**: Default ESP-XXXXXX network name
**Solutions**:
- Check WiFi configuration in code
- Verify EEPROM settings
- Reset device to factory defaults

#### 3. ESP-NOW Communication Issues
**Symptoms**: No data received by parent device
**Solutions**:
- Verify parent MAC address configuration
- Check WiFi channel settings
- Ensure parent device is in ESP-NOW slave mode
- Check serial monitor for transmission errors

#### 4. Web Interface Not Loading
**Symptoms**: "File not found" or connection errors
**Solutions**:
- Verify correct IP address (192.168.4.1)
- Check WiFi connection to device
- Clear browser cache
- Try different browser

### Debug Information

#### Serial Monitor Output
The device provides detailed debug information via serial monitor:
- WiFi initialization status
- Sensor readings and calculations
- ESP-NOW transmission results
- Configuration loading/saving
- Error messages and warnings

#### MAC Address Information
- **WiFi MAC**: Used for Access Point identification
- **ESP-NOW MAC**: Used for wireless communication
- **Display MAC**: Shown on web interface for configuration

## Technical Details

### Hardware Specifications
- **Microcontroller**: ESP8266 (80MHz, 4MB Flash)
- **Sensor Range**: 2cm - 400cm
- **Operating Voltage**: 3.3V (Wemos), 5V (Sensor)
- **Current Consumption**: ~50mA (normal operation)

### Software Architecture
- **Framework**: Arduino for ESP8266
- **Libraries**: ESP8266WiFi, ESP8266WebServer, EEPROM, ESP-NOW
- **Storage**: EEPROM for configuration persistence
- **Communication**: HTTP (web interface), ESP-NOW (data transmission)

### File Structure
```
SR04M (Ultrasonic distance sensor)/
├── README.md                    # This file
└── Distanse Sensor/            # PlatformIO project
    ├── platformio.ini          # Project configuration
    ├── src/
    │   └── main.cpp           # Main application code
    ├── include/               # Header files
    └── lib/                   # Library files
```

## Development

### Building from Source
1. Clone or download the project
2. Open in PlatformIO or Arduino IDE
3. Install required libraries
4. Configure board settings
5. Build and upload

### Customization
- Modify sensor pins in `main.cpp`
- Adjust default configuration values
- Customize web interface styling
- Add additional sensor types
- Implement data logging features

## License

This project is provided as-is for educational and personal use. Feel free to modify and distribute according to your needs.

## Support

For issues and questions:
1. Check the troubleshooting section above
2. Review serial monitor output for error messages
3. Verify hardware connections and power supply
4. Test with known working components

---

**Note**: This device is designed for water level monitoring in containers. Ensure proper mounting and calibration for accurate readings. 