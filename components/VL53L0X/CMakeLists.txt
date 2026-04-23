# components/VL53L0X/CMakeLists.txt
idf_component_register(
    SRCS "VL53L0X.cpp"
    INCLUDE_DIRS "include"
    REQUIRES i2c_bus
    PRIV_REQUIRES log
)