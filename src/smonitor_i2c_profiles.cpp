#include "smonitor_i2c.h"

#include <memory>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"

extern const char profile_ads1115_json_start[] asm(
    "_binary_ads1115_json_start");
extern const char profile_aht20_json_start[] asm(
    "_binary_aht20_json_start");
extern const char profile_bh1750_json_start[] asm(
    "_binary_bh1750_json_start");
extern const char profile_bme280_json_start[] asm(
    "_binary_bme280_json_start");
extern const char profile_bmp280_json_start[] asm(
    "_binary_bmp280_json_start");
extern const char profile_ina219_json_start[] asm(
    "_binary_ina219_json_start");
extern const char profile_mpu6050_json_start[] asm(
    "_binary_mpu6050_json_start");
extern const char profile_qmc5883l_json_start[] asm(
    "_binary_qmc5883l_json_start");
extern const char profile_sht31_json_start[] asm(
    "_binary_sht31_json_start");
extern const char profile_vl53l0x_json_start[] asm(
    "_binary_vl53l0x_json_start");

namespace {

constexpr const char *TAG = "smonitor_i2c_profiles";

const char *selected_profile_json()
{
#if CONFIG_SMONITOR_I2C_SENSOR_BME280
    return profile_bme280_json_start;
#elif CONFIG_SMONITOR_I2C_SENSOR_BMP280
    return profile_bmp280_json_start;
#elif CONFIG_SMONITOR_I2C_SENSOR_SHT31
    return profile_sht31_json_start;
#elif CONFIG_SMONITOR_I2C_SENSOR_AHT20
    return profile_aht20_json_start;
#elif CONFIG_SMONITOR_I2C_SENSOR_BH1750
    return profile_bh1750_json_start;
#elif CONFIG_SMONITOR_I2C_SENSOR_ADS1115
    return profile_ads1115_json_start;
#elif CONFIG_SMONITOR_I2C_SENSOR_INA219
    return profile_ina219_json_start;
#elif CONFIG_SMONITOR_I2C_SENSOR_MPU6050
    return profile_mpu6050_json_start;
#elif CONFIG_SMONITOR_I2C_SENSOR_QMC5883L
    return profile_qmc5883l_json_start;
#elif CONFIG_SMONITOR_I2C_SENSOR_VL53L0X
    return profile_vl53l0x_json_start;
#else
#error Unsupported SensMonitor I2C sensor selection
#endif
}

esp_err_t replace_number(cJSON *object, const char *name, double value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (item == nullptr) {
        item = cJSON_AddNumberToObject(object, name, value);
        return item != nullptr ? ESP_OK : ESP_ERR_NO_MEM;
    }
    if (!cJSON_IsNumber(item)) {
        cJSON *replacement = cJSON_CreateNumber(value);
        if (replacement == nullptr) {
            return ESP_ERR_NO_MEM;
        }
        return cJSON_ReplaceItemInObjectCaseSensitive(object, name, replacement)
            ? ESP_OK
            : ESP_FAIL;
    }
    cJSON_SetNumberValue(item, value);
    return ESP_OK;
}

esp_err_t replace_bool(cJSON *object, const char *name, bool value)
{
    cJSON *replacement = cJSON_CreateBool(value);
    if (replacement == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (item == nullptr) {
        return cJSON_AddItemToObject(object, name, replacement)
            ? ESP_OK
            : ESP_FAIL;
    }
    return cJSON_ReplaceItemInObjectCaseSensitive(object, name, replacement)
        ? ESP_OK
        : ESP_FAIL;
}

esp_err_t apply_profile_config(cJSON *root,
                               const smonitor_i2c_profile_config_t *config)
{
    cJSON *bus = cJSON_GetObjectItemCaseSensitive(root, "bus");
    ESP_RETURN_ON_FALSE(cJSON_IsObject(bus), ESP_ERR_INVALID_ARG, TAG,
                        "Selected profile does not contain a bus object");

    ESP_RETURN_ON_ERROR(replace_number(bus, "port", config->port), TAG,
                        "Failed to override I2C port");
    ESP_RETURN_ON_ERROR(replace_number(bus, "sda_pin", config->sda_pin), TAG,
                        "Failed to override I2C SDA pin");
    ESP_RETURN_ON_ERROR(replace_number(bus, "scl_pin", config->scl_pin), TAG,
                        "Failed to override I2C SCL pin");
    ESP_RETURN_ON_ERROR(
        replace_number(bus, "frequency_hz", config->frequency_hz), TAG,
        "Failed to override I2C frequency");
    ESP_RETURN_ON_ERROR(
        replace_bool(bus, "internal_pullups", config->internal_pullups), TAG,
        "Failed to override I2C pullup setting");

    cJSON *devices = cJSON_GetObjectItemCaseSensitive(root, "devices");
    ESP_RETURN_ON_FALSE(cJSON_IsArray(devices), ESP_ERR_INVALID_ARG, TAG,
                        "Selected profile does not contain a devices array");
    cJSON *device = cJSON_GetArrayItem(devices, 0);
    ESP_RETURN_ON_FALSE(cJSON_IsObject(device), ESP_ERR_INVALID_ARG, TAG,
                        "Selected profile does not contain a device object");

    ESP_RETURN_ON_ERROR(
        replace_number(device, "address", config->device_address), TAG,
        "Failed to override I2C device address");

    return ESP_OK;
}

} // namespace

extern "C" const char *smonitor_i2c_selected_sensor_name(void)
{
#if CONFIG_SMONITOR_I2C_SENSOR_BME280
    return "BME280";
#elif CONFIG_SMONITOR_I2C_SENSOR_BMP280
    return "BMP280";
#elif CONFIG_SMONITOR_I2C_SENSOR_SHT31
    return "SHT31";
#elif CONFIG_SMONITOR_I2C_SENSOR_AHT20
    return "AHT20";
#elif CONFIG_SMONITOR_I2C_SENSOR_BH1750
    return "BH1750";
#elif CONFIG_SMONITOR_I2C_SENSOR_ADS1115
    return "ADS1115";
#elif CONFIG_SMONITOR_I2C_SENSOR_INA219
    return "INA219";
#elif CONFIG_SMONITOR_I2C_SENSOR_MPU6050
    return "MPU6050";
#elif CONFIG_SMONITOR_I2C_SENSOR_QMC5883L
    return "QMC5883L";
#elif CONFIG_SMONITOR_I2C_SENSOR_VL53L0X
    return "VL53L0X";
#else
#error Unsupported SensMonitor I2C sensor selection
#endif
}

extern "C" esp_err_t smonitor_i2c_create_selected_profile(
    const smonitor_i2c_profile_config_t *config,
    smonitor_i2c_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(config != nullptr, ESP_ERR_INVALID_ARG, TAG,
                        "Profile config is required");
    ESP_RETURN_ON_FALSE(out_handle != nullptr, ESP_ERR_INVALID_ARG, TAG,
                        "Output handle is required");

    using JsonRoot = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
    JsonRoot root(cJSON_Parse(selected_profile_json()), cJSON_Delete);
    ESP_RETURN_ON_FALSE(root != nullptr, ESP_ERR_INVALID_ARG, TAG,
                        "Failed to parse selected sensor profile");

    ESP_RETURN_ON_ERROR(apply_profile_config(root.get(), config), TAG,
                        "Failed to apply board I2C configuration");

    using JsonText = std::unique_ptr<char, decltype(&cJSON_free)>;
    JsonText json(cJSON_PrintUnformatted(root.get()), cJSON_free);
    ESP_RETURN_ON_FALSE(json != nullptr, ESP_ERR_NO_MEM, TAG,
                        "Failed to serialize selected sensor profile");

    return smonitor_i2c_create_from_json(json.get(), out_handle);
}
