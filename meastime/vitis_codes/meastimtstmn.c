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

#include <stdio.h>
#include <string.h>
#include "platform.h"
#include <stdint.h>
#include <inttypes.h>
#include "xparameters.h"

#define REG(k) *(volatile unsigned int*)(XPAR_MEASTIME_0_S00_AXI_BASEADDR + 4*k)
#define CMDLEN 128

void startTimer()
{
	REG(1) = 0x1;
}

void stop_timer(){
	REG(1) = 0x0;
}

uint64_t getElapasedTime(void)
{
	uint64_t Time0, Time1, Time;
	Time0 = REG(0);
	Time1 = REG(1);
	Time = ((Time1 << 32) | Time0);
	return Time;
}

void my_getline(char *cmd)
{
	char ch;
	int i;
	for(i = 0; i < CMDLEN - 1; i++){
		ch = getchar();
		if (ch == '\r' || ch == '\n')
		{
			ch = '\0';
			break;
		}
		cmd[i] = ch;
	}
	cmd[i] = '\0';
}

void print_help(){
	printf("help    : to print this help menu. \n");
	printf("exit    : to exit the program. \n");
	printf("start   : to start the program. \n");
}


int main()
{
	int terminate = 0;
	char cmd[CMDLEN];
	int xdata;

	uint64_t start_time, end_time, elapsed_tick, elapsed_ns;
    init_platform();

    print_help();
    do{
    	printf(">> "); fflush(stdout);
    	my_getline(cmd);
    	if(!strcmp(cmd, "start"))
    	{
    		startTimer();
    		start_time = getElapasedTime();

    		printf("help. \n");


    		end_time = getElapasedTime();
    		elapsed_tick = end_time - start_time;
    		printf("the elapsed ticks are : %"PRIu64" \n", elapsed_tick); // each tick is 10ns
    		printf("the time precesion is %"PRIu64" ns \n", elapsed_tick * 10);
    		stop_timer();
    	} else if(!strcmp(cmd, "start2")){
    		REG(2) = 0x1;      // reset counter
    		REG(1) = 0x1;      // start
    		int kk;
    		for(int j = 0; j < 1000000; j++){
    			kk++;
    		}
    		REG(1) = 0x0;      // stop — counter frozen

    		uint32_t t0 = REG(0);
    		uint32_t t1 = REG(1);
    		uint64_t elapsed_tick = ((uint64_t)t1 << 32) | (uint64_t)t0;
    		uint64_t elapsed_ns = elapsed_tick * 10ULL;   // 100 MHz PL clock

    		printf("printf took: %" PRIu64 " ticks = %" PRIu64 " ns = %.3f us\n",
    		       elapsed_tick, elapsed_ns, elapsed_ns / 1000.0);
    	} else if(!strcmp(cmd, "help")){
    		print_help();
    	} else if(!strcmp(cmd, "exit")){
    		terminate = 1;
    	} else if(!strcmp(cmd, "reset")){
    		REG(2) = 0;
    		printf("counter reseted. \n");
    	} else if(strlen(cmd) > 1){

    		printf("invalid entry : %s \n", cmd);
    	}
    }while(terminate == 0);

    printf("Hello World\n\r");
    printf("Successfully ran Hello World application");
    cleanup_platform();
    return 0;
}










