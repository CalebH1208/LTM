#include "LEDs.h"

static const char *TAG = "LED Functions";

void LED_on(int gpio)
{
    /* Set the GPIO level according to the state (LOW or HIGH)*/
    gpio_set_level(gpio, 1);
}

void LED_off(int gpio)
{
    /* Set the GPIO level according to the state (LOW or HIGH)*/
    gpio_set_level(gpio, 0);
}

void LED_delayed_off(void * params){
    int delayMS = ((delay_parameters *)params)->delayMS;
    int gpio = ((delay_parameters *)params)->gpio;
    vTaskDelay(pdMS_TO_TICKS(delayMS));
    gpio_set_level(gpio,0);
}

void configure_led(int gpio)
{
    ESP_LOGI(TAG, "LED on GPIO %d configured",gpio);
    gpio_reset_pin(gpio);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
}