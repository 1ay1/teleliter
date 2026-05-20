#pragma once

// Closed sum of every TDLib update the app cares about. The rest of
// the codebase reacts ONLY to these — we never let raw td_api::update*
// pointers leak past src/td/. That's the seal that makes the rest of
// the program testable without TDLib installed (see the STUB section at
// the bottom: when TELELITER_HAS_TDLIB is off, the variant alternatives
// still exist as empty types, but no Producer ever constructs them).
//
// Strings carry pre-formatted display strings — utf-8 only, no html
// entities, no markdown. The mapping layer in td/mapping.hpp owns that
// normalisation. Anything richer (formatted entities, message links,
// rich-media metadata) lives inside the model:: VMs the events carry.

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "model/view_models.hpp"
#include "td/auth_stage.hpp"
#include "td/ids.hpp"

namespace tl::td::event {

// ─── Auth ──────────────────────────────────────────────────────────────────
// AuthStage lives in td/auth_stage.hpp to break the include cycle with
// model/view_models.hpp (which carries the AuthVM that holds an AuthStage).

struct AuthStateChanged {
    AuthStage   stage = AuthStage::Connecting;
    std::string hint;        // optional human-readable detail (e.g. "code sent to +1***")
};

// Surfaces TDLib errors raised in response to a previous Submit*. The
// UI renders these into the auth overlay's hint line.
struct AuthError {
    std::string message;
};

// ─── Connection ───────────────────────────────────────────────────────────
// connectionState{Connecting,Updating,Ready,…}. We expose the simple
// enum + the same five states TDLib does; the UI renders a status dot
// in the header.

enum class ConnectionStage : unsigned char {
    WaitingForNetwork,
    ConnectingToProxy,
    Connecting,
    Updating,
    Ready,
};

struct ConnectionStateChanged {
    ConnectionStage stage = ConnectionStage::Connecting;
};

// ─── Self info ────────────────────────────────────────────────────────────
struct MeLoaded {
    model::UserVM me;
};

// ─── Chat list ────────────────────────────────────────────────────────────
// One event per chat learnt about. The order they arrive in matches
// TDLib's sort order on the main chat list. The receiver upserts into
// AppModel.chats keyed by id; ChatPositionChanged moves an existing
// row to a new slot.

struct ChatUpserted {
    model::ChatListItemVM chat;
};

struct ChatRemoved {
    TdChatId id{};
};

// updateChatLastMessage / updateChatReadInbox / updateChatReadOutbox /
// updateChatTitle / updateChatPhoto all collapse into this re-snapshot
// of the relevant header fields. Keeps the wire surface narrow at the
// cost of a tiny bit of redundancy on chat-photo updates.
struct ChatPatched {
    TdChatId      id{};
    model::ChatListItemVM patch;       // partial snapshot to merge
};

// ─── Messages ─────────────────────────────────────────────────────────────
// MessageNew arrives for both incoming peer messages AND the echo of
// our own send (TDLib emits updateNewMessage for our own message once
// it lands on the server). The receiver decides whether the message
// already exists (deduplicate on id) before inserting.

struct MessageNew {
    TdChatId           chat_id{};
    model::MessageVM   message;
};

struct MessageEdited {
    TdChatId           chat_id{};
    TdMessageId        message_id{};
    std::string        new_body;        // re-rendered text body (entities flattened)
};

struct MessageDeleted {
    TdChatId                     chat_id{};
    std::vector<TdMessageId>     message_ids;
};

// Read state advanced — peer marked our outbound messages as read up
// to and including last_read_outbox_id (mirrors TDLib's semantics).
struct OutboxReadAdvanced {
    TdChatId    chat_id{};
    TdMessageId last_read{};
};

// ─── Presence / typing ────────────────────────────────────────────────────
struct UserStatusUpdated {
    TdUserId      user_id{};
    model::Presence presence = model::Presence::Offline;
};

struct TypingStarted {
    TdChatId    chat_id{};
    TdUserId    user_id{};
};

// ─── Files / media downloads ──────────────────────────────────────────────
struct FileDownloadProgress {
    TdFileId    file_id{};
    std::int64_t downloaded = 0;
    std::int64_t total      = 0;
};

struct FileReady {
    TdFileId    file_id{};
    std::string local_path;     // empty on failure
};

// ─── Members / info-panel data ────────────────────────────────────────────
struct MembersLoaded {
    TdChatId                 chat_id{};
    std::vector<model::MemberVM> members;
};

struct PeerInfoLoaded {
    TdChatId                 chat_id{};
    model::UserVM            user;       // carries phone/username/bio
};

struct SharedMediaLoaded {
    TdChatId                                chat_id{};
    int                                     tab = 0;     // 0=Media 1=Files 2=Links 3=Voice
    std::vector<model::MediaItemVM>         items;
};

// History batch — emitted from getChatHistory in correct oldest→newest
// order. The receiver replaces m.messages wholesale for the target chat.
struct HistoryLoaded {
    TdChatId                          chat_id{};
    std::vector<model::MessageVM>     messages;
};

// updateMessageInteractionInfo — reactions, view counts. We only carry
// reactions for now.
struct ReactionsUpdated {
    TdChatId                       chat_id{};
    TdMessageId                    message_id{};
    std::vector<model::ReactionVM> reactions;
};

// Avatar download landed — patch the chat row OR the peer/user with the
// freshly-localised path. file_id matches the small profile photo we
// requested at upsert.
struct ChatAvatarReady {
    TdChatId    chat_id{};
    std::string local_path;
};
struct UserAvatarReady {
    TdUserId    user_id{};
    std::string local_path;
};

// ─── The variant ──────────────────────────────────────────────────────────

using Event = std::variant<
    AuthStateChanged,
    AuthError,
    ConnectionStateChanged,
    MeLoaded,
    ChatUpserted,
    ChatRemoved,
    ChatPatched,
    MessageNew,
    MessageEdited,
    MessageDeleted,
    OutboxReadAdvanced,
    UserStatusUpdated,
    TypingStarted,
    FileDownloadProgress,
    FileReady,
    MembersLoaded,
    PeerInfoLoaded,
    SharedMediaLoaded,
    HistoryLoaded,
    ReactionsUpdated,
    ChatAvatarReady,
    UserAvatarReady
>;

}  // namespace tl::td::event
