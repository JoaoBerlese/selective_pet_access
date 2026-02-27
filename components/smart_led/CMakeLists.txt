# components/smart_led/CMakeLists.txt
idf_component_register(
    SRCS "smart_led.cpp"
    INCLUDE_DIRS "include"
    REQUIRES led_strip freertos log driver
)