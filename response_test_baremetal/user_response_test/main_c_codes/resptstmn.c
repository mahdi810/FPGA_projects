/*
 * helloworld.c: simple test application
 *
 * This application configures UART 16550 to baud rate 9600.
 * PS7 UART (Zynq) is not initialized by this application, since
 * bootrom/bsp configures it to baud rate 115200
 *
 * ------------------------------------------------
 * | UART TYPE   BAUD RATE                        |
 * ------------------------------------------------
 *   uartns550   9600
 *   uartlite    Configurable only in HW design
 *   ps7_uart    115200 (configured by bootrom/bsp)
 */


/*
 * the idea for this program is to have a interrupt firing each 250us, and each 250us is going to be treated
 * as one tick, and then when the led is one untill the user presses the button, we will see how many ticks
 * is passed and after the user pressed the button, then we will convert the ticks into time and
 * change the unit to ms.
 *
 *
 */

#include <stdio.h>
#include "platform.h"
#include <string.h>
#include <sleep.h>
#include "xil_printf.h"
#include "xparameters.h"

// new includes for the interrupt handler.
#include "xscugic.h" // Generic Interrupt Controller driver
#include "xil_exception.h" // ARM exception vector registration
#include "xscutimer.h" // SCU private timer driver

// register address to access the peripherals.
#define REG(k) *(volatile unsigned int*)(XPAR_RESPTST_0_S00_AXI_BASEADDR + 4*k)
#define CMDLEN 128
#define ROTATE_TICK_COUNTS 800

/* ------------------------------------------------------------
 * Interrupt controller (GIC) instance and its config pointer.
 * The config pointer is filled in by XScuGic_LookupConfig() and
 * only describes the hardware; the Intc struct is the actual
 * "live" driver instance we operate on afterward.
 * one timer intance and one config pointer should be define.
 * ------------------------------------------------------------ */
static XScuGic Intc;
static XScuGic_Config *IntcConfig;


/* ------------------------------------------------------------
 * SCU private timer instance and its config pointer. Same split
 * as above: pTimerConfig describes the hardware, pTimer is the
 * live instance.
 * ------------------------------------------------------------ */
static XScuTimer pTimer;
static XScuTimer_Config* pTimerConfig;

/*
 * TIMER_LOAD_VALUE sets the tick period. The private timer counts
 * down from this value to zero at (CPU clock / 2), then reloads
 * (since we enable auto-reload below) and fires an interrupt each
 * time it hits zero.
 *
 * 83333 assumes a ~333 MHz timer clock (666 MHz CPU / 2), giving
 * 83333 / 333e6 ~= 250 us per tick, i.e. 4 kHz.
 *
 * IMPORTANT: this depends on actual PS clock configuration in
 * Vivado. If PYNQ-Z2 project's CPU clock differs from 666 MHz,
 * recompute: TIMER_LOAD_VALUE = (CPU_freq_Hz / 2) * 0.00025
 * It should be checked in real CPU clock in xparameters.h, look for
 * XPAR_CPU_CORTEXA9_0_CPU_CLK_FREQ_HZ.
 */
#define TIMER_LOAD_VALUE 81250 // 250us (4khz) = 650/2 * 0.000250

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
 * The pb extraction is: pb = xdata & 0xf  (4 buttons, bits [3:0]).
 * We use bit 0 (the first pushbutton) as the response button here -
 * This can be changeed accordingly, if a different physical button is selected.
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

// variable for the rotation of leds during the idle state.
static volatile int ISR_Count;
static volatile int ISR_Led = 0;

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

// interrupt details
// activate the interrupt on the SCU timer.
// enable the timer on the ARM processor.
// Set up the interrupt controller.


/* ------------------------------------------------------------
 * Timer interrupt handler.
 *
 * This fires once every TIMER_LOAD_VALUE-derived period (~250 us).
 * Everything time-critical for the measurement lives here, NOT in
 * main() - this is what gives the measurement a fixed, known tick
 * rate independent of whatever main() happens to be doing.
 * ------------------------------------------------------------ */
static void TimerIntrHandler(void* CallBackRef)
{
	// casting the void pointer into XscuTimer pointer.
	// a void pointer can be casted into any datatype.
	XScuTimer* TimerInstance = (XScuTimer*)CallBackRef;
	unsigned int xdata;
	int pb;

	/*
	 * MANDATORY first step: acknowledge the timer's interrupt flag.
	 * If this is skipped, the flag stays set and the interrupt
	 * re-fires immediately (or the IRQ line just stays asserted),
	 * effectively locking up the CPU in the ISR.
	 */
	XScuTimer_ClearInterruptStatus(TimerInstance);

	// reading the peripherals
	xdata = REG(0);
	pb = xdata & 0xf;

	switch (ISR_State){
	case ISRST_IDLE:
		// can be left empty, but we can rotate the leds.
		if(ISR_Count > ROTATE_TICK_COUNTS){
			ISR_Count = 0;
			if(ISR_Led ==0){
				ISR_Led = 1;
			} else {
				ISR_Led = (ISR_Led << 1)&0xf;
			}
			REG(0) = ISR_Led;
		} else {
			ISR_Count++;
		}
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
		Wait_Timer++;
		if((pb & BTN_RESP) != 0){
			ISR_State = ISRST_ERROR;
		}

		if(Wait_Timer >= Wait_Value){
			// turning the leds on and gogin to the running state.
			REG(0) = 0xf;
			ISR_State = ISRST_RUN;
		}
		break;

	case ISRST_RUN:
		if(REG(0) & BTN_RESP){
			REG(0) = 0;
			ISR_State = ISRST_DONE;
		}
		Resp_Timer++;
		if(Resp_Timer > 40000){
			REG(0) = 0;
			ISR_State = ISRST_TIMEOUT;
		}
		break;

	case ISRST_TIMEOUT:

		break;

	case ISRST_DONE:
		break;

	case ISRST_ERROR:
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
	char *cptr;
	int pb, sw;
	int second;


	/* ------------------------------------------------------------
	 * Interrupt setup - three layers, all required (CPU / GIC /
	 * peripheral), same pattern as resptstmn.c on the ZedBoard.
	 * ------------------------------------------------------------ */
	Xil_ExceptionInit();

	// lookup the config information for GIC
	IntcConfig = XScuGic_LookupConfig(XPAR_SCUGIC_0_DEVICE_ID);

	// initialize the GIC using the config information
	XScuGic_CfgInitialize(&Intc, IntcConfig, IntcConfig->CpuBaseAddress);
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_IRQ_INT, (Xil_ExceptionHandler)XScuGic_InterruptHandler, &Intc);

	/* 2. Peripheral level: configure the private timer itself -
	 *    load value, auto-reload so it free-runs periodically. */
	pTimerConfig = XScuTimer_LookupConfig(XPAR_XSCUTIMER_0_DEVICE_ID);
	XScuTimer_CfgInitialize(&pTimer, pTimerConfig, pTimerConfig->BaseAddr);
	XScuTimer_EnableAutoReload(&pTimer);
	XScuTimer_LoadTimer(&pTimer, TIMER_LOAD_VALUE);

	/* 3. GIC level: connect our handler to the timer's specific
	 *    interrupt ID, then unmask that ID at the GIC. */
	XScuGic_Connect(&Intc, XPAR_SCUTIMER_INTR, (Xil_ExceptionHandler)TimerIntrHandler, (void*)&pTimer);
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


    init_platform();
    do{
    	printf(">> "); fflush(stdout);
    	my_getline(CMD);
    	if(!strcmp(CMD, "exit")){
    		terminate = 1;
    	} else if(!strcmp(CMD, "help")){
    		print_help();
    	} else if(!strcmp(CMD, "test")){
    		printf("test will be implemented here. \n");

    		// writing the value of 15 to the leds.
    		xdata = 15;
    		REG(0) = (xdata & 0b1111);
    		printf("the led is on for 10 sec and then turns off. \n");
    		usleep(3000000);
    		REG(0) = 0;
    		printf("now the leds are off. \n");

    		// reading the value of the switches and push buttons.
    		xdata = REG(0);
    		pb = (xdata & 0xf);
    		sw = (xdata >> 4)&0b11;
    		printf("pushbuttons : %x, switches : %x ", pb, sw);
    		printf("test run. \n");
    	} else if(!strncmp(CMD, "start", 5)){
    		if(sscanf(&CMD[5], "%d", &second) != 1){
    			printf("failed to parse the command. \n");
    			printf("usage is : start N (N = 1, 2, 3, ....). \n");
    		} else {
    			Wait_Value = 4000 * second;
    			/* Hand control to the ISR by arming it, then busy-wait
				 * here until the ISR reaches a terminal state. main()
				 * does no timing itself - it's just watching. */
    			ISR_State = ISRST_ARMED;

    			while(ISR_State != ISRST_ERROR && ISR_State != ISRST_TIMEOUT && ISR_State != ISRST_DONE){
    				// spin and do nothing.
    			}

    			if(ISR_State == ISRST_ERROR){
    				printf("that is a false start. \n");
    			} else if(ISR_State == ISRST_TIMEOUT){
    				printf("no button pressed during this time. \n");
    			} else {
    				printf("The response time is : %.4f ms \n", 0.25 * Resp_Timer);
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

    printf("Thank you for using Response Test Application. \n");
    cleanup_platform();
    return 0;
}
