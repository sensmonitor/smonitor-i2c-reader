# JSON Format

## Root Object

```json
{
  "schema_version": 1,
  "bus": {},
  "devices": []
}
```

`schema_version` is required and reserved for format migrations. Runtime 0.1
expects version `1`.

## Bus

```json
{
  "port": 0,
  "sda_pin": 21,
  "scl_pin": 22,
  "frequency_hz": 400000,
  "internal_pullups": true
}
```

| Field | Meaning |
|---|---|
| `port` | ESP-IDF I2C port |
| `sda_pin` | SDA GPIO |
| `scl_pin` | SCL GPIO |
| `frequency_hz` | Default bus speed |
| `internal_pullups` | Enables internal GPIO pull-up resistors |

## Device

```json
{
  "id": "light",
  "address": "0x23",
  "frequency_hz": 100000,
  "initialize": [],
  "measurement": {
    "actions": []
  },
  "decoder": {}
}
```

Register values, masks, addresses, and bytes may be JSON numbers or strings
with a `0x` prefix.

## Operations

### Write

Without an internal register address:

```json
{"op": "write", "data": ["0x2C", "0x06"]}
```

With an 8-bit register address:

```json
{
  "op": "write",
  "register": "0x6B",
  "register_bits": 8,
  "data": ["0x00"]
}
```

Use `"register_bits": 16` for a 16-bit register address.

### Read

```json
{
  "op": "read",
  "register": "0x3B",
  "register_bits": 8,
  "length": 14,
  "destination": "sample"
}
```

An omitted `register_bits`, or a value of `0`, means a read without an
internal register address. One declarative transfer is limited to 64 bytes.

### Delay

```json
{"op": "delay", "milliseconds": 15}
```

Delay uses a FreeRTOS task delay and does not busy-wait.

### Poll

```json
{
  "op": "poll",
  "register": "0x06",
  "register_bits": 8,
  "mask": "0x01",
  "expected": "0x01",
  "timeout_ms": 100,
  "interval_ms": 5
}
```

The condition is:

```text
(read_byte & mask) == expected
```

`register_bits: 0` enables polling a device without an internal register
address.

### CRC-8

```json
{
  "op": "crc8",
  "destination": "sample",
  "offset": 0,
  "data_length": 2,
  "crc_offset": 2,
  "polynomial": "0x31",
  "initial": "0xFF"
}
```

`destination` is the name of a previously read buffer. A failed CRC stops
reading that device with `ESP_ERR_INVALID_CRC`.

## Declarative Output

```json
{
  "id": "temperature",
  "type": "temperature",
  "unit": "celsius",
  "source": "sample",
  "byte_offset": 0,
  "width": 2,
  "endianness": "big",
  "signed": true,
  "right_shift": 0,
  "mask": "0xFFFF",
  "scale": 0.01,
  "offset": -40.0,
  "clamp_min": -40.0,
  "clamp_max": 125.0,
  "invalid_raw": "0xFFFF"
}
```

Transformation order:

```text
1. read width bytes
2. apply byte order
3. right shift
4. apply mask
5. apply signed sign extension
6. value = raw * scale + offset
7. clamp
```

`width` may be between 1 and 4 bytes. This covers common 8, 12, 16, 20, 24,
and 32-bit results without an expression interpreter.

## Native Decoder

```json
{
  "decoder": {
    "type": "native",
    "name": "bosch_bme280",
    "outputs": [
      {
        "id": "temperature",
        "type": "temperature",
        "unit": "celsius",
        "scale": 1.0,
        "offset": 0.0
      }
    ]
  }
}
```

Built-in names:

- `bosch_bme280`
- `bosch_bmp280`

Other names must be registered with
`smonitor_i2c_register_native_driver`.

## User Calibration

Output-level `scale` and `offset` provide user calibration:

```text
final = decoded * scale + offset
```

This is different from factory sensor calibration. The Bosch native decoder
reads the BME280/BMP280 factory coefficients directly from the device.
