# Native Decoders

## When to Use a Native Decoder

A native decoder is appropriate when a device requires:

- complex factory compensation;
- algorithmic state across measurements;
- vendor firmware or a vendor API;
- large FIFO or streaming processing;
- a calibration flow with branching;
- precisely defined 64-bit arithmetic.

## Built-in Bosch Decoder

Names:

```text
bosch_bme280
bosch_bmp280
```

The decoder:

1. verifies chip ID `0x60` or `0x58`;
2. performs a soft reset;
3. waits for NVM calibration copying to finish;
4. reads T/P/H factory coefficients;
5. configures normal mode and x16 oversampling;
6. reads one block starting at register `0xF7`;
7. calculates every output from the same sample.

This prevents temperature, pressure, and humidity from coming from different
measurement cycles.

## Registering an External Native Decoder

```c
static esp_err_t my_create(const smonitor_i2c_native_io_t *io,
                           const char *device_json,
                           void **state);

static esp_err_t my_read(void *state,
                         smonitor_i2c_sample_t *samples,
                         size_t capacity,
                         size_t *count);

static void my_destroy(void *state);

static const smonitor_i2c_native_driver_t driver = {
    .create = my_create,
    .read = my_read,
    .destroy = my_destroy,
};

void register_drivers(void)
{
    ESP_ERROR_CHECK(
        smonitor_i2c_register_native_driver("vl53l0x", &driver));
}
```

Registration must occur before `smonitor_i2c_create_from_json`.

`smonitor_i2c_native_io_t` gives the native module:

- `read(context, reg, reg_bits, data, length)`;
- `write(context, reg, reg_bits, data, length)`;
- `delay_ms(milliseconds)`.

The native module therefore does not directly depend on `i2c_bus.h` and can
be unit-tested with a mock transport.

## VL53L0X

`profiles/vl53l0x.json` intentionally expects a registered `vl53l0x` native
driver. The recommended adapter should use the complete ST VL53L0X API or a
verified ESP-IDF component and map only the result into
`smonitor_i2c_sample_t`.

Do not copy an arbitrary "magic register" initialization sequence into JSON.
Such a configuration is difficult to review, may depend on a particular
module or API version, and can easily omit required calibration.
