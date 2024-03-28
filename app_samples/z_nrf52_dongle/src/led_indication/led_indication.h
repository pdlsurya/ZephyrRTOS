#ifndef LED_INDICATION_H
#define LED_INDICATION_H

typedef enum 
{
  LED_INDICATION_NORMAL_BLINK, LED_INDICATION_FAST_BLINK, LED_INDICATION
}led_indication_t;

void led_indication_set(led_indication_t indication_type);
void led_indication_clear(led_indication_t indication_type);
int led_indication_init();

#endif // LED_INDICATION_H
