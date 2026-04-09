# components/lid_controller/CMakeLists.txt
idf_component_register(
    SRCS "lid_controller.cpp"
    INCLUDE_DIRS "include"
    REQUIRES ledc_servo log
)