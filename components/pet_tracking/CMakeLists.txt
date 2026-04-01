# components/pet_tracking/CMakeLists.txt
idf_component_register(
    SRCS "PetProximityTracker.cpp"
    INCLUDE_DIRS "include"
    REQUIRES beacon_core freertos log
)