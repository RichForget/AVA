#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/http/transport.h"

namespace ava::http {
// Experimental ACP-only transport. Each request owns all handles and sockets;
// no transport subprocess can survive termination of the AVA process.
class LibcurlTransport final : public Transport
{
 public:
  [[nodiscard]] ava::core::Result<HttpResponse> send(HttpRequest const& request) override;
  [[nodiscard]] ava::core::Result<HttpResponse> send(HttpRequest const& request, CancelCallback cancel_requested) override;
  [[nodiscard]] bool supports_streaming() const noexcept override { return true; }
  [[nodiscard]] ava::core::Result<HttpResponse> send_streaming(HttpRequest const& request, BodyChunkSink on_body_chunk,
                                                               CancelCallback cancel_requested = nullptr) override;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};
}  // namespace ava::http
