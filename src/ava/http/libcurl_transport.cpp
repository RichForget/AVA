#include "sys.h"
#include "ava/http/libcurl_transport.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <curl/curl.h>

namespace ava::http {
namespace {
constexpr std::size_t kMaxBody = 8U * 1024U * 1024U;
constexpr std::size_t kMaxHeaders = 64U * 1024U;
using Clock = std::chrono::steady_clock;

ava::core::Error error(std::string message)
{
  return ava::core::Error(ava::core::ErrorCategory::Provider, std::move(message));
}

struct GlobalCurl
{
  CURLcode status = curl_global_init(CURL_GLOBAL_DEFAULT);
  ~GlobalCurl()
  {
    if (status == CURLE_OK)
      curl_global_cleanup();
  }
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

struct Transfer
{
  HttpResponse response;
  Transport::BodyChunkSink sink;
  std::optional<ava::core::Error> failure;
  std::size_t header_bytes = 0;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

std::size_t body_callback(char* bytes, std::size_t size, std::size_t count, void* opaque) noexcept
{
  auto& transfer = *static_cast<Transfer*>(opaque);
  try
  {
    if (size != 1 || count > kMaxBody - transfer.response.body.size())
    {
      transfer.failure = error("libcurl response byte limit exceeded");
      return 0;
    }
    std::string_view chunk(bytes, count);
    transfer.response.body.append(chunk);
    if (transfer.sink)
    {
      auto sent = transfer.sink(chunk);
      if (!sent)
      {
        transfer.failure = std::move(sent.error());
        return 0;
      }
    }
    return count;
  }
  catch (...)
  {
    return 0; // Never unwind through libcurl's C ABI.
  }
}

std::size_t header_callback(char* bytes, std::size_t size, std::size_t count, void* opaque) noexcept
{
  auto& transfer = *static_cast<Transfer*>(opaque);
  try
  {
    if (size != 1 || count > kMaxHeaders - transfer.header_bytes)
    {
      transfer.failure = error("libcurl header byte limit exceeded");
      return 0;
    }
    transfer.header_bytes += count;
    std::string_view line(bytes, count);
    if (line.starts_with("HTTP/"))
      transfer.response.headers.clear();
    auto colon = line.find(':');
    if (colon != std::string_view::npos)
    {
      std::string key(line.substr(0, colon));
      std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : char(c); });
      auto value = line.substr(colon + 1);
      while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.remove_prefix(1);
      while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' '))
        value.remove_suffix(1);
      transfer.response.headers.insert_or_assign(std::move(key), std::string(value));
    }
    return count;
  }
  catch (...)
  {
    return 0;
  }
}

struct HeaderList
{
  std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> value{nullptr, curl_slist_free_all};
  bool append(std::string const& entry)
  {
    auto* result = curl_slist_append(value.get(), entry.c_str());
    if (!result)
      return false;
    static_cast<void>(value.release());
    value.reset(result);
    return true;
  }
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

struct Attachment
{
  CURLM* multi;
  CURL* easy;
  ~Attachment() { curl_multi_remove_handle(multi, easy); }
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

ava::core::Result<HttpResponse> perform(HttpRequest const& request, Transport::BodyChunkSink sink, Transport::CancelCallback canceled)
{
  static GlobalCurl const global;
  if (global.status != CURLE_OK)
    return std::unexpected(error("libcurl initialization failed"));
  if (request.url.find('\0') != std::string::npos || request.method.find_first_of("\r\n") != std::string::npos ||
      request.method.find('\0') != std::string::npos || request.body.size() > kMaxBody)
    return std::unexpected(error("invalid libcurl request"));
  // Resolve overrides are not needed by ACP providers; reject instead of silently
  // bypassing an authority restriction a future caller may expect to be honored.
  if (!request.resolve_hosts.empty())
    return std::unexpected(error("ACP libcurl resolve overrides are unsupported"));
  std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easy(curl_easy_init(), curl_easy_cleanup);
  std::unique_ptr<CURLM, decltype(&curl_multi_cleanup)> multi(curl_multi_init(), curl_multi_cleanup);
  if (!easy || !multi)
    return std::unexpected(error("libcurl handle allocation failed"));
  Transfer transfer{.response = {}, .sink = std::move(sink), .failure = std::nullopt};
  HeaderList headers;
  for (auto const& [key, value] : request.headers)
  {
    if (key.empty() || key.find_first_of(":\r\n") != std::string::npos || key.find('\0') != std::string::npos ||
        value.find_first_of("\r\n") != std::string::npos || value.find('\0') != std::string::npos)
      return std::unexpected(error("invalid libcurl request header"));
    if (!headers.append(key + ": " + value))
      return std::unexpected(error("libcurl header allocation failed"));
  }
  auto set = [&](CURLoption option, auto value) { return curl_easy_setopt(easy.get(), option, value) == CURLE_OK; };
  if (!set(CURLOPT_URL, request.url.c_str()) || !set(CURLOPT_CUSTOMREQUEST, request.method.c_str()) || !set(CURLOPT_PROTOCOLS_STR, "http,https") ||
      !set(CURLOPT_REDIR_PROTOCOLS_STR, "http,https") || !set(CURLOPT_FOLLOWLOCATION, request.follow_redirects ? 1L : 0L) || !set(CURLOPT_MAXREDIRS, 5L) ||
      !set(CURLOPT_NOSIGNAL, 1L) || !set(CURLOPT_TIMEOUT_MS, long(std::max(1, request.timeout_ms))) || !set(CURLOPT_PROXY, "") ||
      !set(CURLOPT_SSL_VERIFYPEER, 1L) || !set(CURLOPT_SSL_VERIFYHOST, 2L) || !set(CURLOPT_HTTPHEADER, headers.value.get()) ||
      !set(CURLOPT_WRITEFUNCTION, body_callback) || !set(CURLOPT_WRITEDATA, &transfer) || !set(CURLOPT_HEADERFUNCTION, header_callback) ||
      !set(CURLOPT_HEADERDATA, &transfer))
    return std::unexpected(error("libcurl option setup failed"));
  if (!request.body.empty() &&
      (!set(CURLOPT_POSTFIELDS, request.body.data()) || !set(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()))))
    return std::unexpected(error("libcurl body setup failed"));
  if (curl_multi_add_handle(multi.get(), easy.get()) != CURLM_OK)
    return std::unexpected(error("libcurl request attachment failed"));
  Attachment const attachment{multi.get(), easy.get()};
  auto deadline = Clock::now() + std::chrono::milliseconds(std::max(1, request.timeout_ms));
  int running = 0;
  do
  {
    if (canceled && canceled())
      return std::unexpected(error("transport request canceled"));
    if (Clock::now() >= deadline)
      return std::unexpected(error("libcurl request deadline expired"));
    if (curl_multi_perform(multi.get(), &running) != CURLM_OK)
      return std::unexpected(error("libcurl transfer failed"));
    if (running && curl_multi_poll(multi.get(), nullptr, 0, 25, nullptr) != CURLM_OK)
      return std::unexpected(error("libcurl transfer poll failed"));
  }
  while (running);
  if (transfer.failure)
    return std::unexpected(std::move(*transfer.failure));
  int remaining = 0;
  auto* message = curl_multi_info_read(multi.get(), &remaining);
  if (!message || message->msg != CURLMSG_DONE)
    return std::unexpected(error("libcurl completion missing"));
  if (message->data.result != CURLE_OK)
  {
    auto failure = error("libcurl transport failed");
    failure.with_context("curl_code", std::to_string(static_cast<int>(message->data.result)));
    failure.with_context("response_bytes", std::to_string(transfer.response.body.size()));
    return std::unexpected(std::move(failure));
  }
  long status = 0;
  if (curl_easy_getinfo(easy.get(), CURLINFO_RESPONSE_CODE, &status) != CURLE_OK)
    return std::unexpected(error("libcurl response status missing"));
  transfer.response.status_code = static_cast<int>(status);
  return std::move(transfer.response);
}
}  // namespace

ava::core::Result<HttpResponse> LibcurlTransport::send(HttpRequest const& request)
{
  return send(request, nullptr);
}
ava::core::Result<HttpResponse> LibcurlTransport::send(HttpRequest const& request, CancelCallback canceled)
{
  return send_streaming(request, nullptr, std::move(canceled));
}
ava::core::Result<HttpResponse> LibcurlTransport::send_streaming(HttpRequest const& request, BodyChunkSink sink, CancelCallback canceled)
{
  try
  {
    return perform(request, std::move(sink), std::move(canceled));
  }
  catch (...)
  {
    return std::unexpected(error("libcurl request callback failed"));
  }
}
}  // namespace ava::http
