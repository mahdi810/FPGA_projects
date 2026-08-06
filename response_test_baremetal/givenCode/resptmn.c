/*
 * helloworld.c: response time test application (PYNQ-Z2)
 *
 * This application configures UART 16550 to baud rate 9600.
 * PS7 UART (Zynq) is not initialized by this application, since
 * bootrom/bsp configures it to baud rate 115200
 */

#include <stdio.h>
#include "platform.h"
#include <string.h>
#include <sleep.h>
#include "xil_printf.h"
#include "xparameters.h"

/* --- new includes for interrupt handling --- */
#include "xscugic.h"      // Generic Interrupt Controller driver
#include "xil_exception.h" // ARM exception vector registration
#include "xscutimer.h"     // SCU private timer driver

#define REG(k) *(volatile unsigned int*)(XPAR_RESPTST_0_S00_AXI_BASEADDR + 4*k)
#define CMDLEN 128

/* ------------------------------------------------------------
 * Interrupt controller (GIC) instance and its config pointer.
 * The config pointer is filled in by XScuGic_LookupConfig() and
 * only describes the hardware; the Intc struct is the actual
 * "live" driver instance we operate on afterward.
 * ------------------------------------------------------------ */
static XScuGic        Intc;
static XScuGic_Config *IntcConfig;

/* ------------------------------------------------------------
 * SCU private timer instance and its config pointer. Same split
 * as above: pTimerConfig describes the hardware, pTimer is the
 * live instance.
 * ------------------------------------------------------------ */
static XScuTimer        pTimer;
static XScuTimer_Config *pTimerConfig;

/*
 * TIMER_LOAD_VALUE sets the tick period. The private timer counts
 * down from this value to zero at (CPU clock / 2), then reloads
 * (since we enable auto-reload below) and fires an interrupt each
 * time it hits zero.
 *
 * 83333 assumes a ~333 MHz timer clock (666 MHz CPU / 2), giving
 * 83333 / 333e6 ~= 250 us per tick, i.e. 4 kHz.
 *
 * IMPORTANT: this depends on your actual PS clock configuration in
 * Vivado. If your PYNQ-Z2 project's CPU clock differs from 666 MHz,
 * recompute: TIMER_LOAD_VALUE = (CPU_freq_Hz / 2) * 0.00025
 * You can check your real CPU clock in xparameters.h, look for
 * XPAR_CPU_CORTEXA9_0_CPU_CLK_FREQ_HZ.
 */
#define TIMER_LOAD_VALUE  83333   // ~250 us per tick (4 kHz)

/*
 * State machine states for the response-time measurement.
 * This runs entirely inside the ISR - main() only ever reads
 * ISR_State, it never writes the timing logic itself.
 */
#define ISRST_IDLE     0   // nothing happening, timer just ticking
#define ISRST_ARMED    1   // main() requested a new trial to start
#define ISRST_WAIT     2   // counting down the random pre-stimulus delay
#define ISRST_RUN      3   // LED is on, waiting for the button press
#define ISRST_TIMEOUT  4   // no button press within the timeout window
#define ISRST_DONE     5   // button was pressed - trial complete
#define ISRST_ERROR    6   // button was pressed too early (false start)

/*
 * Bit mask for the button we treat as the "response" button.
 * Your pb extraction is: pb = xdata & 0xf  (4 buttons, bits [3:0]).
 * We use bit 0 (the first pushbutton) as the response button here -
 * change this mask if you want a different physical button.
 */
#define BTN_RESP 0x1

/*
 * All of these are shared between main() (writer of ISR_State to
 * ARM a trial, reader of the rest to report results) and the ISR
 * (writer of everything). They MUST be volatile: without it, the
 * compiler is free to cache them in registers inside main()'s
 * busy-wait loop and never see the ISR's updates.
 */
static volatile int ISR_State;
static volatile int Wait_Timer;   // ticks counted during the WAIT state
static volatile int Wait_Value;   // target tick count for WAIT (set by main() before arming)
static volatile int Resp_Timer;   // ticks counted during the RUN state - this becomes the result

#define CMDLEN 128

void my_getline(char* cmd)
{
	int i;
	char c;
	for(i = 0; i < CMDLEN - 1; i++){
		c = getchar();
		if(c == '\r' || c == '\n'){
			c = '\0';
			break;
		}
		cmd[i] = c;
	}
	cmd[i] = '\0';
}

void print_help()
{
	printf("exit     : to quit the program. \n");
	printf("help     : to print the current help menu. \n");
	printf("test     : to test the current board peripherals. \n");
	printf("start N  : run a reaction-time trial, N seconds max pre-stimulus delay. \n");
}

/* ------------------------------------------------------------
 * Timer interrupt handler.
 *
 * This fires once every TIMER_LOAD_VALUE-derived period (~250 us).
 * Everything time-critical for the measurement lives here, NOT in
 * main() - this is what gives the measurement a fixed, known tick
 * rate independent of whatever main() happens to be doing.
 * ------------------------------------------------------------ */
static void TimerIntrHandler(void *CallBackRef)
{
	XScuTimer *TimerInstance = (XScuTimer *)CallBackRef;
	unsigned int xdata;
	int pb;

	/*
	 * MANDATORY first step: acknowledge the timer's interrupt flag.
	 * If this is skipped, the flag stays set and the interrupt
	 * re-fires immediately (or the IRQ line just stays asserted),
	 * effectively locking up the CPU in the ISR.
	 */
	XScuTimer_ClearInterruptStatus(TimerInstance);

	/* Read the peripheral once per tick - this is the only place
	 * in the whole program that reads the button during a trial. */
	xdata = REG(0);
	pb = xdata & 0xf;

	switch (ISR_State) {

		case ISRST_IDLE:
			/* Nothing to do - timer just ticks in the background.
			 * (Left empty on purpose; no heartbeat LED pattern here
			 * since your test command already exercises the LEDs.) */
			break;

		case ISRST_ARMED:
			/* main() just requested a new trial. Reset both counters
			 * and blank the LEDs so the trial starts from a clean
			 * state on the very next tick. */
			Wait_Timer = 0;
			Resp_Timer = 0;
			REG(0) = 0;
			ISR_State = ISRST_WAIT;
			break;

		case ISRST_WAIT:
			/* Counting the pre-stimulus delay. If the button is
			 * pressed before the LED comes on, that's a false start. */
			if ((pb & BTN_RESP) != 0) {
				ISR_State = ISRST_ERROR;
				break;
			}
			Wait_Timer++;
			if (Wait_Timer >= Wait_Value) {
				/* Delay elapsed - turn the LED on RIGHT NOW, on this
				 * exact tick. This is the stimulus onset, and it's
				 * what Resp_Timer will be measured relative to. */
				REG(0) = 0xf;      // all 4 LEDs on as the stimulus
				ISR_State = ISRST_RUN;
			}
			break;

		case ISRST_RUN:
			/* Counting ticks since the LED came on. Stops counting
			 * (by leaving this case) the instant the button is seen. */
			if ((pb & BTN_RESP) != 0) {
				REG(0) = 0;         // LEDs off - trial complete
				ISR_State = ISRST_DONE;
				break;
			}
			Resp_Timer++;
			if (Resp_Timer >= 40000) {   // 40000 ticks * 250us = 10s timeout
				REG(0) = 0;
				ISR_State = ISRST_TIMEOUT;
			}
			break;

		case ISRST_DONE:
		case ISRST_TIMEOUT:
		case ISRST_ERROR:
			/* Terminal states - do nothing, wait for main() to notice
			 * (it's busy-polling ISR_State) and print the result. */
			break;

		default:
			ISR_State = ISRST_IDLE;
	}
}

int main()
{
	int terminate = 0;
	char CMD[CMDLEN];
	int xdata;
	int pb, sw;
	int seconds;

    init_platform();

    /* ------------------------------------------------------------
     * Interrupt setup - three layers, all required (CPU / GIC /
     * peripheral), same pattern as resptstmn.c on the ZedBoard.
     * ------------------------------------------------------------ */

    /* 1. CPU level: register the ARM exception vector for IRQ so
     *    it jumps into the GIC's dispatcher when any IRQ occurs. */
    Xil_ExceptionInit();

    IntcConfig = XScuGic_LookupConfig(XPAR_SCUGIC_0_DEVICE_ID);
    XScuGic_CfgInitialize(&Intc, IntcConfig, IntcConfig->CpuBaseAddress);

    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_IRQ_INT,
    		(Xil_ExceptionHandler)XScuGic_InterruptHandler, &Intc);

    /* 2. Peripheral level: configure the private timer itself -
     *    load value, auto-reload so it free-runs periodically. */
    pTimerConfig = XScuTimer_LookupConfig(XPAR_XSCUTIMER_0_DEVICE_ID);
    XScuTimer_CfgInitialize(&pTimer, pTimerConfig, pTimerConfig->BaseAddr);
    XScuTimer_EnableAutoReload(&pTimer);
    XScuTimer_LoadTimer(&pTimer, TIMER_LOAD_VALUE);

    /* 3. GIC level: connect our handler to the timer's specific
     *    interrupt ID, then unmask that ID at the GIC. */
    XScuGic_Connect(&Intc, XPAR_SCUTIMER_INTR,
    		(Xil_ExceptionHandler)TimerIntrHandler, (void *)&pTimer);
    XScuGic_Enable(&Intc, XPAR_SCUTIMER_INTR);

    /* Tell the timer peripheral to actually assert its IRQ line
     * on expiry (separate from the GIC-level unmask above). */
    XScuTimer_EnableInterrupt(&pTimer);

    /* Global switch: unmask IRQ at the CPU core. Nothing above
     * takes effect until this runs. */
    Xil_ExceptionEnable();

    /* Start the timer running - ticks (and TimerIntrHandler calls)
     * begin now, in ISRST_IDLE, doing nothing until a trial starts. */
    ISR_State = ISRST_IDLE;
    XScuTimer_Start(&pTimer);

    do{
    	printf(">> "); fflush(stdout);
    	my_getline(CMD);
    	if(!strcmp(CMD, "exit")){
    		terminate = 1;
    	} else if(!strcmp(CMD, "help")){
    		print_help();
    	} else if(!strcmp(CMD, "test")){
    		printf("test will be implemented here. \n");
    		xdata = 15;
    		REG(0) = (xdata & 0b1111);
    		printf("the led is on for 10 sec and then turns off. \n");
    		usleep(10000000);
    		REG(0) = 0;
    		printf("now the leds are off. \n");
    		xdata = REG(0);
    		pb = (xdata & 0xf);
    		sw = (xdata >> 4)&0b11;
    		printf("pushbuttons : %x, switches : %x ", pb, sw);
    		printf("test run. \n");
    	} else if(!strncmp(CMD, "start", 5)){
    		/* Parse the seconds argument, e.g. "start 3" */
    		if (sscanf(&CMD[5], "%d", &seconds) != 1 || seconds < 1 || seconds > 10) {
    			printf("usage: start N   (N = 1..10 seconds)\n");
    		} else {
    			/* Convert seconds to ticks: 1 tick = 250 us,
    			 * so 1 second = 4000 ticks. */
    			Wait_Value = 4000 * seconds;

    			/* Hand control to the ISR by arming it, then busy-wait
    			 * here until the ISR reaches a terminal state. main()
    			 * does no timing itself - it's just watching. */
    			ISR_State = ISRST_ARMED;
    			printf("get ready...\n");
    			while (ISR_State != ISRST_DONE &&
    				   ISR_State != ISRST_TIMEOUT &&
    				   ISR_State != ISRST_ERROR) {
    				/* spin */
    			}

    			if (ISR_State == ISRST_ERROR) {
    				printf("false start - button pressed before the LED lit.\n");
    			} else if (ISR_State == ISRST_TIMEOUT) {
    				printf("timeout - no button press within 10s.\n");
    			} else {
    				/* Each tick = 250 us = 0.25 ms, so multiply the
    				 * tick count by 0.25 to get milliseconds. */
    				printf("response time: %.2f ms\n", 0.25 * Resp_Timer);
    			}
    			ISR_State = ISRST_IDLE;
    		}
    	} else if(strlen(CMD) > 0){
    		printf("invalid command : %s \n", CMD);
    	}
    }while(!terminate);

    /* Clean shutdown: stop the timer and disable interrupts before
     * exiting, so nothing keeps firing after cleanup_platform(). */
    XScuTimer_Stop(&pTimer);
    Xil_ExceptionDisable();
    XScuTimer_DisableInterrupt(&pTimer);
    XScuGic_Disable(&Intc, XPAR_SCUTIMER_INTR);

    print("Thank you for using Response Test Application. \n");
    cleanup_platform();
    return 0;
}