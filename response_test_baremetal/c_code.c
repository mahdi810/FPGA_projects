#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "platform.h"

// new includes for the interrupt handler.
#include "xscugic.h" // Generic Interrupt Controller driver
#include "xil_exception.h" // ARM exception vector registration
#include "xscutimer.h" // SCU private timer driver

static XScuGic Intc;
static XScuGic_Config *IntcConfig;

static XScuTimer pTimer;
static XScuTimer_Config pTimer_Config;

#define TIMER_LOAD_VALUE 81250; // 650e6/2 * 0.00025

Xil_ExceptionInit();

int main(){

    return 0;
}
