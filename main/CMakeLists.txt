idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "include"
    REQUIRES freertos log smart_led i2c_bus AHT25 VL53L0X ledc_servo 
        nvs_flash nimble_scanner pet_tracking
)
