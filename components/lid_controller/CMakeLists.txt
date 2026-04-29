# components/lid_controller/CMakeLists.txt
idf_component_register(
    SRCS "lid_controller.cpp"
    INCLUDE_DIRS "include"
    REQUIRES freertos ledc_servo diagnostics_service
    PRIV_REQUIRES log
)