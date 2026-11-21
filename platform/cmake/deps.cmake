include(FetchContent)

option(ENABLE_EDDSA "Enable/disable EdDSA (Ed25519/Ed448) support" ON)
configure_bool_option(
    ENABLE_EDDSA
    ""
    "EdDSA support:\t\t enabled"
    "EdDSA support:\t\t disabled"
)

set(MBEDTLS_PATH "${CMAKE_CURRENT_LIST_DIR}/../third-party/mbedtls")
set(TINYCBOR_PATH "${CMAKE_CURRENT_LIST_DIR}/../third-party/tinycbor")
set(MLKEM_PATH "${CMAKE_CURRENT_LIST_DIR}/../third-party/mlkem")
set(MLDSA_PATH "${CMAKE_CURRENT_LIST_DIR}/../third-party/mldsa")

set(STK_MBEDTLS_STD_REPO "https://github.com/Mbed-TLS/mbedtls.git")
set(STK_MBEDTLS_STD_REF "v3.6.7")
set(STK_MBEDTLS_EDDSA_REPO "https://github.com/polhenarejos/mbedtls.git")
set(STK_MBEDTLS_EDDSA_REF "v3.6.7-eddsa")

set(STK_TINYCBOR_REPO "https://github.com/intel/tinycbor.git")
set(STK_TINYCBOR_REF "v0.6.1")
set(STK_MLKEM_REPO "https://github.com/pq-code-package/mlkem-native.git")
set(STK_MLKEM_REF "v1.2.0")
set(STK_MLDSA_REPO "https://github.com/pq-code-package/mldsa-native.git")
set(STK_MLDSA_REF "v1.0.0-beta2")

set(STK_FETCH_DEPS_ON_DEMAND ON CACHE BOOL "Fetch third-party deps into platform/third-party when missing")

function(stk_sync_dep name repo ref dest)
    set(_marker "${dest}/.stk_dep_source")
    set(_need_fetch OFF)

    if(NOT EXISTS "${dest}")
        if(NOT STK_FETCH_DEPS_ON_DEMAND)
            message(FATAL_ERROR "${name} source code not found at ${dest}. Enable STK_FETCH_DEPS_ON_DEMAND or provide the source tree manually.")
        endif()
        set(_need_fetch ON)
    else()
        set(_repo_ok OFF)
        set(_ref_ok OFF)
        if(EXISTS "${_marker}")
            file(STRINGS "${_marker}" _meta_lines)
            foreach(_line IN LISTS _meta_lines)
                if(_line STREQUAL "REPO=${repo}")
                    set(_repo_ok ON)
                endif()
                if(_line STREQUAL "REF=${ref}")
                    set(_ref_ok ON)
                endif()
            endforeach()
        endif()
        if(NOT _repo_ok OR NOT _ref_ok)
            set(_need_fetch ON)
        endif()
    endif()

    if(_need_fetch)
        message(STATUS "[deps] ${name}: repo=${repo} ref=${ref} status=updating (this may take a few seconds)")
        if(EXISTS "${dest}")
            file(REMOVE_RECURSE "${dest}")
        endif()
        execute_process(
            COMMAND git clone ${repo} ${dest}
            RESULT_VARIABLE _clone_rc
            OUTPUT_VARIABLE _clone_out
            ERROR_VARIABLE _clone_err
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_STRIP_TRAILING_WHITESPACE
        )
        if(NOT _clone_rc EQUAL 0)
            message(FATAL_ERROR "Failed to clone ${name} from ${repo}\nstdout: ${_clone_out}\nstderr: ${_clone_err}")
        endif()
        execute_process(
            COMMAND git -C ${dest} checkout ${ref}
            RESULT_VARIABLE _checkout_rc
            OUTPUT_VARIABLE _checkout_out
            ERROR_VARIABLE _checkout_err
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_STRIP_TRAILING_WHITESPACE
        )
        if(NOT _checkout_rc EQUAL 0)
            message(FATAL_ERROR "Failed to checkout ${name} ref ${ref}\nstdout: ${_checkout_out}\nstderr: ${_checkout_err}")
        endif()
        if(NOT EXISTS "${dest}")
            message(FATAL_ERROR "Failed to fetch ${name} into ${dest}")
        endif()
        file(WRITE "${_marker}" "REPO=${repo}\nREF=${ref}\n")
    else()
        message(STATUS "[deps] ${name}: repo=${repo} ref=${ref} status=cached")
    endif()
endfunction()

if(ENABLE_EDDSA)
    set(MBEDTLS_ORIGIN "${STK_MBEDTLS_EDDSA_REPO}")
    set(MBEDTLS_REF "${STK_MBEDTLS_EDDSA_REF}")

    add_compile_definitions(
        MBEDTLS_ECP_DP_ED25519_ENABLED=1
        MBEDTLS_ECP_DP_ED448_ENABLED=1
        MBEDTLS_EDDSA_C=1
        MBEDTLS_SHA3_C=1
    )
else()
    set(MBEDTLS_ORIGIN "${STK_MBEDTLS_STD_REPO}")
    set(MBEDTLS_REF "${STK_MBEDTLS_STD_REF}")
endif()

stk_sync_dep(mbedtls_dep "${MBEDTLS_ORIGIN}" "${MBEDTLS_REF}" "${MBEDTLS_PATH}")

if(USB_ITF_HID)
    stk_sync_dep(tinycbor_dep "${STK_TINYCBOR_REPO}" "${STK_TINYCBOR_REF}" "${TINYCBOR_PATH}")
endif()
if(ENABLE_PQC)
    stk_sync_dep(mlkem_dep "${STK_MLKEM_REPO}" "${STK_MLKEM_REF}" "${MLKEM_PATH}")
    stk_sync_dep(mldsa_dep "${STK_MLDSA_REPO}" "${STK_MLDSA_REF}" "${MLDSA_PATH}")
endif()
