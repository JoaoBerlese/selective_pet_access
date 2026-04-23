# components/bluetooth/nimble_scanner/CMakeLists.txt
idf_component_register(
    SRCS "NimbleScanner.cpp"
    INCLUDE_DIRS "include"
    # REQUIRES makes these dependencies transitively available to anyone 
    # who includes NimbleScanner.hpp.
    REQUIRES beacon_core freertos
    # PRIV_REQUIRES hides the implementation details (like NimBLE headers)
    # from leaking up to the main application. log is also private since
    # ESP_LOGx is only invoked from NimbleScanner.cpp.
    PRIV_REQUIRES bt log
)