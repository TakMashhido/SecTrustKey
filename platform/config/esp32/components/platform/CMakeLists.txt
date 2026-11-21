set(STK_PLATFORM_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../..)

set(STK_INCLUDE_DIRS
    ${STK_PLATFORM_DIR}/src
    ${STK_PLATFORM_DIR}/src/fs
    ${STK_PLATFORM_DIR}/src/otp
    ${STK_PLATFORM_DIR}/src/rng
    ${STK_PLATFORM_DIR}/src/usb
    ${STK_PLATFORM_DIR}/src/led
    ${STK_PLATFORM_DIR}/third-party/mbedtls/include
    ${STK_PLATFORM_DIR}/../board
)

set(STK_REQUIRES
    bootloader_support
    app_update
    esp_partition
    spi_flash
    esp_tinyusb
    efuse
    mbedtls
    esp_hw_support
)
if(USB_ITF_HID)
    list(APPEND STK_REQUIRES
        tinycbor
    )
endif()

if(ENABLE_PQC)
    list(APPEND STK_INCLUDE_DIRS
        ${STK_PLATFORM_DIR}/third-party/mlkem/mlkem
        ${STK_PLATFORM_DIR}/config/mlkem
        ${STK_PLATFORM_DIR}/third-party/mldsa/mldsa
        ${STK_PLATFORM_DIR}/config/mldsa
    )
    list(APPEND STK_REQUIRES
        mlkem512
        mlkem768
        mlkem1024
        mldsa44
        mldsa65
        mldsa87
    )
endif()

idf_component_register(
    SRCS ${STK_SOURCES}
    INCLUDE_DIRS ${STK_INCLUDE_DIRS}
    REQUIRES ${STK_REQUIRES}
)

idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE ON)
