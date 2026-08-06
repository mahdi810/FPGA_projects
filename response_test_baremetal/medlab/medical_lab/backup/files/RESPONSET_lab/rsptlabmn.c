/*
 * resptstmn.c: response test main
 */


#include <stdio.h>
#include "platform.h"
#include "xscugic.h"
#include "xil_exception.h"
#include "xscutimer.h"
#include "xparameters.h"

#define NREGS 4
#define ZBIPREG_REG(k)  (*(volatile unsigned int *)(XPAR_ZBPERIPH_0_S00_AXI_BASEADDR+4*k))

// ---- interrupt controller -----
static XScuGic  Intc;					// interrupt controller instance
static XScuGic_Config  *IntcConfig;		// configuration instance

// ---- scu timer -----
static XScuTimer  pTimer;				// private Timer instance
static XScuTimer_Config  *pTimerConfig;	// configuration instance
// define TIMER_LOAD_VALUE  166667		// should be 2 KHz (500.0015us)
#define TIMER_LOAD_VALUE  83333		    // should be 4 KHz (249.99925us)


static volatile int ISR_Count;
static volatile unsigned int ISR_Leds;
static volatile int ISR_State;
static volatile int Wait_Timer, Wait_Value, Resp_Timer;

#define ISRST_IDLE     0
#define ISRST_ARMED    1
#define ISRST_WAIT     2
#define ISRST_RUN      3
#define ISRST_TIMEOUT  4
#define ISRST_DONE     5
#define ISRST_ERROR    6

// Center Button
#define BTN_C 0x0100

#define CBUF_LEN 64

/*
 * ------------------------------------------------------------
 * Interrupt handler (ZYNQ private timer)
 * ------------------------------------------------------------
 */
static void TimerIntrHandler(void *CallBackRef)
{
	XScuTimer *TimerInstance = (XScuTimer *)CallBackRef;
	unsigned int xdata;

	XScuTimer_ClearInterruptStatus(TimerInstance);
	switch (ISR_State) {
		case ISRST_IDLE:
			if (ISR_Count >= 400) {
				ISR_Count = 0;
				if (ISR_Leds == 0) {
					ISR_Leds = 1;
				} else {
					ISR_Leds = (ISR_Leds << 1) & 0x0ff;
				}
				ZBIPREG_REG(0) = ISR_Leds;
			} else {
				ISR_Count++;
			}
			break;

		case ISRST_ARMED:
			Wait_Timer = 0;
			Resp_Timer = 0;
			ZBIPREG_REG(0) = 0;
			ISR_State = ISRST_DONE;
			break;

		case ISRST_WAIT:
			break;

		case ISRST_RUN:
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
	unsigned int  xval, rnum;
	char cbuf[CBUF_LEN], *chp;
	int  terminate, isr_run, xi, kk;

    init_platform();
    printf("--- Response Time Test V0.1 ---\n\r");
    ISR_State = ISRST_IDLE;
    ISR_Count = 0;
    ISR_Leds = 0;
    Wait_Timer = 0;
    Resp_Timer = 0;
    Wait_Value = 0;

    printf(" * initialize exceptions...\n\r");
    Xil_ExceptionInit();

    printf(" * lookup config GIC...\n\r");
    IntcConfig = XScuGic_LookupConfig(XPAR_SCUGIC_0_DEVICE_ID);
    printf(" * initialize GIC...\n\r");
    XScuGic_CfgInitialize(&Intc, IntcConfig, IntcConfig->CpuBaseAddress);

	// Connect the interrupt controller interrupt handler to the hardware
    printf(" * connect interrupt controller handler...\n\r");
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_IRQ_INT,
				(Xil_ExceptionHandler)XScuGic_InterruptHandler, &Intc);

    printf(" * lookup config scu timer...\n\r");
    pTimerConfig = XScuTimer_LookupConfig(XPAR_XSCUTIMER_0_DEVICE_ID);
    printf(" * initialize scu timer...\n\r");
    XScuTimer_CfgInitialize(&pTimer, pTimerConfig, pTimerConfig->BaseAddr);
    printf(" * Enable Auto reload mode...\n\r");
	XScuTimer_EnableAutoReload(&pTimer);
    printf(" * load scu timer...\n\r");
    XScuTimer_LoadTimer(&pTimer, TIMER_LOAD_VALUE);

    printf(" * set up timer interrupt...\n\r");
    XScuGic_Connect(&Intc, XPAR_SCUTIMER_INTR, (Xil_ExceptionHandler)TimerIntrHandler,
    				(void *)&pTimer);
    printf(" * enable interrupt for timer at GIC...\n\r");
    XScuGic_Enable(&Intc, XPAR_SCUTIMER_INTR);
    printf(" * enable interrupt on timer...\n\r");
    XScuTimer_EnableInterrupt(&pTimer);

	// Enable interrupts in the Processor.
    printf(" * enable processor interrupts...\n\r");
	Xil_ExceptionEnable();

    // check if we are still alive...
    xval = ZBIPREG_REG(0);
    printf("Buttons/Switches = %x\n\r", xval);

    terminate = 0;
    isr_run = 0;
    do {
    	printf(">> "); fflush(stdout);
    	fgets(cbuf, CBUF_LEN, stdin);
    	cbuf[CBUF_LEN-1] = '\0';
    	chp = cbuf;
    	do {
    		if (*chp == '\n' || *chp == '\r' ) {
    			*chp = '\0';
    		}
    	} while (*chp++ != '\0');
    	printf("\r");  fflush(stdout);
    	if (!strcmp(cbuf, "exit")) {
        	terminate = 1;
    	} else if (!strcmp(cbuf, "isr")) {
    		ISR_State = ISRST_IDLE;
    		if (isr_run == 0) {
    		    // start scu timer
    		    printf(" * start timer...");
    		    XScuTimer_Start(&pTimer);
    		    isr_run = 1;
    		    printf("interrupt ON.\n\r");
    		} else {
    		    // stop scu timer
    		    printf(" * stop timer...");
    		    XScuTimer_Stop(&pTimer);
    			isr_run = 0;
    		    printf("interrupt OFF.\n\r");
    		}
    		printf("ISR count: %d\n\r", ISR_Count);
    	} else if (!strcmp(cbuf, "rr")) {
    		for (kk = 0; kk < NREGS; kk++) {
        	    xval = ZBIPREG_REG(kk);
        	    printf("R%d: %8x\n\r", kk, xval);
    		}
    	} else if (!strncmp(cbuf, "wr", 2)) {
    		if (sscanf(&cbuf[2], "%x %x", &rnum, &xval) != 2) {
    			printf(" *** int read error.\n\r");
    		} else {
    			printf("  %x ==> R%d\n\r", xval, rnum);
    			ZBIPREG_REG(rnum) = xval;
    		}
    	} else if (!strncmp(cbuf, "start", 5)) {
    		if (isr_run == 0) {
    			printf(" *** interrupts are off.\n\r");
    		} else if (sscanf(&cbuf[5], "%d", &xi) != 1) {
    			printf(" *** int (2) read error.\n\r");
    		} else if ((xi < 1) || (xi > 10)) {
    			printf(" *** value out of range.\n\r");
    		} else {
    			printf("Run...\n\r");
    			Wait_Value = 4000 * xi;
    			ISR_State = ISRST_ARMED;
    			do {
    				xi = ISR_State;
    			} while ((xi != ISRST_DONE) && (xi != ISRST_ERROR) && (xi != ISRST_TIMEOUT));
    			if (xi == ISRST_ERROR) {
        			printf(" *** button center activated before all LEDs were on ***\n\r");
    			} else if (xi == ISRST_TIMEOUT) {
        			printf("  ==> No button activated since: %.2f ms\n\r", 0.25*Resp_Timer);
    			} else {
        			printf("  ==> Response time: %.2f ms\n\r", 0.25*Resp_Timer);
    			}
    			printf("  ....done.\n\r");
    		}
    	} else if (strlen(cbuf) > 0) {
    		xil_printf(" *** unknown command |%s|\n\r", cbuf);
    	}
    } while (terminate == 0);


    printf("shutting down...\n\r");
    XScuTimer_Stop(&pTimer);
	Xil_ExceptionDisable();
    XScuTimer_DisableInterrupt(&pTimer);
    XScuGic_Disable(&Intc, XPAR_SCUTIMER_INTR);
    ZBIPREG_REG(0) = 0x0;
    printf("Thank you for using Response Time Test V0.1.\n\r");
    cleanup_platform();
    return 0;
}
