#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/event/RuntimeEvent.h"
#include "ava/core/result.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace ava::app::acp {

inline constexpr std::size_t kMaxPromptSessionUpdates = 16U * 1024U;
inline constexpr std::size_t kMaxPromptSessionUpdateBytes = 16U * 1024U * 1024U;
inline constexpr std::size_t kMaxStreamContentChunkBytes = 1024U;
inline constexpr std::size_t kMaxToolUpdateContentBytes = 128U * 1024U;

struct AcpTextContent
{
  std::string text;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

struct AcpToolCallLocation
{
  std::string path;
  std::optional<std::size_t> line = std::nullopt;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

struct AcpToolCallTextContent
{
  AcpTextContent content;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

using AcpToolCallContent = std::variant<AcpToolCallTextContent>;

enum class AcpContentChunkKind
{
  UserMessage,
  AgentMessage,
  AgentThought,
};

struct AcpContentChunkUpdate
{
  AcpContentChunkKind kind = AcpContentChunkKind::AgentMessage;
  AcpTextContent content;
  std::optional<std::string> message_id = std::nullopt;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

struct AcpToolCallUpdate
{
  bool initial = false;
  std::string tool_call_id;
  std::optional<std::string> title = std::nullopt;
  std::optional<std::string> kind = std::nullopt;
  std::optional<std::string> status = std::nullopt;
  std::optional<std::vector<AcpToolCallContent>> content = std::nullopt;
  std::optional<std::vector<AcpToolCallLocation>> locations = std::nullopt;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

using SessionUpdate = std::variant<AcpContentChunkUpdate, AcpToolCallUpdate>;

struct RuntimeSessionUpdateMapperOptions
{
  std::filesystem::path workspace_root;
  std::string message_id;
  std::size_t max_updates = kMaxPromptSessionUpdates;
  std::size_t max_encoded_bytes = kMaxPromptSessionUpdateBytes;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

class RuntimeSessionUpdateMapper
{
 public:
  explicit RuntimeSessionUpdateMapper(RuntimeSessionUpdateMapperOptions options);

  [[nodiscard]] ava::core::Result<std::optional<SessionUpdate>> map(ava::event::RuntimeEvent const& event);
  [[nodiscard]] ava::core::Result<std::optional<std::string>> map_and_encode(ava::event::RuntimeEvent const& event);
  [[nodiscard]] ava::core::Result<std::vector<std::string>> map_coalesced_and_encode(ava::event::RuntimeEvent const& event);
  [[nodiscard]] ava::core::Result<std::vector<std::string>> flush_coalesced();

  [[nodiscard]] bool has_pending_content() const noexcept { return pending_content_.has_value(); }
  [[nodiscard]] bool streamed_agent_text() const noexcept;
  [[nodiscard]] std::size_t update_count() const noexcept;
  [[nodiscard]] std::size_t encoded_bytes() const noexcept;

 private:
  [[nodiscard]] ava::core::Result<std::optional<std::string>> account_and_encode(std::optional<SessionUpdate> update);
  [[nodiscard]] ava::core::Result<std::optional<std::string>> flush_pending_content();

  RuntimeSessionUpdateMapperOptions options_;
  std::optional<AcpContentChunkUpdate> pending_content_;
  bool streamed_agent_text_ = false;
  std::size_t update_count_ = 0;
  std::size_t encoded_bytes_ = 0;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

// One prompt owns one sleeping worker. The mutex orders mapping, timed flushes,
// and sends; finish joins before the prompt response or mapper lifetime ends.
class TimedSessionUpdates
{
 public:
  using Sink = std::function<ava::core::VoidResult(std::string const&)>;
  TimedSessionUpdates(RuntimeSessionUpdateMapperOptions options, Sink sink);
  ~TimedSessionUpdates();
  [[nodiscard]] ava::core::VoidResult submit(ava::event::RuntimeEvent const& event);
  [[nodiscard]] ava::core::VoidResult finish();
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
 private:
  ava::core::VoidResult send(ava::core::Result<std::vector<std::string>> updates);
  void run();
  RuntimeSessionUpdateMapper mapper_;
  Sink sink_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  std::optional<ava::core::Error> failure_;
  bool stopping_ = false;
  std::jthread worker_;
};

[[nodiscard]] ava::core::Result<std::string> encode_session_update(SessionUpdate const& update);
[[nodiscard]] std::string acp_tool_kind(std::string_view tool_name);

}  // namespace ava::app::acp
