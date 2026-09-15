#pragma once
#include <QString>
#include <optional>

/// Discord's local IPC pipe. The handshake alone answers with a READY event that carries the user
/// the desktop app is logged in as: no OAuth scope, no approval from Discord, nothing sent but our
/// application id. Everything past READY (voice, activities) is for approved partners only, so
/// this asks for nothing else. Blocking for up to timeoutMs per pipe: call it off the UI thread.
namespace DiscordIpc {
struct User {
	QString id, username;
};
/// Who the Discord app on this PC is logged in as, or nothing when it is not running.
std::optional<User> currentUser(int timeoutMs = 1500);
} // namespace DiscordIpc
