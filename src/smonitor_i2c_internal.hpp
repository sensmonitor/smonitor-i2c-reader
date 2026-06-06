#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cJSON.h"
#include "i2c_bus.h"
#include "smonitor_i2c.h"

namespace smonitor::i2c {

struct NativeBinding {
    smonitor_i2c_native_driver_t driver{};
    void *state = nullptr;
};

const smonitor_i2c_native_driver_t *find_native_driver(const std::string &name);
const smonitor_i2c_native_driver_t *bosch_native_driver();

esp_err_t transport_read(void *context, uint16_t reg, uint8_t reg_bits,
                         uint8_t *data, size_t length);
esp_err_t transport_write(void *context, uint16_t reg, uint8_t reg_bits,
                          const uint8_t *data, size_t length);
void transport_delay_ms(uint32_t milliseconds);

std::string json_string(cJSON *object, const char *name,
                        const char *fallback = "");
int json_int(cJSON *object, const char *name, int fallback = 0);
double json_double(cJSON *object, const char *name, double fallback = 0.0);
bool json_bool(cJSON *object, const char *name, bool fallback = false);
uint32_t parse_u32(cJSON *item, uint32_t fallback = 0);
void copy_text(char *destination, size_t size, const std::string &source);

}  // namespace smonitor::i2c
