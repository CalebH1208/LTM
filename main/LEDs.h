#ifndef LED_H
#define LED_H
#include "shared.h"

typedef struct {
    int gpio;
    int delayMS;
} delay_parameters;

 void LED_on(int gpio);
 void LED_off(int gpio);
 void LED_delayed_off(void * params);
 void configure_led(int gpio);
 
#endif // LED_H