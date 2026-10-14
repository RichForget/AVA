if(NOT DEFINED AVA_LIBCURL_TRANSPORT_SOURCE)
  message(FATAL_ERROR "libcurl transport static contract requires a source path")
endif()

file(READ "${AVA_LIBCURL_TRANSPORT_SOURCE}" libcurl_source)

foreach(required IN ITEMS
    "CURLOPT_FOLLOWLOCATION, 0L"
    "CURLOPT_REDIR_PROTOCOLS_STR, \"https\""
    "CURLOPT_SSL_VERIFYPEER, 1L"
    "CURLOPT_SSL_VERIFYHOST, 2L"
    "CURLOPT_PROXY, \"\"")
  string(FIND "${libcurl_source}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "libcurl transport static contract is missing: ${required}")
  endif()
endforeach()

foreach(forbidden IN ITEMS
    "CURLOPT_FOLLOWLOCATION, request.follow_redirects"
    "CURLOPT_REDIR_PROTOCOLS_STR, \"http,https\"")
  string(FIND "${libcurl_source}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR "libcurl transport regained redirect authority: ${forbidden}")
  endif()
endforeach()
