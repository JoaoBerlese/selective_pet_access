# components/AHT25/CMakeLists.txt
idf_component_register(
    SRCS "AHT25.cpp"
    INCLUDE_DIRS "include"
    REQUIRES freertos log i2c_bus
)