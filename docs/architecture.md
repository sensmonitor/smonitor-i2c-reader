# Architecture

## Layers

```text
application / server JSON
            |
            v
public smonitor_i2c API
            |
            +---- JSON parser and validator
            |
            +---- declarative action engine
            |
            +---- native decoder registry
            |
            v
thin smonitor transport adapter
            |
            v
espressif/i2c_bus
            |
            v
ESP-IDF driver/i2c_master.h
```

The transport adapter does not implement the I2C protocol. It maps three
access modes:

```text
register_bits = 0
    -> i2c_bus_read/write_bytes(..., NULL_I2C_MEM_ADDR, ...)

register_bits = 8
    -> i2c_bus_read/write_bytes(...)

register_bits = 16
    -> i2c_bus_read/write_reg16(...)
```

## Why Not Make Everything Native?

Simple sensors share the same pattern:

```text
write configuration
wait or poll
read bytes
extract field
linear conversion
```

A dedicated C driver for each of them would mostly repeat the same code. A
JSON profile is easier to inspect and can be changed without modifying the
library.

## Why Not Put Everything in JSON?

BME280/BMP280 compensation uses:

- factory coefficients with different signed types;
- 32-bit and 64-bit integer arithmetic;
- the intermediate `t_fine` value;
- pressure and humidity dependencies on temperature;
- controlled shift and clamp operations.

VL53L0X requires a long initialization sequence, SPAD and timing
configuration, and calibration. Describing such algorithms in JSON would
require a small programming-language interpreter. That would increase RAM and
flash use, test surface, and security risk.

The library therefore supports two decoder types:

```text
declarative
    generic actions + fixed safe transforms

native
    tested C/C++ implementation behind the same sample API
```

## Lifecycle

`smonitor_i2c_create_from_json`:

1. parses the JSON document;
2. creates one `i2c_bus`;
3. creates a device handle for every device;
4. parses actions and outputs;
5. creates native state when required;
6. executes `initialize` actions for declarative devices.

`smonitor_i2c_read_all`:

1. visits devices in configuration order;
2. executes measurement actions or the native `read` function;
3. decodes outputs;
4. returns a common sample array with validity and error status.

`smonitor_i2c_destroy` releases resources in reverse order.

## Memory

The JSON text is not retained after initialization. The runtime stores parsed
actions, output descriptors, and read buffers. A declarative transfer is
limited to 64 bytes.

Use a native module for devices with large firmware blobs, FIFO streams, or
complex persistent state.

## Thread Safety

`i2c_bus` provides a bus mutex. However, one `smonitor_i2c_handle_t` should not
be called concurrently from multiple tasks without an application-level
mutex, because the runtime reuses its internal read buffers.
