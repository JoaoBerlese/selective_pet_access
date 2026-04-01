# components/bluetooth/beacon_core/CMakeLists.txt
idf_component_register(
    SRCS "EddystoneParser.cpp"
    INCLUDE_DIRS "include"
    # Notice: No REQUIRES here. This component is completely decoupled.
)