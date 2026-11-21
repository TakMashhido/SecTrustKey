#
# This file is part of the Pico FIDO distribution (https://github.com/polhenarejos/pico-fido).
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

cmake_minimum_required(VERSION 3.13)

option(CARDPUTER_ADV "Build the Cardputer ADV port" OFF)

# USB identity. Override at configure time, e.g. -DUSB_VID=0x1209 -DUSB_PID=0x0001, and the
# strings with -DUSB_MANUFACTURER="..." -DUSB_PRODUCT="...".
if(NOT DEFINED USB_VID)
    set(USB_VID 0x2E8A)
endif()
if(NOT DEFINED USB_PID)
    set(USB_PID 0x10FE)
endif()
if(NOT DEFINED USB_MANUFACTURER)
    set(USB_MANUFACTURER "Koustav Kumar Mondal")
endif()
if(NOT DEFINED USB_PRODUCT)
    set(USB_PRODUCT "SecTrustKey")
endif()
add_compile_definitions(USB_MANUFACTURER_STR="${USB_MANUFACTURER}" USB_PRODUCT_STR="${USB_PRODUCT}")

if(ESP_PLATFORM)
    set(STK_COMPONENTS fido)
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)
else()
    if(NOT ENABLE_EMULATION)
        message(FATAL_ERROR "SecTrustKey builds for the ESP32-S3 with ESP-IDF, or as a host "
                            "emulator with -DENABLE_EMULATION=ON.")
    endif()

    project(sectrustkey C CXX ASM)

    set(CMAKE_C_STANDARD 11)
    set(CMAKE_CXX_STANDARD 17)

    add_executable(sectrustkey)
endif()

include(platform/cmake/options.cmake)

option(ENABLE_POWER_ON_RESET "Enable/disable power cycle on reset" ON)
configure_bool_option(
    ENABLE_POWER_ON_RESET
    ENABLE_POWER_ON_RESET
    "Power cycle on reset: \t enabled"
    "Power cycle on reset: \t disabled"
)

option(ENABLE_OATH_APP "Enable/disable OATH application" ON)
configure_bool_option(
    ENABLE_OATH_APP
    ENABLE_OATH_APP
    "OATH Application: \t\t enabled"
    "OATH Application: \t\t disabled"
)

option(ENABLE_OTP_APP "Enable/disable OTP application" ON)
configure_bool_option(
    ENABLE_OTP_APP
    ENABLE_OTP_APP
    "OTP Application: \t\t enabled"
    "OTP Application: \t\t disabled"
)

if(FORBID_DISABLE_AUV)
    add_compile_definitions(FORBID_DISABLE_AUV=1)
endif()
if(DEFAULT_MCUV_NOT_REQUIRED)
    add_compile_definitions(DEFAULT_MCUV_NOT_REQUIRED=1)
endif()
if(DEFAULT_PIN_POLICY)
    add_compile_definitions(DEFAULT_PIN_POLICY=1)
endif()
if(MAX_PIN_RETRIES)
    add_compile_definitions(MAX_PIN_RETRIES=${MAX_PIN_RETRIES})
endif()
if(FORCE_BUTTON_WAIT OR CARDPUTER_ADV)
    # The ADV has no boot button sequence for protocol presence; every core
    # request that asks for UP must go through the keyboard/display bridge.
    add_compile_definitions(FORCE_BUTTON_WAIT=1)
endif()

if(ENABLE_OTP_APP OR ENABLE_OATH_APP)
    set(USB_ITF_CCID 1)
    set(USB_ITF_WCID 1)
else()
    set(USB_ITF_CCID 0)
endif()

set(USB_ITF_HID 1)
include(platform/platform_import.cmake)
if(CARDPUTER_ADV)
    include(cmake/board.cmake)
    list(APPEND SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/apps/fido/cardputer_credentials.c
    )
    list(APPEND INCLUDES
        ${CMAKE_CURRENT_LIST_DIR}/board
    )
endif()
SET_BUILD_NUMBER("${CMAKE_CURRENT_LIST_DIR}")

if(NOT ESP_PLATFORM)
    set(SOURCES ${STK_SOURCES})
endif()

list(APPEND SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/apps/fido/fido.c
    ${CMAKE_CURRENT_LIST_DIR}/apps/fido/object_authorization.c
    ${CMAKE_CURRENT_LIST_DIR}/apps/fido/object_provider.c
    ${CMAKE_CURRENT_LIST_DIR}/apps/fido/resident_container.c
    ${CMAKE_CURRENT_LIST_DIR}/apps/fido/files.c
    ${CMAKE_CURRENT_LIST_DIR}/apps/fido/cmd_register.c
    ${CMAKE_CURRENT_LIST_DIR}/apps/fido/cmd_authenticate.c
    ${CMAKE_CURRENT_LIST_DIR}/apps/fido/cmd_version.c
    ${CMAKE_CURRENT_LIST_DIR}/apps/fido/cbor.c
    ${CMAKE_CURRENT_LIST_DIR}/apps/fido/cbor_reset.c
    ${CMAKE_CURRENT_LIST_DIR}/apps/fido/cbor_get_info.c
    ${CMAKE_CURRENT_LIST_DIR}/apps/fido/cbor_make_credential.c
    ${CMAKE_CURRENT_LIST_DIR}/apps/fido/event.c
    ${CMAKE_CURRENT_LIST_DIR}/apps/fido/known_apps.c
    ${CMAKE_CURRENT_LIST_DIR}/apps/fido/cbor_client_pin.c
    ${CMAKE_CURRENT_LIST_DIR}/apps/fido/credential.c
    ${CMAKE_CURRENT_LIST_DIR}/apps/fido/cbor_get_assertion.c
    ${CMAKE_CURRENT_LIST_DIR}/apps/fido/cbor_selection.c
    ${CMAKE_CURRENT_LIST_DIR}/apps/fido/cbor_cred_mgmt.c
    ${CMAKE_CURRENT_LIST_DIR}/apps/fido/cbor_config.c
    ${CMAKE_CURRENT_LIST_DIR}/apps/fido/cbor_vendor.c
    ${CMAKE_CURRENT_LIST_DIR}/apps/fido/fido_vault.c
    ${CMAKE_CURRENT_LIST_DIR}/apps/fido/cbor_large_blobs.c
    ${CMAKE_CURRENT_LIST_DIR}/apps/fido/management.c
    ${CMAKE_CURRENT_LIST_DIR}/apps/fido/defs.c
)

# OpenPGP card + PIV (provenance in NOTICE.md). The FIDO
# management.c/defs.c already advertise OpenPGP/PIV through app_exists(), so the
# OpenPGP copies of those two files are intentionally not built.
option(ENABLE_OPENPGP_APP "Enable/disable OpenPGP card and PIV applications" ON)
if(ENABLE_OPENPGP_APP)
    add_compile_definitions(ENABLE_OPENPGP_APP=1)
    foreach(_f openpgp object_provider key_container openpgp_vault piv
               cmd_select cmd_get_data cmd_verify cmd_put_data cmd_select_data
               cmd_import_data cmd_version cmd_change_pin cmd_mse cmd_internal_aut
               cmd_challenge cmd_activate_file cmd_terminate_df cmd_pso
               cmd_keypair_gen cmd_reset_retry do stk_isolation)
        list(APPEND SOURCES ${CMAKE_CURRENT_LIST_DIR}/apps/openpgp/${_f}.c)
    endforeach()
    foreach(_f cmd_version cmd_select cmd_get_serial cmd_verify cmd_get_data
               cmd_get_metadata cmd_authenticate cmd_asym_keygen cmd_put_data
               cmd_set_mgmkey cmd_move_key cmd_change_pin cmd_reset_retry
               cmd_set_retries cmd_reset cmd_attestation cmd_import_asym)
        list(APPEND SOURCES ${CMAKE_CURRENT_LIST_DIR}/apps/piv/${_f}.c)
    endforeach()
endif()

if(ENABLE_OATH_APP)
    list(APPEND SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/apps/fido/oath_container.c
        ${CMAKE_CURRENT_LIST_DIR}/apps/fido/oath.c
        ${CMAKE_CURRENT_LIST_DIR}/apps/fido/stk_totp.c
    )
endif()

if(ENABLE_OTP_APP)
    list(APPEND SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/apps/fido/otp_container.c
        ${CMAKE_CURRENT_LIST_DIR}/apps/fido/otp.c
    )
endif()

SET_VERSION(ver_major ver_minor "${CMAKE_CURRENT_LIST_DIR}/apps/fido/version.h")
if(ESP_PLATFORM)
    project(sectrustkey)
    if(CARDPUTER_ADV)
        # Managed M5Unified sources carry a warning in optional PowerHub RTC
        # code. Keep it visible for this third-party component, but do not let
        # it fail the ADV build.
        idf_build_get_property(_cardputer_components BUILD_COMPONENTS)
        foreach(_cardputer_component ${_cardputer_components})
            if(_cardputer_component MATCHES "m5unified|m5gfx")
                idf_component_get_property(_cardputer_lib ${_cardputer_component}
                                           COMPONENT_LIB)
                if(TARGET ${_cardputer_lib})
                    target_compile_options(${_cardputer_lib} PRIVATE
                        -Wno-error=maybe-uninitialized)
                endif()
            endif()
        endforeach()
    endif()
endif()

set(INCLUDES ${INCLUDES}
    ${CMAKE_CURRENT_LIST_DIR}/apps/fido
    ${CMAKE_CURRENT_LIST_DIR}/apps
    ${CMAKE_CURRENT_LIST_DIR}/apps/openpgp
    ${CMAKE_CURRENT_LIST_DIR}/apps/piv
)

if(NOT ESP_PLATFORM)
    target_sources(sectrustkey PUBLIC ${SOURCES})
    target_include_directories(sectrustkey PUBLIC ${INCLUDES})

    set(COMMON_COMPILE_OPTIONS
        -Wall
    )
    target_compile_options(sectrustkey PRIVATE ${COMMON_COMPILE_OPTIONS})

    stk_apply_strict_flags(
        SOURCES ${SOURCES}
        FILTER_REGEX "/apps/fido/|/platform/src/|/platform/config/"
    )

    if(NOT MSVC)
        string(FIND ${CMAKE_C_COMPILER} ":" COMPILER_COLON)
        if(${COMPILER_COLON} GREATER_EQUAL 0)
            target_compile_options(sectrustkey PRIVATE
                -Wno-error=use-after-free
            )
        endif()
    endif()

    if(ENABLE_EMULATION)
        enable_testing()
        if(NOT MSVC)
            set(EMULATION_NON_MSVC_COMPILE_OPTIONS
                -fdata-sections
                -ffunction-sections
            )
            target_compile_options(sectrustkey PRIVATE ${EMULATION_NON_MSVC_COMPILE_OPTIONS})
        endif()

        if(APPLE)
            set(EMULATION_APPLE_LINK_OPTIONS
                -Wl,-dead_strip
            )
            target_link_options(sectrustkey PRIVATE ${EMULATION_APPLE_LINK_OPTIONS})

            if(DEBUG_APDU)
                set(DEBUG_APDU_SANITIZER_OPTIONS
                    -fsanitize=address
                    -g
                    -O1
                    -fno-omit-frame-pointer
                )
                target_compile_options(sectrustkey PRIVATE ${DEBUG_APDU_SANITIZER_OPTIONS})
                target_link_options(sectrustkey PRIVATE ${DEBUG_APDU_SANITIZER_OPTIONS})
            endif()
        elseif(NOT MSVC)
            set(EMULATION_NON_APPLE_LINK_OPTIONS
                -Wl,--gc-sections
            )
            target_link_options(sectrustkey PRIVATE ${EMULATION_NON_APPLE_LINK_OPTIONS})
        endif()

        target_link_libraries(sectrustkey PRIVATE stk_platform mbedtls)
        if(NOT MSVC)
            target_link_libraries(sectrustkey PRIVATE pthread m)
        endif()

    endif()
endif()
