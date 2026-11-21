idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS . .. ../../board ../openpgp ../piv
    REQUIRES esp_tinyusb mbedtls efuse platform board
)
idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE ON)
