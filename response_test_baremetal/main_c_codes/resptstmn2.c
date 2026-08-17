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
 * resptstmn.c : response test main
 */

#include <stdio.h>
#include <string.h>
#include "platform.h"
#include "xparameters.h"

#define REG(k) *(volatile unsigned int*)(XPAR_RESPTST_0_S00_AXI_BASEADDR + 4*k)
#define CMDLEN 128

typedef enum {
	ISRST_IDLE,
	ISRST_ARMED,
	ISRST_WAIT,
	ISRST_RUN,
	ISRST_TIMEOUT,
	ISRST_ERROR,
	ISRST_DONE
}ISR_STATE;

volatile ISR_STATE isr_state = ISRST_IDLE;

volatile static int isr_count;
volatile static int wait_time;
volatile static int wait_value;
volatile static int resp_time;
volatile static int isr_led;


#define TIMER_LOAD_VALUE 81250

// includes for the interrupts and the timer.
// this program is going to generate a sotware interrupt each 250us and increment a counter.
// and later that counter will be used as a base for calculating the time taken for certain action.
// in this method the resolution is 250us or 0.25ms
#include "xscugic.h"
#include "xil_exception.h"
#include "xscutimer.h"

// defining the interrupt and the timer instances and their configuration files.
static XScuGic Intc;
static XScuGic_Config *IntcConfig;

static XScuTimer pTimer;
static XScuTimer_Config *pTimerConfig;

// defining the handler function
static void myTimerIntrHandler(void *CallBackRef);
void print_help(){
	printf("exit     : to exit the program. \n");
	printf("help     : to print this help menu. \n");
	printf("start N  : to start the response test, N is the pre delay. \n");
}

void my_getline(char* cmd)
{
	char ch;
	int i;
	for(i = 0; i < CMDLEN - 1; i++)
	{
		ch = getchar();
		if(ch == '\r' || ch == '\n'){
			ch = '0';
			break;
		}
		cmd[i] = ch;
	}
	cmd[i] = '\0';
}


int main()
{
	int status;
    init_platform();


    // 1. Initialize the exception table.
    Xil_ExceptionInit();
    printf("Xil_ExceptionInit initialized.... \n");

    // 2. Initialize the interrupt controller (GIC).
    // 2a. initialize the lookup table for the gic
    IntcConfig = XScuGic_LookupConfig(XPAR_SCUGIC_0_DEVICE_ID);
    if (IntcConfig == NULL) {
        return XST_FAILURE;
    }
    printf("Interrupt controller lookup table configured... \n");

    // 2b. initialize the cfg interrupt controller.
    status = XScuGic_CfgInitialize(&Intc, IntcConfig, IntcConfig->CpuBaseAddress);
    if (status != XST_SUCCESS){
        	return XST_FAILURE;
        }
    printf("cgf of the interrupt controller initialized.... \n");

    // 3. Register the interrupt controller's handler in the exception table
    //    (connects the ARM IRQ vector to the GIC dispatcher).
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
    					 (Xil_ExceptionHandler)XScuGic_InterruptHandler,
    					 &Intc);
    printf("interrupt handler registered with the xil_exception.....\n");

    // 4. Initialize the timer peripheral (config + load value/prescaler/auto-reload).
    // Lookup, then cfg-initialize — same two-step pattern as the GIC
    pTimerConfig =  XScuTimer_LookupConfig(XPAR_XSCUTIMER_0_DEVICE_ID);
    if(pTimerConfig == NULL)
    {
    	return XST_FAILURE;
    }
    printf("the timer configuration initialized..... \n");


    // cfg initializing for the timer
    status = XScuTimer_CfgInitialize(&pTimer, pTimerConfig, pTimerConfig->BaseAddr);
    if (status != XST_SUCCESS){
    	return XST_FAILURE;
    }
    XScuTimer_LoadTimer(&pTimer, TIMER_LOAD_VALUE);
    XScuTimer_EnableAutoReload(&pTimer);
    printf("cfg initialized for the timer.....\n");

    // 5. Connect your ISR to the timer's interrupt ID in the GIC.
    status = XScuGic_Connect(&Intc, XPAR_SCUTIMER_INTR,
    			(Xil_InterruptHandler)myTimerIntrHandler, (void *)&pTimer);
    if (status != XST_SUCCESS){
        	return XST_FAILURE;
        }
    printf("the isr connected to the GIC.....\n");

    // 6. Enable that interrupt ID at the GIC level.
    XScuGic_Enable(&Intc, XPAR_SCUTIMER_INTR);
    printf("the interrupt at GIC level enabled....\n");

    // 7. Enable interrupt generation at the timer itself.
    XScuTimer_EnableInterrupt(&pTimer);
    printf("Interrupt generation enabled at the timer itself.....\n");

    // 8. Globally enable exceptions/interrupts (must be last).
    Xil_ExceptionEnable();
    printf("global exception/interrupt enabled......\n");

    // 9. Start the timer.
    XScuTimer_Start(&pTimer);
    printf("isr timer started......\n");


    char cmd[CMDLEN];
    int xdata;
    int terminate = 0;
    int second;

    print_help();
    do
    {
    	printf(">> "); fflush(stdout);
    	my_getline(cmd);
    	if(!strcmp(cmd, "exit")){
    		terminate = 1;
    	} else if(!strcmp(cmd, "help")){
    		print_help();
    	} else if(!strncmp(cmd, "start", 5)){
    		if(sscanf(&cmd[5], "%d", &second) != 1){
    			printf("invalid entry : %s \n", cmd);
    		} else {
    		    wait_value = 4000 * second;
    		    isr_state = ISRST_ARMED;

    		    // wait for the trial to actually finish
    		    while (isr_state != ISRST_TIMEOUT &&
    		           isr_state != ISRST_ERROR   &&
    		           isr_state != ISRST_DONE) {
    		        // spin — ISR updates isr_state in the background
    		    }
    		}

    		if (isr_state == ISRST_TIMEOUT) {
    		    printf("you didn't press any button in time. \n");
    		    isr_state = ISRST_IDLE;
    		} else if (isr_state == ISRST_ERROR) {
    		    printf("you pressed the button before the leds are on \n");
    		    isr_state = ISRST_IDLE;
    		} else if (isr_state == ISRST_DONE) {
    			printf("the response time is : %.4f ms \n", resp_time * 0.250);
    		}
    	}
    }while(terminate == 0);

    printf("shutting down...\n\r");
	XScuTimer_Stop(&pTimer);
	Xil_ExceptionDisable();
	XScuTimer_DisableInterrupt(&pTimer);
	XScuGic_Disable(&Intc, XPAR_SCUTIMER_INTR);
	printf("Thank you for using Response Test Application. \n");
    cleanup_platform();
    return 0;
}



static void myTimerIntrHandler(void *CallBackRef){
	// get the interrupt timer from the callbackref argument
	XScuTimer *TimerInstance = (XScuTimer *)CallBackRef;

	// clear the timer interrupt status
	XScuTimer_ClearInterruptStatus(&pTimer);

	switch (isr_state){
		case ISRST_IDLE:
			if (isr_count >= 400)
			{
				isr_count = 0;
				if(isr_led == 0){
					isr_led = 1;
				}
				REG(0) = isr_led;
				isr_led = (isr_led << 1) & 0xf;
			}
			else
			{
				isr_count++;
			}
			break;
		case ISRST_ARMED:
			resp_time = 0;
			wait_time = 0;
			REG(0) = 0;
			isr_state = ISRST_WAIT;
			break;
		case ISRST_WAIT:
			wait_time++;
			if(wait_time >= wait_value)
			{
				REG(0) = 0xf;
				isr_state = ISRST_RUN;
			}
			else if((REG(0) & 0x1) == 1){
				isr_state = ISRST_ERROR;
			}
			break;
		case ISRST_RUN:
			resp_time++;
			if(REG(0) &0x1){
				isr_state = ISRST_DONE;
				REG(0) = 0;
			}
			else if(resp_time >= 40000)
			{
				isr_state = ISRST_TIMEOUT;
				REG(0) = 0;
			}
			break;
		case ISRST_TIMEOUT:
			break;
		case ISRST_ERROR:
			break;
		case ISRST_DONE:
			isr_state = ISRST_IDLE;
			break;
		default:
			isr_state = ISRST_IDLE;
	}
}
