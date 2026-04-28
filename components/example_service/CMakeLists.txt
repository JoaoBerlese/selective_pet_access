# components/example_service/CMakeLists.txt
#
# Standard reference component. See docs/06_Firmware_Design_Guidelines.md §12.
#
# REQUIRES vs PRIV_REQUIRES rule (see §9.1):
#   - freertos and i2c_bus are public dependencies (their types appear in the
#     ExampleService.hpp / IExampleService.hpp public headers).
#   - rtos is public (StaticMutex member type appears in ExampleService.hpp).
#   - log is private (ESP_LOGx is only used in ExampleService.cpp).
idf_component_register(
    SRCS "ExampleService.cpp"
    INCLUDE_DIRS "include"
    REQUIRES freertos i2c_bus rtos
    PRIV_REQUIRES log
)
