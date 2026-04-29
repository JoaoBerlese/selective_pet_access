# components/diagnostics_service/CMakeLists.txt
idf_component_register(
    SRCS "DiagnosticsService.cpp"
    INCLUDE_DIRS "include"
    REQUIRES freertos rtos
    PRIV_REQUIRES log esp_timer
)
