#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *smonitor_i2c_handle_t;

typedef struct {
    char device_id[32];
    char output_id[32];
    char type[24];
    char unit[16];
    double value;
    bool valid;
    esp_err_t error;
} smonitor_i2c_sample_t;

/**
 * Create a runtime from a JSON document.
 *
 * The JSON must contain one bus and one or more complete device profiles.
 * The input string is parsed during this call and does not need to remain
 * valid afterwards.
 */
esp_err_t smonitor_i2c_create_from_json(const char *json,
                                        smonitor_i2c_handle_t *out_handle);

typedef struct {
    int port;
    int sda_pin;
    int scl_pin;
    uint32_t frequency_hz;
    bool internal_pullups;
    uint8_t device_address;
} smonitor_i2c_profile_config_t;

/**
 * Return the sensor name selected through the smonitor-i2c-reader Kconfig.
 */
const char *smonitor_i2c_selected_sensor_name(void);

/**
 * Create a runtime using the sensor profile selected through Kconfig.
 *
 * The sensor-specific device profile lives in smonitor-i2c-reader. The caller
 * provides only the board/application-specific bus wiring and device address.
 */
esp_err_t smonitor_i2c_create_selected_profile(
    const smonitor_i2c_profile_config_t *config,
    smonitor_i2c_handle_t *out_handle);

/**
 * Read every configured device once.
 *
 * On success, out_count contains the number of produced samples. A failed
 * declarative device produces invalid samples with the corresponding ESP-IDF
 * error when enough output capacity is available. Native drivers control
 * their own error sample behavior.
 */
esp_err_t smonitor_i2c_read_all(smonitor_i2c_handle_t handle,
                                smonitor_i2c_sample_t *samples,
                                size_t capacity,
                                size_t *out_count);

/**
 * Release devices, the I2C bus, native decoder state and runtime memory.
 */
void smonitor_i2c_destroy(smonitor_i2c_handle_t *handle);

typedef struct {
    void *context;
    esp_err_t (*read)(void *context, uint16_t reg, uint8_t reg_bits,
                      uint8_t *data, size_t length);
    esp_err_t (*write)(void *context, uint16_t reg, uint8_t reg_bits,
                       const uint8_t *data, size_t length);
    void (*delay_ms)(uint32_t milliseconds);
} smonitor_i2c_native_io_t;

typedef struct {
    esp_err_t (*create)(const smonitor_i2c_native_io_t *io,
                        const char *device_json,
                        void **driver_state);
    esp_err_t (*read)(void *driver_state,
                      smonitor_i2c_sample_t *samples,
                      size_t capacity,
                      size_t *out_count);
    void (*destroy)(void *driver_state);
} smonitor_i2c_native_driver_t;

/**
 * Register an application-provided native decoder before creating a runtime.
 *
 * This is intended for complex devices such as VL53L0X where the vendor API
 * should be adapted instead of recreated as a JSON expression interpreter.
 */
esp_err_t smonitor_i2c_register_native_driver(
    const char *name,
    const smonitor_i2c_native_driver_t *driver);

#ifdef __cplusplus
}
#endif
