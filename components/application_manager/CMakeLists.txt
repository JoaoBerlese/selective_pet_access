# components/application_manager/CMakeLists.txt
idf_component_register(
    SRCS "ApplicationManager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES freertos
        telemetry_service lid_controller pet_tracking
    PRIV_REQUIRES log esp_timer
)