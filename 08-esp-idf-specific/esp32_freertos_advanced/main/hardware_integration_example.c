#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gptimer_types.h"
#include "driver/gptimer.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "HW_INTEGRATION";

// ISR callback
bool IRAM_ATTR timer_callback(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data)
{
    BaseType_t high_task_awoken = pdFALSE;
    SemaphoreHandle_t sem = (SemaphoreHandle_t)user_data;
    xSemaphoreGiveFromISR(sem, &high_task_awoken);
    return (high_task_awoken == pdTRUE);
}

// Task: toggle LED when timer fires
void hardware_event_task(void *parameter)
{
    SemaphoreHandle_t sem = (SemaphoreHandle_t)parameter;
    bool led_state = false;

    while (1)
    {
        if (xSemaphoreTake(sem, portMAX_DELAY) == pdTRUE)
        {
            led_state = !led_state;
            gpio_set_level(GPIO_NUM_2, led_state);
            ESP_LOGI(TAG, "Timer tick → LED %s on Core %d",
                     led_state ? "ON" : "OFF", xPortGetCoreID());
        }
    }
}

// Main hardware integration setup
void hardware_integration_example(void)
{
    ESP_LOGI(TAG, "Initializing hardware timer + LED...");

    // Create semaphore
    SemaphoreHandle_t sem = xSemaphoreCreateBinary();

    // Configure GPIO2 for LED output
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_2),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_conf);

    // Configure timer
    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1 tick = 1 µs
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

    gptimer_alarm_config_t alarm_config = {
        .reload_count = 0,
        .alarm_count = 1000000, // 1 second
        .flags.auto_reload_on_alarm = true,
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));

    gptimer_event_callbacks_t cbs = {
        .on_alarm = timer_callback,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, sem));
    ESP_ERROR_CHECK(gptimer_enable(gptimer));
    ESP_ERROR_CHECK(gptimer_start(gptimer));

    // Create event task on Core 0 (real-time)
    xTaskCreatePinnedToCore(hardware_event_task, "HWEventTask", 2048, sem, 10, NULL, 0);

    ESP_LOGI(TAG, "Hardware timer started successfully");
}
