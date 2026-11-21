#
# This file is part of the Pico Keys SDK distribution (https://github.com/polhenarejos/pico-keys-sdk).
# Copyright (c) 2022 Pol Henarejos.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, version 3.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.
#

include(platform/cmake/version.cmake OPTIONAL)
include(platform/cmake/options.cmake OPTIONAL)

option(VIDPID "Set specific VID/PID from a known platform {NitroHSM, NitroFIDO2, NitroStart, NitroPro, Nitro3, Yubikey5, YubikeyNeo, YubiHSM, Gnuk, GnuPG}" "None")

message(STATUS "VIDPID:\t\t\t '${VIDPID}'")

if(VIDPID STREQUAL "NitroHSM")
    set(USB_VID 0x20A0)
    set(USB_PID 0x4230)
elseif(VIDPID STREQUAL "NitroFIDO2")
    set(USB_VID 0x20A0)
    set(USB_PID 0x42B1)
elseif(VIDPID STREQUAL "NitroStart")
    set(USB_VID 0x20A0)
    set(USB_PID 0x4211)
elseif(VIDPID STREQUAL "NitroPro")
    set(USB_VID 0x20A0)
    set(USB_PID 0x4108)
elseif(VIDPID STREQUAL "Nitro3")
    set(USB_VID 0x20A0)
    set(USB_PID 0x42B2)
elseif(VIDPID STREQUAL "Yubikey5")
    set(USB_VID 0x1050)
    set(USB_PID 0x0407)
elseif(VIDPID STREQUAL "YubikeyNeo")
    set(USB_VID 0x1050)
    set(USB_PID 0x0116)
elseif(VIDPID STREQUAL "YubiHSM")
    set(USB_VID 0x1050)
    set(USB_PID 0x0030)
elseif(VIDPID STREQUAL "Gnuk")
    set(USB_VID 0x234B)
    set(USB_PID 0x0000)
elseif(VIDPID STREQUAL "GnuPG")
    set(USB_VID 0x1209)
    set(USB_PID 0x2440)
endif()

if(ESP_PLATFORM)
    if(DEFINED CONFIG_TINYUSB_DESC_CUSTOM_VID)
        set(USB_VID CONFIG_TINYUSB_DESC_CUSTOM_VID)
    endif()
    if(DEFINED CONFIG_TINYUSB_DESC_CUSTOM_PID)
        set(USB_PID CONFIG_TINYUSB_DESC_CUSTOM_PID)
    endif()
    if(NOT DEFINED ENABLE_POWER_ON_RESET)
        set(ENABLE_POWER_ON_RESET 0)
    endif()
    if(NOT DEFINED ENABLE_PQC)
        set(ENABLE_PQC 0)
    endif()
    if(NOT DEFINED SDKCONFIG_DEFAULTS AND NOT DEFINED ENV{SDKCONFIG_DEFAULTS})
        set(SDKCONFIG_DEFAULTS "platform/config/esp32/sdkconfig.defaults")
    endif()
    set(EXTRA_COMPONENT_DIRS
        apps/${STK_COMPONENTS}
        platform/config/esp32/components/mbedtls
        platform/config/esp32/components/platform
    )
    set(COMPONENTS ${STK_COMPONENTS} platform)
    if (USB_ITF_HID)
        list(APPEND EXTRA_COMPONENT_DIRS
            platform/config/esp32/components/tinycbor
        )
        list(APPEND COMPONENTS tinycbor)
    endif()
    if(ENABLE_PQC)
        list(APPEND EXTRA_COMPONENT_DIRS
            platform/config/esp32/components/mlkem512
            platform/config/esp32/components/mlkem768
            platform/config/esp32/components/mlkem1024
        )
    endif()
endif()

if(NOT DEFINED USB_VID)
    set(USB_VID 0x2E8A)
endif()
add_compile_definitions(USB_VID=${USB_VID})

if(NOT DEFINED USB_PID)
    set(USB_PID 0x10FD)
endif()
add_compile_definitions(USB_PID=${USB_PID})

if(NOT DEFINED DEBUG_APDU)
    set(DEBUG_APDU 0)
endif()
if(NOT DEFINED ENABLE_EMULATION)
    set(ENABLE_EMULATION 0)
endif()
if(ESP_PLATFORM AND COMMAND idf_build_set_property)
    idf_build_set_property(USB_ITF_HID "${USB_ITF_HID}")
endif()
include(${CMAKE_CURRENT_LIST_DIR}/cmake/openssl.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/build_helpers.cmake)

if(USB_ITF_HID)
    add_compile_definitions(USB_ITF_HID=1)
    message(STATUS "USB HID Interface:\t\t enabled")
endif()
if(USB_ITF_CCID)
    add_compile_definitions(USB_ITF_CCID=1)
    message(STATUS "USB CCID Interface:\t\t enabled")
    if(USB_ITF_WCID)
        add_compile_definitions(USB_ITF_WCID=1)
        message(STATUS "USB WebCCID Interface:\t enabled")
    endif()
endif()
if(ESP_PLATFORM)
    add_compile_definitions(MBEDTLS_AES_ALT=1)
    add_compile_definitions(MBEDTLS_GCM_ALT=1)
    add_compile_definitions(MBEDTLS_SHA256_ALT=1)
    add_compile_definitions(MBEDTLS_SHA1_ALT=1)
    add_compile_definitions(MBEDTLS_SHA512_ALT=1)
    add_compile_definitions(MBEDTLS_MPI_EXP_MOD_ALT=1)
    add_compile_definitions(MBEDTLS_MPI_MUL_MPI_ALT=1)
    add_compile_definitions(CFG_TUSB_CONFIG_FILE="${CMAKE_CURRENT_LIST_DIR}/src/usb/tusb_config.h")
endif()
add_compile_definitions(DEBUG_APDU=${DEBUG_APDU})
add_compile_definitions(MBEDTLS_CONFIG_FILE="${CMAKE_CURRENT_LIST_DIR}/config/mbedtls_config.h")
if (ENABLE_DIAGNOSTICS)
    add_compile_definitions(ENABLE_DIAGNOSTICS=1)
    message(STATUS "Persistent diagnostics:\t enabled")
endif()

message(STATUS "USB VID/PID:\t\t\t ${USB_VID}:${USB_PID}")
include(${CMAKE_CURRENT_LIST_DIR}/cmake/deps.cmake)

option(ENABLE_PQC "Enable/disable PQC support" OFF)
option(ENABLE_DETERMINISTIC_OTP
    "Use a host-emulation OTP file instead of a system TPM" OFF)
configure_bool_option(
    ENABLE_PQC
    ""
    "PQC support:\t\t\t enabled"
    "PQC support:\t\t\t disabled"
)
if(ENABLE_PQC)
    add_compile_definitions(ENABLE_PQC)
endif()

set(MBEDTLS_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/aes.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/asn1parse.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/asn1write.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/bignum.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/bignum_core.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/ccm.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/cmac.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/cipher.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/cipher_wrap.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/constant_time.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/ecdsa.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/ecdh.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/ecp.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/ecp_curves.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/gcm.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/hkdf.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/md.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/md5.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/oid.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/pkcs5.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/platform_util.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/rsa.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/rsa_alt_helpers.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/sha1.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/sha256.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/sha512.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/chachapoly.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/chacha20.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/poly1305.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/ripemd160.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/des.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/x509write.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/x509write_crt.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/x509_create.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/x509write_csr.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/base64.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/pem.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/pk.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/pk_wrap.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/pkwrite.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/x509.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/x509_crt.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/pkparse.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/pk_ecc.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/pkcs12.c
)

if(ENABLE_EDDSA)
    list(APPEND MBEDTLS_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/eddsa.c
        ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/sha3.c
    )
endif()

if(ENABLE_PQC)
    if(NOT ESP_PLATFORM)
        file(GLOB_RECURSE MLKEM_SOURCES
            ${CMAKE_CURRENT_LIST_DIR}/third-party/mlkem/mlkem/src/*.c
        )
        list(FILTER MLKEM_SOURCES EXCLUDE REGEX "/native/")

        add_library(mlkem512 STATIC ${MLKEM_SOURCES})
        target_include_directories(mlkem512 PRIVATE
            ${CMAKE_CURRENT_LIST_DIR}/third-party/mlkem/mlkem/src
            ${CMAKE_CURRENT_LIST_DIR}/config/mlkem
        )
        target_compile_definitions(mlkem512 PRIVATE
            MLK_CONFIG_PARAMETER_SET=512
            MLK_CONFIG_MULTILEVEL_WITH_SHARED
            MLK_CONFIG_NAMESPACE_PREFIX=mlkem
        )

        add_library(mlkem768 STATIC ${MLKEM_SOURCES})
        target_include_directories(mlkem768 PRIVATE
            ${CMAKE_CURRENT_LIST_DIR}/third-party/mlkem/mlkem/src
            ${CMAKE_CURRENT_LIST_DIR}/config/mlkem
        )
        target_compile_definitions(mlkem768 PRIVATE
            MLK_CONFIG_PARAMETER_SET=768
            MLK_CONFIG_MULTILEVEL_NO_SHARED
            MLK_CONFIG_NAMESPACE_PREFIX=mlkem
        )

        add_library(mlkem1024 STATIC ${MLKEM_SOURCES})
        target_include_directories(mlkem1024 PRIVATE
            ${CMAKE_CURRENT_LIST_DIR}/third-party/mlkem/mlkem/src
            ${CMAKE_CURRENT_LIST_DIR}/config/mlkem
        )
        target_compile_definitions(mlkem1024 PRIVATE
            MLK_CONFIG_PARAMETER_SET=1024
            MLK_CONFIG_MULTILEVEL_NO_SHARED
            MLK_CONFIG_NAMESPACE_PREFIX=mlkem
        )

        file(GLOB_RECURSE MLDSA_SOURCES
            ${CMAKE_CURRENT_LIST_DIR}/third-party/mldsa/mldsa/src/*.c
        )
        list(FILTER MLDSA_SOURCES EXCLUDE REGEX "/native/")

        add_library(mldsa44 STATIC ${MLDSA_SOURCES})
        target_include_directories(mldsa44 PRIVATE
            ${CMAKE_CURRENT_LIST_DIR}/third-party/mldsa/mldsa/src
            ${CMAKE_CURRENT_LIST_DIR}/config/mldsa
        )
        target_compile_definitions(mldsa44 PRIVATE
            MLD_CONFIG_PARAMETER_SET=44
            MLD_CONFIG_MULTILEVEL_WITH_SHARED
            MLD_CONFIG_NAMESPACE_PREFIX=mldsa
        )

        add_library(mldsa65 STATIC ${MLDSA_SOURCES})
        target_include_directories(mldsa65 PRIVATE
            ${CMAKE_CURRENT_LIST_DIR}/third-party/mldsa/mldsa/src
            ${CMAKE_CURRENT_LIST_DIR}/config/mldsa
        )
        target_compile_definitions(mldsa65 PRIVATE
            MLD_CONFIG_PARAMETER_SET=65
            MLD_CONFIG_MULTILEVEL_NO_SHARED
            MLD_CONFIG_NAMESPACE_PREFIX=mldsa
        )

        add_library(mldsa87 STATIC ${MLDSA_SOURCES})
        target_include_directories(mldsa87 PRIVATE
            ${CMAKE_CURRENT_LIST_DIR}/third-party/mldsa/mldsa/src
            ${CMAKE_CURRENT_LIST_DIR}/config/mldsa
        )
        target_compile_definitions(mldsa87 PRIVATE
            MLD_CONFIG_PARAMETER_SET=87
            MLD_CONFIG_MULTILEVEL_NO_SHARED
            MLD_CONFIG_NAMESPACE_PREFIX=mldsa
        )
    endif()

    list(APPEND INCLUDES
        ${CMAKE_CURRENT_LIST_DIR}/third-party/mlkem/mlkem
        ${CMAKE_CURRENT_LIST_DIR}/config/mlkem
        ${CMAKE_CURRENT_LIST_DIR}/third-party/mldsa/mldsa
        ${CMAKE_CURRENT_LIST_DIR}/config/mldsa
    )
    list(APPEND SYSTEM_INCLUDES
        ${CMAKE_CURRENT_LIST_DIR}/third-party/mlkem/mlkem
        ${CMAKE_CURRENT_LIST_DIR}/config/mlkem
        ${CMAKE_CURRENT_LIST_DIR}/third-party/mldsa/mldsa
        ${CMAKE_CURRENT_LIST_DIR}/config/mldsa
    )
    add_compile_definitions(
        MLK_CONFIG_NAMESPACE_PREFIX=mlkem
        MLK_CONFIG_MULTILEVEL_BUILD=1
        MLD_CONFIG_NAMESPACE_PREFIX=mldsa
        MLD_CONFIG_MULTILEVEL_BUILD=1
    )
endif()

if(ESP_PLATFORM)
    if(CARDPUTER_ADV)
        list(APPEND STK_SOURCES
            ${CMAKE_CURRENT_LIST_DIR}/../board/key_store.c
            ${CMAKE_CURRENT_LIST_DIR}/../board/key_seal.c
            ${CMAKE_CURRENT_LIST_DIR}/../board/flash_layout.c
            ${CMAKE_CURRENT_LIST_DIR}/../board/key_store_esp32.c
        )
        list(APPEND INCLUDES
            ${CMAKE_CURRENT_LIST_DIR}/../board
        )
    else()
        list(APPEND STK_SOURCES
            ${CMAKE_CURRENT_LIST_DIR}/src/otp/otp_esp32.c
        )
    endif()
elseif(ENABLE_EMULATION)
    # The host emulator keeps its device keys in a file (STK_OTP_FILE); it never touches
    # real key storage.
    list(APPEND STK_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/src/otp/otp_file.c
    )
endif()

list(APPEND STK_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/main.c
    ${CMAKE_CURRENT_LIST_DIR}/src/usb/usb.c
    ${CMAKE_CURRENT_LIST_DIR}/src/fs/file.c
    ${CMAKE_CURRENT_LIST_DIR}/src/fs/vault_container.c
    ${CMAKE_CURRENT_LIST_DIR}/src/fs/object_store.c
    ${CMAKE_CURRENT_LIST_DIR}/src/fs/object_store_txn.c
    ${CMAKE_CURRENT_LIST_DIR}/src/fs/object_container.c
    ${CMAKE_CURRENT_LIST_DIR}/src/fs/object_container_store.c
    ${CMAKE_CURRENT_LIST_DIR}/src/fs/object_policy.c
    ${CMAKE_CURRENT_LIST_DIR}/src/fs/object_crypto_provider.c
    ${CMAKE_CURRENT_LIST_DIR}/src/fs/flash.c
    ${CMAKE_CURRENT_LIST_DIR}/src/fs/low_flash.c
    ${CMAKE_CURRENT_LIST_DIR}/src/fs/phy.c
    ${CMAKE_CURRENT_LIST_DIR}/src/otp/otp.c
    ${CMAKE_CURRENT_LIST_DIR}/src/rng/random.c
    ${CMAKE_CURRENT_LIST_DIR}/src/rng/hwrng.c
    ${CMAKE_CURRENT_LIST_DIR}/src/eac.c
    ${CMAKE_CURRENT_LIST_DIR}/src/crypto_utils.c
    ${CMAKE_CURRENT_LIST_DIR}/src/tlv.c
    ${CMAKE_CURRENT_LIST_DIR}/src/debug.c
    ${CMAKE_CURRENT_LIST_DIR}/src/apdu.c
    ${CMAKE_CURRENT_LIST_DIR}/src/rescue.c
    ${CMAKE_CURRENT_LIST_DIR}/src/serial.c
    ${CMAKE_CURRENT_LIST_DIR}/src/stk_time.c
    ${CMAKE_CURRENT_LIST_DIR}/src/button.c
    ${CMAKE_CURRENT_LIST_DIR}/src/led/led.c
    ${CMAKE_CURRENT_LIST_DIR}/src/signal.c
    ${CMAKE_CURRENT_LIST_DIR}/src/vault.c
)

if(ESP_PLATFORM)
    list(APPEND STK_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/src/led/led_neopixel.c
        ${CMAKE_CURRENT_LIST_DIR}/src/led/led_gpio.c
    )
endif()

## mbedTLS reports an stringop overflow for cmac.c
if(NOT ENABLE_EMULATION AND NOT APPLE)
    set_source_files_properties(
        ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/cmac.c
        PROPERTIES
        COMPILE_FLAGS "-Wno-error=stringop-overflow= -Wno-stringop-overflow"
    )
endif()
list(APPEND INCLUDES
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/src/usb
    ${CMAKE_CURRENT_LIST_DIR}/src/fs
    ${CMAKE_CURRENT_LIST_DIR}/src/rng
    ${CMAKE_CURRENT_LIST_DIR}/src/led
    ${CMAKE_CURRENT_LIST_DIR}/src/otp
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library
)
set(SYSTEM_INCLUDES
    ${SYSTEM_INCLUDES}
    ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/include
)
if(USB_ITF_HID)
    list(APPEND SYSTEM_INCLUDES
        ${CMAKE_CURRENT_LIST_DIR}/third-party/tinycbor/src
    )
endif()

if(USB_ITF_HID)
    list(APPEND MBEDTLS_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/x509write_crt.c
        ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/x509_create.c
        ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/x509write_csr.c
        ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/pk.c
        ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/pk_wrap.c
        ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/pkwrite.c
    )
endif()

set(CBOR_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/third-party/tinycbor/src/cborencoder.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/tinycbor/src/cborparser.c
    ${CMAKE_CURRENT_LIST_DIR}/third-party/tinycbor/src/cborparser_dup_string.c
)

set(LIBRARIES)
if(NOT SKIP_MBEDTLS_FOR_OPENSSL_EMULATION)
    list(APPEND LIBRARIES mbedtls)
endif()
if(USE_OPENSSL_EMULATION_WRAPPER)
    list(APPEND LIBRARIES OpenSSL::Crypto)
endif()

if(UNIX AND NOT APPLE AND ENABLE_EMULATION)
    find_library(TSS2_ESYS_LIB NAMES tss2-esys)
    find_library(TSS2_TCTILDR_LIB NAMES tss2-tctildr)
    if(TSS2_ESYS_LIB AND TSS2_TCTILDR_LIB)
        list(APPEND LIBRARIES ${TSS2_ESYS_LIB} ${TSS2_TCTILDR_LIB})
    else()
        message(WARNING "Linux OTP TPM backend enabled but tpm2-tss libraries not found (need tss2-esys and tss2-tctildr)")
    endif()
endif()

if(NOT ESP_PLATFORM)
    if(NOT SKIP_MBEDTLS_FOR_OPENSSL_EMULATION)
        add_library(mbedtls STATIC ${MBEDTLS_SOURCES})
        target_include_directories(mbedtls SYSTEM PUBLIC ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/include)
    endif()
    if(USB_ITF_HID)
        add_library(tinycbor STATIC ${CBOR_SOURCES})
        target_include_directories(tinycbor SYSTEM PUBLIC ${CMAKE_CURRENT_LIST_DIR}/third-party/tinycbor/src)
        list(APPEND LIBRARIES tinycbor)
    endif()
endif()

if(ENABLE_PQC)
    list(APPEND LIBRARIES
        mlkem768
        mlkem1024
        mlkem512
        mldsa44
        mldsa65
        mldsa87
    )
endif()

function(add_impl_library target)
    add_library(${target} INTERFACE)
    string(TOUPPER ${target} TARGET_UPPER)
    target_compile_definitions(${target} INTERFACE LIB_${TARGET_UPPER}=1)
endfunction()

if(USB_ITF_HID)
    list(APPEND STK_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/src/usb/hid/hid.c
    )
    list(APPEND INCLUDES
        ${CMAKE_CURRENT_LIST_DIR}/src/usb/hid
    )
endif()

if(USB_ITF_CCID)
    list(APPEND STK_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/src/usb/ccid/ccid.c
    )
    list(APPEND INCLUDES
        ${CMAKE_CURRENT_LIST_DIR}/src/usb/ccid
    )
endif()

if(NOT MSVC)
    add_compile_options("-fmacro-prefix-map=${CMAKE_CURRENT_LIST_DIR}/=")
else()
    list(APPEND STK_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/src/fs/mman.c
    )
endif()
if(ENABLE_EMULATION)
    if(APPLE)
        add_definitions("-Wno-deprecated-declarations")
        target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
                "-framework IOKit"
                "-framework CoreFoundation"
        )
    elseif(MSVC)
        target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Ncrypt)
    elseif(UNIX AND NOT APPLE AND TSS2_ESYS_LIB AND TSS2_TCTILDR_LIB)
        target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${TSS2_ESYS_LIB} ${TSS2_TCTILDR_LIB})
    endif()
    add_compile_definitions(ENABLE_EMULATION)
    list(APPEND STK_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/src/usb/emulation/emulation.c
    )
    if(USE_OPENSSL_EMULATION_WRAPPER)
        list(APPEND STK_SOURCES
            ${CMAKE_CURRENT_LIST_DIR}/src/usb/emulation/openssl.c
        )
    endif()
    list(APPEND MBEDTLS_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/third-party/mbedtls/library/aesni.c
    )
    list(APPEND INCLUDES
        ${CMAKE_CURRENT_LIST_DIR}/src/usb/emulation
    )
else()
    list(APPEND STK_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/src/usb/usb_descriptors.c
    )
endif()

if(MSVC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -wd5045 -wd4820 -wd5105")

    add_compile_definitions(
        _CRT_SECURE_NO_WARNINGS
        __STDC_WANT_SECURE_LIB__=0
        _WIN32_WINNT_WIN10_TH2=0
        _WIN32_WINNT_WIN10_RS1=0
        _WIN32_WINNT_WIN10_RS2=0
        _WIN32_WINNT_WIN10_RS3=0
        _WIN32_WINNT_WIN10_RS4=0
        _WIN32_WINNT_WIN10_RS5=0
        _STRALIGN_USE_SECURE_CRT=0
        NTDDI_WIN11_DT=0
    )
endif()


set(INTERNAL_SOURCES ${STK_SOURCES})

if(NOT TARGET stk_platform)
    add_impl_library(stk_platform)
    target_sources(stk_platform INTERFACE ${STK_SOURCES})
    target_include_directories(stk_platform INTERFACE ${INCLUDES})
    target_include_directories(stk_platform SYSTEM INTERFACE ${SYSTEM_INCLUDES})
    target_link_libraries(stk_platform INTERFACE ${LIBRARIES})
endif()
