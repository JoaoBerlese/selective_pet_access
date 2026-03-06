# components/VL53L0X/CMakeLists.txt
idf_component_register(
    SRCS "VL53L0X.cpp"
    INCLUDE_DIRS "include"
    REQUIRES freertos log i2c_bus
)