#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include "WiFiManager.h"
#include "FreeRTOS.h"
#include "task.h"

/**
 * Network Manager for umod4 MDL (Motorbike Data Link).
 *
 * Manages HTTP server and mDNS responder on top of WiFi (and later USB-Ethernet).
 * Initializes lwIP httpd and registers the device as "motorcycle.local" via mDNS.
 */
class NetworkManager {
public:
    NetworkManager(WiFiManager* wifiMgr);
    ~NetworkManager();

    /**
     * FreeRTOS task function that monitors WiFi state and manages HTTP server.
     */
    void NetworkManager_task();

private:
    enum class State {
        WAITING_FOR_WIFI,
        STARTING_SERVICES,
        RUNNING,
        STOPPING_SERVICES
    };

    WiFiManager* wifiMgr_;
    TaskHandle_t taskHandle_;
    State state_;
    bool httpd_running_;
    bool mdns_running_;

    void start_http_server();
    void stop_http_server();
    void start_mdns();
    void stop_mdns();
};

#endif // NETWORK_MANAGER_H
