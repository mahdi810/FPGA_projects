# Timer and Interrupt Functions

This document explains the timer- and interrupt-related functions used in the Zynq response-time application. The application uses the ARM Cortex-A9 **SCU private timer** as a periodic timing source and the **Generic Interrupt Controller (GIC)** to deliver timer interrupts to the processor.

The timer is configured to generate one interrupt approximately every 250 µs. Each interrupt is treated as one timing tick for the reaction-time state machine.

## Objects Used

```c
static XScuGic Intc;
static XScuGic_Config *IntcConfig;

static XScuTimer pTimer;
static XScuTimer_Config *pTimerConfig;
```

| Object | Purpose |
|---|---|
| `Intc` | Live software-driver instance for the GIC. It is used to connect, enable, and disable interrupts. |
| `IntcConfig` | Pointer to the hardware configuration of the GIC, supplied by the board support package (BSP). |
| `pTimer` | Live software-driver instance for the SCU private timer. |
| `pTimerConfig` | Pointer to the hardware configuration of the private timer, supplied by the BSP. |

The configuration pointers describe where the hardware is located. The driver instances store the initialized state used by the application.

## Timer Tick Configuration

```c
#define TIMER_LOAD_VALUE 81250
```

The private timer counts down from `TIMER_LOAD_VALUE` to zero. In this project, the intended timer period is 250 µs:

```text
Timer clock = CPU clock / 2
Timer clock = 650 MHz / 2 = 325 MHz
Tick period = 81250 / 325,000,000 = 250 µs
```

Thus, the timer should generate approximately 4,000 interrupts per second. The value must be recalculated if the actual CPU clock differs from 650 MHz.

## Exception-System Functions

### `Xil_ExceptionInit()`

```c
Xil_ExceptionInit();
```

Initializes the ARM exception system. This prepares the exception vector mechanism before IRQ handlers are registered and enabled.

It must be called before installing the GIC interrupt handler.

### `Xil_ExceptionRegisterHandler()`

```c
Xil_ExceptionRegisterHandler(
    XIL_EXCEPTION_ID_IRQ_INT,
    (Xil_ExceptionHandler)XScuGic_InterruptHandler,
    &Intc
);
```

Registers the GIC's top-level interrupt dispatcher as the processor's IRQ exception handler.

When an IRQ reaches the Cortex-A9, the processor enters the IRQ exception path and calls `XScuGic_InterruptHandler()`. The GIC handler identifies the interrupt source and invokes the application-specific handler associated with that interrupt ID.

| Argument | Meaning |
|---|---|
| `XIL_EXCEPTION_ID_IRQ_INT` | Selects the normal IRQ exception type. |
| `XScuGic_InterruptHandler` | The GIC dispatcher function. |
| `&Intc` | Pointer passed to the GIC dispatcher so it can access the initialized GIC instance. |

### `Xil_ExceptionEnable()`

```c
Xil_ExceptionEnable();
```

Globally enables IRQ exceptions at the ARM processor level. Even if the timer and GIC are configured correctly, timer interrupts cannot reach `TimerIntrHandler()` until this function is called.

### `Xil_ExceptionDisable()`

```c
Xil_ExceptionDisable();
```

Globally disables ARM exceptions during shutdown. It prevents the processor from accepting another timer interrupt while the application is stopping the timer and cleaning up the platform.

## GIC Functions

### `XScuGic_LookupConfig()`

```c
IntcConfig = XScuGic_LookupConfig(XPAR_SCUGIC_0_DEVICE_ID);
```

Finds the BSP-generated configuration for the GIC identified by `XPAR_SCUGIC_0_DEVICE_ID`. The returned configuration contains hardware information, including the GIC base address.

### `XScuGic_CfgInitialize()`

```c
XScuGic_CfgInitialize(&Intc, IntcConfig, IntcConfig->CpuBaseAddress);
```

Initializes the GIC driver instance named `Intc`.

| Argument | Meaning |
|---|---|
| `&Intc` | Address of the live GIC driver instance to initialize. |
| `IntcConfig` | BSP configuration returned by `XScuGic_LookupConfig()`. |
| `IntcConfig->CpuBaseAddress` | CPU interface base address for the GIC. |

After this call, the application can connect interrupt handlers and enable specific interrupt IDs.

### `XScuGic_Connect()`

```c
XScuGic_Connect(
    &Intc,
    XPAR_SCUTIMER_INTR,
    (Xil_ExceptionHandler)TimerIntrHandler,
    (void *)&pTimer
);
```

Associates the SCU private-timer interrupt ID with the application's timer ISR, `TimerIntrHandler()`.

When the GIC receives interrupt `XPAR_SCUTIMER_INTR`, it calls `TimerIntrHandler()` and passes `&pTimer` as its callback reference. The callback pointer lets the handler access the timer instance that generated the interrupt.

### `XScuGic_Enable()`

```c
XScuGic_Enable(&Intc, XPAR_SCUTIMER_INTR);
```

Unmasks the SCU private-timer interrupt in the GIC. The timer may assert an interrupt signal, but the GIC will not forward that signal to the CPU until this interrupt ID is enabled.

### `XScuGic_InterruptHandler()`

```c
XScuGic_InterruptHandler
```

This function is not called directly by `main()`. It is registered with the ARM exception system and acts as the GIC's interrupt dispatcher.

Its role is to determine the active interrupt ID, locate the callback registered by `XScuGic_Connect()`, and call the associated handler. For the timer interrupt, it dispatches to `TimerIntrHandler()`.

### `XScuGic_Disable()`

```c
XScuGic_Disable(&Intc, XPAR_SCUTIMER_INTR);
```

Masks the timer interrupt in the GIC during shutdown. This prevents the GIC from delivering further SCU timer interrupts to the CPU.

## SCU Private-Timer Functions

### `XScuTimer_LookupConfig()`

```c
pTimerConfig = XScuTimer_LookupConfig(XPAR_XSCUTIMER_0_DEVICE_ID);
```

Finds the BSP configuration for the SCU private timer. `XPAR_XSCUTIMER_0_DEVICE_ID` identifies the timer instance in the hardware platform.

### `XScuTimer_CfgInitialize()`

```c
XScuTimer_CfgInitialize(&pTimer, pTimerConfig, pTimerConfig->BaseAddr);
```

Initializes the live timer driver instance, `pTimer`, using the configuration returned by `XScuTimer_LookupConfig()`.

| Argument | Meaning |
|---|---|
| `&pTimer` | Address of the timer driver instance to initialize. |
| `pTimerConfig` | Timer hardware configuration from the BSP. |
| `pTimerConfig->BaseAddr` | Memory-mapped base address of the SCU private timer. |

### `XScuTimer_EnableAutoReload()`

```c
XScuTimer_EnableAutoReload(&pTimer);
```

Enables periodic mode. When the timer counter reaches zero, it automatically reloads the load value and starts counting down again.

Without auto-reload, the timer would generate one expiration event and stop. Auto-reload is necessary because the application requires a continuous 250 µs tick.

### `XScuTimer_LoadTimer()`

```c
XScuTimer_LoadTimer(&pTimer, TIMER_LOAD_VALUE);
```

Loads `TIMER_LOAD_VALUE` into the timer countdown register. The timer then counts down from this value to zero when started.

Because auto-reload is enabled, this same value is loaded again after every expiry.

### `XScuTimer_EnableInterrupt()`

```c
XScuTimer_EnableInterrupt(&pTimer);
```

Enables interrupt generation inside the timer peripheral. This is separate from `XScuGic_Enable()`:

- `XScuTimer_EnableInterrupt()` allows the timer hardware to assert its interrupt output.
- `XScuGic_Enable()` allows the GIC to forward that interrupt to the CPU.

Both must be enabled.

### `XScuTimer_Start()`

```c
XScuTimer_Start(&pTimer);
```

Starts the countdown operation. Once started, the timer repeatedly counts down, expires, reloads, and generates periodic interrupts.

The application sets `ISR_State` to `ISRST_IDLE` before starting the timer. Therefore, the first interrupts perform only the idle LED-rotation behavior until the user starts a reaction-time test.

### `XScuTimer_ClearInterruptStatus()`

```c
XScuTimer_ClearInterruptStatus(TimerInstance);
```

Clears the private timer's interrupt-pending flag. This is the first operation performed inside `TimerIntrHandler()`.

The acknowledgement is mandatory. When the counter reaches zero, the timer sets an interrupt status bit. If that bit is not cleared, the interrupt remains pending and the processor can repeatedly re-enter the handler instead of returning to normal execution.

### `XScuTimer_Stop()`

```c
XScuTimer_Stop(&pTimer);
```

Stops the private timer during application shutdown. After this call, the timer no longer counts down or creates new expiration events.

### `XScuTimer_DisableInterrupt()`

```c
XScuTimer_DisableInterrupt(&pTimer);
```

Disables interrupt generation in the timer peripheral during shutdown. This is performed after stopping the timer and globally disabling CPU exceptions.

## Application Timer ISR

### `TimerIntrHandler()`

```c
static void TimerIntrHandler(void *CallBackRef)
```

This is the application-specific timer interrupt service routine (ISR). It is executed once per timer expiration, approximately every 250 µs.

The `CallBackRef` argument is the pointer registered during `XScuGic_Connect()`:

```c
(void *)&pTimer
```

The handler converts it back to a timer pointer:

```c
XScuTimer *TimerInstance = (XScuTimer *)CallBackRef;
```

### ISR Operation Sequence

Each invocation performs these actions:

1. Clears the timer interrupt status with `XScuTimer_ClearInterruptStatus()`.
2. Reads the custom AXI peripheral register with `REG(0)`.
3. Extracts the pushbutton bits using `pb = xdata & 0xf`.
4. Executes one step of the reaction-test state machine.
5. Updates LEDs, tick counters, and `ISR_State` as needed.

The ISR implements the following states:

| State | ISR behavior |
|---|---|
| `ISRST_IDLE` | Rotates the LED pattern periodically using `ISR_Count` and `ISR_Led`. |
| `ISRST_ARMED` | Clears `Wait_Timer` and `Resp_Timer`, turns LEDs off, and enters `ISRST_WAIT`. |
| `ISRST_WAIT` | Counts pre-stimulus ticks. A button press causes a false-start error. When `Wait_Value` is reached, all LEDs turn on and the state becomes `ISRST_RUN`. |
| `ISRST_RUN` | Counts response-time ticks while LEDs are on. A valid button press ends the test; more than 40,000 ticks causes a timeout. |
| `ISRST_DONE` | Keeps the completed result available for `main()` to print. |
| `ISRST_TIMEOUT` | Keeps the timeout result available for `main()` to print. |
| `ISRST_ERROR` | Keeps the false-start result available for `main()` to print. |

## Shared Variables and `volatile`

```c
static volatile int ISR_State;
static volatile int Wait_Timer;
static volatile int Wait_Value;
static volatile int Resp_Timer;
```

These variables are shared between normal program flow in `main()` and asynchronous execution in `TimerIntrHandler()`. They are declared `volatile` so the compiler always reads their current memory value instead of reusing an outdated value stored in a CPU register.

For example, `main()` waits for the ISR to finish a trial:

```c
while (ISR_State != ISRST_ERROR &&
       ISR_State != ISRST_TIMEOUT &&
       ISR_State != ISRST_DONE) {
}
```

Without `volatile`, the compiler could optimize this loop incorrectly and fail to observe state changes made by the ISR.

## Initialization Sequence

The timer and interrupt configuration follows this order:

```c
Xil_ExceptionInit();

IntcConfig = XScuGic_LookupConfig(XPAR_SCUGIC_0_DEVICE_ID);
XScuGic_CfgInitialize(&Intc, IntcConfig, IntcConfig->CpuBaseAddress);
Xil_ExceptionRegisterHandler(
    XIL_EXCEPTION_ID_IRQ_INT,
    (Xil_ExceptionHandler)XScuGic_InterruptHandler,
    &Intc
);

pTimerConfig = XScuTimer_LookupConfig(XPAR_XSCUTIMER_0_DEVICE_ID);
XScuTimer_CfgInitialize(&pTimer, pTimerConfig, pTimerConfig->BaseAddr);
XScuTimer_EnableAutoReload(&pTimer);
XScuTimer_LoadTimer(&pTimer, TIMER_LOAD_VALUE);

XScuGic_Connect(
    &Intc,
    XPAR_SCUTIMER_INTR,
    (Xil_ExceptionHandler)TimerIntrHandler,
    (void *)&pTimer
);
XScuGic_Enable(&Intc, XPAR_SCUTIMER_INTR);

XScuTimer_EnableInterrupt(&pTimer);
Xil_ExceptionEnable();

ISR_State = ISRST_IDLE;
XScuTimer_Start(&pTimer);
```

The order matters: initialize the exception system first, initialize the GIC and timer, connect and enable the timer interrupt in the GIC, enable interrupt generation in the timer, enable CPU IRQs globally, and only then start the timer.

## Shutdown Sequence

The application stops interrupt activity before calling `cleanup_platform()`:

```c
XScuTimer_Stop(&pTimer);
Xil_ExceptionDisable();
XScuTimer_DisableInterrupt(&pTimer);
XScuGic_Disable(&Intc, XPAR_SCUTIMER_INTR);
```

Stopping and disabling the timer interrupt prevents the ISR from executing while the application is shutting down.

## Important Implementation Note

In `ISRST_RUN`, the code currently checks the response with:

```c
if (REG(0) & BTN_RESP)
```

The ISR already sampled the pushbuttons earlier using:

```c
xdata = REG(0);
pb = xdata & 0xf;
```

For clarity and to avoid a possible issue when register reads return LED output values, the response test should normally use:

```c
if ((pb & BTN_RESP) != 0)
```

This ensures that the decision is based on the button value sampled for the current interrupt tick.
