# Ten Sensor Profiles

Each file in `profiles/` is a complete runtime document. For a quick test,
change the pins and address, then embed it as `profile.json`.

## BME280

File: `profiles/bme280.json`

```json
"decoder": {
  "type": "native",
  "name": "bosch_bme280",
  "outputs": [
    {"id": "temperature", "type": "temperature", "unit": "celsius"},
    {"id": "pressure", "type": "pressure", "unit": "hpa"},
    {"id": "humidity", "type": "humidity", "unit": "percent_rh"}
  ]
}
```

Typical addresses are `0x76` and `0x77`.

## BMP280

File: `profiles/bmp280.json`

Uses the same Bosch native core without the humidity output. Typical
addresses are `0x76` and `0x77`.

## MPU6050

File: `profiles/mpu6050.json`

The profile wakes the device, selects +/-2 g and +/-250 degrees per second,
then reads one 14-byte block from `0x3B`. Outputs are accelerometer XYZ,
temperature, and gyroscope XYZ.

The alternative address with AD0 high is `0x69`. For another full-scale
range, update both the configuration register and `scale`.

## BH1750

File: `profiles/bh1750.json`

Sends power-on and continuous high-resolution commands. The result is:

```text
lux = raw / 1.2
```

Typical addresses are `0x23` and `0x5C`.

## ADS1115

File: `profiles/ads1115.json`

The example measures single-ended AIN0 in single-shot mode at +/-4.096 V:

```text
volts = signed_raw * 0.000125
```

For another MUX or PGA selection, update both the configuration bytes and the
scale. Typical addresses are `0x48` through `0x4B`.

## SHT31

File: `profiles/sht31.json`

Uses the high-repeatability, clock-stretch-disabled command, reads six bytes,
and verifies both CRC-8 bytes. Typical addresses are `0x44` and `0x45`.

## AHT20

File: `profiles/aht20.json`

Performs initialization, triggers a measurement, polls the busy status, and
decodes two 20-bit fields. The typical address is `0x38`.

## QMC5883L

File: `profiles/qmc5883l.json`

The example uses continuous mode and reads little-endian signed XYZ values.
QMC5883L is not register-compatible with HMC5883L; changing only the address
does not make this profile suitable for HMC5883L.

## INA219

File: `profiles/ina219.json`

The initial profile returns:

- shunt voltage;
- bus voltage.

Current and power measurements require the shunt resistor value, a selected
current LSB, and a calibration register value. Create a project-specific
profile for those values instead of assuming universal calibration.

## VL53L0X

File: `profiles/vl53l0x.json`

The JSON defines the address and standard distance output, but requires a
native driver registered as `vl53l0x`. See `native-drivers.md`.

## Combining Profiles

Keep one `bus` object and merge the objects from the individual `devices`
arrays:

```json
{
  "schema_version": 1,
  "bus": {
    "port": 0,
    "sda_pin": 21,
    "scl_pin": 22,
    "frequency_hz": 400000
  },
  "devices": [
    {
      "id": "environment",
      "address": "0x76",
      "decoder": {
        "type": "native",
        "name": "bosch_bme280",
        "outputs": [
          {"id": "temperature", "type": "temperature", "unit": "celsius"},
          {"id": "pressure", "type": "pressure", "unit": "hpa"},
          {"id": "humidity", "type": "humidity", "unit": "percent_rh"}
        ]
      }
    },
    {
      "id": "light",
      "address": "0x23",
      "initialize": [
        {"op": "write", "data": ["0x01"]},
        {"op": "write", "data": ["0x10"]},
        {"op": "delay", "milliseconds": 180}
      ],
      "measurement": {
        "actions": [
          {"op": "read", "length": 2, "destination": "sample"}
        ]
      },
      "decoder": {
        "type": "declarative",
        "outputs": [
          {"id": "illuminance", "type": "illuminance", "unit": "lux", "source": "sample", "width": 2, "scale": 0.8333333333333334}
        ]
      }
    }
  ]
}
```
