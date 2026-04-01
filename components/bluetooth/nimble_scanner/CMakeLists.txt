# components/bluetooth/nimble_scanner/CMakeLists.txt
idf_component_register(
    SRCS "NimbleScanner.cpp"
    INCLUDE_DIRS "include"
    # REQUIRES makes these dependencies transitively available to anyone 
    # who includes NimbleScanner.hpp.
    REQUIRES beacon_core freertos log
    # PRIV_REQUIRES hides the implementation details (like NimBLE headers) 
    # from leaking up to the main application.
    PRIV_REQUIRES bt
)