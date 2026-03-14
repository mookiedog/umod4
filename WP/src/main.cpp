#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <malloc.h>

#include "hardware.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "pico/mutex.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "timers.h"

#include "FlashConfig.h"
#include "FlashWp.h"
#include "Gps.h"
#include "lfsMgr.h"
#include "log_ids.h"
#include "Logger.h"
#include "NeoPixelConnect.h"
#include "Shell.h"
#include "Swd.h"
#include "Uart.h"
#include "umod4_WP.h"
#include "uart_rx32.pio.h"
#include "WiFiManager.h"
#include "NetworkManager.h"
#include "file_io_task.h"
#include "ota_flash_task.h"
#include "api_handlers.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/stats.h"
#include "lwip/memp.h"


extern "C" {
    extern char __bss_end__;
    extern char __StackLimit;
    extern char __StackTop;
    void* _sbrk(ptrdiff_t incr);
}


const char* SYSTEM_JSON = "{\"BT\":\"" __DATE__ " " __TIME__ "\"}";

NeoPixelConnect* rgb_led;
Logger* logger;
Shell* dbgShell;
Swd* swd;

static int pio_sm_uart;

WiFiManager* wifiMgr = nullptr;
NetworkManager* networkMgr = nullptr;
Gps* gps = nullptr;

// Device config loaded from flash at boot (or compile-time defaults)
flash_config_t g_flash_config;

// Device name exposed to api_handlers.cpp and upload_handler.cpp
char g_device_name[64] = "";
const char* get_device_name(void) { return g_device_name; }

void pico_set_led(bool led_on);

// This array tracks the most recently-received data from the ECU data stream
// The array is indexed by the ECU log ID
//  * 8-bit log entries are stored in the lower byte of each 16-bit word
//  * 16-bit log entries are stored in the full 16-bit word
uint16_t ecuLiveLog[256];

// Timestamp (time_us_32) of the most recent word received from the EP via UART.
// Updated inside isr_rx32 on every log event. Zero until first event arrives.
// Read by generate_api_ecu_live_data_json() to compute data age for the UI.
volatile uint32_t g_last_ecu_data_us = 0;
uint32_t get_last_ecu_data_us(void) { return g_last_ecu_data_us; }

// Timestamp (time_us_32) of the most recent crankshaft event from the ECU.
// Updated in isr_rx32 when LOGID_ECU_CRANKREF_ID_TYPE_U8 is received.
// Zero until the first crank event — engine is considered running if this
// is less than 1 second old. Used to guard image-store write operations.
volatile uint32_t g_last_crank_event_us = 0;
uint32_t get_last_crank_event_us(void) { return g_last_crank_event_us; }

#ifdef configSUPPORT_STATIC_ALLOCATION
// If FreeRTOS static allocation is allowed, then we are required to
// statically allocate the idle and timer task here:

// Core 0: Single variable
static StaticTask_t xIdleTaskTCB __attribute__((aligned(8)));
static StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE] __attribute__((aligned(8)));

// Core 1+: Arrays
static StaticTask_t xPassiveIdleTaskTCBs[configNUMBER_OF_CORES - 1] __attribute__((aligned(8)));
static StackType_t uxPassiveIdleTaskStacks[configNUMBER_OF_CORES - 1][configMINIMAL_STACK_SIZE] __attribute__((aligned(8)));


void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer,
                                    StackType_t **ppxIdleTaskStackBuffer,
                                    uint32_t *pulIdleTaskStackSize )
{
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = &uxIdleTaskStack[0];

    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void vApplicationGetPassiveIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer,
                                           StackType_t **ppxIdleTaskStackBuffer,
                                           uint32_t *pulIdleTaskStackSize,
                                           BaseType_t xCoreID )
{
    // NOTE: xCoreID will be 0 for RP2350 core.1, meaning that xCoreID 0 is the zeroth __additional__ core beyond RP2350 core.0.
    // Subscript [xCoreID][0] points to the start of the specific core's stack
    *ppxIdleTaskTCBBuffer = &xPassiveIdleTaskTCBs[xCoreID];
    *ppxIdleTaskStackBuffer = &uxPassiveIdleTaskStacks[xCoreID][0];

    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

// And for the timer task
static StaticTask_t xTimerTaskTCB;
static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];

void vApplicationGetTimerTaskMemory( StaticTask_t **ppxTimerTaskTCBBuffer,
                                     StackType_t **ppxTimerTaskStackBuffer,
                                     uint32_t *pulTimerTaskStackSize )
{
    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}
#endif

// --------------------------------------------------------------------------------------------
// Add an idle task where we sleep to provide a minor power savings.
void vApplicationIdleHook( void )
{
    __wfi();
}

// --------------------------------------------------------------------------------------------
// Stack overflow detection hook - called when FreeRTOS detects a stack overflow
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    printf("STACK OVERFLOW in task: %s\n", pcTaskName);
    panic("Stack overflow");
}


// ----------------------------------------------------------------------------------
void startGps()
{
    static Uart* uart_gps;

    // Set up the UART connected to the GPS
    uart_gps = new Uart(GPS_UART_ID, GPS_TX_PIN, GPS_RX_PIN);
    uart_gps->configFormat(8, 1, UART_PARITY_NONE);
    uart_gps->configFlowControl(false, false);
    uart_gps->configBaud(9600);
    uart_gps->enable();
    uart_gps->rxIntEnable();

    // Create the GPS object now that we have its UART
    // The Gps object will create its own FreeRTOS task to process incoming GPS data
    gps = new Gps(uart_gps);

    //initPpsInterrupt();
}


// ----------------------------------------------------------------------------------
// The ISR for the 32-bit UART receiving ECU logging data from the EP.
// The EP always sends over log events as a full word of data:
//      bits 0..8:      length, which can only be 1, 2, or 3:
//                          - 1 means LogID only
//                          - 2 means LogID and LSB
//                          - 3 means LogID and LSB and MSB
//      bits 8..15:     8-bit LogID
//      bits 16..23:    LSB of the log data
//      bits 24..31:    MSB of the log data
//
// This ISR moves ECU events from the receive FIFO to the merged log buffer.
#define EP_FLOWCTRL_THRESHOLD 4

// ---- Image selector capture (populated from EP log stream at boot) ----
// EP sends the entire image_selector as one compact JSON array string via
// LOGID_EP_IMGSEL_TYPE_CS, then sends LOGID_EP_LOADED_SLOT_TYPE_U8 to
// indicate which slot was actually loaded (0 = fell back to failsafe image).

#define EP_IMGSEL_STR_LEN  512

static char             ep_imgsel_str[EP_IMGSEL_STR_LEN] = "";
static int              ep_imgsel_str_pos  = 0;
static volatile bool    ep_imgsel_complete = false;
static volatile uint8_t ep_loaded_slot     = 0xFF;  // 0xFF=not yet received, 0=limp, 1-255=slot

const char* get_ep_imgsel_str(void)   { return ep_imgsel_str; }
bool        get_ep_imgsel_complete(void) { return ep_imgsel_complete; }
uint8_t     get_ep_loaded_slot(void)  { return ep_loaded_slot; }

uint32_t elapsed_max;
void __time_critical_func(isr_rx32)()
{
    // If we are getting behind in our receiving duties, tell the EP to pause sending
    uint level = pio_sm_get_rx_fifo_level(PIO_UART, pio_sm_uart);
    bool backpressure_required = (level >= EP_FLOWCTRL_THRESHOLD);

    if (backpressure_required) {
        gpio_put(EPLOG_FLOWCTRL_PIN, 1);  // pause EP
    }

    while(!pio_sm_is_rx_fifo_empty(PIO_UART, pio_sm_uart)) {
        uint32_t t0 = time_us_32();
        uint32_t rxWord = uart_rx32_program_get(PIO_UART, pio_sm_uart);
        logger->logData_fromISR(rxWord);

        // Update the live ECU log data array
        uint8_t logId  = (rxWord >> 8)  & 0xFF;
        uint8_t data8  = (rxWord >> 16) & 0xFF;
        ecuLiveLog[logId] = (rxWord >> 16) & 0xFFFF;
        g_last_ecu_data_us = t0;

        // Track last crank event for engine-running detection
        if (logId == LOGID_ECU_CRANKREF_ID_TYPE_U8) {
            g_last_crank_event_us = t0;
        }

        // Capture image_selector string from EP (sent once after the loading pass completes)
        if (logId == LOGID_EP_IMGSEL_TYPE_CS && !ep_imgsel_complete) {
            char c = (char)data8;
            if (c != '\0') {
                if (ep_imgsel_str_pos < EP_IMGSEL_STR_LEN - 1) {
                    ep_imgsel_str[ep_imgsel_str_pos++] = c;
                }
            } else {
                ep_imgsel_str[ep_imgsel_str_pos] = '\0';
                ep_imgsel_complete = true;
            }
        } else if (logId == LOGID_EP_LOADED_SLOT_TYPE_U8) {
            ep_loaded_slot = data8;
        }

        uint32_t elapsed = time_us_32() - t0;
        if (elapsed > elapsed_max) {
            elapsed_max = elapsed;
        }
    }

    // FIFO is known to be empty now so EP can resume
    gpio_put(EPLOG_FLOWCTRL_PIN, 0);

    // The fifo-not-empty interrupt is cleared automatically when we empty out the FIFO
}


// ----------------------------------------------------------------------------------
// The EP PIO UART receives the ECU data stream.
// During normal system operation, this UART is unidirectional, meaning receive only.
// It must be a PIO-based UART because we need it to receive data in 32-bit units
// and the RP2350 UART silicon only does 8-bit transfers.
// All ECU data comes across as 32-bit words, each containing a complete log event
// that we will inserted into the log in atomic fashion.
void initEpUart() {

    // Set up the PIO unit to act as our UART
    pio_sm_uart = pio_claim_unused_sm(PIO_UART, true);
    uint offset = pio_add_program(PIO_UART, &uart_rx32_program);
    uart_rx32_program_init(PIO_UART, pio_sm_uart, offset, EPLOG_RX_PIN, EP_TO_WP_BAUDRATE);
    printf("UART_RX32: Using PIO%d, SM%d, program start @ offset %d (size: %d instructions)\n",
           pio_get_index(PIO_UART), pio_sm_uart, offset, uart_rx32_program.length);


    // Assign an interrupt handler
    irq_set_exclusive_handler(PIO_UART_RX_IRQ, isr_rx32);

    printf("%s: UART_RX32 ISR will be serviced by RP2350 core %d\n", __FUNCTION__, get_core_num());
    // Leave interrupts off until we enable the flowcontrol signal
}

// ----------------------------------------------------------------------------------
void allowEpToSendData()
{
    assert(logger != nullptr);

    // Validate the state machine number
    if ((pio_sm_uart < 0) || (pio_sm_uart > 3)) {
        panic("Invalid RX32 PIO state machine number");
    }

    // Make sure the rxQ is empty prior to giving the EP the OK to send
    // in case we received some garbage during boot.
    while(!pio_sm_is_rx_fifo_empty(PIO_UART, pio_sm_uart)) {
        uint16_t logEvent = uart_rx32_program_get(PIO_UART, pio_sm_uart);
    }

    // Enable RX FIFO Not Empty interrupt in this core's NVIC
    irq_set_enabled(PIO_UART_RX_IRQ, true);

    // Enable the statemachine-specific RX-FIFO-not-empty interrupt inside the proper PIO unit
    PIO_UART->inte0 = PIO_INTR_SM0_RXNEMPTY_BITS << pio_sm_uart;


    // Tell the EP we are ready for data
    gpio_put(EPLOG_FLOWCTRL_PIN, 0);
    gpio_set_dir(EPLOG_FLOWCTRL_PIN, GPIO_OUT);
}

// ----------------------------------------------------------------------------------
// Heap snapshot globals — updated every time show_heap_stats() is called, including
// from my_panic_handler. Readable from the debugger after a panic halt even if
// serial output didn't flush. Inspect these in GDB/Cortex-Debug:
//   g_heap_max        total heap space (bss_end to StackLimit)
//   g_heap_committed  bytes sbrk has claimed (never shrinks after free)
//   g_heap_remaining  uncommitted space still available to sbrk
//   g_heap_inuse      bytes in live malloc() allocations right now
//   g_heap_free       free bytes inside committed arena (may be fragmented)
volatile uint32_t g_heap_max       = 0;
volatile uint32_t g_heap_committed = 0;
volatile uint32_t g_heap_remaining = 0;
volatile uint32_t g_heap_inuse     = 0;
volatile uint32_t g_heap_free      = 0;

// ----------------------------------------------------------------------------------
void show_heap_stats(bool all=false)
{
    uintptr_t heap_start = (uintptr_t)&__bss_end__;
    uintptr_t heap_limit = (uintptr_t)&__StackLimit;

    const uint32_t max_heap = heap_limit - heap_start;

    struct mallinfo mi = mallinfo();

    // Store in globals FIRST so they're readable from the debugger even if
    // the printf below never completes (e.g. stdio mutex held at panic time).
    g_heap_max       = max_heap;
    g_heap_committed = (uint32_t)mi.arena;
    g_heap_remaining = max_heap - (uint32_t)mi.arena;
    g_heap_inuse     = (uint32_t)mi.uordblks;
    g_heap_free      = (uint32_t)mi.fordblks;

    if (all) {
        printf("Heap [sbrk-max/sbrk-remaining/committed/inuse/free]: [%u/%u/%u/%u/%u]\n",
            g_heap_max, g_heap_remaining,g_heap_committed,
            g_heap_inuse, g_heap_free);
    } else {
        printf("Heap [sbrk-remaining/committed/inuse/free]: [%u/%u/%u/%u]\n",
            g_heap_remaining, g_heap_committed, g_heap_inuse, g_heap_free);
    }
}

// ----------------------------------------------------------------------------------
// Called by the SDK's panic() via PICO_PANIC_FUNCTION before breakpointing.
// Printing heap stats here gives us a snapshot of heap state at the moment of panic
// so we can distinguish OOM (remaining=0) from fragmentation (free is fragmented).
//
// The SDK's panic() becomes a naked wrapper that calls us, then loops on bkpt #0.
// We must handle all output ourselves and must not return.
extern "C" void __attribute__((noreturn)) my_panic_handler(const char *fmt, ...)
{
    puts("\n*** PANIC ***\n");
    if (fmt) {
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
        puts("\n");
    }
    show_heap_stats(true);
    stdio_flush();
    while (1) tight_loop_contents();
}

// ----------------------------------------------------------------------------------
// DEBUG: print lwIP pool stats every 2s to find which pool exhausts during parallel page loads.
// Remove once root cause is identified.
static void debug_lwip_stats()
{
    xTimerStart(
        xTimerCreate("lwip_dbg", pdMS_TO_TICKS(2000), pdTRUE, nullptr,
                     [](TimerHandle_t) {
                         printf("lwIP mem: used=%u max=%u err=%u | "
                                "pbuf_pool: used=%u max=%u err=%u | "
                                "tcp_seg: used=%u max=%u err=%u | "
                                "tcp_pcb: used=%u max=%u err=%u\n",
                                (unsigned)lwip_stats.mem.used,
                                (unsigned)lwip_stats.mem.max,
                                (unsigned)lwip_stats.mem.err,
                                (unsigned)lwip_stats.memp[MEMP_PBUF_POOL]->used,
                                (unsigned)lwip_stats.memp[MEMP_PBUF_POOL]->max,
                                (unsigned)lwip_stats.memp[MEMP_PBUF_POOL]->err,
                                (unsigned)lwip_stats.memp[MEMP_TCP_SEG]->used,
                                (unsigned)lwip_stats.memp[MEMP_TCP_SEG]->max,
                                (unsigned)lwip_stats.memp[MEMP_TCP_SEG]->err,
                                (unsigned)lwip_stats.memp[MEMP_TCP_PCB]->used,
                                (unsigned)lwip_stats.memp[MEMP_TCP_PCB]->max,
                                (unsigned)lwip_stats.memp[MEMP_TCP_PCB]->err);
                     }),
        0);
}

// ----------------------------------------------------------------------------------
// This function is used to perform the FreeRTOS task initialization.
// FreeRTOS is known to be running when this routine is called so it is free
// to use any FreeRTOS constructs and call any FreeRTOS routines.
void boot_system(void* args)
{
    BaseType_t err;

    // Get the LED working first so that it can be used by the rest of the system.
    // We need to tell it which PIO to use, but it will claim its own state machine.
    rgb_led = new NeoPixelConnect(WS2812_PIN, WS2812_PIXCNT, PIO_WS2812);
    rgb_led->neoPixelSetValue(0, 16, 16, 16, true);

    initEpUart();

    // The logger may be started early since it has a big buffer to handle extremely long LittleFS write times.
    printf("%s: Creating the logger\n", __FUNCTION__);
    static uint8_t logBuf[LOG_BUFFER_SIZE];
    logger = new Logger(logBuf, LOG_BUFFER_SIZE);

    // First thing we log is what version of the log we are generating:
    uint8_t v = LOGID_GEN_WP_LOG_VER_VAL_V0;
    logger->logData(LOGID_GEN_WP_LOG_VER_TYPE_U8, LOGID_GEN_WP_LOG_VER_DLEN, &v);

    // Register Logger as the filesystem mount/unmount listener
    lfs_register_mount_callbacks(
        [](lfs_t* lfs) -> bool {
            if (!logger->init(lfs)) {
                logger->deinit();
                return false;
            }
            ecu_live_config_load();
            return true;
        },
        []() {
            logger->deinit();
        }
    );

    printf("%s: Starting the filesystem\n", __FUNCTION__);
    startFileSystem();

    // Now that the UART and logger are up: signal the EP that is is OK to send us data
    printf("%s: EP is enabled to send us data\n", __FUNCTION__);
    allowEpToSendData();

    printf("%s: Starting the GPS\n", __FUNCTION__);
    startGps();

    printf("%s: Starting the debug shell\n", __FUNCTION__);
    dbgShell = new Shell(&lfs);

    // Load persistent config from flash; fall back to compile-time defaults if blank/corrupt
    printf("%s: Loading config from flash\n", __FUNCTION__);
    flash_config_load(&g_flash_config);
    strncpy(g_device_name, g_flash_config.device_name, sizeof(g_device_name) - 1);

    // Create the wifi manager and configure it from flash config
    printf("%s: Creating WiFi manager\n", __FUNCTION__);
    wifiMgr = new WiFiManager();
    wifiMgr->setCredentials(g_flash_config.wifi_ssid, g_flash_config.wifi_password);
    wifiMgr->setGps(gps);
    if (g_flash_config.server_host[0] != '\0') {
        wifiMgr->setServerAddress(g_flash_config.server_host, g_flash_config.server_port);
    }


    printf("%s: Creating Network manager (MDL HTTP server)\n", __FUNCTION__);
    networkMgr = new NetworkManager(wifiMgr);

    printf("%s: Initializing file I/O task\n", __FUNCTION__);
    file_io_task_init();

    printf("%s: Initializing OTA flash task\n", __FUNCTION__);
    ota_flash_task_init();

    // Instantiate an SWD object to interact with the EP
    swd = new Swd(PIO_SWD, EP_SWCLK_PIN, EP_SWDAT_PIN, /*verbose=*/false, SPARE2_PIN);

    // Read EP flash image-store once at startup and cache the JSON.
    // EP flash is static; doing this here keeps SWD off the lwIP tcpip thread.
    image_store_init();

    // A bit more accurate now that we have mostly booted:
    show_heap_stats();

    // Periodically print heap stats so we can track usage trends over time.
    // If memory is leaking or fragmenting, this will show it in the RTT log.
    xTimerStart(
        xTimerCreate("heap", pdMS_TO_TICKS(300000), pdTRUE, nullptr,
                     [](TimerHandle_t) { show_heap_stats(false); }),
        0);

    // Enable this to debug lwip memory usage as program runs
    //debug_lwip_stats();

    // All done booting: delete this task
    vTaskDelete(NULL);
}

// --------------------------------------------------------------------------------------------
// The Pico2W does not have a simple LED, but the umod4 board adds one for it to drive.
void pico_led_init(void)
{
    gpio_init(SPARE1_LED_PIN);
    gpio_put(SPARE1_LED_PIN, 0);
    gpio_set_dir(SPARE1_LED_PIN, GPIO_OUT);
}

void pico_set_led(bool led_on)
{
    gpio_put(SPARE1_LED_PIN, led_on);
}

void pico_toggle_led()
{
    gpio_xor_mask(1u<<SPARE1_LED_PIN);
}

// --------------------------------------------------------------------------------------------
// Fast flash the LED 'count' times as a most basic sign of life as we boot.
void hello(int32_t count)
{
    pico_led_init();

    for (uint32_t i=0; i<count; i++) {
        pico_set_led(true);
        sleep_ms(10);
        pico_set_led(false);
        sleep_ms(50);
    }
}

// --------------------------------------------------------------------------------------------
// Init all three SPAREx GPIOs as inputs, with pullups.
void initSpareIos()
{
    #if defined SCOPE_TRIGGER_PIN
    // Scope trigger will be rising edge
    gpio_init(SCOPE_TRIGGER_PIN);
    gpio_put(SCOPE_TRIGGER_PIN, 0);
    gpio_set_dir(SCOPE_TRIGGER_PIN, GPIO_OUT);
    #else
    gpio_init(SPARE0_PIN);
    gpio_set_dir(SPARE0_PIN, GPIO_IN);
    gpio_set_pulls(SPARE0_PIN, false, true);    // pulldown
    #endif

    #if !defined SPARE1_LED_PIN
    gpio_init(SPARE1_PIN);
    gpio_set_dir(SPARE1_PIN, GPIO_IN);
    gpio_set_pulls(SPARE1_PIN, false, true);    // pulldown
    #endif

    gpio_init(SPARE2_PIN);
    gpio_set_dir(SPARE2_PIN, GPIO_IN);
    gpio_set_pulls(SPARE2_PIN, true, false);    // pullup
}

// ----------------------------------------------------------------------------------
void epResetAndRun()
{
    gpio_init(EP_RUN_PIN);
    gpio_set_dir(EP_RUN_PIN, GPIO_OUT);
    gpio_put(EP_RUN_PIN, 0);
    sleep_us(100);
    gpio_put(EP_RUN_PIN, 1);
}

// ----------------------------------------------------------------------------------
// Helper functions for MDL API handlers

const char* get_wp_version(void) {
    // Return the build system-generated version info (git hash + build time)
    return SYSTEM_JSON;
}

bool wifi_is_connected(void) {
    return (wifiMgr != nullptr) && wifiMgr->isReady() && !wifiMgr->isInApMode();
}

bool wifi_is_ap_mode(void) {
    return (wifiMgr != nullptr) && wifiMgr->isInApMode();
}

const char* wifi_get_ssid(void) {
    return g_flash_config.wifi_ssid;
}

const char* wifi_get_ap_ssid(void) {
    return (wifiMgr != nullptr) ? wifiMgr->getApSSID() : "";
}

int32_t wifi_get_rssi(void) {
    if (!wifi_is_connected()) return 0;
    int32_t rssi = 0;
    cyw43_wifi_get_rssi(&cyw43_state, &rssi);
    return rssi;
}

const char* get_current_log_name(void) {
    if (logger == nullptr) return "";
    const char* name = logger->getCurrentLogName();
    return name ? name : "";
}

// ----------------------------------------------------------------------------------
// Helper functions for OTA flash task

bool ota_logger_valid(void)
{
    return (logger != nullptr);
}

void ota_shutdown_logger(void)
{
    if (logger != nullptr) {
        logger->deinit ();
    }
}

// ----------------------------------------------------------------------------------
static void show_partition_info()
{
    int32_t bs = FlashWp::get_boot_slot();
    printf("WP Boot slot:   %d/%c\n", bs, (bs>=0) ? ('A'-1+bs) : '?');
    int32_t ts = FlashWp::get_target_slot();
    printf("WP Target slot: %d/%c\n", ts, (ts>=0) ? ('A'-1+ts) : '?');
    printf("WP OTA is%s available\n", FlashWp::get_ota_availability() ? "" : " NOT");
    printf("\n");
}

// ----------------------------------------------------------------------------------
// TBYB (Try Before You Buy) OTA Update Commitment
//
// If we just booted from a newly-flashed OTA image, we have ~16.7 seconds
// to call rom_explicit_buy() or the bootrom will revert to the previous
// partition on the next reboot.
//
// That makes it sound easier than it really is though. The problem is that a
// TBYB boot is a warm boot. It does NOT reset massive parts of the silicon.
// That means that any code that assumes that the system truly rebooted will NOT
// be in a true reset state. For example, the wifi driver. Attempting to treat
// it like is in a clean reset state is not workable. For the moment, we avoid
// the problems associated with resetting everything "by hand" and blindly
// commit this new code, then do a "real" reset to reboot the committed system.
void check_tbyb()
{
    if (!FlashWp::get_ota_availability()) {
        printf("%s: Skipping TBYB check: OTA not available\n", __FUNCTION__);
        return;
    }

    if (!FlashWp::isOtaPending()) {
        printf("%s: No commit required\n", __FUNCTION__);
    }
    else {
        extern void unpause_watchdog_tick();   // fixme

        // Get the WS2812 LED working again after the reboot.
        // Start off by resetting *all* PIO blocks - we don't need to figure out which one the WS2812 might be using
        reset_block(RESETS_RESET_PIO0_BITS | RESETS_RESET_PIO1_BITS | RESETS_RESET_PIO2_BITS);
        unreset_block_wait(RESETS_RESET_PIO0_BITS | RESETS_RESET_PIO1_BITS | RESETS_RESET_PIO2_BITS);
        rgb_led = new NeoPixelConnect(WS2812_PIN, WS2812_PIXCNT, PIO_WS2812);

        // Drive LED BLUE to indicate commit event
        rgb_led->neoPixelSetValue(0, 0,0,30, true);

        busy_wait_ms(1000);

        // This boot was a warm boot to perform a TBYB check on this image that is running right now.
        printf("%s: Committing OTA update\n", __FUNCTION__);
        // For now, we assume that this new image is working
        int32_t commit_result = FlashWp::commitOtaUpdate();
        if (commit_result == 0) {
            printf("%s:   Commit succeeded!\n", __FUNCTION__);
            rgb_led->neoPixelSetValue(0, 0,30,0, true);
        }
        else {
            printf("%s: Commit failed: %d\n", __FUNCTION__, commit_result);
            rgb_led->neoPixelSetValue(0, 30,0,0, true);
        }
        busy_wait_ms(1000);

        // We have an issue here. Because a TBYB boot event represents a 'warm' restart
        // of the system, the system hardware (CPU, wifi module, PIO, etc.) may
        // in an indeterminate state because there was no hard RESET event.
        // The simplest way to fix that is to perform a watchdog reboot here now
        // that we are committed. The watchdog reset event will select this new image
        // now that it is committed.
        unpause_watchdog_tick();        // Allow the watchdog to time out even if a debugger is connected
        watchdog_enable(1, false);
        while (true) {
            __wfi();
        }
    }
}

// ----------------------------------------------------------------------------------
// Before main() is called, the Pico boot code in pico-sdk/src/rp2_common/pico_standard_link/crt0.S
// calls function runtime_init() in pico-sdk/src/rp2_common/pico_runtime/runtime.c
// That is where all the behind-the-scenes initialization of the runtime occurs.
int main()
{
    // Note: With configNUMBER_OF_CORES=1, core 1 is never used.

    // Simulate having pullup resistors on EPLOG_RX_PIN and EPLOG_FLOWCTRL_PIN.
    // Any future PCB4.2 rev of the PCB must add pullups to both of these signals!
    gpio_init(EPLOG_RX_PIN);
    gpio_set_dir(EPLOG_RX_PIN, GPIO_IN);
    gpio_set_pulls(EPLOG_RX_PIN, true, false);

    #if defined EPLOG_FLOWCTRL_PIN
    gpio_init(EPLOG_FLOWCTRL_PIN);
    gpio_set_pulls(EPLOG_FLOWCTRL_PIN, true, false);
    gpio_set_dir(EPLOG_FLOWCTRL_PIN, GPIO_IN);
    #endif

    // Init PPS pin to have a pulldown so that it can't cause rising-edge interrupts
    // if no module is present
    gpio_init(GPS_PPS_PIN);
    gpio_set_dir(GPS_PPS_PIN, GPIO_IN);
    gpio_set_pulls(GPS_PPS_PIN, false, true);

    {
        // While bench testing, it is hugely useful to reset the EP here which
        // mimics both processors getting reset at ignition key ON.
        #warning " ********** EXTREMELY TEMP - RESETTING THE EP **************"
        epResetAndRun();
    }

    hello(3);

    initSpareIos();

    stdio_init_all();

    printf("\n\nWP Core %d booting on board %s\n", get_core_num(), STRINGIFY(PICO_BOARD));
    printf("WP Version JSON: %s\n", SYSTEM_JSON);
    uint32_t f_clk_sys = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);
    printf("WP System clock: %.1f MHz\n", f_clk_sys / 1000.0);

    printf("WP ");          // prepend heap stats to make it look like the other boot msgs
    show_heap_stats(true);

    show_partition_info();
    check_tbyb();

    // Create a transient task to boot the rest of the system
    BaseType_t err = xTaskCreate(
        boot_system,        // func ptr of task to run
        "boot_system",      // task name
        2048,               // stack size (words)
        NULL,               // args
        TASK_MAX_PRIORITY,  // make sure the boot task runs to completion before anything it starts
        NULL                // task handle (if any)
    );

    if (err != pdPASS) {
        panic("Boot Task creation failed!");
    }

    vTaskStartScheduler();
}
