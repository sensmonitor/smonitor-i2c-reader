#include "smonitor_i2c_internal.hpp"

#include <map>
#include <mutex>

namespace {

std::map<std::string, smonitor_i2c_native_driver_t> &registry()
{
    static std::map<std::string, smonitor_i2c_native_driver_t> drivers;
    return drivers;
}

std::mutex &registry_mutex()
{
    static std::mutex mutex;
    return mutex;
}

}  // namespace

namespace smonitor::i2c {

const smonitor_i2c_native_driver_t *find_native_driver(const std::string &name)
{
    if (name == "bosch_bmp280" || name == "bosch_bme280") {
        return bosch_native_driver();
    }

    std::lock_guard<std::mutex> lock(registry_mutex());
    auto iterator = registry().find(name);
    return iterator == registry().end() ? nullptr : &iterator->second;
}

}  // namespace smonitor::i2c

extern "C" esp_err_t smonitor_i2c_register_native_driver(
    const char *name,
    const smonitor_i2c_native_driver_t *driver)
{
    if (name == nullptr || name[0] == '\0' || driver == nullptr ||
        driver->create == nullptr || driver->read == nullptr ||
        driver->destroy == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(registry_mutex());
    registry()[name] = *driver;
    return ESP_OK;
}
