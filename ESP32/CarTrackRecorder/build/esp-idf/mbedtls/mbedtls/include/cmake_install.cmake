# Install script for directory: /Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "TRUE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/Users/aubreyhan/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20260121/xtensa-esp-elf/bin/xtensa-esp32s3-elf-objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/mbedtls" TYPE FILE PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ FILES
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/aes.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/aria.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/asn1.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/asn1write.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/base64.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/bignum.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/block_cipher.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/build_info.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/camellia.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/ccm.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/chacha20.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/chachapoly.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/check_config.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/cipher.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/cmac.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/compat-2.x.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/config_adjust_legacy_crypto.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/config_adjust_legacy_from_psa.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/config_adjust_psa_from_legacy.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/config_adjust_psa_superset_legacy.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/config_adjust_ssl.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/config_adjust_x509.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/config_psa.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/constant_time.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/ctr_drbg.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/debug.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/des.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/dhm.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/ecdh.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/ecdsa.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/ecjpake.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/ecp.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/entropy.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/error.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/gcm.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/hkdf.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/hmac_drbg.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/lms.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/mbedtls_config.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/md.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/md5.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/memory_buffer_alloc.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/net_sockets.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/nist_kw.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/oid.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/pem.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/pk.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/pkcs12.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/pkcs5.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/pkcs7.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/platform.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/platform_time.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/platform_util.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/poly1305.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/private_access.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/psa_util.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/ripemd160.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/rsa.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/sha1.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/sha256.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/sha3.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/sha512.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/ssl.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/ssl_cache.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/ssl_ciphersuites.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/ssl_cookie.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/ssl_ticket.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/threading.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/timing.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/version.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/x509.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/x509_crl.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/x509_crt.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/mbedtls/x509_csr.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/psa" TYPE FILE PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ FILES
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/psa/build_info.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/psa/crypto.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/psa/crypto_adjust_auto_enabled.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/psa/crypto_adjust_config_dependencies.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/psa/crypto_adjust_config_key_pair_types.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/psa/crypto_adjust_config_synonyms.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/psa/crypto_builtin_composites.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/psa/crypto_builtin_key_derivation.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/psa/crypto_builtin_primitives.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/psa/crypto_compat.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/psa/crypto_config.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/psa/crypto_driver_common.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/psa/crypto_driver_contexts_composites.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/psa/crypto_driver_contexts_key_derivation.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/psa/crypto_driver_contexts_primitives.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/psa/crypto_extra.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/psa/crypto_legacy.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/psa/crypto_platform.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/psa/crypto_se_driver.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/psa/crypto_sizes.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/psa/crypto_struct.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/psa/crypto_types.h"
    "/Users/aubreyhan/.espressif/v5.5.6/esp-idf/components/mbedtls/mbedtls/include/psa/crypto_values.h"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/Users/aubreyhan/Tools/git_repo/PersonalDev/ESP32/CarTrackRecorder/build/esp-idf/mbedtls/mbedtls/include/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
