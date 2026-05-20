#pragma once

// Closed sum of every action the app can ask TDLib to perform. Same
// design as event.hpp: the rest of the codebase only ever constructs
// values of `Command`, never raw td_api::function pointers. The runtime
// in td/client.hpp owns the translation Command → td_api::*.
//
// Symmetric to event.hpp: events flow in, commands flow out. update()
// in program.hpp returns these inside a maya::Cmd<Msg> payload, which
// the program loop hands to the TDLib client.

#include <cstdint>
#include <string>
#include <variant>

#include "td/ids.hpp"

namespace tl::td::cmd {

// ─── Auth ─────────────────────────────────────────────────────────────────
struct SubmitPhone    { std::string phone;    };
struct SubmitCode     { std::string code;     };
struct SubmitPassword { std::string password; };
struct Logout {};

// ─── Chat list ────────────────────────────────────────────────────────────
struct LoadChats { int limit = 30; };

// Idiomatic open/close pair — TDLib tracks a "currently open chat" set
// per client and only delivers full read state / typing updates for
// open ones. The app opens on selection, closes on deselection.
struct OpenChat  { TdChatId id{}; };
struct CloseChat { TdChatId id{}; };

// Ask TDLib for older messages above the given anchor. `from_message`
// is the oldest currently-loaded id; TDLib returns up to `limit`
// older ones. `from_message == 0` means "start from the chat's last
// message" (used on chat-open).
struct LoadHistory {
    TdChatId    chat_id{};
    TdMessageId from_message{};   // 0 = latest
    int         limit = 50;
};

// ─── Sending ──────────────────────────────────────────────────────────────
struct SendText {
    TdChatId    chat_id{};
    std::string body;
    // 0 = not a reply. Telegram requires the reply target to be in the
    // same chat; we enforce that on the call site, not here.
    TdMessageId reply_to{};
};

struct SendPhoto {
    TdChatId    chat_id{};
    std::string local_path;
    std::string caption;
    TdMessageId reply_to{};
};

struct SendDocument {
    TdChatId    chat_id{};
    std::string local_path;
    std::string caption;
    TdMessageId reply_to{};
};

struct SendVoice {
    TdChatId       chat_id{};
    std::string    local_path;
    int            duration_secs = 0;
    TdMessageId    reply_to{};
};

// ─── Read state ───────────────────────────────────────────────────────────
struct MarkRead {
    TdChatId                  chat_id{};
    std::vector<TdMessageId>  message_ids;   // empty = mark whole chat read
};

// ─── File downloads ───────────────────────────────────────────────────────
// Explicit download request. Auto-download covers most cases; this is
// for the long-tail (user clicks a doc card) and for re-downloading
// after a deleted local file.
struct DownloadFile {
    TdFileId    id{};
    int         priority = 1;     // 1..32, higher = more urgent
};

// ─── Info-panel data fetch ────────────────────────────────────────────────
// Asks TDLib for the userFullInfo (bio/phone/username), member list,
// or shared-media list for the given chat. Idempotent on the runtime
// side — repeated calls during a session just re-emit the same event.
struct LoadPeerInfo    { TdChatId chat_id{}; TdUserId user_id{}; };
struct LoadMembers     { TdChatId chat_id{}; };
struct LoadSharedMedia { TdChatId chat_id{}; int tab = 0; };

// ─── The variant ──────────────────────────────────────────────────────────

using Command = std::variant<
    SubmitPhone, SubmitCode, SubmitPassword, Logout,
    LoadChats, OpenChat, CloseChat, LoadHistory,
    SendText, SendPhoto, SendDocument, SendVoice,
    MarkRead, DownloadFile,
    LoadPeerInfo, LoadMembers, LoadSharedMedia
>;

}  // namespace tl::td::cmd
