#include "smonitor_i2c_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <utility>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace smonitor::i2c {

namespace {

constexpr uint8_t kNoRegister = 0;
constexpr size_t kMaxTransferLength = 64;

struct Action {
    std::string operation;
    uint16_t reg = 0;
    uint8_t reg_bits = 0;
    std::vector<uint8_t> data;
    size_t length = 0;
    std::string destination;
    uint32_t delay_ms = 0;
    uint32_t timeout_ms = 100;
    uint32_t interval_ms = 5;
    uint32_t mask = 0xff;
    uint32_t expected = 0;
    size_t offset = 0;
    size_t data_length = 0;
    size_t crc_offset = 0;
    uint8_t polynomial = 0x31;
    uint8_t initial = 0xff;
};

struct Field {
    std::string id;
    std::string type;
    std::string unit;
    std::string source;
    size_t byte_offset = 0;
    uint8_t width = 1;
    bool little_endian = false;
    bool is_signed = false;
    uint8_t right_shift = 0;
    uint64_t mask = UINT64_MAX;
    double scale = 1.0;
    double add = 0.0;
    bool has_invalid = false;
    int64_t invalid = 0;
    double clamp_min = -INFINITY;
    double clamp_max = INFINITY;
};

struct Device {
    std::string id;
    std::string decoder_type;
    std::string native_name;
    i2c_bus_device_handle_t handle = nullptr;
    std::vector<Action> initialize;
    std::vector<Action> measure;
    std::vector<Field> outputs;
    std::map<std::string, std::vector<uint8_t>> buffers;
    NativeBinding native;
};

struct Runtime {
    i2c_bus_handle_t bus = nullptr;
    std::vector<Device> devices;
};

uint8_t crc8(const uint8_t *data, size_t length, uint8_t polynomial,
             uint8_t initial)
{
    uint8_t crc = initial;
    for (size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80) != 0
                ? static_cast<uint8_t>((crc << 1) ^ polynomial)
                : static_cast<uint8_t>(crc << 1);
        }
    }
    return crc;
}

esp_err_t parse_bytes(cJSON *array, std::vector<uint8_t> &bytes)
{
    if (!cJSON_IsArray(array)) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *item = nullptr;
    cJSON_ArrayForEach(item, array) {
        uint32_t value = parse_u32(item, 0x100);
        if (value > 0xff) {
            return ESP_ERR_INVALID_ARG;
        }
        bytes.push_back(static_cast<uint8_t>(value));
    }
    return ESP_OK;
}

esp_err_t parse_action(cJSON *node, Action &action)
{
    if (!cJSON_IsObject(node)) {
        return ESP_ERR_INVALID_ARG;
    }

    action.operation = json_string(node, "op");
    action.reg = static_cast<uint16_t>(parse_u32(
        cJSON_GetObjectItemCaseSensitive(node, "register")));
    action.reg_bits = static_cast<uint8_t>(json_int(node, "register_bits", 0));
    action.length = static_cast<size_t>(json_int(node, "length", 0));
    action.destination = json_string(node, "destination");
    action.delay_ms = static_cast<uint32_t>(json_int(node, "milliseconds", 0));
    action.timeout_ms = static_cast<uint32_t>(json_int(node, "timeout_ms", 100));
    action.interval_ms = static_cast<uint32_t>(json_int(node, "interval_ms", 5));
    action.mask = parse_u32(cJSON_GetObjectItemCaseSensitive(node, "mask"), 0xff);
    action.expected = parse_u32(
        cJSON_GetObjectItemCaseSensitive(node, "expected"), 0);
    action.offset = static_cast<size_t>(json_int(node, "offset", 0));
    action.data_length = static_cast<size_t>(json_int(node, "data_length", 0));
    action.crc_offset = static_cast<size_t>(json_int(node, "crc_offset", 0));
    action.polynomial = static_cast<uint8_t>(parse_u32(
        cJSON_GetObjectItemCaseSensitive(node, "polynomial"), 0x31));
    action.initial = static_cast<uint8_t>(parse_u32(
        cJSON_GetObjectItemCaseSensitive(node, "initial"), 0xff));

    cJSON *data = cJSON_GetObjectItemCaseSensitive(node, "data");
    if (data != nullptr) {
        esp_err_t result = parse_bytes(data, action.data);
        if (result != ESP_OK) {
            return result;
        }
    }

    if (action.operation == "delay") {
        return action.delay_ms > 0 ? ESP_OK : ESP_ERR_INVALID_ARG;
    }
    if (action.operation == "write") {
        return !action.data.empty() ? ESP_OK : ESP_ERR_INVALID_ARG;
    }
    if (action.operation == "read") {
        return action.length > 0 && action.length <= kMaxTransferLength &&
                       !action.destination.empty()
            ? ESP_OK
            : ESP_ERR_INVALID_ARG;
    }
    if (action.operation == "poll") {
        return action.reg_bits == 0 || action.reg_bits == 8 ||
                       action.reg_bits == 16
            ? ESP_OK
            : ESP_ERR_INVALID_ARG;
    }
    if (action.operation == "crc8") {
        return !action.destination.empty() && action.data_length > 0
            ? ESP_OK
            : ESP_ERR_INVALID_ARG;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t parse_actions(cJSON *array, std::vector<Action> &actions)
{
    if (array == nullptr) {
        return ESP_OK;
    }
    if (!cJSON_IsArray(array)) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *node = nullptr;
    cJSON_ArrayForEach(node, array) {
        Action action;
        esp_err_t result = parse_action(node, action);
        if (result != ESP_OK) {
            return result;
        }
        actions.push_back(std::move(action));
    }
    return ESP_OK;
}

esp_err_t parse_field(cJSON *node, Field &field)
{
    if (!cJSON_IsObject(node)) {
        return ESP_ERR_INVALID_ARG;
    }

    field.id = json_string(node, "id");
    field.type = json_string(node, "type");
    field.unit = json_string(node, "unit");
    field.source = json_string(node, "source");
    field.byte_offset = static_cast<size_t>(json_int(node, "byte_offset", 0));
    field.width = static_cast<uint8_t>(json_int(node, "width", 1));
    field.little_endian = json_string(node, "endianness", "big") == "little";
    field.is_signed = json_bool(node, "signed", false);
    field.right_shift = static_cast<uint8_t>(json_int(node, "right_shift", 0));
    field.mask = parse_u32(cJSON_GetObjectItemCaseSensitive(node, "mask"),
                           field.width == 4 ? UINT32_MAX
                                            : ((1ULL << (field.width * 8)) - 1));
    field.scale = json_double(node, "scale", 1.0);
    field.add = json_double(node, "offset", 0.0);
    field.clamp_min = json_double(node, "clamp_min", -INFINITY);
    field.clamp_max = json_double(node, "clamp_max", INFINITY);

    cJSON *invalid = cJSON_GetObjectItemCaseSensitive(node, "invalid_raw");
    if (cJSON_IsNumber(invalid) || cJSON_IsString(invalid)) {
        field.has_invalid = true;
        field.invalid = static_cast<int64_t>(parse_u32(invalid));
    }

    if (field.id.empty() || field.type.empty() || field.source.empty() ||
        field.width == 0 || field.width > 4) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t execute_action(Device &device, const Action &action)
{
    if (action.operation == "delay") {
        transport_delay_ms(action.delay_ms);
        return ESP_OK;
    }
    if (action.operation == "write") {
        return transport_write(device.handle, action.reg, action.reg_bits,
                               action.data.data(), action.data.size());
    }
    if (action.operation == "read") {
        std::vector<uint8_t> &buffer = device.buffers[action.destination];
        buffer.assign(action.length, 0);
        return transport_read(device.handle, action.reg, action.reg_bits,
                              buffer.data(), buffer.size());
    }
    if (action.operation == "poll") {
        const TickType_t started = xTaskGetTickCount();
        const TickType_t timeout = pdMS_TO_TICKS(action.timeout_ms);
        do {
            uint8_t value = 0;
            esp_err_t result = transport_read(device.handle, action.reg,
                                              action.reg_bits, &value, 1);
            if (result != ESP_OK) {
                return result;
            }
            if ((value & action.mask) == action.expected) {
                return ESP_OK;
            }
            transport_delay_ms(action.interval_ms);
        } while ((xTaskGetTickCount() - started) <= timeout);
        return ESP_ERR_TIMEOUT;
    }
    if (action.operation == "crc8") {
        auto iterator = device.buffers.find(action.destination);
        if (iterator == device.buffers.end()) {
            return ESP_ERR_NOT_FOUND;
        }
        const std::vector<uint8_t> &buffer = iterator->second;
        if (action.offset + action.data_length > buffer.size() ||
            action.crc_offset >= buffer.size()) {
            return ESP_ERR_INVALID_SIZE;
        }
        uint8_t actual = crc8(buffer.data() + action.offset,
                              action.data_length,
                              action.polynomial,
                              action.initial);
        return actual == buffer[action.crc_offset] ? ESP_OK
                                                   : ESP_ERR_INVALID_CRC;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t execute_actions(Device &device, const std::vector<Action> &actions)
{
    for (const Action &action : actions) {
        esp_err_t result = execute_action(device, action);
        if (result != ESP_OK) {
            return result;
        }
    }
    return ESP_OK;
}

esp_err_t decode_field(const Device &device, const Field &field,
                       smonitor_i2c_sample_t &sample)
{
    auto iterator = device.buffers.find(field.source);
    if (iterator == device.buffers.end()) {
        return ESP_ERR_NOT_FOUND;
    }
    const std::vector<uint8_t> &buffer = iterator->second;
    if (field.byte_offset + field.width > buffer.size()) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint64_t raw = 0;
    for (uint8_t index = 0; index < field.width; ++index) {
        size_t source_index = field.little_endian
            ? field.byte_offset + field.width - index - 1
            : field.byte_offset + index;
        raw = (raw << 8) | buffer[source_index];
    }
    raw = (raw >> field.right_shift) & field.mask;

    int64_t numeric = static_cast<int64_t>(raw);
    if (field.is_signed) {
        uint8_t effective_bits = static_cast<uint8_t>(field.width * 8 -
                                                       field.right_shift);
        while (effective_bits > 1 &&
               ((field.mask >> (effective_bits - 1)) & 1U) == 0) {
            --effective_bits;
        }
        uint64_t sign_bit = 1ULL << (effective_bits - 1);
        if ((raw & sign_bit) != 0) {
            numeric = static_cast<int64_t>(raw | (~0ULL << effective_bits));
        }
    }

    sample.valid = !field.has_invalid || numeric != field.invalid;
    sample.error = sample.valid ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
    sample.value = std::clamp(numeric * field.scale + field.add,
                              field.clamp_min, field.clamp_max);
    copy_text(sample.device_id, sizeof(sample.device_id), device.id);
    copy_text(sample.output_id, sizeof(sample.output_id), field.id);
    copy_text(sample.type, sizeof(sample.type), field.type);
    copy_text(sample.unit, sizeof(sample.unit), field.unit);
    return ESP_OK;
}

esp_err_t append_failed_samples(const Device &device, esp_err_t error,
                                smonitor_i2c_sample_t *samples,
                                size_t capacity, size_t &count)
{
    for (const Field &field : device.outputs) {
        if (count >= capacity) {
            return ESP_ERR_INVALID_SIZE;
        }
        smonitor_i2c_sample_t &sample = samples[count++];
        sample = {};
        sample.valid = false;
        sample.error = error;
        copy_text(sample.device_id, sizeof(sample.device_id), device.id);
        copy_text(sample.output_id, sizeof(sample.output_id), field.id);
        copy_text(sample.type, sizeof(sample.type), field.type);
        copy_text(sample.unit, sizeof(sample.unit), field.unit);
    }
    return ESP_OK;
}

esp_err_t parse_bus(cJSON *root, Runtime &runtime)
{
    cJSON *bus = cJSON_GetObjectItemCaseSensitive(root, "bus");
    if (!cJSON_IsObject(bus)) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_config_t config{};
    config.mode = I2C_MODE_MASTER;
    config.sda_io_num = json_int(bus, "sda_pin", -1);
    config.scl_io_num = json_int(bus, "scl_pin", -1);
    config.sda_pullup_en = json_bool(bus, "internal_pullups", true);
    config.scl_pullup_en = json_bool(bus, "internal_pullups", true);
    config.master.clk_speed = static_cast<uint32_t>(
        json_int(bus, "frequency_hz", 100000));

    int port = json_int(bus, "port", 0);
    if (config.sda_io_num < 0 || config.scl_io_num < 0 ||
        config.master.clk_speed == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    runtime.bus = i2c_bus_create(static_cast<i2c_port_t>(port), &config);
    return runtime.bus != nullptr ? ESP_OK : ESP_FAIL;
}

esp_err_t parse_device(Runtime &runtime, cJSON *node, Device &device)
{
    device.id = json_string(node, "id");
    uint32_t address = parse_u32(
        cJSON_GetObjectItemCaseSensitive(node, "address"), 0x100);
    uint32_t frequency = static_cast<uint32_t>(
        json_int(node, "frequency_hz", 0));
    if (device.id.empty() || address > 0x7f) {
        return ESP_ERR_INVALID_ARG;
    }

    device.handle = i2c_bus_device_create(
        runtime.bus, static_cast<uint8_t>(address), frequency);
    if (device.handle == nullptr) {
        return ESP_FAIL;
    }

    esp_err_t result = parse_actions(
        cJSON_GetObjectItemCaseSensitive(node, "initialize"),
        device.initialize);
    if (result != ESP_OK) {
        return result;
    }

    cJSON *measurement = cJSON_GetObjectItemCaseSensitive(node, "measurement");
    if (cJSON_IsObject(measurement)) {
        result = parse_actions(
            cJSON_GetObjectItemCaseSensitive(measurement, "actions"),
            device.measure);
        if (result != ESP_OK) {
            return result;
        }
    }

    cJSON *decoder = cJSON_GetObjectItemCaseSensitive(node, "decoder");
    if (!cJSON_IsObject(decoder)) {
        return ESP_ERR_INVALID_ARG;
    }
    device.decoder_type = json_string(decoder, "type", "declarative");

    if (device.decoder_type == "native") {
        device.native_name = json_string(decoder, "name");
        const smonitor_i2c_native_driver_t *driver =
            find_native_driver(device.native_name);
        if (driver == nullptr) {
            return ESP_ERR_NOT_SUPPORTED;
        }
        device.native.driver = *driver;
        char *device_json = cJSON_PrintUnformatted(node);
        if (device_json == nullptr) {
            return ESP_ERR_NO_MEM;
        }
        smonitor_i2c_native_io_t io{};
        io.context = device.handle;
        io.read = transport_read;
        io.write = transport_write;
        io.delay_ms = transport_delay_ms;
        result = device.native.driver.create(
            &io, device_json, &device.native.state);
        cJSON_free(device_json);
        return result;
    }

    cJSON *outputs = cJSON_GetObjectItemCaseSensitive(decoder, "outputs");
    if (!cJSON_IsArray(outputs)) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *output = nullptr;
    cJSON_ArrayForEach(output, outputs) {
        Field field;
        result = parse_field(output, field);
        if (result != ESP_OK) {
            return result;
        }
        device.outputs.push_back(std::move(field));
    }
    return device.outputs.empty() ? ESP_ERR_INVALID_ARG : ESP_OK;
}

void destroy_runtime(Runtime *runtime)
{
    if (runtime == nullptr) {
        return;
    }
    for (Device &device : runtime->devices) {
        if (device.native.state != nullptr &&
            device.native.driver.destroy != nullptr) {
            device.native.driver.destroy(device.native.state);
        }
        if (device.handle != nullptr) {
            i2c_bus_device_delete(&device.handle);
        }
    }
    if (runtime->bus != nullptr) {
        i2c_bus_delete(&runtime->bus);
    }
    delete runtime;
}

}  // namespace

esp_err_t transport_read(void *context, uint16_t reg, uint8_t reg_bits,
                         uint8_t *data, size_t length)
{
    auto device = static_cast<i2c_bus_device_handle_t>(context);
    if (reg_bits == kNoRegister) {
        return i2c_bus_read_bytes(device, NULL_I2C_MEM_ADDR, length, data);
    }
    if (reg_bits == 8) {
        return i2c_bus_read_bytes(device, static_cast<uint8_t>(reg),
                                  length, data);
    }
    if (reg_bits == 16) {
        return i2c_bus_read_reg16(device, reg, length, data);
    }
    return ESP_ERR_INVALID_ARG;
}

esp_err_t transport_write(void *context, uint16_t reg, uint8_t reg_bits,
                          const uint8_t *data, size_t length)
{
    auto device = static_cast<i2c_bus_device_handle_t>(context);
    if (reg_bits == kNoRegister) {
        return i2c_bus_write_bytes(device, NULL_I2C_MEM_ADDR, length, data);
    }
    if (reg_bits == 8) {
        return i2c_bus_write_bytes(device, static_cast<uint8_t>(reg),
                                   length, data);
    }
    if (reg_bits == 16) {
        return i2c_bus_write_reg16(device, reg, length, data);
    }
    return ESP_ERR_INVALID_ARG;
}

void transport_delay_ms(uint32_t milliseconds)
{
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

std::string json_string(cJSON *object, const char *name, const char *fallback)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) && item->valuestring != nullptr
        ? item->valuestring
        : fallback;
}

int json_int(cJSON *object, const char *name, int fallback)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

double json_double(cJSON *object, const char *name, double fallback)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(item) ? item->valuedouble : fallback;
}

bool json_bool(cJSON *object, const char *name, bool fallback)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsBool(item) ? cJSON_IsTrue(item) : fallback;
}

uint32_t parse_u32(cJSON *item, uint32_t fallback)
{
    if (cJSON_IsNumber(item) && item->valuedouble >= 0) {
        return static_cast<uint32_t>(item->valuedouble);
    }
    if (cJSON_IsString(item) && item->valuestring != nullptr) {
        char *end = nullptr;
        unsigned long value = std::strtoul(item->valuestring, &end, 0);
        if (end != item->valuestring && *end == '\0') {
            return static_cast<uint32_t>(value);
        }
    }
    return fallback;
}

void copy_text(char *destination, size_t size, const std::string &source)
{
    if (size == 0) {
        return;
    }
    std::strncpy(destination, source.c_str(), size - 1);
    destination[size - 1] = '\0';
}

}  // namespace smonitor::i2c

extern "C" esp_err_t smonitor_i2c_create_from_json(
    const char *json, smonitor_i2c_handle_t *out_handle)
{
    using namespace smonitor::i2c;
    if (json == nullptr || out_handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_handle = nullptr;

    cJSON *root = cJSON_Parse(json);
    if (root == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (json_int(root, "schema_version", 0) != 1) {
        cJSON_Delete(root);
        return ESP_ERR_NOT_SUPPORTED;
    }

    std::unique_ptr<Runtime, decltype(&destroy_runtime)> runtime(
        new (std::nothrow) Runtime(), destroy_runtime);
    if (!runtime) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result = parse_bus(root, *runtime);
    cJSON *devices = cJSON_GetObjectItemCaseSensitive(root, "devices");
    if (result == ESP_OK && !cJSON_IsArray(devices)) {
        result = ESP_ERR_INVALID_ARG;
    }

    cJSON *node = nullptr;
    if (result == ESP_OK) {
        cJSON_ArrayForEach(node, devices) {
            Device device;
            result = parse_device(*runtime, node, device);
            if (result != ESP_OK) {
                if (device.handle != nullptr) {
                    i2c_bus_device_delete(&device.handle);
                }
                break;
            }
            result = execute_actions(device, device.initialize);
            if (result != ESP_OK) {
                if (device.native.state != nullptr) {
                    device.native.driver.destroy(device.native.state);
                }
                i2c_bus_device_delete(&device.handle);
                break;
            }
            runtime->devices.push_back(std::move(device));
        }
    }

    cJSON_Delete(root);
    if (result != ESP_OK || runtime->devices.empty()) {
        return result != ESP_OK ? result : ESP_ERR_INVALID_ARG;
    }

    *out_handle = runtime.release();
    return ESP_OK;
}

extern "C" esp_err_t smonitor_i2c_read_all(
    smonitor_i2c_handle_t handle,
    smonitor_i2c_sample_t *samples,
    size_t capacity,
    size_t *out_count)
{
    using namespace smonitor::i2c;
    if (handle == nullptr || samples == nullptr || out_count == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    auto *runtime = static_cast<Runtime *>(handle);
    size_t count = 0;
    esp_err_t overall = ESP_OK;

    for (Device &device : runtime->devices) {
        if (device.decoder_type == "native") {
            size_t produced = 0;
            esp_err_t result = device.native.driver.read(
                device.native.state, samples + count, capacity - count,
                &produced);
            if (result != ESP_OK) {
                overall = result;
            }
            count += produced;
            continue;
        }

        esp_err_t result = execute_actions(device, device.measure);
        if (result != ESP_OK) {
            overall = result;
            result = append_failed_samples(device, result, samples,
                                           capacity, count);
            if (result != ESP_OK) {
                *out_count = count;
                return result;
            }
            continue;
        }

        for (const Field &field : device.outputs) {
            if (count >= capacity) {
                *out_count = count;
                return ESP_ERR_INVALID_SIZE;
            }
            samples[count] = {};
            result = decode_field(device, field, samples[count]);
            if (result != ESP_OK) {
                samples[count].valid = false;
                samples[count].error = result;
                overall = result;
            }
            ++count;
        }
    }

    *out_count = count;
    return overall;
}

extern "C" void smonitor_i2c_destroy(smonitor_i2c_handle_t *handle)
{
    using namespace smonitor::i2c;
    if (handle == nullptr || *handle == nullptr) {
        return;
    }
    destroy_runtime(static_cast<Runtime *>(*handle));
    *handle = nullptr;
}
