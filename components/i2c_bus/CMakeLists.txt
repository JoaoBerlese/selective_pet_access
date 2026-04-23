# components/i2c_bus/CMakeLists.txt
idf_component_register(
    SRCS "I2CMasterBus.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver freertos rtos
    PRIV_REQUIRES log
)