# components/application_manager/CMakeLists.txt
idf_component_register(
    SRCS "ApplicationManager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES freertos log esp_timer
        telemetry_service lid_controller pet_tracking
)