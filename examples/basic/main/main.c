#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "smonitor_i2c.h"

static const char *TAG = "smonitor_i2c_example";

extern const char profile_json_start[] asm("_binary_profile_json_start");

void app_main(void)
{
    smonitor_i2c_handle_t reader = NULL;
    ESP_ERROR_CHECK(
        smonitor_i2c_create_from_json(profile_json_start, &reader));

    while (true) {
        smonitor_i2c_sample_t samples[16] = {0};
        size_t count = 0;
        esp_err_t result =
            smonitor_i2c_read_all(reader, samples, 16, &count);

        for (size_t index = 0; index < count; ++index) {
            const smonitor_i2c_sample_t *sample = &samples[index];
            ESP_LOGI(TAG, "%s/%s = %.3f %s, valid=%d, error=%s",
                     sample->device_id,
                     sample->output_id,
                     sample->value,
                     sample->unit,
                     sample->valid,
                     esp_err_to_name(sample->error));
        }

        if (result != ESP_OK) {
            ESP_LOGW(TAG, "Read cycle completed with %s",
                     esp_err_to_name(result));
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
