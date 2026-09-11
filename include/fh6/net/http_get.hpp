#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace fh6::net {

// Blocking in-memory HTTP(S) GET. Body on HTTP 200, else nullopt. Bounded timeouts.
// extra_header is an optional raw header line (e.g. "Authorization: ...").
std::optional<std::string> http_get(std::string_view url, std::string_view extra_header = {}, int timeout_ms = 15000);

} // namespace fh6::net
