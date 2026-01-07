#include "NetworkManager.h"
#include "api_handlers.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/apps/httpd.h"
#include "lwip/apps/mdns.h"
#include <cstdio>

#include "umod4_WP.h"

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
{
    BaseType_t err = xTaskCreate(
        start_networkMgr_task,
        "NetMgrTask",
        2048,
        this,
        TASK_NORMAL_PRIORITY,
        &taskHandle_
    );

    if (err != pdPASS) {
        printf("NetworkMgr: Critical - Task creation failed\n");
        panic("Unable to create NetworkManager task");
    }

    // Pin to Core 0 for lwIP thread safety
    vTaskCoreAffinitySet(taskHandle_, (1 << 0));
}

NetworkManager::~NetworkManager()
{
    stop_http_server();
    stop_mdns();
}

void NetworkManager::start_http_server()
{
    if (httpd_running_) return;

    printf("NetworkMgr: Starting HTTP server on port 80...\n");
    httpd_init();

    // Register CGI handlers
    api_handlers_register();

    httpd_running_ = true;
    printf("NetworkMgr: HTTP server running\n");
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

    printf("NetworkMgr: Starting mDNS responder...\n");
    mdns_resp_init();
    mdns_resp_add_netif(netif, "motorcycle");

    mdns_running_ = true;
    printf("NetworkMgr: mDNS responder running (motorcycle.local)\n");
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
                start_mdns();
                start_http_server();
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
