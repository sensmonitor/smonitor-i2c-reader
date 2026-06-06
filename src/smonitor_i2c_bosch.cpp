#include "smonitor_i2c_internal.hpp"

#include <algorithm>
#include <cstring>
#include <new>

namespace smonitor::i2c {

namespace {

struct OutputConfig {
    std::string id;
    std::string type;
    std::string unit;
    double scale = 1.0;
    double offset = 0.0;
};

struct BoschState {
    smonitor_i2c_native_io_t io{};
    std::string device_id;
    bool humidity_supported = false;

    uint16_t t1 = 0;
    int16_t t2 = 0;
    int16_t t3 = 0;
    uint16_t p1 = 0;
    int16_t p2 = 0;
    int16_t p3 = 0;
    int16_t p4 = 0;
    int16_t p5 = 0;
    int16_t p6 = 0;
    int16_t p7 = 0;
    int16_t p8 = 0;
    int16_t p9 = 0;
    uint8_t h1 = 0;
    int16_t h2 = 0;
    uint8_t h3 = 0;
    int16_t h4 = 0;
    int16_t h5 = 0;
    int8_t h6 = 0;

    OutputConfig temperature{"temperature", "temperature", "celsius"};
    OutputConfig pressure{"pressure", "pressure", "hpa"};
    OutputConfig humidity{"humidity", "humidity", "percent_rh"};
};

uint16_t u16le(const uint8_t *data)
{
    return static_cast<uint16_t>(data[0] | (data[1] << 8));
}

int16_t s16le(const uint8_t *data)
{
    return static_cast<int16_t>(u16le(data));
}

int16_t s12(uint16_t value)
{
    value &= 0x0fff;
    if ((value & 0x0800) != 0) {
        value |= 0xf000;
    }
    return static_cast<int16_t>(value);
}

esp_err_t read_register(BoschState *state, uint8_t reg, uint8_t *data,
                        size_t length)
{
    return state->io.read(state->io.context, reg, 8, data, length);
}

esp_err_t write_register(BoschState *state, uint8_t reg, uint8_t value)
{
    return state->io.write(state->io.context, reg, 8, &value, 1);
}

void parse_output(cJSON *outputs, const char *type, OutputConfig &output)
{
    if (!cJSON_IsArray(outputs)) {
        return;
    }
    cJSON *node = nullptr;
    cJSON_ArrayForEach(node, outputs) {
        if (json_string(node, "type") != type) {
            continue;
        }
        output.id = json_string(node, "id", output.id.c_str());
        output.unit = json_string(node, "unit", output.unit.c_str());
        output.scale = json_double(node, "scale", 1.0);
        output.offset = json_double(node, "offset", 0.0);
        return;
    }
}

esp_err_t load_calibration(BoschState *state)
{
    uint8_t primary[24]{};
    esp_err_t result = read_register(state, 0x88, primary, sizeof(primary));
    if (result != ESP_OK) {
        return result;
    }

    state->t1 = u16le(primary + 0);
    state->t2 = s16le(primary + 2);
    state->t3 = s16le(primary + 4);
    state->p1 = u16le(primary + 6);
    state->p2 = s16le(primary + 8);
    state->p3 = s16le(primary + 10);
    state->p4 = s16le(primary + 12);
    state->p5 = s16le(primary + 14);
    state->p6 = s16le(primary + 16);
    state->p7 = s16le(primary + 18);
    state->p8 = s16le(primary + 20);
    state->p9 = s16le(primary + 22);

    if (!state->humidity_supported) {
        return ESP_OK;
    }

    result = read_register(state, 0xa1, &state->h1, 1);
    if (result != ESP_OK) {
        return result;
    }

    uint8_t humidity[7]{};
    result = read_register(state, 0xe1, humidity, sizeof(humidity));
    if (result != ESP_OK) {
        return result;
    }
    state->h2 = s16le(humidity + 0);
    state->h3 = humidity[2];
    state->h4 = s12(static_cast<uint16_t>((humidity[3] << 4) |
                                         (humidity[4] & 0x0f)));
    state->h5 = s12(static_cast<uint16_t>((humidity[5] << 4) |
                                         (humidity[4] >> 4)));
    state->h6 = static_cast<int8_t>(humidity[6]);
    return ESP_OK;
}

esp_err_t initialize(BoschState *state)
{
    uint8_t chip_id = 0;
    esp_err_t result = read_register(state, 0xd0, &chip_id, 1);
    if (result != ESP_OK) {
        return result;
    }
    if (chip_id != 0x58 && chip_id != 0x60) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    state->humidity_supported = chip_id == 0x60;

    result = write_register(state, 0xe0, 0xb6);
    if (result != ESP_OK) {
        return result;
    }
    state->io.delay_ms(5);

    for (int attempt = 0; attempt < 100; ++attempt) {
        uint8_t status = 0;
        result = read_register(state, 0xf3, &status, 1);
        if (result != ESP_OK) {
            return result;
        }
        if ((status & 0x01) == 0) {
            break;
        }
        state->io.delay_ms(2);
        if (attempt == 99) {
            return ESP_ERR_TIMEOUT;
        }
    }

    result = load_calibration(state);
    if (result != ESP_OK) {
        return result;
    }

    if (state->humidity_supported) {
        result = write_register(state, 0xf2, 0x05);  // humidity x16
        if (result != ESP_OK) {
            return result;
        }
    }
    result = write_register(state, 0xf5, 0x00);      // filter off, 0.5 ms
    if (result != ESP_OK) {
        return result;
    }
    return write_register(state, 0xf4, 0xb7);        // T/P x16, normal mode
}

void fill_sample(smonitor_i2c_sample_t &sample, const BoschState *state,
                 const OutputConfig &output, double value)
{
    sample = {};
    copy_text(sample.device_id, sizeof(sample.device_id), state->device_id);
    copy_text(sample.output_id, sizeof(sample.output_id), output.id);
    copy_text(sample.type, sizeof(sample.type), output.type);
    copy_text(sample.unit, sizeof(sample.unit), output.unit);
    sample.value = value * output.scale + output.offset;
    sample.valid = true;
    sample.error = ESP_OK;
}

esp_err_t bosch_create(const smonitor_i2c_native_io_t *io,
                       const char *device_json, void **driver_state)
{
    if (io == nullptr || device_json == nullptr || driver_state == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    auto *state = new (std::nothrow) BoschState();
    if (state == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    state->io = *io;

    cJSON *root = cJSON_Parse(device_json);
    if (root == nullptr) {
        delete state;
        return ESP_ERR_INVALID_ARG;
    }
    state->device_id = json_string(root, "id");
    cJSON *decoder = cJSON_GetObjectItemCaseSensitive(root, "decoder");
    cJSON *outputs = cJSON_GetObjectItemCaseSensitive(decoder, "outputs");
    parse_output(outputs, "temperature", state->temperature);
    parse_output(outputs, "pressure", state->pressure);
    parse_output(outputs, "humidity", state->humidity);
    cJSON_Delete(root);

    esp_err_t result = initialize(state);
    if (result != ESP_OK) {
        delete state;
        return result;
    }
    *driver_state = state;
    return ESP_OK;
}

esp_err_t bosch_read(void *driver_state, smonitor_i2c_sample_t *samples,
                     size_t capacity, size_t *out_count)
{
    auto *state = static_cast<BoschState *>(driver_state);
    if (state == nullptr || samples == nullptr || out_count == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t required = state->humidity_supported ? 3 : 2;
    if (capacity < required) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t data[8]{};
    esp_err_t result = read_register(state, 0xf7, data,
                                     state->humidity_supported ? 8 : 6);
    if (result != ESP_OK) {
        return result;
    }

    int32_t adc_p = static_cast<int32_t>(
        (static_cast<uint32_t>(data[0]) << 12) |
        (static_cast<uint32_t>(data[1]) << 4) | (data[2] >> 4));
    int32_t adc_t = static_cast<int32_t>(
        (static_cast<uint32_t>(data[3]) << 12) |
        (static_cast<uint32_t>(data[4]) << 4) | (data[5] >> 4));
    int32_t adc_h = static_cast<int32_t>((data[6] << 8) | data[7]);

    int32_t var1 = ((((adc_t >> 3) -
                      (static_cast<int32_t>(state->t1) << 1))) *
                    static_cast<int32_t>(state->t2)) >>
                   11;
    int32_t var2 = (((((adc_t >> 4) -
                       static_cast<int32_t>(state->t1)) *
                      ((adc_t >> 4) -
                       static_cast<int32_t>(state->t1))) >>
                     12) *
                    static_cast<int32_t>(state->t3)) >>
                   14;
    int32_t t_fine = var1 + var2;
    double temperature = ((t_fine * 5 + 128) >> 8) / 100.0;

    int64_t p_var1 = static_cast<int64_t>(t_fine) - 128000;
    int64_t p_var2 = p_var1 * p_var1 * state->p6;
    p_var2 += (p_var1 * state->p5) << 17;
    p_var2 += static_cast<int64_t>(state->p4) << 35;
    p_var1 = ((p_var1 * p_var1 * state->p3) >> 8) +
             ((p_var1 * state->p2) << 12);
    p_var1 = (((static_cast<int64_t>(1) << 47) + p_var1) * state->p1) >>
             33;
    if (p_var1 == 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    int64_t pressure = 1048576 - adc_p;
    pressure = (((pressure << 31) - p_var2) * 3125) / p_var1;
    p_var1 = (static_cast<int64_t>(state->p9) * (pressure >> 13) *
              (pressure >> 13)) >>
             25;
    p_var2 = (static_cast<int64_t>(state->p8) * pressure) >> 19;
    pressure = ((pressure + p_var1 + p_var2) >> 8) +
               (static_cast<int64_t>(state->p7) << 4);
    double pressure_hpa = (pressure / 256.0) / 100.0;

    fill_sample(samples[0], state, state->temperature, temperature);
    fill_sample(samples[1], state, state->pressure, pressure_hpa);
    *out_count = 2;

    if (state->humidity_supported) {
        int32_t humidity = t_fine - 76800;
        humidity = (((((adc_h << 14) -
                       (static_cast<int32_t>(state->h4) << 20) -
                       (static_cast<int32_t>(state->h5) * humidity)) +
                      16384) >>
                     15) *
                    (((((((humidity * state->h6) >> 10) *
                          (((humidity * state->h3) >> 11) + 32768)) >>
                         10) +
                        2097152) *
                           state->h2 +
                       8192) >>
                     14));
        humidity -= (((((humidity >> 15) * (humidity >> 15)) >> 7) *
                      state->h1) >>
                     4);
        humidity = std::clamp(
            humidity,
            static_cast<int32_t>(0),
            static_cast<int32_t>(419430400));
        double humidity_percent = (humidity >> 12) / 1024.0;
        fill_sample(samples[2], state, state->humidity, humidity_percent);
        *out_count = 3;
    }
    return ESP_OK;
}

void bosch_destroy(void *driver_state)
{
    delete static_cast<BoschState *>(driver_state);
}

}  // namespace

const smonitor_i2c_native_driver_t *bosch_native_driver()
{
    static const smonitor_i2c_native_driver_t driver{
        bosch_create,
        bosch_read,
        bosch_destroy,
    };
    return &driver;
}

}  // namespace smonitor::i2c
