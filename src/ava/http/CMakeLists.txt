add_library(ava_http
  "curl_transport.cpp"
  "transport.cpp"
)

target_include_directories(ava_http
  PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(ava_http
  PUBLIC
    AVA::core
    AVA::observability
    AVA::process
)

ava_apply_debugging(ava_http)
ava_apply_warnings(ava_http)
ava_apply_sanitizers(ava_http)

# Create an ALIAS target.
add_library(AVA::http ALIAS ava_http)
# Experimental ACP transport only; ordinary AVA builds retain curl CLI.
option(AVA_ACP_INPROCESS_HTTP "Build the isolated ACP libcurl transport" OFF)
if(AVA_ACP_INPROCESS_HTTP)
  # Exact official release archive; curl license is recorded in notices.
  set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
  set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
  set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
  set(CURL_DISABLE_OPENSSL_AUTO_LOAD_CONFIG ON CACHE BOOL "" FORCE)
  set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
  set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
  set(CURL_USE_GSSAPI OFF CACHE BOOL "" FORCE)
  set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
  set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
  set(CURL_ZLIB OFF CACHE STRING "" FORCE)
  set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
  set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
  set(CURL_DISABLE_IPFS ON CACHE BOOL "" FORCE)
  set(CURL_DISABLE_WEBSOCKETS ON CACHE BOOL "" FORCE)
  set(ENABLE_THREADED_RESOLVER ON CACHE BOOL "" FORCE)
  set(CURL_DISABLE_NETRC ON CACHE BOOL "" FORCE)
  foreach(protocol DICT FILE FTP GOPHER IMAP LDAP LDAPS MQTT POP3 RTSP SMTP TELNET TFTP)
    set(CURL_DISABLE_${protocol} ON CACHE BOOL "" FORCE)
  endforeach()
  find_package(OpenSSL 3.6.5 EXACT REQUIRED)
  FetchContent_Declare(ava_libcurl
    URL https://curl.se/download/curl-8.22.0.tar.xz
    URL_HASH SHA256=f7ef3ae8a22e521f289803fe93543eb64c329b58aa73a9e224dfd915a2a5f4f7
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(ava_libcurl)
  target_sources(ava_http PRIVATE libcurl_transport.cpp)
  target_link_libraries(ava_http PRIVATE CURL::libcurl_static)
  target_compile_definitions(ava_http PUBLIC AVA_ACP_INPROCESS_HTTP=1)
endif()
