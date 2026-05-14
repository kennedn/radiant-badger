add_executable(badger)

# Default definitions
set(TZ "GMT0BST,M3.5.0/1,M10.5.0/2" CACHE STRING "Specify the explicit timezone string") # defualt is BST
set(NTP_SERVER "pool.ntp.org" CACHE STRING "Specify the NTP server")
set(DEBUG_PRINT 0 CACHE STRING "Enable debug printing to USB UART")

set(JSON_FILEPATH "${PROJECT_SOURCE_DIR}/config/tiles.json" CACHE STRING "Location of tiles json")

execute_process(COMMAND "${PROJECT_SOURCE_DIR}/tools/json_to_c_array.py" "-f" "${JSON_FILEPATH}" OUTPUT_VARIABLE "TILES_DATA")

target_compile_options(badger PRIVATE -Wall)
target_compile_definitions(badger PRIVATE 
    WIFI_SSID=\"${WIFI_SSID}\"
    WIFI_PASSWORD=\"${WIFI_PASSWORD}\"
    API_SERVER=\"${API_SERVER}\"
    API_IP=\"${API_IP}\"
    TZ=\"${TZ}\"
    NTP_SERVER=\"${NTP_SERVER}\"
    TILES_DATA=${TILES_DATA}
    DEBUG_PRINT=1
)
add_subdirectory(modules)

target_sources(badger PRIVATE 
    badger.cpp 
)

target_include_directories(badger PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

include(${PIMORONI_PICO_PATH}/libraries/badger2040w/badger2040w.cmake)
include(${PIMORONI_PICO_PATH}/common/pimoroni_bus.cmake)

target_link_libraries(badger
    hardware_adc
    badger2040w
    pimoroni_bus
    pico_stdlib
    pico_cyw43_arch_lwip_threadsafe_background
)

pico_enable_stdio_usb(badger 0)
pico_enable_stdio_uart(badger 1)

pico_add_extra_outputs(badger)
