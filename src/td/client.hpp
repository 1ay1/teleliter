#pragma once

// TDLib runtime — owns the ClientManager + receive thread, translates
// updates into td::event::Event, and exposes a single Cmd that the
// program boots once to wire the inbound stream into the maya update()
// loop.
//
// Threading model:
//
//   ┌── jthread (receive) ───────────────────────────┐
//   │ ClientManager::receive(timeout)                │
//   │   → translate td_api::*  →  td::event::Event   │
//   │   → enqueue into a lock-free vector under mtx  │
//   │   → signal via condition_variable              │
//   └────────────────────────────────────────────────┘
//                          │
//                          ▼
//   ┌── maya isolated task (drain) ──────────────────┐
//   │ wait_for events, drain queue, call dispatch()  │
//   │   → dispatch is maya's `void(Msg)` channel,    │
//   │     which the runtime synchronises into the    │
//   │     UI loop via its wake_fd                     │
//   └────────────────────────────────────────────────┘
//
// Outbound: dispatch_command(td::cmd::Command) is callable from any
// thread; it acquires the send_mutex briefly and forwards to TDLib's
// own thread-safe ClientManager::send.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <maya/maya.hpp>

#include "msg/msg.hpp"
#include "td/command.hpp"
#include "td/event.hpp"
#include "td/ids.hpp"
#include "util/debug_log.hpp"

#ifdef TELELITER_HAS_TDLIB
#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

#include "td/mapping.hpp"
#endif

namespace tl::td {

#ifdef TELELITER_HAS_TDLIB
// Local alias — placed at namespace scope so member functions of
// Client below can name td_api::* without re-aliasing inside the
// class body.
namespace td_api = ::td::td_api;
#endif

namespace detail {

#ifdef TELELITER_HAS_TDLIB

// ─── Hardcoded credentials ──────────────────────────────────
// Pulled from the prior teleliter codebase on origin/dummy. They tie
// this binary to a single registered app; not secrets in the cryptographic
// sense (every published Telegram client ships theirs in plaintext),
// but Telegram does ban duplicated api_ids that misbehave.
inline constexpr std::int32_t kApiId   = 34533272;
inline constexpr const char*  kApiHash = "0bd07411a17b475a31e96d09cd8474f6";

#endif  // TELELITER_HAS_TDLIB

}  // namespace detail

// ─── Public Runtime (singleton) ──────────────────────────────────────────
// Held as a static-local inside `instance()` so the program header stays
// header-only. Constructed on first access (right before the maya task
// fires up); destroyed at program exit, which joins the receive thread.

class Client {
public:
    using EventCallback = std::function<void(event::Event)>;

    // Singleton accessor. Threading: callers may call from any thread;
    // the static-local initialiser is C++11 thread-safe.
    [[nodiscard]] static Client& instance() {
        static Client c;
        return c;
    }

    // Wire the inbound channel. The callback is invoked from a
    // background thread (not the UI thread) — the maya runtime is
    // responsible for marshalling onto the dispatch fd. Pass nullptr
    // to detach (used on shutdown).
    void set_event_callback(EventCallback cb) {
        std::lock_guard lk(callback_mutex_);
        event_callback_ = std::move(cb);
    }

    // Start the receive thread + send the initial getOption that kicks
    // TDLib into emitting its first authorizationState update. Idempotent.
    void start() {
#ifdef TELELITER_HAS_TDLIB
        bool expected = false;
        if (!started_.compare_exchange_strong(expected, true)) return;

        TL_DLOG("td", "Client::start — spinning up ClientManager + receive thread");
        // Quiet TDLib logs to errors-only. fatal=0, error=1, warn=2.
        ::td::ClientManager::execute(
            td_api::make_object<td_api::setLogVerbosityLevel>(1));

        manager_   = std::make_unique<::td::ClientManager>();
        client_id_ = manager_->create_client_id();
        TL_DLOG("td", "client_id=%d", client_id_);

        // Wake TDLib by sending a no-op; the response also gives us
        // a version string for the log.
        send_raw_(td_api::make_object<td_api::getOption>("version"), nullptr);

        worker_ = std::jthread([this](std::stop_token st) { receive_loop_(st); });
#endif
    }

    // Outbound entrypoint. Translates a td::cmd::Command variant into
    // the corresponding TDLib function and dispatches it. Thread-safe.
    void dispatch_command(cmd::Command c) {
#ifdef TELELITER_HAS_TDLIB
        if (!started_) {
            TL_DLOG("td", "dispatch_command dropped — client not started yet");
            return;
        }
        std::visit([this](auto&& v) {
            using T = std::decay_t<decltype(v)>;
            TL_DLOG("td-cmd", "%s", cmd_name_<T>());
            send_command_(std::forward<decltype(v)>(v));
        }, std::move(c));
#else
        (void)c;
#endif
    }

    ~Client() {
#ifdef TELELITER_HAS_TDLIB
        if (started_) {
            // Politely close the TDLib session so it flushes DBs.
            send_raw_(td_api::make_object<td_api::close>(), nullptr);
            worker_.request_stop();
            // worker_ is a jthread — its destructor joins, but we want
            // to be sure we don't outrun the close handshake. Wait up
            // to 2s for receive_loop_ to see authorizationStateClosed.
            if (worker_.joinable()) {
                worker_.join();
            }
        }
#endif
    }

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

private:
    Client() = default;

#ifdef TELELITER_HAS_TDLIB
    // Compile-time name lookup for command alternatives — keeps the
    // dispatch log greppable without RTTI noise.
    template <typename T>
    static constexpr const char* cmd_name_() noexcept {
        if constexpr (std::is_same_v<T, cmd::SubmitPhone>)    return "SubmitPhone";
        else if constexpr (std::is_same_v<T, cmd::SubmitCode>)    return "SubmitCode";
        else if constexpr (std::is_same_v<T, cmd::SubmitPassword>)return "SubmitPassword";
        else if constexpr (std::is_same_v<T, cmd::Logout>)        return "Logout";
        else if constexpr (std::is_same_v<T, cmd::LoadChats>)     return "LoadChats";
        else if constexpr (std::is_same_v<T, cmd::OpenChat>)      return "OpenChat";
        else if constexpr (std::is_same_v<T, cmd::CloseChat>)     return "CloseChat";
        else if constexpr (std::is_same_v<T, cmd::LoadHistory>)   return "LoadHistory";
        else if constexpr (std::is_same_v<T, cmd::SendText>)      return "SendText";
        else if constexpr (std::is_same_v<T, cmd::SendPhoto>)     return "SendPhoto";
        else if constexpr (std::is_same_v<T, cmd::SendDocument>)  return "SendDocument";
        else if constexpr (std::is_same_v<T, cmd::SendVoice>)     return "SendVoice";
        else if constexpr (std::is_same_v<T, cmd::MarkRead>)      return "MarkRead";
        else if constexpr (std::is_same_v<T, cmd::DownloadFile>)  return "DownloadFile";
        else if constexpr (std::is_same_v<T, cmd::LoadPeerInfo>)  return "LoadPeerInfo";
        else if constexpr (std::is_same_v<T, cmd::LoadMembers>)   return "LoadMembers";
        else if constexpr (std::is_same_v<T, cmd::LoadSharedMedia>) return "LoadSharedMedia";
        else                                                       return "?";
    }
#endif

    // Fields accessible regardless of TDLib presence — they're the
    // bridge between the maya runtime's dispatch callback and whatever
    // event source is active (real TDLib in the HAS_TDLIB build, no-op
    // in the stub build).
    std::mutex                              callback_mutex_;
    EventCallback                           event_callback_;

#ifdef TELELITER_HAS_TDLIB

    // ─── Outbound helpers ────────────────────────────────────────────────

    void send_raw_(td_api::object_ptr<td_api::Function> f,
                   std::function<void(td_api::object_ptr<td_api::Object>)> on_reply)
    {
        std::lock_guard lk(send_mutex_);
        const auto qid = ++next_query_id_;
        if (on_reply) {
            std::lock_guard hlk(reply_mutex_);
            reply_handlers_[qid] = std::move(on_reply);
        }
        manager_->send(client_id_, qid, std::move(f));
    }

    void send_command_(cmd::SubmitPhone p) {
        auto req = td_api::make_object<td_api::setAuthenticationPhoneNumber>();
        req->phone_number_ = std::move(p.phone);
        send_raw_(std::move(req), make_auth_error_reporter_("setPhone"));
    }
    void send_command_(cmd::SubmitCode c) {
        auto req = td_api::make_object<td_api::checkAuthenticationCode>();
        req->code_ = std::move(c.code);
        send_raw_(std::move(req), make_auth_error_reporter_("checkCode"));
    }
    void send_command_(cmd::SubmitPassword p) {
        auto req = td_api::make_object<td_api::checkAuthenticationPassword>();
        req->password_ = std::move(p.password);
        send_raw_(std::move(req), make_auth_error_reporter_("checkPassword"));
    }
    void send_command_(cmd::Logout) {
        send_raw_(td_api::make_object<td_api::logOut>(), nullptr);
    }
    void send_command_(cmd::LoadChats lc) {
        auto req = td_api::make_object<td_api::loadChats>();
        req->chat_list_ = td_api::make_object<td_api::chatListMain>();
        req->limit_     = lc.limit;
        send_raw_(std::move(req), nullptr);
    }
    void send_command_(cmd::OpenChat o) {
        auto req = td_api::make_object<td_api::openChat>();
        req->chat_id_ = o.id.get();
        send_raw_(std::move(req), nullptr);
        // Kick a history load — TDLib doesn't auto-send updateNewMessage
        // for old messages on chat open. We reverse the result so the
        // batch is oldest→newest (the view's expected order).
        auto hist = td_api::make_object<td_api::getChatHistory>();
        hist->chat_id_         = o.id.get();
        hist->from_message_id_ = 0;
        hist->offset_          = 0;
        hist->limit_           = 50;
        hist->only_local_      = false;
        send_raw_(std::move(hist),
            [this, chat_id = o.id](td_api::object_ptr<td_api::Object> resp) {
                if (!resp || resp->get_id() != td_api::messages::ID) return;
                auto& ms = static_cast<td_api::messages&>(*resp);
                std::vector<model::MessageVM> out;
                out.reserve(ms.messages_.size());
                // TDLib delivers newest→oldest; reverse iterate.
                for (auto it = ms.messages_.rbegin(); it != ms.messages_.rend(); ++it) {
                    if (*it) out.push_back(build_message_vm_(**it));
                }
                emit_(event::HistoryLoaded{chat_id, std::move(out)});
            });
    }
    void send_command_(cmd::CloseChat c) {
        auto req = td_api::make_object<td_api::closeChat>();
        req->chat_id_ = c.id.get();
        send_raw_(std::move(req), nullptr);
    }
    void send_command_(cmd::LoadHistory h) {
        auto req = td_api::make_object<td_api::getChatHistory>();
        req->chat_id_         = h.chat_id.get();
        req->from_message_id_ = h.from_message.get();
        req->offset_          = 0;
        req->limit_           = h.limit;
        req->only_local_      = false;
        send_raw_(std::move(req),
            [this, chat_id = h.chat_id](td_api::object_ptr<td_api::Object> resp) {
                if (!resp || resp->get_id() != td_api::messages::ID) return;
                auto& ms = static_cast<td_api::messages&>(*resp);
                std::vector<model::MessageVM> out;
                out.reserve(ms.messages_.size());
                for (auto it = ms.messages_.rbegin(); it != ms.messages_.rend(); ++it) {
                    if (*it) out.push_back(build_message_vm_(**it));
                }
                emit_(event::HistoryLoaded{chat_id, std::move(out)});
            });
    }
    void send_command_(cmd::SendText s) {
        auto req = td_api::make_object<td_api::sendMessage>();
        req->chat_id_ = s.chat_id.get();
        if (s.reply_to.get() != 0) {
            auto rt = td_api::make_object<td_api::inputMessageReplyToMessage>();
            rt->message_id_ = s.reply_to.get();
            req->reply_to_  = std::move(rt);
        }
        auto content = td_api::make_object<td_api::inputMessageText>();
        auto ft      = td_api::make_object<td_api::formattedText>();
        ft->text_    = std::move(s.body);
        content->text_ = std::move(ft);
        req->input_message_content_ = std::move(content);
        send_raw_(std::move(req), nullptr);
    }
    void send_command_(cmd::SendPhoto sp) {
        auto req = td_api::make_object<td_api::sendMessage>();
        req->chat_id_ = sp.chat_id.get();
        auto content  = td_api::make_object<td_api::inputMessagePhoto>();
        auto file     = td_api::make_object<td_api::inputFileLocal>();
        file->path_   = std::move(sp.local_path);
        content->photo_ = std::move(file);
        if (!sp.caption.empty()) {
            auto ft = td_api::make_object<td_api::formattedText>();
            ft->text_ = std::move(sp.caption);
            content->caption_ = std::move(ft);
        }
        req->input_message_content_ = std::move(content);
        send_raw_(std::move(req), nullptr);
    }
    void send_command_(cmd::SendDocument sd) {
        auto req = td_api::make_object<td_api::sendMessage>();
        req->chat_id_ = sd.chat_id.get();
        auto content  = td_api::make_object<td_api::inputMessageDocument>();
        auto file     = td_api::make_object<td_api::inputFileLocal>();
        file->path_   = std::move(sd.local_path);
        content->document_ = std::move(file);
        if (!sd.caption.empty()) {
            auto ft = td_api::make_object<td_api::formattedText>();
            ft->text_ = std::move(sd.caption);
            content->caption_ = std::move(ft);
        }
        req->input_message_content_ = std::move(content);
        send_raw_(std::move(req), nullptr);
    }
    void send_command_(cmd::SendVoice sv) {
        auto req = td_api::make_object<td_api::sendMessage>();
        req->chat_id_ = sv.chat_id.get();
        auto content  = td_api::make_object<td_api::inputMessageVoiceNote>();
        auto file     = td_api::make_object<td_api::inputFileLocal>();
        file->path_   = std::move(sv.local_path);
        content->voice_note_ = std::move(file);
        content->duration_   = sv.duration_secs;
        req->input_message_content_ = std::move(content);
        send_raw_(std::move(req), nullptr);
    }
    void send_command_(cmd::MarkRead mr) {
        auto req = td_api::make_object<td_api::viewMessages>();
        req->chat_id_      = mr.chat_id.get();
        req->force_read_   = true;
        for (auto id : mr.message_ids) req->message_ids_.push_back(id.get());
        send_raw_(std::move(req), nullptr);
    }
    void send_command_(cmd::DownloadFile df) {
        auto req = td_api::make_object<td_api::downloadFile>();
        req->file_id_    = static_cast<std::int32_t>(df.id.get());
        req->priority_   = df.priority;
        req->offset_     = 0;
        req->limit_      = 0;
        req->synchronous_ = false;
        send_raw_(std::move(req), nullptr);
    }

    void send_command_(cmd::LoadPeerInfo lpi) {
        auto req = td_api::make_object<td_api::getUserFullInfo>();
        req->user_id_ = lpi.user_id.get();
        send_raw_(std::move(req),
            [this, chat_id = lpi.chat_id, user_id = lpi.user_id]
            (td_api::object_ptr<td_api::Object> resp) {
                if (!resp || resp->get_id() != td_api::userFullInfo::ID) return;
                auto& fi = static_cast<td_api::userFullInfo&>(*resp);
                model::UserVM vm{};
                {
                    std::lock_guard lk(cache_mutex_);
                    auto it = users_.find(user_id.get());
                    if (it != users_.end()) vm = it->second;
                }
                if (fi.bio_) vm.bio = fi.bio_->text_;
                emit_(event::PeerInfoLoaded{chat_id, std::move(vm)});
            });
    }

    void send_command_(cmd::LoadMembers lm) {
        // Fetch up to 200 members. For supergroups we need
        // getSupergroupMembers + getSupergroupFullInfo first to learn
        // the supergroup_id; for basic groups, getBasicGroupFullInfo.
        // We branch on the cached chat type.
        td_api::object_ptr<td_api::ChatType> kind;
        {
            std::lock_guard lk(cache_mutex_);
            auto it = chat_types_.find(lm.chat_id.get());
            if (it == chat_types_.end()) return;
            kind = std::move(it->second);
            it->second = clone_chat_type_(*kind);  // restore for future calls
        }
        if (!kind) return;
        if (kind->get_id() == td_api::chatTypeBasicGroup::ID) {
            auto& g = static_cast<td_api::chatTypeBasicGroup&>(*kind);
            auto req = td_api::make_object<td_api::getBasicGroupFullInfo>();
            req->basic_group_id_ = g.basic_group_id_;
            send_raw_(std::move(req),
                [this, chat_id = lm.chat_id](td_api::object_ptr<td_api::Object> resp) {
                    if (!resp || resp->get_id() != td_api::basicGroupFullInfo::ID) return;
                    auto& fi = static_cast<td_api::basicGroupFullInfo&>(*resp);
                    std::vector<model::MemberVM> members;
                    for (auto& m : fi.members_) {
                        if (!m) continue;
                        members.push_back(member_from_user_id_(
                            m->member_id_ && m->member_id_->get_id() == td_api::messageSenderUser::ID
                                ? static_cast<td_api::messageSenderUser&>(*m->member_id_).user_id_
                                : 0));
                    }
                    emit_(event::MembersLoaded{chat_id, std::move(members)});
                });
        } else if (kind->get_id() == td_api::chatTypeSupergroup::ID) {
            auto& g = static_cast<td_api::chatTypeSupergroup&>(*kind);
            auto req = td_api::make_object<td_api::getSupergroupMembers>();
            req->supergroup_id_ = g.supergroup_id_;
            req->filter_ = td_api::make_object<td_api::supergroupMembersFilterRecent>();
            req->offset_ = 0;
            req->limit_  = 200;
            send_raw_(std::move(req),
                [this, chat_id = lm.chat_id](td_api::object_ptr<td_api::Object> resp) {
                    if (!resp || resp->get_id() != td_api::chatMembers::ID) return;
                    auto& cm = static_cast<td_api::chatMembers&>(*resp);
                    std::vector<model::MemberVM> members;
                    for (auto& m : cm.members_) {
                        if (!m || !m->member_id_) continue;
                        if (m->member_id_->get_id() != td_api::messageSenderUser::ID) continue;
                        members.push_back(member_from_user_id_(
                            static_cast<td_api::messageSenderUser&>(*m->member_id_).user_id_));
                    }
                    emit_(event::MembersLoaded{chat_id, std::move(members)});
                });
        }
    }

    void send_command_(cmd::LoadSharedMedia lsm) {
        // 4 buckets: Media (Photo) / Files (Document) / Links (Url) / Voice.
        auto req = td_api::make_object<td_api::searchChatMessages>();
        req->chat_id_         = lsm.chat_id.get();
        req->query_           = "";
        req->from_message_id_ = 0;
        req->offset_          = 0;
        req->limit_           = 50;
        switch (lsm.tab) {
            case 0: req->filter_ = td_api::make_object<td_api::searchMessagesFilterPhoto>();     break;
            case 1: req->filter_ = td_api::make_object<td_api::searchMessagesFilterDocument>();  break;
            case 2: req->filter_ = td_api::make_object<td_api::searchMessagesFilterUrl>();       break;
            case 3: req->filter_ = td_api::make_object<td_api::searchMessagesFilterVoiceNote>(); break;
            default: return;
        }
        const int tab = lsm.tab;
        send_raw_(std::move(req),
            [this, chat_id = lsm.chat_id, tab]
            (td_api::object_ptr<td_api::Object> resp) {
                if (!resp) return;
                if (resp->get_id() != td_api::foundChatMessages::ID) return;
                auto& fcm = static_cast<td_api::foundChatMessages&>(*resp);
                std::vector<model::MediaItemVM> items;
                items.reserve(fcm.messages_.size());
                for (auto& m : fcm.messages_) {
                    if (!m) continue;
                    auto item = media_item_from_message_(*m, tab);
                    if (!item.label.empty() || !item.kind_glyph.empty()) {
                        items.push_back(std::move(item));
                    }
                }
                emit_(event::SharedMediaLoaded{chat_id, tab, std::move(items)});
            });
    }

    // ─── Error reporting helper ──────────────────────────────────────────
    std::function<void(td_api::object_ptr<td_api::Object>)>
    make_auth_error_reporter_(const char* origin) {
        return [this, origin](td_api::object_ptr<td_api::Object> resp) {
            if (!resp || resp->get_id() != td_api::error::ID) return;
            auto& e = static_cast<td_api::error&>(*resp);
            TL_DLOG("td-auth", "error %s: %s", origin, e.message_.c_str());
            emit_(event::AuthError{
                std::string{origin} + ": " + e.message_
            });
        };
    }

    // ─── Receive loop ────────────────────────────────────────────────────

    void receive_loop_(std::stop_token st) {
        TL_DLOG("td", "receive_loop entered");
        while (!st.stop_requested()) {
            auto response = manager_->receive(0.1);  // 100ms blocking
            if (!response.object) continue;
            process_response_(std::move(response));
        }
        TL_DLOG("td", "receive_loop exiting");
    }

    void process_response_(::td::ClientManager::Response response) {
        if (response.request_id != 0) {
            // Reply to a previous send_raw_.
            std::function<void(td_api::object_ptr<td_api::Object>)> h;
            {
                std::lock_guard lk(reply_mutex_);
                auto it = reply_handlers_.find(response.request_id);
                if (it != reply_handlers_.end()) {
                    h = std::move(it->second);
                    reply_handlers_.erase(it);
                }
            }
            if (h) h(std::move(response.object));
            return;
        }
        // Unsolicited update.
        process_update_(std::move(response.object));
    }

    void process_update_(td_api::object_ptr<td_api::Object> obj) {
        if (!obj) return;
        td_api::downcast_call(*obj, [this](auto& u) { on_update_(u); });
    }

    // ─── Update handlers (one per kind we care about) ────────────────────

    template <typename U>
    void on_update_(U&) { /* unhandled — silently drop */ }

    void on_update_(td_api::updateAuthorizationState& u) {
        if (!u.authorization_state_) return;
        td_api::downcast_call(*u.authorization_state_,
            [this](auto& s) { on_auth_(s); });
    }

    // Each authorisationState* arm sets the stage + (occasionally) replies
    // with the bootstrap setTdlibParameters request the server needs.
    void on_auth_(td_api::authorizationStateWaitTdlibParameters&) {
        TL_DLOG("td-auth", "WaitTdlibParameters — sending setTdlibParameters");
        send_tdlib_parameters_();
        emit_(event::AuthStateChanged{event::AuthStage::Connecting, ""});
    }
    void on_auth_(td_api::authorizationStateWaitPhoneNumber&) {
        TL_DLOG("td-auth", "WaitPhoneNumber");
        emit_(event::AuthStateChanged{event::AuthStage::WaitPhone,
            "enter your phone number with country code"});
    }
    void on_auth_(td_api::authorizationStateWaitCode& s) {
        std::string hint = "code sent";
        if (s.code_info_ && !s.code_info_->phone_number_.empty()) {
            hint = "code sent to " + s.code_info_->phone_number_;
        }
        TL_DLOG("td-auth", "WaitCode (%s)", hint.c_str());
        emit_(event::AuthStateChanged{event::AuthStage::WaitCode, std::move(hint)});
    }
    void on_auth_(td_api::authorizationStateWaitPassword& s) {
        std::string hint = "two-step verification";
        if (!s.password_hint_.empty()) hint += " (hint: " + s.password_hint_ + ")";
        TL_DLOG("td-auth", "WaitPassword");
        emit_(event::AuthStateChanged{event::AuthStage::WaitPassword, std::move(hint)});
    }
    void on_auth_(td_api::authorizationStateReady&) {
        TL_DLOG("td-auth", "Ready — logged in");
        emit_(event::AuthStateChanged{event::AuthStage::LoggedIn, ""});
        // Cache the self user id for from_me detection.
        send_raw_(td_api::make_object<td_api::getMe>(),
            [this](td_api::object_ptr<td_api::Object> resp) {
                if (!resp || resp->get_id() != td_api::user::ID) return;
                auto& u = static_cast<td_api::user&>(*resp);
                self_user_id_ = u.id_;
                auto vm = map::to_user(u);
                vm.is_self = true;
                // Cache into the user table so subsequent message ingestion
                // can resolve author info synchronously.
                {
                    std::lock_guard lk(cache_mutex_);
                    users_[u.id_] = vm;
                }
                emit_(event::MeLoaded{std::move(vm)});
            });
        // Kick the initial chat list load.
        send_command_(cmd::LoadChats{30});
    }
    void on_auth_(td_api::authorizationStateLoggingOut&) {}
    void on_auth_(td_api::authorizationStateClosing&)   {}
    void on_auth_(td_api::authorizationStateClosed&) {
        emit_(event::AuthStateChanged{event::AuthStage::LoggedOut, ""});
    }
    // Newer TDLib introduced extra wait states (email, registration,
    // premium purchase, device confirmation). We don't surface UI for
    // any of them yet — a stub keeps the visitor exhaustive without
    // pretending we handle the flow. The user sees "please wait" until
    // they switch to a supported login method.
    void on_auth_(td_api::authorizationStateWaitEmailAddress&) {
        emit_(event::AuthStateChanged{event::AuthStage::Connecting,
            "email login required — use the mobile app to switch to phone+code"});
    }
    void on_auth_(td_api::authorizationStateWaitEmailCode&) {
        emit_(event::AuthStateChanged{event::AuthStage::Connecting,
            "awaiting email code"});
    }
    void on_auth_(td_api::authorizationStateWaitOtherDeviceConfirmation&) {
        emit_(event::AuthStateChanged{event::AuthStage::Connecting,
            "confirm sign-in on another device"});
    }
    void on_auth_(td_api::authorizationStateWaitRegistration&) {
        emit_(event::AuthStateChanged{event::AuthStage::Connecting,
            "this number isn't registered — sign up via the mobile app first"});
    }
    void on_auth_(td_api::authorizationStateWaitPremiumPurchase&) {
        emit_(event::AuthStateChanged{event::AuthStage::Connecting,
            "premium purchase required"});
    }

    void send_tdlib_parameters_() {
        auto req = td_api::make_object<td_api::setTdlibParameters>();
        req->use_test_dc_              = false;
        // Persist DBs + downloaded media under $HOME/.teleliter — mirrors
        // the prior implementation so existing logins survive the rewrite.
        const char* home = std::getenv("HOME");
        std::string base = (home && *home) ? std::string{home} + "/.teleliter"
                                           : std::string{"/tmp/teleliter"};
        req->database_directory_       = base;
        req->files_directory_          = base + "/files";
        req->database_encryption_key_  = "";
        req->use_file_database_        = true;
        req->use_chat_info_database_   = true;
        req->use_message_database_     = true;
        req->use_secret_chats_         = false;
        req->api_id_                   = detail::kApiId;
        req->api_hash_                 = detail::kApiHash;
        req->system_language_code_     = "en";
        req->device_model_             = "teleliter";
        req->system_version_           = "linux";
        req->application_version_      = "0.2.0";
        send_raw_(std::move(req),
            [this](td_api::object_ptr<td_api::Object> resp) {
                if (resp && resp->get_id() == td_api::error::ID) {
                    auto& e = static_cast<td_api::error&>(*resp);
                    emit_(event::AuthError{
                        "setTdlibParameters: " + e.message_});
                }
            });
    }

    void on_update_(td_api::updateConnectionState& u) {
        using S = event::ConnectionStage;
        S s = S::Connecting;
        if (u.state_) {
            switch (u.state_->get_id()) {
                case td_api::connectionStateWaitingForNetwork::ID: s = S::WaitingForNetwork; break;
                case td_api::connectionStateConnectingToProxy::ID: s = S::ConnectingToProxy; break;
                case td_api::connectionStateConnecting::ID:        s = S::Connecting;        break;
                case td_api::connectionStateUpdating::ID:          s = S::Updating;          break;
                case td_api::connectionStateReady::ID:             s = S::Ready;             break;
            }
        }
        emit_(event::ConnectionStateChanged{s});
    }

    void on_update_(td_api::updateUser& u) {
        if (!u.user_) return;
        auto vm = map::to_user(*u.user_);
        if (u.user_->id_ == self_user_id_) vm.is_self = true;
        {
            std::lock_guard lk(cache_mutex_);
            users_[u.user_->id_] = vm;
        }
        // Fire off the small profile-photo download so peer bubbles +
        // chat-list rows + the dm info hero can show an actual picture
        // once it lands.
        maybe_download_user_photo_(*u.user_);
    }

    void on_update_(td_api::updateUserStatus& u) {
        std::lock_guard lk(cache_mutex_);
        auto it = users_.find(u.user_id_);
        if (it != users_.end() && u.status_) {
            it->second.presence = map::to_presence(*u.status_);
        }
        if (u.status_) {
            emit_(event::UserStatusUpdated{
                TdUserId{u.user_id_}, map::to_presence(*u.status_)});
        }
    }

    void on_update_(td_api::updateNewChat& u) {
        if (!u.chat_) return;
        cache_chat_(*u.chat_);
        auto row = map::to_chat_row(*u.chat_, "", 0);
        // Carry partner_presence from the user cache for DMs.
        if (row.kind == model::ChatKind::Direct && row.peer_user_id != 0) {
            std::lock_guard lk(cache_mutex_);
            auto it = users_.find(row.peer_user_id);
            if (it != users_.end()) {
                row.partner_presence = it->second.presence;
                row.avatar_path      = it->second.avatar_path;
            }
        }
        // Initial preview from the last_message_ field if present — we
        // don't get an updateChatLastMessage event for chats that
        // already had one when we loaded the list.
        if (u.chat_->last_message_) {
            row.last_message_preview = preview_for_(*u.chat_->last_message_);
            row.last_message_time    = map::hhmm_local(u.chat_->last_message_->date_);
        }
        emit_(event::ChatUpserted{std::move(row)});
        maybe_download_chat_photo_(*u.chat_);
    }

    void on_update_(td_api::updateChatPhoto& u) {
        if (!u.photo_ || !u.photo_->small_) return;
        const auto fid = u.photo_->small_->id_;
        {
            std::lock_guard lk(cache_mutex_);
            chat_photo_files_[fid] = u.chat_id_;
        }
        if (u.photo_->small_->local_
         && u.photo_->small_->local_->is_downloading_completed_) {
            emit_(event::ChatAvatarReady{
                TdChatId{u.chat_id_},
                u.photo_->small_->local_->path_});
        } else {
            send_command_(cmd::DownloadFile{TdFileId{fid}, 16});
        }
    }

    // Chat moved in / out of the main list, or its order changed.
    void on_update_(td_api::updateChatPosition& u) {
        if (!u.position_ || !u.position_->list_) return;
        if (u.position_->list_->get_id() != td_api::chatListMain::ID) return;
        model::ChatListItemVM patch{};
        patch.id     = model::ChatId{u.chat_id_};
        patch.order  = u.position_->order_;
        patch.pinned = u.position_->is_pinned_;
        emit_(event::ChatPatched{TdChatId{u.chat_id_}, std::move(patch)});
    }

    void on_update_(td_api::updateChatLastMessage& u) {
        std::string preview;
        std::int32_t when = 0;
        if (u.last_message_) {
            when = u.last_message_->date_;
            preview = preview_for_(*u.last_message_);
        }
        model::ChatListItemVM patch{};
        patch.id = model::ChatId{u.chat_id_};
        patch.last_message_preview = std::move(preview);
        patch.last_message_time    = map::hhmm_local(when);
        emit_(event::ChatPatched{TdChatId{u.chat_id_}, std::move(patch)});
    }

    void on_update_(td_api::updateChatReadInbox& u) {
        model::ChatListItemVM patch{};
        patch.id = model::ChatId{u.chat_id_};
        patch.unread_count = static_cast<std::size_t>(std::max(0, u.unread_count_));
        emit_(event::ChatPatched{TdChatId{u.chat_id_}, std::move(patch)});
    }

    void on_update_(td_api::updateChatReadOutbox& u) {
        emit_(event::OutboxReadAdvanced{
            TdChatId{u.chat_id_}, TdMessageId{u.last_read_outbox_message_id_}});
    }

    void on_update_(td_api::updateChatTitle& u) {
        model::ChatListItemVM patch{};
        patch.id    = model::ChatId{u.chat_id_};
        patch.title = u.title_;
        emit_(event::ChatPatched{TdChatId{u.chat_id_}, std::move(patch)});
    }

    void on_update_(td_api::updateNewMessage& u) {
        if (!u.message_) return;
        ingest_message_(TdChatId{u.message_->chat_id_}, *u.message_);
    }

    void on_update_(td_api::updateMessageContent& u) {
        // Re-render the body as a patch — caller decides whether to
        // merge in place or rebuild. We collapse all media edits to a
        // text-update event for now; the message_bubble re-render path
        // is what we care about, not the media kind.
        std::string body;
        if (u.new_content_ && u.new_content_->get_id() == td_api::messageText::ID) {
            auto& mt = static_cast<td_api::messageText&>(*u.new_content_);
            if (mt.text_) body = mt.text_->text_;
        }
        emit_(event::MessageEdited{
            TdChatId{u.chat_id_}, TdMessageId{u.message_id_}, std::move(body)});
    }

    void on_update_(td_api::updateMessageInteractionInfo& u) {
        std::vector<model::ReactionVM> reactions;
        if (u.interaction_info_ && u.interaction_info_->reactions_) {
            for (auto& r : u.interaction_info_->reactions_->reactions_) {
                if (!r || !r->type_) continue;
                model::ReactionVM rv{};
                rv.count        = r->total_count_;
                rv.self_reacted = r->is_chosen_;
                if (r->type_->get_id() == td_api::reactionTypeEmoji::ID) {
                    rv.emoji = static_cast<td_api::reactionTypeEmoji&>(*r->type_).emoji_;
                } else {
                    rv.emoji = "\xF0\x9F\x91\x8D";   // 👍 placeholder
                }
                reactions.push_back(std::move(rv));
            }
        }
        emit_(event::ReactionsUpdated{
            TdChatId{u.chat_id_}, TdMessageId{u.message_id_},
            std::move(reactions)});
    }

    void on_update_(td_api::updateDeleteMessages& u) {
        if (!u.is_permanent_) return;  // tdlib also fires for cache evictions
        std::vector<TdMessageId> ids;
        ids.reserve(u.message_ids_.size());
        for (auto id : u.message_ids_) ids.push_back(TdMessageId{id});
        emit_(event::MessageDeleted{TdChatId{u.chat_id_}, std::move(ids)});
    }

    void on_update_(td_api::updateFile& u) {
        if (!u.file_) return;
        const auto fid = TdFileId{u.file_->id_};
        if (u.file_->local_ && u.file_->local_->is_downloading_completed_) {
            const auto& path = u.file_->local_->path_;
            // Route to chat-avatar / user-avatar / generic file based
            // on the registered cache. We pop the entry once consumed
            // so subsequent updateFile noise for the same id is silent.
            std::int64_t chat_id = 0;
            std::int64_t user_id = 0;
            {
                std::lock_guard lk(cache_mutex_);
                if (auto it = chat_photo_files_.find(u.file_->id_);
                    it != chat_photo_files_.end()) {
                    chat_id = it->second;
                    chat_photo_files_.erase(it);
                }
                if (auto it = user_photo_files_.find(u.file_->id_);
                    it != user_photo_files_.end()) {
                    user_id = it->second;
                    user_photo_files_.erase(it);
                    auto uit = users_.find(user_id);
                    if (uit != users_.end()) uit->second.avatar_path = path;
                }
            }
            if (chat_id != 0) {
                emit_(event::ChatAvatarReady{TdChatId{chat_id}, path});
            }
            if (user_id != 0) {
                emit_(event::UserAvatarReady{TdUserId{user_id}, path});
            }
            // Always surface the generic FileReady so message-bubble
            // attached photos / docs can swap their preview path in.
            emit_(event::FileReady{fid, path});
        } else if (u.file_->local_ && u.file_->local_->is_downloading_active_) {
            emit_(event::FileDownloadProgress{
                fid,
                u.file_->local_->downloaded_size_,
                u.file_->size_});
        }
    }

    void on_update_(td_api::updateChatAction& u) {
        if (!u.action_) return;
        if (u.action_->get_id() == td_api::chatActionTyping::ID) {
            if (u.sender_id_ && u.sender_id_->get_id() == td_api::messageSenderUser::ID) {
                auto& s = static_cast<td_api::messageSenderUser&>(*u.sender_id_);
                emit_(event::TypingStarted{
                    TdChatId{u.chat_id_}, TdUserId{s.user_id_}});
            }
        }
    }

    // ─── Caches + helpers ───────────────────────────────────────────────

    void cache_chat_(const td_api::chat& c) {
        std::lock_guard lk(cache_mutex_);
        chats_[c.id_] = c.title_;
        if (c.type_) chat_types_[c.id_] = clone_chat_type_(*c.type_);
    }

    [[nodiscard]] static td_api::object_ptr<td_api::ChatType>
    clone_chat_type_(const td_api::ChatType& src) {
        switch (src.get_id()) {
            case td_api::chatTypePrivate::ID: {
                auto& s = static_cast<const td_api::chatTypePrivate&>(src);
                return td_api::make_object<td_api::chatTypePrivate>(s.user_id_);
            }
            case td_api::chatTypeSecret::ID: {
                auto& s = static_cast<const td_api::chatTypeSecret&>(src);
                return td_api::make_object<td_api::chatTypeSecret>(
                    s.secret_chat_id_, s.user_id_);
            }
            case td_api::chatTypeBasicGroup::ID: {
                auto& s = static_cast<const td_api::chatTypeBasicGroup&>(src);
                return td_api::make_object<td_api::chatTypeBasicGroup>(s.basic_group_id_);
            }
            case td_api::chatTypeSupergroup::ID: {
                auto& s = static_cast<const td_api::chatTypeSupergroup&>(src);
                return td_api::make_object<td_api::chatTypeSupergroup>(
                    s.supergroup_id_, s.is_channel_);
            }
            default: return nullptr;
        }
    }

    [[nodiscard]] static std::int32_t now_unix_() noexcept {
        return static_cast<std::int32_t>(std::time(nullptr));
    }

    // Build a MessageVM with author resolution from the user cache.
    [[nodiscard]] model::MessageVM build_message_vm_(const td_api::message& m) {
        std::int64_t author = 0;
        if (m.sender_id_ && m.sender_id_->get_id() == td_api::messageSenderUser::ID) {
            author = static_cast<const td_api::messageSenderUser&>(*m.sender_id_).user_id_;
        }
        std::string name, initials, avatar;
        bool from_me = (author == self_user_id_ && self_user_id_ != 0);
        {
            std::lock_guard lk(cache_mutex_);
            auto it = users_.find(author);
            if (it != users_.end()) {
                name     = it->second.name;
                initials = it->second.initials;
                avatar   = it->second.avatar_path;
            } else if (author != 0) {
                name     = "User " + std::to_string(author);
                initials = "?";
            }
        }
        return map::to_message(m, name, initials, avatar, from_me, now_unix_());
    }

    // Resolve a user id to a MemberVM out of the cache. Falls back to
    // a stub name when the user hasn't been seen yet (the runtime will
    // request the user via the cache miss path on next render — for
    // now we just label it).
    [[nodiscard]] model::MemberVM member_from_user_id_(std::int64_t uid) {
        model::MemberVM mem{};
        std::lock_guard lk(cache_mutex_);
        auto it = users_.find(uid);
        if (it != users_.end()) {
            mem.user = it->second;
        } else if (uid != 0) {
            mem.user.id   = model::UserId{uid};
            mem.user.name = "User " + std::to_string(uid);
            mem.user.initials = "?";
        }
        return mem;
    }

    // Translate a TDLib message into a MediaItemVM for the shared-media
    // tabs. Picks the kind glyph the dm_info_panel filters on. Empty
    // labels mean "skip this message".
    [[nodiscard]] model::MediaItemVM
    media_item_from_message_(const td_api::message& m, int tab) {
        model::MediaItemVM out{};
        if (!m.content_) return out;
        switch (m.content_->get_id()) {
            case td_api::messagePhoto::ID: {
                auto& mp = static_cast<const td_api::messagePhoto&>(*m.content_);
                out.kind_glyph = "\xF0\x9F\x93\xB7";   // 📷
                out.label      = "photo";
                std::size_t sz = 0;
                if (mp.photo_ && !mp.photo_->sizes_.empty()) {
                    auto& biggest = mp.photo_->sizes_.back();
                    if (biggest->photo_)
                        sz = static_cast<std::size_t>(
                            std::max<std::int64_t>(0, biggest->photo_->size_));
                    if (biggest->photo_ && biggest->photo_->local_)
                        out.href = biggest->photo_->local_->path_;
                }
                out.subtitle  = human_size_(sz);
                out.open_hint = "xdg-open \xE2\x86\x97";
                break;
            }
            case td_api::messageDocument::ID: {
                auto& md = static_cast<const td_api::messageDocument&>(*m.content_);
                out.kind_glyph = "\xF0\x9F\x93\x84";   // 📄
                if (md.document_) {
                    out.label = md.document_->file_name_;
                    std::size_t sz = 0;
                    if (md.document_->document_) {
                        sz = static_cast<std::size_t>(
                            std::max<std::int64_t>(0, md.document_->document_->size_));
                        if (md.document_->document_->local_)
                            out.href = md.document_->document_->local_->path_;
                    }
                    out.subtitle = human_size_(sz);
                }
                out.open_hint = "xdg-open \xE2\x86\x97";
                break;
            }
            case td_api::messageText::ID: {
                if (tab != 2) break;   // Links only
                auto& mt = static_cast<const td_api::messageText&>(*m.content_);
                if (mt.link_preview_) {
                    out.kind_glyph = "\xF0\x9F\x94\x97";   // 🔗
                    out.label      = mt.link_preview_->site_name_.empty()
                        ? mt.link_preview_->url_
                        : mt.link_preview_->site_name_;
                    out.subtitle   = mt.link_preview_->url_;
                    out.href       = mt.link_preview_->url_;
                    out.open_hint  = "in browser \xE2\x86\x97";
                }
                break;
            }
            case td_api::messageVoiceNote::ID: {
                auto& mv = static_cast<const td_api::messageVoiceNote&>(*m.content_);
                out.kind_glyph = "\xF0\x9F\x8E\xB5";   // 🎵
                if (mv.voice_note_) {
                    out.label = "voice " + format_duration_(mv.voice_note_->duration_);
                    if (mv.voice_note_->voice_ && mv.voice_note_->voice_->local_)
                        out.href = mv.voice_note_->voice_->local_->path_;
                }
                out.subtitle  = "voice note";
                out.open_hint = "mpv \xE2\x86\x97";
                break;
            }
            default: break;
        }
        return out;
    }

    [[nodiscard]] static std::string human_size_(std::size_t bytes) {
        if (bytes < 1024) return std::to_string(bytes) + " B";
        if (bytes < 1024 * 1024)
            return std::to_string(bytes / 1024) + " KB";
        return std::to_string(bytes / (1024 * 1024)) + " MB";
    }

    [[nodiscard]] static std::string format_duration_(int secs) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d:%02d", secs / 60, secs % 60);
        return buf;
    }

    // Translate a chat.photo file id into an avatar download. The file
    // id we receive on chat creation is small thumb; we register it in
    // `chat_photo_files_` so updateFile can match it back to the chat.
    void maybe_download_chat_photo_(const td_api::chat& c) {
        if (!c.photo_ || !c.photo_->small_) return;
        const auto fid = c.photo_->small_->id_;
        {
            std::lock_guard lk(cache_mutex_);
            chat_photo_files_[fid] = c.id_;
        }
        // If the file is already local, emit the ready event
        // immediately. Otherwise issue a download request.
        if (c.photo_->small_->local_
         && c.photo_->small_->local_->is_downloading_completed_) {
            emit_(event::ChatAvatarReady{
                TdChatId{c.id_},
                c.photo_->small_->local_->path_});
        } else {
            send_command_(cmd::DownloadFile{TdFileId{fid}, 16});
        }
    }

    // Same idea for users (peer DMs + group members get an avatar).
    void maybe_download_user_photo_(const td_api::user& u) {
        if (!u.profile_photo_ || !u.profile_photo_->small_) return;
        const auto fid = u.profile_photo_->small_->id_;
        {
            std::lock_guard lk(cache_mutex_);
            user_photo_files_[fid] = u.id_;
        }
        if (u.profile_photo_->small_->local_
         && u.profile_photo_->small_->local_->is_downloading_completed_) {
            emit_(event::UserAvatarReady{
                TdUserId{u.id_},
                u.profile_photo_->small_->local_->path_});
            std::lock_guard lk(cache_mutex_);
            auto it = users_.find(u.id_);
            if (it != users_.end())
                it->second.avatar_path = u.profile_photo_->small_->local_->path_;
        } else {
            send_command_(cmd::DownloadFile{TdFileId{fid}, 8});
        }
    }

    // Resolve a message's author info from the user cache + decide from_me.
    void ingest_message_(TdChatId chat, const td_api::message& m) {
        emit_(event::MessageNew{chat, build_message_vm_(m)});
    }

    // Best-effort preview text for last_message in the chat list.
    [[nodiscard]] std::string preview_for_(const td_api::message& m) const {
        if (!m.content_) return "";
        switch (m.content_->get_id()) {
            case td_api::messageText::ID: {
                auto& mt = static_cast<const td_api::messageText&>(*m.content_);
                return mt.text_ ? mt.text_->text_ : std::string{};
            }
            case td_api::messagePhoto::ID:     return "📷 Photo";
            case td_api::messageVideo::ID:     return "🎬 Video";
            case td_api::messageVoiceNote::ID: return "🎙 Voice";
            case td_api::messageVideoNote::ID: return "🎬 Video note";
            case td_api::messageSticker::ID:   return "Sticker";
            case td_api::messageDocument::ID:  return "📎 Document";
            case td_api::messageAudio::ID:     return "🎵 Audio";
            case td_api::messageAnimation::ID: return "GIF";
            case td_api::messageLocation::ID:  return "📍 Location";
            case td_api::messageContact::ID:   return "👤 Contact";
            case td_api::messagePoll::ID:      return "📊 Poll";
            default: return "";
        }
    }

    // ─── Outbound queue ──────────────────────────────────────────────────

    void emit_(event::Event e) {
        TL_DLOG("td-ev", "emit variant=%zu", e.index());
        EventCallback cb;
        {
            std::lock_guard lk(callback_mutex_);
            cb = event_callback_;
        }
        if (cb) cb(std::move(e));
        else TL_DLOG("td-ev", "event dropped — no callback registered");
    }

    // ─── State ───────────────────────────────────────────────────────────

    std::atomic<bool>                       started_{false};
    std::unique_ptr<::td::ClientManager>    manager_;
    std::int32_t                            client_id_ = 0;
    std::jthread                            worker_;

    std::mutex                              send_mutex_;
    std::uint64_t                           next_query_id_ = 0;
    std::mutex                              reply_mutex_;
    std::unordered_map<std::uint64_t,
        std::function<void(td_api::object_ptr<td_api::Object>)>> reply_handlers_;

    std::mutex                              cache_mutex_;
    std::unordered_map<std::int64_t, model::UserVM> users_;
    std::unordered_map<std::int64_t, std::string>   chats_;
    std::unordered_map<std::int64_t,
        td_api::object_ptr<td_api::ChatType>>   chat_types_;
    // file_id → chat_id / user_id mapping for in-flight avatar
    // downloads. updateFile completion looks these up so we know which
    // entity to patch.
    std::unordered_map<std::int32_t, std::int64_t> chat_photo_files_;
    std::unordered_map<std::int32_t, std::int64_t> user_photo_files_;
    std::int64_t                            self_user_id_ = 0;

#endif  // TELELITER_HAS_TDLIB
};

// ─── Public Sub factory ──────────────────────────────────────────────────
// Returns a Cmd that, when interpreted by the maya runtime, spins up the
// TDLib client and bridges its events into the program's msg::Msg stream.
// The task is `task_isolated` because it parks indefinitely on a
// condition-variable wait; it must not consume one of the shared bg
// pool's slots.

[[nodiscard]] inline maya::Cmd<msg::Msg> boot_command() {
#ifdef TELELITER_HAS_TDLIB
    TL_DLOG("td", "boot_command — wiring isolated task");
    return maya::Cmd<msg::Msg>::task_isolated(
        [](std::function<void(msg::Msg)> dispatch) {
            TL_DLOG("td", "boot task running — registering event callback");
            auto& c = Client::instance();
            // Bridge: every td::event::Event becomes one msg::TdEvent
            // delivered via the runtime's wake-fd-backed dispatch fn.
            c.set_event_callback([dispatch](event::Event e) {
                dispatch(msg::TdEvent{std::move(e)});
            });
            c.start();
            // Park forever — the task closure must outlive the program
            // so the callback survives. We sleep on a condition variable
            // and only return when start() shuts down at process exit.
            std::mutex mx;
            std::condition_variable cv;
            std::unique_lock lk(mx);
            cv.wait(lk, [] { return false; });
        });
#else
    TL_DLOG("td", "boot_command — TDLib disabled, returning Cmd::none");
    return maya::Cmd<msg::Msg>::none();
#endif
}

// Forward an outbound command from update() into the runtime. Wrapped
// in a Cmd::task so the program loop's update() stays pure — side
// effects flow out, not in.
[[nodiscard]] inline maya::Cmd<msg::Msg> dispatch(cmd::Command c) {
#ifdef TELELITER_HAS_TDLIB
    return maya::Cmd<msg::Msg>::task(
        [c = std::move(c)](std::function<void(msg::Msg)>) mutable {
            Client::instance().dispatch_command(std::move(c));
        });
#else
    (void)c;
    return maya::Cmd<msg::Msg>::none();
#endif
}

}  // namespace tl::td
