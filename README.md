# SensMonitor I2C Reader

`smonitor-i2c-reader` is an ESP-IDF component for configurable I2C sensor reading.
It uses the official Espressif `i2c_bus` component as its transport layer and
adds:

- JSON configuration for buses, devices, and outputs;
- generic `write`, `read`, `delay`, `poll`, and `crc8` steps;
- endian, signed integer, and bit-field decoding;
- output `scale`, `offset`, and clamping;
- a standard `smonitor_i2c_sample_t` result;
- a native decoder registry for complex sensors;
- a built-in BME280/BMP280 Bosch decoder.

The library does not implement START/STOP, ACK/NACK, or the ESP32 I2C driver.
The call stack is:

```text
smonitor-i2c
    -> espressif/i2c_bus
        -> ESP-IDF driver/i2c_master.h
            -> ESP32 I2C hardware
```

## Sensor Status

| Sensor | Model | Status |
|---|---|---|
| BME280 | native | Built-in decoder, T/P/H burst reading |
| BMP280 | native | Built-in decoder, T/P burst reading |
| MPU6050 | declarative | Raw accelerometer, gyroscope, and temperature |
| BH1750 | declarative | Continuous high-resolution mode |
| ADS1115 | declarative | AIN0 single-shot, +/-4.096 V |
| SHT31 | declarative | High repeatability with CRC-8 |
| AHT20 | declarative | Trigger, busy polling, and T/H decoding |
| QMC5883L | declarative | XYZ magnetic field |
| INA219 | declarative | Bus and shunt voltage |
| VL53L0X | native extension | Profile included; vendor/native adapter required |

The profiles are readable starting configurations. Always verify the address,
range, gain, and operating mode against the exact module before production
use.

## Quick Start

Add the component to the application's `main/idf_component.yml`:

```yaml
dependencies:
  smonitor_i2c:
    git: https://github.com/sensmonitor/smonitor-i2c-reader
    version: "v0.1.0"
```

For local development, use `EXTRA_COMPONENT_DIRS`:

```cmake
set(EXTRA_COMPONENT_DIRS "~/Code/smonitor-i2c-reader")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_sensor_app)
```

Minimal example:

```c
#include "esp_log.h"
#include "smonitor_i2c.h"

extern const char profile_json[] asm("_binary_profile_json_start");

void app_main(void)
{
    smonitor_i2c_handle_t reader = NULL;
    ESP_ERROR_CHECK(smonitor_i2c_create_from_json(profile_json, &reader));

    while (true) {
        smonitor_i2c_sample_t samples[16];
        size_t count = 0;
        esp_err_t result =
            smonitor_i2c_read_all(reader, samples, 16, &count);

        for (size_t i = 0; i < count; ++i) {
            ESP_LOGI("sensor", "%s/%s = %.3f %s valid=%d error=%s",
                     samples[i].device_id,
                     samples[i].output_id,
                     samples[i].value,
                     samples[i].unit,
                     samples[i].valid,
                     esp_err_to_name(samples[i].error));
        }

        if (result != ESP_OK) {
            ESP_LOGW("sensor", "Read cycle: %s", esp_err_to_name(result));
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

A complete project is available in [`examples/basic`](examples/basic), and
ready-to-use JSON documents are available in [`profiles`](profiles).

## Documentation

- [Installation and usage](docs/getting-started.md)
- [JSON format](docs/json-format.md)
- [Ten sensor profiles](docs/sensor-profiles.md)
- [Architecture](docs/architecture.md)
- [Native decoders](docs/native-drivers.md)
- [Formal JSON Schema](schema/smonitor-i2c.schema.json)

## Current Limitations

- One runtime currently creates one I2C bus.
- A profile is parsed during initialization and stored as internal C++
  structures.
- Declarative mathematics is intentionally limited to safe primitives; it is
  not an arbitrary expression interpreter.
- VL53L0X requires an application/native adapter to the complete ST API.
- The MPU6050 profile does not include DMP, FIFO, or quaternion processing.
- The INA219 profile reads voltages; current and power require a known shunt
  resistor and calibration configuration.

## License

MIT
