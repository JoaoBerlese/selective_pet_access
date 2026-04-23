# components/telemetry_service/CMakeLists.txt
idf_component_register(
    SRCS "telemetry_service.cpp"
    INCLUDE_DIRS "include"
    REQUIRES freertos AHT25 VL53L0X rtos
    PRIV_REQUIRES log
)