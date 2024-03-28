#ifndef __MY_TIMER_H
#define __MY_TIMER_H


typedef enum
{
  MY_TIMER_MODE_SINGLE_SHOT, MY_TIMER_MODE_REPEATED
} my_timer_mode_t;


/* Macro to define  timer instance */
#define MY_TIMER_DEF(name) mytimer_node_t name

#define MS_TO_TICKS(ms) (uint32_t)((uint64_t)ms*32768/1000)   //Macro to convert milliseconds into number of RTC ticks

#define US_TO_TICKS(us) (uint32_t)(((uint64_t)us*32768)/1000000)   //Macro to convert microseconds into number of RTC ticks


typedef void
(*timeout_handler_t) (void);   //Timeout handler function

typedef struct timer_node
{
  bool trig;
  bool is_running;
  my_timer_mode_t mode;
  timeout_handler_t timeout_handler;
  uint32_t interval;
  uint32_t trig_ticks;
  struct timer_node *next_node;
} mytimer_node_t;         // Timer instance structure

//Function to create a timer by initializing the timer instance
void
myTimer_create (mytimer_node_t *instance,
		timeout_handler_t timeout_handler, my_timer_mode_t mode);

//Function to start the timer instance by adding it to the list of running timers.
void
myTimer_start (mytimer_node_t *instance,uint32_t interval);

//Function to stop the timer instance
void
myTimer_stop (mytimer_node_t *instance);

//Function to initialize the timer module
void
myTimer_init ();

#endif //__MY_TIMER_H

