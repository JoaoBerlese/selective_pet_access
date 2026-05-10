# components/wifi_station/CMakeLists.txt
#
# Wi-Fi station manager. See docs/06_Firmware_Design_Guidelines.md §12 for the
# template this component follows.
#
# REQUIRES vs PRIV_REQUIRES rule (§9.1):
#   - freertos is public (TaskHandle_t, QueueHandle_t, UBaseType_t, StaticQueue_t in the header).
#   - esp_netif is public (esp_ip4_addr_t appears in WiFiStatus and IWiFiObserver).
#   - esp_event is public (esp_event_base_t appears in the static wifi_event_handler signature).
#   - rtos is public (StaticMutex member type appears in the header).
#   - log, esp_wifi are private (only used in WiFiStationService.cpp).
idf_component_register(
    SRCS "WiFiStationService.cpp"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_netif esp_event rtos
    PRIV_REQUIRES log esp_wifi
)
