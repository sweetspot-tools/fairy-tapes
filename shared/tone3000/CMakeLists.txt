# Fairy Tapes · shared TONE3000 client (spec 2026-10-21-tone3000-integration-design.md).
# libcurl comes from vendors/curl (a fixed release), built here as a static library with HTTP(S) only; no VSTGUI or VST3
# SDK dependency, so the tests run without a window.

# Without OpenSSL (non-Windows) the client is simply not built, so a machine or CI image without it still configures.
if(NOT WIN32)
  find_package(OpenSSL QUIET)
  if(NOT OpenSSL_FOUND)
    message(STATUS "TONE3000 client disabled: OpenSSL not found")
    return()
  endif()
endif()

set(TONE3000_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(CURL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../vendors/curl")

# Plain variables inside a function: curl's options stay out of the cache and out of the other targets.
function(fairy_add_curl)
  # curl declares its options with cache set()/option() under old policies, which would delete these plain variables.
  set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
  set(CMAKE_POLICY_DEFAULT_CMP0126 NEW)
  set(BUILD_CURL_EXE OFF)
  set(BUILD_SHARED_LIBS OFF)
  set(BUILD_STATIC_LIBS ON)
  set(BUILD_LIBCURL_DOCS OFF)
  set(BUILD_MISC_DOCS OFF)
  set(ENABLE_CURL_MANUAL OFF)
  set(BUILD_TESTING OFF)
  set(CURL_ENABLE_EXPORT_TARGET OFF)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  set(CURL_DISABLE_INSTALL ON)
  # HTTP(S) is all the client needs.
  foreach(proto DICT FILE FTP GOPHER IMAP LDAP LDAPS MQTT POP3 RTSP SMB SMTP TELNET TFTP SCP SFTP)
    set(CURL_DISABLE_${proto} ON)
  endforeach()
  set(CURL_DISABLE_WEBSOCKETS ON)
  set(CURL_USE_LIBPSL OFF)
  set(CURL_USE_LIBSSH2 OFF)
  set(CURL_USE_LIBSSH OFF)
  set(USE_NGHTTP2 OFF)
  set(USE_LIBIDN2 OFF)
  set(CURL_BROTLI OFF)
  set(CURL_ZSTD OFF)
  set(CURL_ZLIB OFF)
  if(WIN32)
    set(CURL_USE_SCHANNEL ON)   # the system's TLS, no extra library to ship
  else()
    set(CURL_USE_OPENSSL ON)    # curl 8.15 removed Secure Transport, so macOS also needs OpenSSL (see the spec, section 7)
  endif()
  add_subdirectory(${CURL_DIR} ${CMAKE_CURRENT_BINARY_DIR}/curl EXCLUDE_FROM_ALL)
endfunction()
fairy_add_curl()

add_library(tone3000 STATIC src/http_policy.cpp src/http_curl.cpp src/pkce.cpp src/metadata.cpp src/api.cpp)
target_include_directories(tone3000 PUBLIC ${TONE3000_DIR}/include)
# nlohmann/json (single header, MIT) as shipped with the NAM core
target_include_directories(tone3000 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../vendors/NeuralAmpModelerCore/Dependencies)
target_link_libraries(tone3000 PUBLIC libcurl_static)
if(WIN32)
  target_link_libraries(tone3000 PUBLIC bcrypt)
endif()
set_target_properties(tone3000 PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(tone3000 PUBLIC cxx_std_20)

# HTTP policy and the libcurl client against a loopback server of the test's own (no internet).
add_executable(tone3000_http_tests tests/test_http.cpp)
target_link_libraries(tone3000_http_tests PRIVATE tone3000)
add_test(NAME tone3000_http_tests COMMAND tone3000_http_tests)
set_tests_properties(tone3000_http_tests PROPERTIES SKIP_RETURN_CODE 77)

# PKCE, rate limiter, safe file names and the model sidecar file: pure logic, no network.
add_executable(tone3000_basics_tests tests/test_basics.cpp)
target_link_libraries(tone3000_basics_tests PRIVATE tone3000)
add_test(NAME tone3000_basics_tests COMMAND tone3000_basics_tests)

# The REST client against a fake HttpClient and files of example answers (no network).
add_executable(tone3000_api_tests tests/test_api.cpp)
target_link_libraries(tone3000_api_tests PRIVATE tone3000)
add_test(NAME tone3000_api_tests COMMAND tone3000_api_tests)
