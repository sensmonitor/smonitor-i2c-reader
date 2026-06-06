# Installation and Usage

## Prerequisites

- ESP-IDF 5.3 or newer;
- C++17 support from the ESP-IDF toolchain;
- I2C pull-up resistors appropriate for the bus and voltage;
- `espressif/i2c_bus` 1.x, minimum 1.2.0, declared automatically in
  `idf_component.yml`. The example has been verified with version 1.5.1.

The internal ESP32 pull-up resistors are useful for initial testing but are
often insufficient for a reliable production bus. Use external pull-up
resistors selected for the bus capacitance, speed, voltage, and module
datasheet.

## Local Development

In the ESP-IDF project's root `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "/home/ivan/Laradock/Code/smonitor-i2c"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sensor_test)
```

The Component Manager reads the library dependency and downloads
`espressif/i2c_bus`.

## Embedding a Profile

In `main/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES smonitor-i2c
    EMBED_TXTFILES "profile.json"
)
```

In the application code:

```c
extern const char profile_start[] asm("_binary_profile_json_start");
extern const char profile_end[] asm("_binary_profile_json_end");
```

`profile_start` is null-terminated when `EMBED_TXTFILES` is used, so it can be
passed directly:

```c
smonitor_i2c_handle_t handle = NULL;
ESP_ERROR_CHECK(smonitor_i2c_create_from_json(profile_start, &handle));
```

A profile may also come from a server, NVS, or a filesystem. The complete JSON
string only needs to remain available during `create_from_json`; the input
buffer is no longer needed afterwards.

## One Read Cycle

```c
smonitor_i2c_sample_t samples[16] = {0};
size_t count = 0;

esp_err_t result =
    smonitor_i2c_read_all(handle, samples, 16, &count);

for (size_t i = 0; i < count; ++i) {
    if (!samples[i].valid) {
        ESP_LOGW("APP", "%s failed: %s",
                 samples[i].output_id,
                 esp_err_to_name(samples[i].error));
        continue;
    }
    ESP_LOGI("APP", "%s = %.3f %s",
             samples[i].output_id,
             samples[i].value,
             samples[i].unit);
}
```

The cycle return value does not replace `sample.valid`. If one declarative
device fails, the remaining devices are still read and invalid samples are
generated for the failed device's outputs. A native decoder decides whether
an error produces invalid samples or only an error code.

## Multiple Devices

All entries in the `devices` array share one bus:

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
      "id": "light",
      "address": "0x23",
      "...": "BH1750 profile fields"
    },
    {
      "id": "adc",
      "address": "0x48",
      "frequency_hz": 100000,
      "...": "ADS1115 profile fields"
    }
  ]
}
```

The optional device-level `frequency_hz` is passed to
`i2c_bus_device_create`. A value of `0` uses the active bus speed.

## Shutdown

```c
smonitor_i2c_destroy(&handle);
```

The function releases native state, device handles, and the bus handle. After
the call, `handle == NULL`.

## Quickly Testing a Profile

1. Copy the desired file from `profiles/` to
   `examples/basic/main/profile.json`.
2. Adjust `sda_pin`, `scl_pin`, the address, and the measurement range.
3. Run:

```bash
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

For a BME280 module responding at `0x77`, change only:

```json
"address": "0x77"
```
