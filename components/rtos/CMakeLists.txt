# components/rtos/CMakeLists.txt
idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES freertos log
)