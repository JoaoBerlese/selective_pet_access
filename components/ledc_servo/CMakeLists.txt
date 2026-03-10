# components/ledc_servo/CMakeLists.txt
idf_component_register(
    SRCS "ledc_servo.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver log
)