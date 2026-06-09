#include "NetworkManager.h"
#include "api_handlers.h"
#include "fs_custom.h"
#include "upload_handler.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/apps/httpd.h"
#include "lwip/apps/mdns.h"
#include <cstdio>

#include "umod4_WP.h"
#include "lfsMgr.h"

extern char g_device_name[64];

// Forward declaration for FreeRTOS task wrapper
extern "C" void start_networkMgr_task(void *pvParameters);

void start_networkMgr_task(void *pvParameters)
{
    NetworkManager* m = static_cast<NetworkManager*>(pvParameters);
    m->NetworkManager_task();
    panic("NetworkManager Task should never return");
}

NetworkManager::NetworkManager(WiFiManager* wifiMgr)
    : wifiMgr_(wifiMgr)
    , taskHandle_(NULL)
    , state_(State::WAITING_FOR_WIFI)
    , httpd_running_(false)
    , mdns_running_(false)
    , lwip_services_initialized_(false)
{
    // Initialize custom filesystem bridge for serving files from LittleFS.
    // Safe to call here (no lwIP involvement).
    printf("NetworkMgr: Initializing custom filesystem bridge\n");
    fs_custom_init(&lfs);

    // Initialize upload handler. Safe to call here (no lwIP involvement).
    printf("NetworkMgr: Initializing upload handler\n");
    upload_handler_init();

    // httpd_init(), api_handlers_register(), and mdns_resp_init() are deferred to
    // init_lwip_services(), called from STARTING_SERVICES once WiFi is up.
    // They require lwIP pool memory (memp_malloc) which is only initialized after
    // cyw43_arch_init() runs inside WiFiManager_task(). Calling them here, in the
    // boot task before WiFiMgrTask runs, would crash with MEMP_MEM_MALLOC=0.

    static StackType_t  s_stack[1024];
    static StaticTask_t s_tcb;
    taskHandle_ = xTaskCreateStatic(
        start_networkMgr_task,
        "NetMgrTask",
        1024,
        this,
        TASK_NORMAL_PRIORITY,
        s_stack, &s_tcb
    );

    if (taskHandle_ == NULL) {
        printf("NetworkMgr: Critical - Task creation failed\n");
        panic("Unable to create NetworkManager task");
    }
}

NetworkManager::~NetworkManager()
{
    stop_http_server();
    stop_mdns();
}

void NetworkManager::init_lwip_services()
{
    if (lwip_services_initialized_) return;

    // Initialize HTTP server ONCE. Binds to TCP port 80.
    // lwIP pool memory must be initialized (via cyw43_arch_init) before this call.
    printf("NetworkMgr: Initializing HTTP server (one-time setup)\n");
    httpd_init();
    api_handlers_register();

    // Initialize mDNS responder ONCE. Binds to UDP port 5353.
    printf("NetworkMgr: Initializing mDNS responder (one-time setup)\n");
    mdns_resp_init();

    lwip_services_initialized_ = true;
}

void NetworkManager::start_http_server()
{
    if (httpd_running_) return;

    printf("NetworkMgr: Starting HTTP server...\n");
    // Note: httpd_init() and api_handlers_register() are called ONCE in constructor
    // The HTTP server is always listening, we just track the state here

    httpd_running_ = true;
    printf("NetworkMgr: HTTP server active\n");
}

void NetworkManager::stop_http_server()
{
    if (!httpd_running_) return;

    printf("NetworkMgr: Stopping HTTP server\n");
    // Note: lwIP httpd doesn't have a clean shutdown function
    // In practice, we just stop registering new connections
    httpd_running_ = false;
}

void NetworkManager::start_mdns()
{
    if (mdns_running_) return;

    struct netif* netif = wifiMgr_->getNetif();
    if (!netif) {
        printf("NetworkMgr: Cannot start mDNS - no netif\n");
        return;
    }

    const char* hostname = g_device_name;

    printf("NetworkMgr: Starting mDNS responder...\n");
    // Note: mdns_resp_init() is called ONCE in constructor
    // Here we only add the netif to the already-initialized mDNS responder
    mdns_resp_add_netif(netif, hostname);

    mdns_running_ = true;
    printf("NetworkMgr: mDNS responder running (%s.local)\n", hostname);
}

void NetworkManager::stop_mdns()
{
    if (!mdns_running_) return;

    printf("NetworkMgr: Stopping mDNS responder\n");
    struct netif* netif = wifiMgr_->getNetif();
    if (netif) {
        mdns_resp_remove_netif(netif);
    }
    mdns_running_ = false;
}

void NetworkManager::NetworkManager_task()
{
    while (true) {
        switch (state_) {
            case State::WAITING_FOR_WIFI:
                if (wifiMgr_->isReady()) {
                    printf("NetworkMgr: WiFi ready, starting network services\n");
                    state_ = State::STARTING_SERVICES;
                } else {
                    vTaskDelay(pdMS_TO_TICKS(1000));
                }
                break;

            case State::STARTING_SERVICES:
                init_lwip_services();   // no-op after first call; safe here (lwIP pools initialized)
                start_mdns();
                start_http_server();
                // Send check-in NOW — httpd is bound to port 80 so the server can
                // immediately query /api/info without getting Connection Refused.
                wifiMgr_->triggerCheckIn();
                state_ = State::RUNNING;
                break;

            case State::RUNNING:
                // Monitor WiFi connection
                if (!wifiMgr_->isReady()) {
                    printf("NetworkMgr: WiFi lost, stopping services\n");
                    state_ = State::STOPPING_SERVICES;
                }
                vTaskDelay(pdMS_TO_TICKS(2000));
                break;

            case State::STOPPING_SERVICES:
                stop_http_server();
                stop_mdns();
                state_ = State::WAITING_FOR_WIFI;
                break;
        }
    }
}
