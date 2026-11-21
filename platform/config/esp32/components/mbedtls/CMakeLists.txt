set(STK_MBEDTLS_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../third-party/mbedtls")
set(STK_MBEDTLS_PORT_DIR "$ENV{IDF_PATH}/components/mbedtls/port")
set(STK_MBEDTLS_CRT_BUNDLE_DIR "$ENV{IDF_PATH}/components/mbedtls/esp_crt_bundle")

if(NOT EXISTS "${STK_MBEDTLS_DIR}/include/mbedtls/mbedtls_config.h")
    message(FATAL_ERROR "MbedTLS sources were not found at ${STK_MBEDTLS_DIR}")
endif()

set(STK_MBEDTLS_PORT_SOURCES
    ${STK_MBEDTLS_PORT_DIR}/esp_hardware.c
    ${STK_MBEDTLS_PORT_DIR}/esp_mem.c
    ${STK_MBEDTLS_PORT_DIR}/esp_platform_time.c
    ${STK_MBEDTLS_PORT_DIR}/esp_timing.c
)

if(CONFIG_MBEDTLS_ROM_MD5)
    list(APPEND STK_MBEDTLS_PORT_SOURCES ${STK_MBEDTLS_PORT_DIR}/md/esp_md.c)
endif()

set(STK_MBEDTLS_PORT_INCLUDE_DIRS
    ${STK_MBEDTLS_PORT_DIR}/include
)

if(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE)
    list(APPEND STK_MBEDTLS_PORT_INCLUDE_DIRS ${STK_MBEDTLS_CRT_BUNDLE_DIR}/include)
    list(APPEND STK_MBEDTLS_PORT_SOURCES ${STK_MBEDTLS_CRT_BUNDLE_DIR}/esp_crt_bundle.c)
endif()

if(CONFIG_SOC_AES_SUPPORTED)
    list(APPEND STK_MBEDTLS_PORT_INCLUDE_DIRS ${STK_MBEDTLS_PORT_DIR}/aes/include)
    list(APPEND STK_MBEDTLS_PORT_SOURCES
        ${STK_MBEDTLS_PORT_DIR}/aes/esp_aes_common.c
        ${STK_MBEDTLS_PORT_DIR}/aes/esp_aes_xts.c
    )
    if(CONFIG_SOC_AES_SUPPORT_DMA)
        list(APPEND STK_MBEDTLS_PORT_INCLUDE_DIRS ${STK_MBEDTLS_PORT_DIR}/aes/dma/include)
        if(CONFIG_SOC_AES_GDMA)
            list(APPEND STK_MBEDTLS_PORT_SOURCES ${STK_MBEDTLS_PORT_DIR}/aes/dma/esp_aes_gdma_impl.c)
        else()
            list(APPEND STK_MBEDTLS_PORT_SOURCES ${STK_MBEDTLS_PORT_DIR}/aes/dma/esp_aes_crypto_dma_impl.c)
        endif()
        list(APPEND STK_MBEDTLS_PORT_SOURCES
            ${STK_MBEDTLS_PORT_DIR}/aes/dma/esp_aes_dma_core.c
            ${STK_MBEDTLS_PORT_DIR}/aes/dma/esp_aes.c
        )
    else()
        list(APPEND STK_MBEDTLS_PORT_SOURCES ${STK_MBEDTLS_PORT_DIR}/aes/block/esp_aes.c)
    endif()
endif()

if(CONFIG_SOC_SHA_SUPPORTED)
    if(CONFIG_SOC_SHA_SUPPORT_PARALLEL_ENG)
        set(STK_MBEDTLS_SHA_PERIPHERAL_DIR parallel_engine)
    else()
        set(STK_MBEDTLS_SHA_PERIPHERAL_DIR core)
    endif()
    list(APPEND STK_MBEDTLS_PORT_SOURCES
        ${STK_MBEDTLS_PORT_DIR}/sha/esp_sha.c
        ${STK_MBEDTLS_PORT_DIR}/sha/${STK_MBEDTLS_SHA_PERIPHERAL_DIR}/sha.c
    )
    list(APPEND STK_MBEDTLS_PORT_INCLUDE_DIRS ${STK_MBEDTLS_PORT_DIR}/sha/${STK_MBEDTLS_SHA_PERIPHERAL_DIR}/include)
    if(STK_MBEDTLS_SHA_PERIPHERAL_DIR STREQUAL "core")
        if(CONFIG_SOC_SHA_GDMA)
            list(APPEND STK_MBEDTLS_PORT_SOURCES ${STK_MBEDTLS_PORT_DIR}/sha/core/esp_sha_gdma_impl.c)
        elseif(CONFIG_SOC_SHA_CRYPTO_DMA)
            list(APPEND STK_MBEDTLS_PORT_SOURCES ${STK_MBEDTLS_PORT_DIR}/sha/core/esp_sha_crypto_dma_impl.c)
        endif()
    endif()
    if(CONFIG_SOC_SHA_SUPPORT_DMA AND CONFIG_SOC_SHA_GDMA)
        list(APPEND STK_MBEDTLS_PORT_SOURCES ${STK_MBEDTLS_PORT_DIR}/crypto_shared_gdma/esp_crypto_shared_gdma.c)
    endif()
    if(CONFIG_MBEDTLS_HARDWARE_SHA)
        list(APPEND STK_MBEDTLS_PORT_SOURCES
            ${STK_MBEDTLS_PORT_DIR}/sha/${STK_MBEDTLS_SHA_PERIPHERAL_DIR}/esp_sha1.c
            ${STK_MBEDTLS_PORT_DIR}/sha/${STK_MBEDTLS_SHA_PERIPHERAL_DIR}/esp_sha256.c
            ${STK_MBEDTLS_PORT_DIR}/sha/${STK_MBEDTLS_SHA_PERIPHERAL_DIR}/esp_sha512.c
        )
    endif()
endif()

if(CONFIG_MBEDTLS_HARDWARE_MPI)
    list(APPEND STK_MBEDTLS_PORT_SOURCES
        ${STK_MBEDTLS_PORT_DIR}/bignum/esp_bignum.c
        ${STK_MBEDTLS_PORT_DIR}/bignum/bignum_alt.c
    )
endif()

if(CONFIG_MBEDTLS_HARDWARE_AES OR CONFIG_MBEDTLS_HARDWARE_GCM)
    list(APPEND STK_MBEDTLS_PORT_SOURCES ${STK_MBEDTLS_PORT_DIR}/aes/esp_aes_gcm.c)
endif()

if(CONFIG_MBEDTLS_HARDWARE_ECC)
    list(APPEND STK_MBEDTLS_PORT_SOURCES
        ${STK_MBEDTLS_PORT_DIR}/ecc/esp_ecc.c
        ${STK_MBEDTLS_PORT_DIR}/ecc/ecc_alt.c
    )
endif()

idf_component_register(
    SRCS ${MBEDTLS_SOURCES} ${STK_MBEDTLS_PORT_SOURCES}
    INCLUDE_DIRS ${STK_MBEDTLS_PORT_INCLUDE_DIRS} ${STK_MBEDTLS_DIR}/include
    PRIV_REQUIRES soc esp_hw_support esp_pm esp_security esp_mm
    KCONFIG "$ENV{IDF_PATH}/components/mbedtls/Kconfig"
)

if(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE)
    idf_build_get_property(python PYTHON)
    set(STK_MBEDTLS_BUNDLE_NAME "x509_crt_bundle")
    set(STK_MBEDTLS_BUNDLE_INPUTS ${STK_MBEDTLS_CRT_BUNDLE_DIR}/cacrt_all.pem ${STK_MBEDTLS_CRT_BUNDLE_DIR}/cacrt_local.pem)
    set(STK_MBEDTLS_BUNDLE_ARGS --input ${STK_MBEDTLS_BUNDLE_INPUTS} -q --max-certs "${CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_MAX_CERTS}")
    set(STK_MBEDTLS_BUNDLE_GENERATOR ${python} ${STK_MBEDTLS_CRT_BUNDLE_DIR}/gen_crt_bundle.py)
    get_filename_component(STK_MBEDTLS_BUNDLE_FILE ${STK_MBEDTLS_BUNDLE_NAME} ABSOLUTE BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    add_custom_command(OUTPUT ${STK_MBEDTLS_BUNDLE_FILE} COMMAND ${STK_MBEDTLS_BUNDLE_GENERATOR} ${STK_MBEDTLS_BUNDLE_ARGS} DEPENDS ${STK_MBEDTLS_BUNDLE_INPUTS} VERBATIM)
    add_custom_target(stk_mbedtls_bundle DEPENDS ${STK_MBEDTLS_BUNDLE_FILE})
    add_dependencies(${COMPONENT_LIB} stk_mbedtls_bundle)
    target_add_binary_data(${COMPONENT_LIB} ${STK_MBEDTLS_BUNDLE_FILE} BINARY)
    set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" APPEND PROPERTY ADDITIONAL_CLEAN_FILES "${STK_MBEDTLS_BUNDLE_FILE}")
endif()
