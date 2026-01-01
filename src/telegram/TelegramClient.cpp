#include "TelegramClient.h"
#include "../ui/MainFrame.h"
#include "../ui/MediaTypes.h"
#include "../ui/WelcomeChat.h"

#include <algorithm>
#include <ctime>
#include <iostream>
#include <set>
#include <sstream>
#include <wx/filename.h>

// Helper to check if a file is actually available locally
// TDLib may report is_downloading_completed_ = true but the file might have
// been deleted
static bool IsFileAvailableLocally(const td_api::file *file) {
  if (!file || !file->local_)
    return false;

  const std::string &path = file->local_->path_;
  if (path.empty())
    return false;

  // File is available if:
  // 1. Download completed (we received the file), OR
  // 2. Upload is active/completed (we're sending a local file - check remote_)
  // In both cases, verify the file actually exists on disk
  bool isDownloaded = file->local_->is_downloading_completed_;
  bool isUploading = file->remote_ && file->remote_->is_uploading_active_;
  bool isUploaded = file->remote_ && file->remote_->is_uploading_completed_;

  if (!isDownloaded && !isUploading && !isUploaded) {
    return false;
  }

  // Actually check if the file exists on disk
  return wxFileName::FileExists(wxString::FromUTF8(path));
}

// Helper to check if we should trigger a download
static bool ShouldDownloadFile(const td_api::file *file) {
  if (!file || !file->local_)
    return true; // No file info, need download
  if (file->local_->is_downloading_active_)
    return false; // Already downloading
  if (!file->local_->is_downloading_completed_)
    return true; // Not complete

  // File marked as complete - verify it actually exists
  const std::string &path = file->local_->path_;
  if (path.empty())
    return true;

  return !wxFileName::FileExists(wxString::FromUTF8(path));
}

// Debug logging - disabled by default for release
#define TDLOG(...)                                                             \
  do {                                                                         \
  } while (0)
// Enable debug logging for download diagnostics (uncomment below):
// #define TDLOG(...) do { fprintf(stderr, "[TDLib] "); fprintf(stderr,
// __VA_ARGS__); fprintf(stderr, "\n"); } while(0)

wxDEFINE_EVENT(wxEVT_TDLIB_UPDATE, wxThreadEvent);

// Static pointer to the singleton instance for event handling
static TelegramClient *s_instance = nullptr;

TelegramClient::TelegramClient()
    : m_clientManager(nullptr), m_clientId(0), m_running(false),
      m_authState(AuthState::WaitTdlibParameters), m_currentQueryId(0),
      m_mainFrame(nullptr), m_welcomeChat(nullptr),
      m_downloadTimeoutTimer(this) {
  s_instance = this;
  // Bind to wxTheApp for proper main thread event handling
  if (wxTheApp) {
    wxTheApp->Bind(wxEVT_TDLIB_UPDATE, &TelegramClient::OnTdlibUpdate, this);
  }

  // Bind download timeout timer
  Bind(wxEVT_TIMER, &TelegramClient::OnDownloadTimeoutTimer, this,
       m_downloadTimeoutTimer.GetId());

  // Start download timeout checker (every 10 seconds)
  m_downloadTimeoutTimer.Start(10000);
}

TelegramClient::~TelegramClient() {
  // Stop download timeout timer
  m_downloadTimeoutTimer.Stop();
  Unbind(wxEVT_TIMER, &TelegramClient::OnDownloadTimeoutTimer, this,
         m_downloadTimeoutTimer.GetId());

  Stop();
  if (wxTheApp) {
    wxTheApp->Unbind(wxEVT_TDLIB_UPDATE, &TelegramClient::OnTdlibUpdate, this);
  }
  s_instance = nullptr;
}

void TelegramClient::Start() {
  if (m_running) {
    TDLOG("Already running, skipping Start()");
    return;
  }

  TDLOG("Starting TelegramClient...");

  // Set TDLib log verbosity (0 = fatal only, 1 = errors, 2 = warnings)
  td::ClientManager::execute(
      td_api::make_object<td_api::setLogVerbosityLevel>(0));

  // Create client manager and client
  m_clientManager = std::make_unique<td::ClientManager>();
  m_clientId = m_clientManager->create_client_id();

  // Set running flag BEFORE first Send() call
  m_running = true;

  // Send initial request to start the client
  Send(td_api::make_object<td_api::getOption>("version"),
       [](td_api::object_ptr<td_api::Object> result) {
         if (result->get_id() == td_api::optionValueString::ID) {
           auto ver = td_api::move_object_as<td_api::optionValueString>(result);
           TDLOG("TDLib version: %s", ver->value_.c_str());
         }
       });
  TDLOG("Client started, launching receive thread");

  // Start receive thread
  m_receiveThread = std::thread(&TelegramClient::ReceiveLoop, this);
}

void TelegramClient::Stop() {
  if (!m_running) {
    return;
  }

  m_running = false;

  // Send close request
  Send(td_api::make_object<td_api::close>(), nullptr);

  // Wait for receive thread
  if (m_receiveThread.joinable()) {
    m_receiveThread.join();
  }

  m_clientManager.reset();
}

void TelegramClient::ReceiveLoop() {
  TDLOG("Receive loop started");
  while (m_running) {
    if (!m_clientManager) {
      TDLOG("Client manager is null, exiting receive loop");
      break;
    }
    auto response =
        m_clientManager->receive(0.1); // 100ms timeout for faster updates
    if (response.object) {
      TDLOG("Received response, id=%d", response.object->get_id());
      ProcessResponse(std::move(response));
    }
  }
  TDLOG("Receive loop ended");
}

void TelegramClient::Send(
    td_api::object_ptr<td_api::Function> f,
    std::function<void(td_api::object_ptr<td_api::Object>)> handler) {
  if (!m_clientManager || !m_running) {
    TDLOG("Cannot send: client manager not ready or not running");
    return;
  }

  auto queryId = ++m_currentQueryId;

  if (handler) {
    std::lock_guard<std::mutex> lock(m_handlersMutex);
    m_handlers[queryId] = std::move(handler);
  }

  m_clientManager->send(m_clientId, queryId, std::move(f));
}

void TelegramClient::ProcessResponse(td::ClientManager::Response response) {
  if (!response.object) {
    return;
  }

  if (response.request_id != 0) {
    // This is a response to a request
    std::function<void(td_api::object_ptr<td_api::Object>)> handler;
    {
      std::lock_guard<std::mutex> lock(m_handlersMutex);
      auto it = m_handlers.find(response.request_id);
      if (it != m_handlers.end()) {
        handler = std::move(it->second);
        m_handlers.erase(it);
      }
    }

    if (handler) {
      handler(std::move(response.object));
    }
  } else {
    // This is an update
    ProcessUpdate(std::move(response.object));
  }
}

void TelegramClient::ProcessUpdate(td_api::object_ptr<td_api::Object> update) {
  td_api::downcast_call(*update, [this](auto &u) {
    using T = std::decay_t<decltype(u)>;

    if constexpr (std::is_same_v<T, td_api::updateAuthorizationState>) {
      OnAuthStateUpdate(u.authorization_state_);
    } else if constexpr (std::is_same_v<T, td_api::updateNewMessage>) {
      OnNewMessage(u.message_);
    } else if constexpr (std::is_same_v<T, td_api::updateMessageContent>) {
      OnMessageEdited(u.chat_id_, u.message_id_, u.new_content_);
    } else if constexpr (std::is_same_v<T, td_api::updateNewChat>) {
      OnChatUpdate(u.chat_);
    } else if constexpr (std::is_same_v<T, td_api::updateChatTitle>) {
      {
        std::unique_lock<std::shared_mutex> lock(m_dataMutex);
        if (auto it = m_chats.find(u.chat_id_); it != m_chats.end()) {
          it->second.title = wxString::FromUTF8(u.title_);
        }
      }
      SetDirty(DirtyFlag::ChatList);
    } else if constexpr (std::is_same_v<T, td_api::updateChatLastMessage>) {
      OnChatLastMessage(u.chat_id_, u.last_message_);
    } else if constexpr (std::is_same_v<T, td_api::updateChatReadInbox>) {
      OnChatReadInbox(u.chat_id_, u.last_read_inbox_message_id_,
                      u.unread_count_);
    } else if constexpr (std::is_same_v<T, td_api::updateChatReadOutbox>) {
      OnChatReadOutbox(u.chat_id_, u.last_read_outbox_message_id_);
    } else if constexpr (std::is_same_v<T, td_api::updateChatPosition>) {
      OnChatPosition(u.chat_id_, u.position_);
    } else if constexpr (std::is_same_v<T, td_api::updateUser>) {
      OnUserUpdate(u.user_);
    } else if constexpr (std::is_same_v<T, td_api::updateUserStatus>) {
      OnUserStatusUpdate(u.user_id_, u.status_);
    } else if constexpr (std::is_same_v<T, td_api::updateConnectionState>) {
      OnConnectionStateUpdate(u.state_);
    } else if constexpr (std::is_same_v<T,
                                        td_api::updateMessageInteractionInfo>) {
      OnMessageInteractionInfo(u.chat_id_, u.message_id_, u.interaction_info_);
    } else if constexpr (std::is_same_v<T, td_api::updateChatAction>) {
      OnChatAction(u.chat_id_, u.sender_id_, u.action_);
    } else if constexpr (std::is_same_v<T, td_api::updateDeleteMessages>) {
      if (u.is_permanent_) {
        OnDeleteMessages(u.chat_id_, u.message_ids_);
      }
    } else if constexpr (std::is_same_v<T,
                                        td_api::updateMessageSendSucceeded>) {
      OnMessageSendSucceeded(u.message_, u.old_message_id_);
    } else if constexpr (std::is_same_v<T, td_api::updateMessageSendFailed>) {
      OnMessageSendFailed(u.message_, u.old_message_id_, u.error_->message_);
    } else if constexpr (std::is_same_v<T, td_api::updateFile>) {
      OnFileUpdate(u.file_);
    } else if constexpr (std::is_same_v<
                             T, td_api::updateChatNotificationSettings>) {
      std::unique_lock<std::shared_mutex> lock(m_dataMutex);
      if (auto it = m_chats.find(u.chat_id_); it != m_chats.end()) {
        it->second.isMuted = u.notification_settings_->mute_for_ > 0;
      }
    }
  });
}

void TelegramClient::OnConnectionStateUpdate(
    td_api::object_ptr<td_api::ConnectionState> &state) {
  if (!state)
    return;

  td_api::downcast_call(*state, [this](auto &s) {
    using T = std::decay_t<decltype(s)>;

    if constexpr (std::is_same_v<T, td_api::connectionStateWaitingForNetwork>) {
      m_connectionState = ConnectionState::WaitingForNetwork;
      TDLOG("Connection state: WaitingForNetwork");
    } else if constexpr (std::is_same_v<
                             T, td_api::connectionStateConnectingToProxy>) {
      m_connectionState = ConnectionState::ConnectingToProxy;
      TDLOG("Connection state: ConnectingToProxy");
    } else if constexpr (std::is_same_v<T, td_api::connectionStateConnecting>) {
      m_connectionState = ConnectionState::Connecting;
      TDLOG("Connection state: Connecting");
    } else if constexpr (std::is_same_v<T, td_api::connectionStateUpdating>) {
      m_connectionState = ConnectionState::Updating;
      TDLOG("Connection state: Updating");
    } else if constexpr (std::is_same_v<T, td_api::connectionStateReady>) {
      m_connectionState = ConnectionState::Ready;
      TDLOG("Connection state: Ready");
    }
  });

  // Set dirty flag so UI can update connection status
  SetDirty(DirtyFlag::Auth);
}

void TelegramClient::OnChatAction(
    int64_t chatId, td_api::object_ptr<td_api::MessageSender> &sender,
    td_api::object_ptr<td_api::ChatAction> &action) {
  if (!sender || !action)
    return;

  // Only track for current chat
  if (chatId != m_currentChatId)
    return;

  wxString senderName;
  td_api::downcast_call(*sender, [this, &senderName](auto &s) {
    using T = std::decay_t<decltype(s)>;
    if constexpr (std::is_same_v<T, td_api::messageSenderUser>) {
      senderName = GetUserDisplayName(s.user_id_);
    }
  });

  if (senderName.IsEmpty())
    return;

  wxString actionText;
  td_api::downcast_call(*action, [&actionText](auto &a) {
    using T = std::decay_t<decltype(a)>;
    if constexpr (std::is_same_v<T, td_api::chatActionTyping>) {
      actionText = "typing";
    } else if constexpr (std::is_same_v<T, td_api::chatActionRecordingVideo>) {
      actionText = "recording video";
    } else if constexpr (std::is_same_v<T, td_api::chatActionUploadingVideo>) {
      actionText = "uploading video";
    } else if constexpr (std::is_same_v<T,
                                        td_api::chatActionRecordingVoiceNote>) {
      actionText = "recording voice";
    } else if constexpr (std::is_same_v<T,
                                        td_api::chatActionUploadingVoiceNote>) {
      actionText = "uploading voice";
    } else if constexpr (std::is_same_v<T, td_api::chatActionUploadingPhoto>) {
      actionText = "uploading photo";
    } else if constexpr (std::is_same_v<T,
                                        td_api::chatActionUploadingDocument>) {
      actionText = "uploading file";
    } else if constexpr (std::is_same_v<T, td_api::chatActionChoosingSticker>) {
      actionText = "choosing sticker";
    } else if constexpr (std::is_same_v<T,
                                        td_api::chatActionRecordingVideoNote>) {
      actionText = "recording video message";
    } else if constexpr (std::is_same_v<T,
                                        td_api::chatActionUploadingVideoNote>) {
      actionText = "uploading video message";
    } else if constexpr (std::is_same_v<T, td_api::chatActionCancel>) {
      actionText = ""; // Action cancelled
    }
  });

  // Queue typing indicator update with timestamp for auto-timeout
  {
    std::lock_guard<std::mutex> lock(m_typingMutex);
    if (actionText.IsEmpty()) {
      m_typingUsers.erase(senderName);
    } else {
      // Store action text and current timestamp
      m_typingUsers[senderName] = {actionText, wxGetUTCTime()};
    }
  }

  SetDirty(DirtyFlag::UserStatus); // Reuse this flag for typing updates
  NotifyUIRefresh(); // Immediately notify UI for responsive typing indicator
}

void TelegramClient::OnDeleteMessages(int64_t chatId,
                                      const std::vector<int64_t> &messageIds) {
  // Remove from cache
  {
    std::unique_lock<std::shared_mutex> lock(m_dataMutex);
    auto chatIt = m_messages.find(chatId);
    if (chatIt != m_messages.end()) {
      auto &msgs = chatIt->second;
      msgs.erase(std::remove_if(msgs.begin(), msgs.end(),
                                [&messageIds](const MessageInfo &m) {
                                  return std::find(messageIds.begin(),
                                                   messageIds.end(),
                                                   m.id) != messageIds.end();
                                }),
                 msgs.end());
    }
  }

  // Queue deleted message IDs for UI
  {
    std::lock_guard<std::mutex> lock(m_deletedMessagesMutex);
    for (int64_t id : messageIds) {
      m_deletedMessages[chatId].push_back(id);
    }
  }

  SetDirty(DirtyFlag::Messages);
}

void TelegramClient::OnMessageSendSucceeded(
    td_api::object_ptr<td_api::message> &message, int64_t oldMessageId) {
  if (!message)
    return;

  MessageInfo newMsg = ConvertMessage(message.get());
  int64_t newId = newMsg.id;

  TDLOG("OnMessageSendSucceeded: oldId=%lld newId=%lld fileId=%d localPath=%s",
        (long long)oldMessageId, (long long)newId, newMsg.mediaFileId,
        newMsg.mediaLocalPath.ToStdString().c_str());

  // Update cache - replace old message with new one
  {
    std::unique_lock<std::shared_mutex> lock(m_dataMutex);
    auto chatIt = m_messages.find(newMsg.chatId);
    if (chatIt != m_messages.end()) {
      for (auto &msg : chatIt->second) {
        if (msg.id == oldMessageId) {
          msg = newMsg;
          break;
        }
      }
    }
  }

  // Queue update for UI with OLD ID so it can find the message
  // The serverMessageId field tells the UI what the new ID should be
  {
    std::lock_guard<std::mutex> lock(m_updatedMessagesMutex);
    MessageInfo updateMsg = newMsg;
    updateMsg.id = oldMessageId;       // Use old ID so UI can find it
    updateMsg.serverMessageId = newId; // New server-assigned ID
    m_updatedMessages[newMsg.chatId].push_back(updateMsg);
  }

  SetDirty(DirtyFlag::Messages);
}

void TelegramClient::OnMessageSendFailed(
    td_api::object_ptr<td_api::message> &message, int64_t oldMessageId,
    const std::string &errorMessage) {
  if (!message)
    return;

  int64_t chatId = message->chat_id_;
  wxString error = wxString::FromUTF8(errorMessage);

  // Queue error for UI to display
  {
    std::lock_guard<std::mutex> lock(m_sendFailedMutex);
    m_sendFailedMessages[chatId].push_back({oldMessageId, error});
  }

  SetDirty(DirtyFlag::Messages);
}

void TelegramClient::OnMessageInteractionInfo(
    int64_t chatId, int64_t messageId,
    td_api::object_ptr<td_api::messageInteractionInfo> &info) {
  if (!info)
    return;

  // Extract reactions
  std::map<wxString, std::vector<wxString>> reactions;

  if (info->reactions_) {
    for (auto &reaction : info->reactions_->reactions_) {
      if (!reaction)
        continue;

      wxString emoji;
      if (reaction->type_) {
        td_api::downcast_call(*reaction->type_, [&emoji](auto &r) {
          using T = std::decay_t<decltype(r)>;
          if constexpr (std::is_same_v<T, td_api::reactionTypeEmoji>) {
            emoji = wxString::FromUTF8(r.emoji_);
          } else if constexpr (std::is_same_v<
                                   T, td_api::reactionTypeCustomEmoji>) {
            emoji = wxString::FromUTF8("⭐"); // Placeholder for custom emoji
          } else if constexpr (std::is_same_v<T, td_api::reactionTypePaid>) {
            emoji = wxString::FromUTF8("⭐"); // Paid reaction
          }
        });
      }

      if (emoji.IsEmpty())
        continue;

      // Get recent senders for this reaction
      std::vector<wxString> senders;
      for (auto &sender : reaction->recent_sender_ids_) {
        if (!sender)
          continue;

        td_api::downcast_call(*sender, [this, &senders](auto &s) {
          using T = std::decay_t<decltype(s)>;
          if constexpr (std::is_same_v<T, td_api::messageSenderUser>) {
            wxString name = GetUserDisplayName(s.user_id_);
            if (!name.IsEmpty()) {
              senders.push_back(name);
            }
          }
        });
      }

      // If no recent senders but has count, show count
      if (senders.empty() && reaction->total_count_ > 0) {
        senders.push_back(wxString::Format("%d", reaction->total_count_));
      }

      if (!senders.empty()) {
        reactions[emoji] = senders;
      }
    }
  }

  // Update message in storage
  {
    std::unique_lock<std::shared_mutex> lock(m_dataMutex);
    auto chatIt = m_messages.find(chatId);
    if (chatIt != m_messages.end()) {
      for (auto &msg : chatIt->second) {
        if (msg.id == messageId) {
          msg.reactions = reactions;
          break;
        }
      }
    }
  }

  // Queue the updated message for UI
  {
    std::unique_lock<std::shared_mutex> lock(m_dataMutex);
    auto chatIt = m_messages.find(chatId);
    if (chatIt != m_messages.end()) {
      for (auto &msg : chatIt->second) {
        if (msg.id == messageId) {
          std::lock_guard<std::mutex> qlock(m_updatedMessagesMutex);
          m_updatedMessages[chatId].push_back(msg);
          break;
        }
      }
    }
  }

  SetDirty(DirtyFlag::Messages);
}

void TelegramClient::OnAuthStateUpdate(
    td_api::object_ptr<td_api::AuthorizationState> &state) {
  td_api::downcast_call(*state, [this](auto &s) {
    using T = std::decay_t<decltype(s)>;

    if constexpr (std::is_same_v<
                      T, td_api::authorizationStateWaitTdlibParameters>) {
      TDLOG("Auth state: WaitTdlibParameters");
      m_authState = AuthState::WaitTdlibParameters;
      HandleAuthWaitTdlibParameters();
    } else if constexpr (std::is_same_v<
                             T, td_api::authorizationStateWaitPhoneNumber>) {
      TDLOG("Auth state: WaitPhoneNumber");
      m_authState = AuthState::WaitPhoneNumber;
      HandleAuthWaitPhoneNumber();
    } else if constexpr (std::is_same_v<T,
                                        td_api::authorizationStateWaitCode>) {
      TDLOG("Auth state: WaitCode");
      m_authState = AuthState::WaitCode;
      HandleAuthWaitCode();
    } else if constexpr (std::is_same_v<
                             T, td_api::authorizationStateWaitPassword>) {
      TDLOG("Auth state: WaitPassword");
      m_authState = AuthState::WaitPassword;
      HandleAuthWaitPassword();
    } else if constexpr (std::is_same_v<T, td_api::authorizationStateReady>) {
      TDLOG("Auth state: Ready");
      m_authState = AuthState::Ready;
      HandleAuthReady();
    } else if constexpr (std::is_same_v<T, td_api::authorizationStateClosed>) {
      TDLOG("Auth state: Closed");
      m_authState = AuthState::Closed;
      HandleAuthClosed();
    } else if constexpr (std::is_same_v<T,
                                        td_api::authorizationStateLoggingOut>) {
      // Logging out, wait for closed state
    } else if constexpr (std::is_same_v<T, td_api::authorizationStateClosing>) {
      // Closing, wait for closed state
    }
  });
}

void TelegramClient::HandleAuthWaitTdlibParameters() {
  TDLOG("Sending TDLib parameters...");

  // Get user home directory for database storage
  wxString homeDir = wxGetHomeDir();
  wxString dbDir = homeDir + "/.teleliter";

  // Create directory if it doesn't exist
  if (!wxDirExists(dbDir)) {
    wxMkdir(dbDir);
  }

  // New TDLib API - setTdlibParameters has all fields directly
  auto request = td_api::make_object<td_api::setTdlibParameters>();
  request->use_test_dc_ = false;
  request->database_directory_ = dbDir.ToStdString();
  request->files_directory_ = (dbDir + "/files").ToStdString();
  request->database_encryption_key_ = ""; // Empty = no encryption
  request->use_file_database_ = true;
  request->use_chat_info_database_ = true;
  request->use_message_database_ = true;
  request->use_secret_chats_ = false;
  request->api_id_ = 34533272;
  request->api_hash_ = "0bd07411a17b475a31e96d09cd8474f6";
  request->system_language_code_ = "en";
  request->device_model_ = "Desktop";
  request->system_version_ = "macOS";
  request->application_version_ = "0.1.0";

  Send(std::move(request), [this](td_api::object_ptr<td_api::Object> result) {
    if (result->get_id() == td_api::error::ID) {
      auto error = td_api::move_object_as<td_api::error>(result);
      TDLOG("setTdlibParameters error: %s", error->message_.c_str());
      PostToMainThread([this, msg = error->message_]() {
        if (m_welcomeChat) {
          m_welcomeChat->OnLoginError(wxString::FromUTF8(msg));
        }
      });
    } else {
      TDLOG("setTdlibParameters success");
    }
  });
}

void TelegramClient::HandleAuthWaitPhoneNumber() {
  TDLOG("Ready for phone number, notifying UI...");
  PostToMainThread([this]() {
    // Update status bar to show connected
    if (m_mainFrame) {
      m_mainFrame->OnConnected();
    }
    // Notify welcome chat
    if (m_welcomeChat) {
      m_welcomeChat->OnAuthStateChanged(static_cast<int>(m_authState));
    }
  });
}

void TelegramClient::HandleAuthWaitCode() {
  PostToMainThread([this]() {
    if (m_welcomeChat) {
      m_welcomeChat->OnCodeRequested();
    }
  });
}

void TelegramClient::HandleAuthWaitPassword() {
  PostToMainThread([this]() {
    if (m_welcomeChat) {
      m_welcomeChat->On2FARequested();
    }
  });
}

void TelegramClient::HandleAuthReady() {
  // Configure auto-download settings for reliable media loading
  // This makes TDLib automatically download photos, videos, and other media
  ConfigureAutoDownload();

  // Get current user info
  Send(td_api::make_object<td_api::getMe>(),
       [this](td_api::object_ptr<td_api::Object> result) {
         if (result->get_id() == td_api::user::ID) {
           auto user = td_api::move_object_as<td_api::user>(result);

           m_currentUser.id = user->id_;
           m_currentUser.firstName = wxString::FromUTF8(user->first_name_);
           m_currentUser.lastName = wxString::FromUTF8(user->last_name_);
           // New API uses usernames_ object
           if (user->usernames_) {
             m_currentUser.username =
                 wxString::FromUTF8(user->usernames_->editable_username_);
           }
           m_currentUser.phoneNumber = wxString::FromUTF8(user->phone_number_);
           m_currentUser.isBot =
               user->type_->get_id() == td_api::userTypeBot::ID;
           m_currentUser.isSelf = true;

           // Store in users map too
           {
             std::unique_lock<std::shared_mutex> lock(m_dataMutex);
             m_users[user->id_] = m_currentUser;
           }

           PostToMainThread([this]() {
             if (m_welcomeChat) {
               m_welcomeChat->OnLoginSuccess(m_currentUser.GetDisplayName(),
                                             m_currentUser.phoneNumber);
             }
             if (m_mainFrame) {
               m_mainFrame->OnLoginSuccess(m_currentUser.GetDisplayName());
             }
           });

           // Load chats
           LoadChats();
         }
       });
}

void TelegramClient::ConfigureAutoDownload() {
  // Create auto-download settings that enable downloading for all network types
  // This mimics how the real Telegram app works - media loads automatically

  // Settings for WiFi (generous limits)
  auto wifiSettings = td_api::make_object<td_api::autoDownloadSettings>();
  wifiSettings->is_auto_download_enabled_ = true;
  wifiSettings->max_photo_file_size_ = 10 * 1024 * 1024;  // 10 MB photos
  wifiSettings->max_video_file_size_ = 100 * 1024 * 1024; // 100 MB videos
  wifiSettings->max_other_file_size_ = 10 * 1024 * 1024;  // 10 MB other files
  wifiSettings->video_upload_bitrate_ = 0;                // No limit
  wifiSettings->preload_large_videos_ = true;
  wifiSettings->preload_next_audio_ = true;
  wifiSettings->preload_stories_ = true;
  wifiSettings->use_less_data_for_calls_ = false;

  // Settings for mobile data (same as WiFi for simplicity)
  auto mobileSettings = td_api::make_object<td_api::autoDownloadSettings>();
  mobileSettings->is_auto_download_enabled_ = true;
  mobileSettings->max_photo_file_size_ = 10 * 1024 * 1024;
  mobileSettings->max_video_file_size_ = 50 * 1024 * 1024; // 50 MB on mobile
  mobileSettings->max_other_file_size_ = 5 * 1024 * 1024;
  mobileSettings->video_upload_bitrate_ = 0;
  mobileSettings->preload_large_videos_ = true;
  mobileSettings->preload_next_audio_ = true;
  mobileSettings->preload_stories_ = true;
  mobileSettings->use_less_data_for_calls_ = false;

  // Settings for roaming (more conservative)
  auto roamingSettings = td_api::make_object<td_api::autoDownloadSettings>();
  roamingSettings->is_auto_download_enabled_ = true;
  roamingSettings->max_photo_file_size_ = 5 * 1024 * 1024;
  roamingSettings->max_video_file_size_ = 10 * 1024 * 1024;
  roamingSettings->max_other_file_size_ = 1 * 1024 * 1024;
  roamingSettings->video_upload_bitrate_ = 0;
  roamingSettings->preload_large_videos_ = false;
  roamingSettings->preload_next_audio_ = true;
  roamingSettings->preload_stories_ = false;
  roamingSettings->use_less_data_for_calls_ = true;

  // Apply auto-download settings
  auto request = td_api::make_object<td_api::setAutoDownloadSettings>();
  request->settings_ = std::move(wifiSettings);
  request->type_ = td_api::make_object<td_api::networkTypeWiFi>();
  Send(std::move(request), nullptr);

  auto request2 = td_api::make_object<td_api::setAutoDownloadSettings>();
  request2->settings_ = std::move(mobileSettings);
  request2->type_ = td_api::make_object<td_api::networkTypeMobile>();
  Send(std::move(request2), nullptr);

  auto request3 = td_api::make_object<td_api::setAutoDownloadSettings>();
  request3->settings_ = std::move(roamingSettings);
  request3->type_ = td_api::make_object<td_api::networkTypeMobileRoaming>();
  Send(std::move(request3), nullptr);

  TDLOG("Configured auto-download settings for all network types");
}

void TelegramClient::HandleAuthClosed() {
  m_running = false;
  PostToMainThread([this]() {
    if (m_mainFrame) {
      m_mainFrame->OnLoggedOut();
    }
  });
}

void TelegramClient::SetPhoneNumber(const wxString &phoneNumber) {
  auto request = td_api::make_object<td_api::setAuthenticationPhoneNumber>();
  request->phone_number_ = phoneNumber.ToStdString();

  Send(std::move(request), [this](td_api::object_ptr<td_api::Object> result) {
    if (result->get_id() == td_api::error::ID) {
      auto error = td_api::move_object_as<td_api::error>(result);
      PostToMainThread([this, msg = error->message_]() {
        if (m_welcomeChat) {
          m_welcomeChat->OnLoginError(wxString::FromUTF8(msg));
        }
      });
    }
  });
}

void TelegramClient::SetAuthCode(const wxString &code) {
  auto request = td_api::make_object<td_api::checkAuthenticationCode>();
  request->code_ = code.ToStdString();

  Send(std::move(request), [this](td_api::object_ptr<td_api::Object> result) {
    if (result->get_id() == td_api::error::ID) {
      auto error = td_api::move_object_as<td_api::error>(result);
      PostToMainThread([this, msg = error->message_]() {
        if (m_welcomeChat) {
          m_welcomeChat->OnLoginError(wxString::FromUTF8(msg));
        }
      });
    }
  });
}

void TelegramClient::SetPassword(const wxString &password) {
  auto request = td_api::make_object<td_api::checkAuthenticationPassword>();
  request->password_ = password.ToStdString();

  Send(std::move(request), [this](td_api::object_ptr<td_api::Object> result) {
    if (result->get_id() == td_api::error::ID) {
      auto error = td_api::move_object_as<td_api::error>(result);
      PostToMainThread([this, msg = error->message_]() {
        if (m_welcomeChat) {
          m_welcomeChat->OnLoginError(wxString::FromUTF8(msg));
        }
      });
    }
  });
}

void TelegramClient::LogOut() {
  Send(td_api::make_object<td_api::logOut>(), nullptr);
}

void TelegramClient::LoadChats(int limit) {
  auto request = td_api::make_object<td_api::loadChats>();
  request->chat_list_ = td_api::make_object<td_api::chatListMain>();
  request->limit_ = limit;

  Send(std::move(request), [this](td_api::object_ptr<td_api::Object> result) {
    if (result->get_id() == td_api::error::ID) {
      // No more chats or error
      return;
    }

    // Get the chat list
    Send(td_api::make_object<td_api::getChats>(
             td_api::make_object<td_api::chatListMain>(), 100),
         [this](td_api::object_ptr<td_api::Object> result) {
           if (result->get_id() == td_api::chats::ID) {
             auto chats = td_api::move_object_as<td_api::chats>(result);

             for (auto chatId : chats->chat_ids_) {
               // Get full chat info
               Send(td_api::make_object<td_api::getChat>(chatId),
                    [this](td_api::object_ptr<td_api::Object> result) {
                      if (result->get_id() == td_api::chat::ID) {
                        auto chat =
                            td_api::move_object_as<td_api::chat>(result);
                        OnChatUpdate(chat);
                      }
                    });
             }

             // REACTIVE MVC: Set dirty flag instead of posting callback
             SetDirty(DirtyFlag::ChatList);
           }
         });
  });
}

std::map<int64_t, ChatInfo> TelegramClient::GetChats() const {
  std::shared_lock<std::shared_mutex> lock(m_dataMutex);
  return m_chats;
}

ChatInfo TelegramClient::GetChat(int64_t chatId, bool *found) const {
  std::shared_lock<std::shared_mutex> lock(m_dataMutex);
  auto it = m_chats.find(chatId);
  if (it != m_chats.end()) {
    if (found)
      *found = true;
    return it->second;
  }
  if (found)
    *found = false;
  return ChatInfo();
}

void TelegramClient::OpenChat(int64_t chatId) {
  TDLOG("OpenChat called for chatId=%lld", (long long)chatId);
  auto request = td_api::make_object<td_api::openChat>();
  request->chat_id_ = chatId;
  Send(std::move(request), nullptr);
}

void TelegramClient::OpenChatAndLoadMessages(int64_t chatId, int limit) {
  TDLOG("OpenChatAndLoadMessages called for chatId=%lld limit=%d",
        (long long)chatId, limit);

  // Track current chat for download prioritization
  m_currentChatId = chatId;

  // Clear typing users from previous chat
  {
    std::lock_guard<std::mutex> lock(m_typingMutex);
    m_typingUsers.clear();
  }

  // Step 1: Open the chat - this tells TDLib we're viewing this chat
  // and triggers background sync of messages from server
  auto openRequest = td_api::make_object<td_api::openChat>();
  openRequest->chat_id_ = chatId;

  Send(std::move(openRequest), [this, chatId,
                                limit](td_api::object_ptr<td_api::Object>
                                           openResult) {
    TDLOG("openChat completed for chatId=%lld", (long long)chatId);

    // Step 2: Get chat info to find the last message ID
    // This helps us know where to start fetching history
    auto getChatRequest = td_api::make_object<td_api::getChat>();
    getChatRequest->chat_id_ = chatId;

    Send(std::move(getChatRequest), [this, chatId,
                                     limit](td_api::object_ptr<td_api::Object>
                                                chatResult) {
      if (chatResult->get_id() == td_api::chat::ID) {
        auto chat = td_api::move_object_as<td_api::chat>(chatResult);
        if (chat->last_message_) {
          TDLOG("Chat has last_message_id=%lld",
                (long long)chat->last_message_->id_);
        }
      }

      // Step 3: Fetch messages starting from the last message
      // Using offset=0 and from_message_id=lastMessageId+1 to include the last
      // message
      auto historyRequest = td_api::make_object<td_api::getChatHistory>();
      historyRequest->chat_id_ = chatId;
      // Use 0 to get from the newest, TDLib will figure out the rest
      historyRequest->from_message_id_ = 0;
      historyRequest->offset_ = 0;
      historyRequest->limit_ = limit > 0 ? limit : 100;
      historyRequest->only_local_ = false;

      Send(std::move(historyRequest), [this, chatId,
                                       limit](td_api::object_ptr<td_api::Object>
                                                  result) {
        TDLOG("getChatHistory response for chatId=%lld", (long long)chatId);

        if (result->get_id() == td_api::messages::ID) {
          auto messages = td_api::move_object_as<td_api::messages>(result);
          size_t count = messages->messages_.size();
          TDLOG("Got %d total, %zu in batch", messages->total_count_, count);

          std::vector<MessageInfo> msgList;
          for (auto &msg : messages->messages_) {
            if (msg) {
              msgList.push_back(ConvertMessage(msg.get()));
            }
          }

          // Store and display what we have
          {
            std::unique_lock<std::shared_mutex> lock(m_dataMutex);
            m_messages[chatId] = msgList;
          }

          PostToMainThread([this, chatId, msgList]() {
            if (m_mainFrame) {
              m_mainFrame->OnMessagesLoaded(chatId, msgList);
            }
          });

          // NOTE: No auto-download here! We use lazy loading:
          // - Thumbnails are downloaded when messages are rendered
          // - Full media is downloaded on-demand (hover/click)

          // If we got fewer messages than requested, TDLib may still be syncing
          // Try to load more after a brief moment
          if (count < (size_t)limit && count > 0) {
            // Get the oldest message ID from what we received
            int64_t oldestMsgId = 0;
            if (!messages->messages_.empty() && messages->messages_.back()) {
              oldestMsgId = messages->messages_.back()->id_;
            }

            TDLOG(
                "Got partial history, will try to load more from message %lld",
                (long long)oldestMsgId);

            // Schedule another fetch for older messages
            LoadMoreMessages(chatId, oldestMsgId, limit);
          }
        } else if (result->get_id() == td_api::error::ID) {
          auto error = td_api::move_object_as<td_api::error>(result);
          TDLOG("getChatHistory ERROR: %d - %s", error->code_,
                error->message_.c_str());
        }
      });
    });
  });
}

void TelegramClient::LoadMoreMessages(int64_t chatId, int64_t fromMessageId,
                                      int limit) {
  TDLOG("LoadMoreMessages for chatId=%lld from=%lld", (long long)chatId,
        (long long)fromMessageId);

  auto request = td_api::make_object<td_api::getChatHistory>();
  request->chat_id_ = chatId;
  request->from_message_id_ = fromMessageId;
  request->offset_ = 0;
  request->limit_ = limit > 0 ? limit : 100;
  request->only_local_ = false;

  Send(std::move(request), [this,
                            chatId](td_api::object_ptr<td_api::Object> result) {
    if (result->get_id() == td_api::messages::ID) {
      auto messages = td_api::move_object_as<td_api::messages>(result);

      if (messages->messages_.empty()) {
        TDLOG("No more messages to load for chatId=%lld", (long long)chatId);
        return;
      }

      TDLOG("LoadMoreMessages got %zu additional messages",
            messages->messages_.size());

      std::vector<MessageInfo> newMessages;
      for (auto &msg : messages->messages_) {
        if (msg) {
          newMessages.push_back(ConvertMessage(msg.get()));
        }
      }

      // Merge with existing messages, avoiding duplicates
      std::vector<MessageInfo> allMessages;
      {
        std::unique_lock<std::shared_mutex> lock(m_dataMutex);
        auto &existing = m_messages[chatId];

        // Build a set of existing message IDs to avoid duplicates
        std::set<int64_t> existingIds;
        for (const auto &msg : existing) {
          existingIds.insert(msg.id);
        }

        // Add only new messages that aren't already present
        for (const auto &msg : newMessages) {
          if (existingIds.find(msg.id) == existingIds.end()) {
            existing.push_back(msg);
            existingIds.insert(msg.id);
          }
        }

        // Sort by message ID to ensure correct chronological order
        std::sort(existing.begin(), existing.end(),
                  [](const MessageInfo &a, const MessageInfo &b) {
                    return a.id < b.id;
                  });

        allMessages = existing; // Copy for thread-safe access
      }

      TDLOG("Total messages after LoadMore: %zu", allMessages.size());

      // Notify UI to refresh with all messages
      PostToMainThread([this, chatId, allMessages]() {
        if (m_mainFrame) {
          m_mainFrame->OnMessagesLoaded(chatId, allMessages);
        }
      });
    }
  });
}

void TelegramClient::LoadMessagesWithRetry(int64_t chatId, int limit,
                                           int retryCount) {
  // This function is kept for compatibility but redirects to the main loader
  OpenChatAndLoadMessages(chatId, limit);
}

void TelegramClient::CloseChat(int64_t chatId) {
  TDLOG("CloseChat called for chatId=%lld", (long long)chatId);

  // Clear current chat tracking if closing the active chat
  if (m_currentChatId == chatId) {
    m_currentChatId = 0;
  }

  auto request = td_api::make_object<td_api::closeChat>();
  request->chat_id_ = chatId;
  Send(std::move(request), nullptr);
}

void TelegramClient::LoadMessages(int64_t chatId, int64_t fromMessageId,
                                  int limit) {
  TDLOG("LoadMessages called for chatId=%lld fromMessageId=%lld limit=%d",
        (long long)chatId, (long long)fromMessageId, limit);

  // Request chat history from TDLib
  // from_message_id=0 means start from the newest message
  // only_local=false ensures we fetch from server if needed
  auto request = td_api::make_object<td_api::getChatHistory>();
  request->chat_id_ = chatId;
  request->from_message_id_ = fromMessageId;
  request->offset_ = 0;
  request->limit_ = limit > 0 ? limit : 100;
  request->only_local_ = false;

  Send(std::move(request),
       [this, chatId](td_api::object_ptr<td_api::Object> result) {
         TDLOG("LoadMessages response received for chatId=%lld result_id=%d",
               (long long)chatId, result->get_id());

         if (result->get_id() == td_api::messages::ID) {
           auto messages = td_api::move_object_as<td_api::messages>(result);

           TDLOG("Got %d total messages, %zu in this batch",
                 messages->total_count_, messages->messages_.size());

           std::vector<MessageInfo> msgList;
           for (auto &msg : messages->messages_) {
             if (msg) {
               msgList.push_back(ConvertMessage(msg.get()));
               TDLOG("  Message %lld: %.50s", (long long)msg->id_,
                     msgList.back().text.ToStdString().c_str());
             }
           }

           TDLOG("Converted %zu messages for chatId=%lld", msgList.size(),
                 (long long)chatId);

           // Store messages (replace to avoid duplicates)
           {
             std::unique_lock<std::shared_mutex> lock(m_dataMutex);
             m_messages[chatId] = msgList;
           }

           PostToMainThread([this, chatId, msgList]() {
             TDLOG("PostToMainThread: OnMessagesLoaded for chatId=%lld with "
                   "%zu messages",
                   (long long)chatId, msgList.size());
             if (m_mainFrame) {
               m_mainFrame->OnMessagesLoaded(chatId, msgList);
             } else {
               TDLOG("ERROR: m_mainFrame is null!");
             }
           });
         } else if (result->get_id() == td_api::error::ID) {
           auto error = td_api::move_object_as<td_api::error>(result);
           TDLOG("LoadMessages ERROR: %d - %s", error->code_,
                 error->message_.c_str());
         }
       });
}

void TelegramClient::SendMessage(int64_t chatId, const wxString &text) {
  SendMessage(chatId, text, 0); // No reply
}

void TelegramClient::SendMessage(int64_t chatId, const wxString &text,
                                 int64_t replyToMessageId) {
  auto content = td_api::make_object<td_api::inputMessageText>();
  content->text_ = td_api::make_object<td_api::formattedText>();
  content->text_->text_ = text.ToStdString();

  auto request = td_api::make_object<td_api::sendMessage>();
  request->chat_id_ = chatId;
  request->input_message_content_ = std::move(content);

  // Set reply if specified
  if (replyToMessageId != 0) {
    auto replyTo = td_api::make_object<td_api::inputMessageReplyToMessage>();
    replyTo->message_id_ = replyToMessageId;
    request->reply_to_ = std::move(replyTo);
  }

  Send(std::move(request), [this](td_api::object_ptr<td_api::Object> result) {
    if (result->get_id() == td_api::error::ID) {
      auto error = td_api::move_object_as<td_api::error>(result);
      PostToMainThread([this, msg = error->message_]() {
        if (m_mainFrame) {
          m_mainFrame->ShowStatusError(wxString::FromUTF8(msg));
        }
      });
    }
  });
}

void TelegramClient::SendFile(int64_t chatId, const wxString &filePath,
                              const wxString &caption) {
  // Determine file type based on extension
  wxString ext = filePath.AfterLast('.').Lower();

  td_api::object_ptr<td_api::InputMessageContent> content;

  auto inputFile = td_api::make_object<td_api::inputFileLocal>();
  inputFile->path_ = filePath.ToStdString();

  auto formattedCaption = td_api::make_object<td_api::formattedText>();
  formattedCaption->text_ = caption.ToStdString();

  if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "gif" ||
      ext == "webp") {
    auto photo = td_api::make_object<td_api::inputMessagePhoto>();
    photo->photo_ = std::move(inputFile);
    photo->caption_ = std::move(formattedCaption);
    content = std::move(photo);
  } else if (ext == "mp4" || ext == "mkv" || ext == "avi" || ext == "mov" ||
             ext == "webm") {
    auto video = td_api::make_object<td_api::inputMessageVideo>();
    video->video_ = std::move(inputFile);
    video->caption_ = std::move(formattedCaption);
    content = std::move(video);
  } else if (ext == "mp3" || ext == "ogg" || ext == "wav" || ext == "flac" ||
             ext == "m4a") {
    auto audio = td_api::make_object<td_api::inputMessageAudio>();
    audio->audio_ = std::move(inputFile);
    audio->caption_ = std::move(formattedCaption);
    content = std::move(audio);
  } else {
    auto doc = td_api::make_object<td_api::inputMessageDocument>();
    doc->document_ = std::move(inputFile);
    doc->caption_ = std::move(formattedCaption);
    content = std::move(doc);
  }

  auto request = td_api::make_object<td_api::sendMessage>();
  request->chat_id_ = chatId;
  request->input_message_content_ = std::move(content);

  Send(std::move(request), [this](td_api::object_ptr<td_api::Object> result) {
    if (result->get_id() == td_api::error::ID) {
      auto error = td_api::move_object_as<td_api::error>(result);
      PostToMainThread([this, msg = error->message_]() {
        if (m_mainFrame) {
          m_mainFrame->ShowStatusError(wxString::FromUTF8(msg));
        }
      });
    }
  });
}

void TelegramClient::RefetchMessage(int64_t chatId, int64_t messageId) {
  if (chatId == 0 || messageId == 0) {
    TDLOG("RefetchMessage: invalid chatId=%lld or messageId=%lld",
          (long long)chatId, (long long)messageId);
    return;
  }

  TDLOG("RefetchMessage: fetching chatId=%lld messageId=%lld",
        (long long)chatId, (long long)messageId);

  auto request = td_api::make_object<td_api::getMessage>();
  request->chat_id_ = chatId;
  request->message_id_ = messageId;

  Send(std::move(request), [this, chatId, messageId](
                               td_api::object_ptr<td_api::Object> result) {
    if (!result) {
      TDLOG("RefetchMessage: no response for messageId=%lld",
            (long long)messageId);
      return;
    }

    if (result->get_id() == td_api::error::ID) {
      auto error = td_api::move_object_as<td_api::error>(result);
      TDLOG("RefetchMessage: error %d: %s", error->code_,
            error->message_.c_str());
      (void)error; // Suppress unused variable warning when TDLOG is disabled
      return;
    }

    if (result->get_id() == td_api::message::ID) {
      auto msg = td_api::move_object_as<td_api::message>(result);
      MessageInfo updatedInfo = ConvertMessage(msg.get());

      TDLOG("RefetchMessage: got updated message, fileId=%d thumbId=%d",
            updatedInfo.mediaFileId, updatedInfo.mediaThumbnailFileId);

      // Update the cached message
      {
        std::unique_lock<std::shared_mutex> lock(m_dataMutex);
        auto it = m_messages.find(chatId);
        if (it != m_messages.end()) {
          for (auto &cachedMsg : it->second) {
            if (cachedMsg.id == messageId) {
              // Update media fields
              cachedMsg.mediaFileId = updatedInfo.mediaFileId;
              cachedMsg.mediaThumbnailFileId = updatedInfo.mediaThumbnailFileId;
              cachedMsg.mediaLocalPath = updatedInfo.mediaLocalPath;
              cachedMsg.mediaThumbnailPath = updatedInfo.mediaThumbnailPath;
              TDLOG(
                  "RefetchMessage: updated cached message fileId=%d thumbId=%d",
                  cachedMsg.mediaFileId, cachedMsg.mediaThumbnailFileId);
              break;
            }
          }
        }
      }

      // Notify UI to refresh the message display
      PostToMainThread([this, chatId, updatedInfo]() {
        if (m_mainFrame) {
          m_mainFrame->OnMessageUpdated(chatId, updatedInfo);
        }
      });
    }
  });
}

void TelegramClient::DownloadFile(int32_t fileId, int priority,
                                  const wxString &fileName, int64_t fileSize) {
  if (fileId == 0) {
    TDLOG("DownloadFile: ignoring invalid fileId=0");
    return;
  }

  TDLOG("DownloadFile: requested fileId=%d priority=%d fileName=%s", fileId,
        priority, fileName.ToStdString().c_str());

  // Allow many concurrent downloads - TDLib handles its own throttling
  static const size_t MAX_CONCURRENT_DOWNLOADS = 20;

  {
    std::lock_guard<std::mutex> lock(m_downloadsMutex);

    // Count active downloads
    size_t activeCount = 0;
    for (const auto &pair : m_activeDownloads) {
      if (pair.second.state == DownloadState::Downloading ||
          pair.second.state == DownloadState::Pending) {
        activeCount++;
      }
    }

    // If at capacity, only allow high priority downloads
    if (activeCount >= MAX_CONCURRENT_DOWNLOADS && priority < 8) {
      TDLOG("DownloadFile: at capacity (%zu downloads), skipping low priority "
            "fileId=%d",
            activeCount, fileId);
      return;
    }

    auto it = m_activeDownloads.find(fileId);
    if (it != m_activeDownloads.end()) {
      // Already downloading or completed - don't start again
      if (it->second.state == DownloadState::Downloading) {
        TDLOG("DownloadFile: fileId=%d already downloading, skipping", fileId);
        return;
      }
      if (it->second.state == DownloadState::Completed) {
        TDLOG("DownloadFile: fileId=%d already completed, skipping", fileId);
        return;
      }
      // If pending, don't add again
      if (it->second.state == DownloadState::Pending) {
        TDLOG("DownloadFile: fileId=%d already pending, skipping", fileId);
        return;
      }
      // If failed, allow retry
      TDLOG("DownloadFile: fileId=%d was in state %d, allowing retry", fileId,
            static_cast<int>(it->second.state));
    }

    // Clean up old completed/failed downloads to prevent memory growth
    if (m_activeDownloads.size() > 100) {
      std::vector<int32_t> toRemove;
      for (const auto &pair : m_activeDownloads) {
        if (pair.second.state == DownloadState::Completed ||
            pair.second.state == DownloadState::Cancelled) {
          toRemove.push_back(pair.first);
        }
      }
      for (int32_t id : toRemove) {
        m_activeDownloads.erase(id);
      }
      TDLOG("DownloadFile: cleaned up %zu old downloads", toRemove.size());
    }

    // Track this download
    DownloadInfo info(fileId, priority);
    info.state = DownloadState::Pending;
    info.totalSize = fileSize;
    m_activeDownloads[fileId] = info;
    TDLOG("DownloadFile: tracking fileId=%d, total active downloads=%zu",
          fileId, m_activeDownloads.size());
  }

  // REACTIVE MVC: Add to started downloads queue for UI to poll
  {
    std::lock_guard<std::mutex> lock(m_startedDownloadsMutex);
    FileDownloadStarted started;
    started.fileId = fileId;
    started.fileName =
        fileName.IsEmpty() ? wxString::Format("File %d", fileId) : fileName;
    started.totalSize = fileSize;
    m_startedDownloads.push_back(started);
  }
  SetDirty(DirtyFlag::Downloads);

  StartDownloadInternal(fileId, priority);
}

void TelegramClient::StartDownloadInternal(int32_t fileId, int priority) {
  TDLOG("StartDownloadInternal: sending downloadFile request for fileId=%d "
        "priority=%d",
        fileId, priority);

  auto request = td_api::make_object<td_api::downloadFile>();
  request->file_id_ = fileId;
  request->priority_ = priority;
  request->synchronous_ = false;

  // Add response handler to catch errors
  Send(std::move(request),
       [this, fileId](td_api::object_ptr<td_api::Object> response) {
         if (!response) {
           OnDownloadError(fileId, "No response from TDLib");
           return;
         }

         if (response->get_id() == td_api::error::ID) {
           auto &error = static_cast<td_api::error &>(*response);
           wxString errorMsg =
               wxString::Format("Download error %d: %s", error.code_,
                                wxString::FromUTF8(error.message_));
           TDLOG("StartDownloadInternal: TDLib error for fileId=%d: %s", fileId,
                 errorMsg.ToStdString().c_str());
           OnDownloadError(fileId, errorMsg);
         } else if (response->get_id() == td_api::file::ID) {
           // Download started successfully - the file object will be returned
           auto &file = static_cast<td_api::file &>(*response);
           TDLOG("StartDownloadInternal: TDLib accepted download for fileId=%d",
                 fileId);
           {
             std::lock_guard<std::mutex> lock(m_downloadsMutex);
             auto it = m_activeDownloads.find(fileId);
             if (it != m_activeDownloads.end()) {
               it->second.state = DownloadState::Downloading;
               it->second.lastProgressTime = wxGetUTCTime();
               if (file.size_ > 0) {
                 it->second.totalSize = file.size_;
               } else if (file.expected_size_ > 0) {
                 it->second.totalSize = file.expected_size_;
               }
             }
           }
         }
       });
}

void TelegramClient::RetryDownload(int32_t fileId) {
  if (fileId == 0)
    return;

  int priority = 1;
  {
    std::lock_guard<std::mutex> lock(m_downloadsMutex);
    auto it = m_activeDownloads.find(fileId);
    if (it == m_activeDownloads.end()) {
      return; // No such download to retry
    }

    if (!it->second.CanRetry()) {
      // Max retries exceeded - UI will see Failed state when it polls
      return;
    }

    it->second.retryCount++;
    it->second.state = DownloadState::Pending;
    it->second.lastProgressTime = wxGetUTCTime();
    priority = it->second.priority;

    TDLOG("Retrying download for file %d (attempt %d/%d)", fileId,
          it->second.retryCount, DownloadInfo::MAX_RETRIES);
  }

  // REACTIVE MVC: Set dirty flag - UI will poll download state
  SetDirty(DirtyFlag::Downloads);

  StartDownloadInternal(fileId, priority);
}

bool TelegramClient::IsDownloading(int32_t fileId) const {
  std::lock_guard<std::mutex> lock(m_downloadsMutex);
  auto it = m_activeDownloads.find(fileId);
  if (it == m_activeDownloads.end())
    return false;
  return it->second.state == DownloadState::Pending ||
         it->second.state == DownloadState::Downloading;
}

DownloadState TelegramClient::GetDownloadState(int32_t fileId) const {
  std::lock_guard<std::mutex> lock(m_downloadsMutex);
  auto it = m_activeDownloads.find(fileId);
  if (it == m_activeDownloads.end())
    return DownloadState::Pending;
  return it->second.state;
}

int TelegramClient::GetDownloadProgress(int32_t fileId) const {
  std::lock_guard<std::mutex> lock(m_downloadsMutex);
  auto it = m_activeDownloads.find(fileId);
  if (it == m_activeDownloads.end())
    return -1;

  if (it->second.state == DownloadState::Completed)
    return 100;
  if (it->second.state != DownloadState::Downloading)
    return -1;

  if (it->second.totalSize <= 0)
    return 0;
  return static_cast<int>((it->second.downloadedSize * 100) /
                          it->second.totalSize);
}

void TelegramClient::BoostDownloadPriority(int32_t fileId) {
  if (fileId == 0)
    return;

  TDLOG("BoostDownloadPriority: boosting fileId=%d to max priority", fileId);

  // Check download state
  {
    std::lock_guard<std::mutex> lock(m_downloadsMutex);
    auto it = m_activeDownloads.find(fileId);
    if (it != m_activeDownloads.end()) {
      if (it->second.state == DownloadState::Completed) {
        TDLOG("BoostDownloadPriority: fileId=%d already completed", fileId);
        return; // Already done
      }

      // Check if download is stuck (Pending for more than 10 seconds or no
      // progress for 30s)
      int64_t now = wxGetUTCTime();
      int64_t elapsed = now - it->second.startTime;
      int64_t lastProgress = now - it->second.lastProgressTime;

      if (it->second.state == DownloadState::Pending && elapsed > 10) {
        TDLOG("BoostDownloadPriority: fileId=%d stuck in Pending for %lld "
              "seconds, restarting",
              fileId, elapsed);
        it->second.startTime = now;
        it->second.lastProgressTime = now;
      } else if (it->second.state == DownloadState::Downloading &&
                 lastProgress > 30) {
        TDLOG("BoostDownloadPriority: fileId=%d no progress for %lld seconds, "
              "restarting",
              fileId, lastProgress);
        it->second.lastProgressTime = now;
      }
    } else {
      // Not tracked at all - start fresh
      TDLOG("BoostDownloadPriority: fileId=%d not tracked, starting download",
            fileId);
    }
  }

  // Send priority boost request to TDLib (priority 32 is max)
  // This also restarts stuck downloads
  auto request = td_api::make_object<td_api::downloadFile>();
  request->file_id_ = fileId;
  request->priority_ = 32; // Maximum priority
  request->synchronous_ = false;

  Send(std::move(request), [this, fileId](
                               td_api::object_ptr<td_api::Object> response) {
    if (!response) {
      TDLOG("BoostDownloadPriority: no response for fileId=%d", fileId);
      return;
    }

    if (response->get_id() == td_api::error::ID) {
      auto &err = static_cast<td_api::error &>(*response);
      (void)err; // Used only when TDLOG is enabled
      TDLOG("BoostDownloadPriority: error for fileId=%d: %s", fileId,
            err.message_.c_str());
    } else if (response->get_id() == td_api::file::ID) {
      auto &file = static_cast<td_api::file &>(*response);
      TDLOG("BoostDownloadPriority: TDLib accepted boost for fileId=%d, "
            "is_downloading=%d, is_completed=%d",
            fileId, file.local_ ? file.local_->is_downloading_active_ : false,
            file.local_ ? file.local_->is_downloading_completed_ : false);

      // If file is already complete, handle it now
      if (file.local_ && file.local_->is_downloading_completed_ &&
          !file.local_->path_.empty()) {
        wxString localPath = wxString::FromUTF8(file.local_->path_);
        {
          std::lock_guard<std::mutex> lock(m_downloadsMutex);
          auto it = m_activeDownloads.find(fileId);
          if (it != m_activeDownloads.end()) {
            it->second.state = DownloadState::Completed;
            it->second.localPath = localPath;
          }
        }
        // Add to completed queue
        {
          std::lock_guard<std::mutex> lock(m_completedDownloadsMutex);
          FileDownloadResult result;
          result.fileId = fileId;
          result.localPath = localPath;
          result.success = true;
          m_completedDownloads.push_back(result);
        }
        SetDirty(DirtyFlag::Downloads);
      } else if (file.local_ && file.local_->is_downloading_active_) {
        // Update state to Downloading
        std::lock_guard<std::mutex> lock(m_downloadsMutex);
        auto it = m_activeDownloads.find(fileId);
        if (it != m_activeDownloads.end()) {
          it->second.state = DownloadState::Downloading;
          it->second.lastProgressTime = wxGetUTCTime();
        }
      }
    }
  });
}

bool TelegramClient::ShouldAutoDownloadMedia(MediaType type,
                                             int64_t fileSize) const {
  // Size limits for auto-download (in bytes)
  static const int64_t MAX_PHOTO_SIZE = 10 * 1024 * 1024;  // 10 MB
  static const int64_t MAX_STICKER_SIZE = 2 * 1024 * 1024; // 2 MB
  static const int64_t MAX_GIF_SIZE = 15 * 1024 * 1024;    // 15 MB
  static const int64_t MAX_VOICE_SIZE = 5 * 1024 * 1024;   // 5 MB
  static const int64_t MAX_VIDEO_NOTE_SIZE =
      20 * 1024 * 1024; // 20 MB (video notes are small)
  static const int64_t MAX_VIDEO_SIZE = 50 * 1024 * 1024; // 50 MB videos

  switch (type) {
  case MediaType::Photo:
    return fileSize <= MAX_PHOTO_SIZE;
  case MediaType::Sticker:
    return fileSize <= MAX_STICKER_SIZE;
  case MediaType::GIF:
    return fileSize <= MAX_GIF_SIZE;
  case MediaType::Voice:
    return fileSize <= MAX_VOICE_SIZE;
  case MediaType::VideoNote:
    return fileSize <= MAX_VIDEO_NOTE_SIZE;
  case MediaType::Video:
    return fileSize <= MAX_VIDEO_SIZE; // Don't auto-download
  case MediaType::File:
  case MediaType::Reaction:
  default:
    return false; // Don't auto-download documents/files
  }
}

void TelegramClient::DownloadMediaFromMessage(const MessageInfo &msg,
                                              int basePriority) {
  // Download thumbnails first (higher priority)
  if (msg.mediaThumbnailFileId != 0 && msg.mediaThumbnailPath.IsEmpty()) {
    DownloadFile(msg.mediaThumbnailFileId, basePriority + 3, "Thumbnail", 0);
  }

  // Determine media type and size
  MediaType type = MediaType::Photo; // Default
  int64_t fileSize = msg.mediaFileSize;

  if (msg.hasPhoto)
    type = MediaType::Photo;
  else if (msg.hasVideo)
    type = MediaType::Video;
  else if (msg.hasVideoNote)
    type = MediaType::VideoNote;
  else if (msg.hasSticker)
    type = MediaType::Sticker;
  else if (msg.hasAnimation)
    type = MediaType::GIF;
  else if (msg.hasVoice)
    type = MediaType::Voice;
  else if (msg.hasDocument)
    type = MediaType::File;

  // Download main file if within auto-download limits
  if (msg.mediaFileId != 0 && msg.mediaLocalPath.IsEmpty()) {
    if (ShouldAutoDownloadMedia(type, fileSize)) {
      DownloadFile(msg.mediaFileId, basePriority,
                   msg.mediaFileName.IsEmpty() ? "Media" : msg.mediaFileName,
                   fileSize);
    }
  }
}

void TelegramClient::AutoDownloadChatMedia(int64_t chatId, int messageLimit) {
  TDLOG("AutoDownloadChatMedia: starting for chatId=%lld limit=%d", chatId,
        messageLimit);

  // Get cached messages for this chat
  std::vector<MessageInfo> messages;
  {
    std::shared_lock<std::shared_mutex> lock(m_dataMutex);
    auto it = m_messages.find(chatId);
    if (it != m_messages.end()) {
      // Get the most recent messages (up to limit)
      size_t count =
          std::min(static_cast<size_t>(messageLimit), it->second.size());
      if (count > 0) {
        auto start = it->second.end() - count;
        messages.assign(start, it->second.end());
      }
    }
  }

  if (messages.empty()) {
    TDLOG("AutoDownloadChatMedia: no messages cached for chatId=%lld", chatId);
    return;
  }

  TDLOG("AutoDownloadChatMedia: processing %zu messages", messages.size());

  // Process messages from newest to oldest (reverse order)
  // Use low priority (1-5) for background downloads to not compete with user
  // requests
  int priority = 5; // Low priority for background downloads

  for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
    // Check if client is shutting down - exit early
    if (!m_running) {
      TDLOG("AutoDownloadChatMedia: client shutting down, stopping early");
      return;
    }

    const MessageInfo &msg = *it;

    // Check if this message has any media
    if (msg.hasPhoto || msg.hasVideo || msg.hasVideoNote || msg.hasSticker ||
        msg.hasAnimation || msg.hasVoice) {
      DownloadMediaFromMessage(msg, priority);
    }

    // Decrease priority for older messages (minimum 1)
    if (priority > 1) {
      priority--;
    }
  }

  TDLOG("AutoDownloadChatMedia: finished for chatId=%lld", chatId);
}

void TelegramClient::OnDownloadError(int32_t fileId, const wxString &error) {
  TDLOG("Download error for file %d: %s", fileId, error.ToStdString().c_str());

  bool shouldRetry = false;
  int retryCount = 0;
  {
    std::lock_guard<std::mutex> lock(m_downloadsMutex);
    auto it = m_activeDownloads.find(fileId);
    if (it != m_activeDownloads.end()) {
      it->second.state = DownloadState::Failed;
      it->second.errorMessage = error;
      shouldRetry = it->second.CanRetry();
      retryCount = it->second.retryCount;
    }
  }

  // REACTIVE MVC: Add to completed downloads queue with error
  {
    std::lock_guard<std::mutex> lock(m_completedDownloadsMutex);
    FileDownloadResult result;
    result.fileId = fileId;
    result.success = false;
    result.error = error;
    m_completedDownloads.push_back(result);
  }
  SetDirty(DirtyFlag::Downloads);

  if (shouldRetry) {
    // Schedule retry after a delay using CallAfter with a one-shot timer
    // Exponential backoff: 500ms, 1000ms, 2000ms based on retry count
    int delayMs = 500 * (1 << retryCount); // 500, 1000, 2000, etc.
    delayMs = std::min(delayMs, 5000);     // Cap at 5 seconds

    // Use CallAfter to schedule the timer on main thread without blocking
    wxTheApp->CallAfter([this, fileId, delayMs]() {
      // Create a one-shot timer for the retry
      wxTimer *retryTimer = new wxTimer();
      retryTimer->Bind(wxEVT_TIMER, [this, fileId, retryTimer](wxTimerEvent &) {
        RetryDownload(fileId);
        retryTimer->Stop();
        delete retryTimer;
      });
      retryTimer->StartOnce(delayMs);
    });
  }
}

void TelegramClient::CheckDownloadTimeouts() {
  std::vector<int32_t> timedOutFiles;

  {
    std::lock_guard<std::mutex> lock(m_downloadsMutex);
    int activeCount = 0;
    for (auto &pair : m_activeDownloads) {
      if (pair.second.state == DownloadState::Downloading) {
        activeCount++;
        if (pair.second.IsTimedOut()) {
          timedOutFiles.push_back(pair.first);
        }
      }
    }
    if (activeCount > 0) {
      TDLOG("CheckDownloadTimeouts: %d active downloads, %zu timed out",
            activeCount, timedOutFiles.size());
    }
  }

  for (int32_t fileId : timedOutFiles) {
    TDLOG("Download timeout for file %d, retrying...", fileId);
    OnDownloadError(fileId, "Download timed out - no progress");
  }
}

void TelegramClient::OnDownloadTimeoutTimer(wxTimerEvent &event) {
  CheckDownloadTimeouts();
}

void TelegramClient::CancelDownload(int32_t fileId) {
  {
    std::lock_guard<std::mutex> lock(m_downloadsMutex);
    auto it = m_activeDownloads.find(fileId);
    if (it != m_activeDownloads.end()) {
      it->second.state = DownloadState::Cancelled;
    }
  }

  auto request = td_api::make_object<td_api::cancelDownloadFile>();
  request->file_id_ = fileId;
  request->only_if_pending_ = false;

  Send(std::move(request), nullptr);
}

UserInfo TelegramClient::GetUser(int64_t userId, bool *found) const {
  std::shared_lock<std::shared_mutex> lock(m_dataMutex);
  auto it = m_users.find(userId);
  if (it != m_users.end()) {
    if (found)
      *found = true;
    return it->second;
  }
  if (found)
    *found = false;
  return UserInfo();
}

wxString TelegramClient::GetUserDisplayName(int64_t userId) const {
  bool found = false;
  UserInfo user = GetUser(userId, &found);
  if (found) {
    return user.GetDisplayName();
  }
  return wxString::Format("User %lld", userId);
}

void TelegramClient::LoadChatMembers(int64_t chatId, int limit) {
  if (chatId == 0)
    return;

  bool found = false;
  ChatInfo chat = GetChat(chatId, &found);
  if (!found) {
    TDLOG("LoadChatMembers: chat not found");
    return;
  }

  // For private chats, just return the two participants
  if (chat.isPrivate || chat.isBot) {
    std::vector<UserInfo> members;

    // Add current user
    members.push_back(m_currentUser);

    // Add the other user
    if (chat.userId != 0) {
      bool userFound = false;
      UserInfo otherUser = GetUser(chat.userId, &userFound);
      if (userFound) {
        members.push_back(otherUser);
      }
    }

    PostToMainThread([this, chatId, members]() {
      if (m_mainFrame) {
        m_mainFrame->OnMembersLoaded(chatId, members);
      }
    });
    return;
  }

  // For supergroups and channels
  if (chat.isSupergroup || chat.isChannel) {
    auto request = td_api::make_object<td_api::getSupergroupMembers>();
    request->supergroup_id_ = chat.supergroupId;
    request->filter_ =
        td_api::make_object<td_api::supergroupMembersFilterRecent>();
    request->offset_ = 0;
    request->limit_ = limit;

    Send(std::move(request), [this, chatId](
                                 td_api::object_ptr<td_api::Object> result) {
      if (!result) {
        TDLOG("LoadChatMembers: null result for supergroup");
        return;
      }

      if (result->get_id() == td_api::error::ID) {
        auto error = td_api::move_object_as<td_api::error>(result);
        TDLOG("LoadChatMembers error: %s", error->message_.c_str());
        return;
      }

      if (result->get_id() != td_api::chatMembers::ID) {
        TDLOG("LoadChatMembers: unexpected result type");
        return;
      }

      auto chatMembers = td_api::move_object_as<td_api::chatMembers>(result);
      std::vector<UserInfo> members;

      for (auto &member : chatMembers->members_) {
        if (!member)
          continue;

        // Get user ID from member
        int64_t memberId = 0;
        if (member->member_id_->get_id() == td_api::messageSenderUser::ID) {
          auto sender = static_cast<td_api::messageSenderUser *>(
              member->member_id_.get());
          memberId = sender->user_id_;
        }

        if (memberId != 0) {
          bool userFound = false;
          UserInfo user = GetUser(memberId, &userFound);
          if (userFound) {
            members.push_back(user);
          }
        }
      }

      PostToMainThread([this, chatId, members]() {
        if (m_mainFrame) {
          m_mainFrame->OnMembersLoaded(chatId, members);
        }
      });
    });
    return;
  }

  // For basic groups
  if (chat.isGroup && chat.basicGroupId != 0) {
    auto request = td_api::make_object<td_api::getBasicGroupFullInfo>();
    request->basic_group_id_ = chat.basicGroupId;

    Send(std::move(request), [this, chatId](
                                 td_api::object_ptr<td_api::Object> result) {
      if (!result) {
        TDLOG("LoadChatMembers: null result for basic group");
        return;
      }

      if (result->get_id() == td_api::error::ID) {
        auto error = td_api::move_object_as<td_api::error>(result);
        TDLOG("LoadChatMembers error: %s", error->message_.c_str());
        return;
      }

      if (result->get_id() != td_api::basicGroupFullInfo::ID) {
        TDLOG("LoadChatMembers: unexpected result type for basic group");
        return;
      }

      auto groupInfo =
          td_api::move_object_as<td_api::basicGroupFullInfo>(result);
      std::vector<UserInfo> members;

      for (auto &member : groupInfo->members_) {
        if (!member)
          continue;

        int64_t memberId = 0;
        if (member->member_id_->get_id() == td_api::messageSenderUser::ID) {
          auto sender = static_cast<td_api::messageSenderUser *>(
              member->member_id_.get());
          memberId = sender->user_id_;
        }

        if (memberId != 0) {
          bool userFound = false;
          UserInfo user = GetUser(memberId, &userFound);
          if (userFound) {
            members.push_back(user);
          }
        }
      }

      PostToMainThread([this, chatId, members]() {
        if (m_mainFrame) {
          m_mainFrame->OnMembersLoaded(chatId, members);
        }
      });
    });
    return;
  }

  TDLOG("LoadChatMembers: unknown chat type");
}

void TelegramClient::MarkChatAsRead(int64_t chatId) {
  // Privacy setting check
  if (!m_sendReadReceipts) {
    TDLOG(
        "MarkChatAsRead: sendReadReceipts is disabled, skipping viewMessages");
    return;
  }

  bool found = false;
  ChatInfo chat = GetChat(chatId, &found);
  if (!found) {
    return;
  }

  // Get all message IDs from the cache
  std::vector<int64_t> messageIds;
  {
    std::shared_lock<std::shared_mutex> lock(m_dataMutex);
    auto &messages = m_messages[chatId];
    for (const auto &msg : messages) {
      if (msg.id > 0) {
        messageIds.push_back(msg.id);
      }
    }
  }

  if (!messageIds.empty()) {
    TDLOG("MarkChatAsRead: chatId=%lld, marking %zu messages as read",
          (long long)chatId, messageIds.size());

    auto request = td_api::make_object<td_api::viewMessages>();
    request->chat_id_ = chatId;
    request->message_ids_ = std::move(messageIds);
    request->force_read_ = true;

    Send(std::move(request),
         [this, chatId](td_api::object_ptr<td_api::Object> result) {
           if (result && result->get_id() == td_api::ok::ID) {
             TDLOG("MarkChatAsRead: successfully marked messages as read for "
                   "chatId=%lld",
                   (long long)chatId);
             // Update local state
             {
               std::unique_lock<std::shared_mutex> lock(m_dataMutex);
               auto it = m_chats.find(chatId);
               if (it != m_chats.end()) {
                 it->second.unreadCount = 0;
                 // Update lastReadInboxMessageId to the newest message
                 auto &msgs = m_messages[chatId];
                 if (!msgs.empty()) {
                   int64_t lastId = 0;
                   for (const auto &m : msgs) {
                     if (m.id > lastId)
                       lastId = m.id;
                   }
                   it->second.lastReadInboxMessageId = lastId;
                 }
               }
             }
           } else if (result && result->get_id() == td_api::error::ID) {
             auto error = td_api::move_object_as<td_api::error>(result);
             TDLOG("MarkChatAsRead: ERROR %d - %s", error->code_,
                   error->message_.c_str());
           }
         });
  }
}

void TelegramClient::OnNewMessage(
    td_api::object_ptr<td_api::message> &message) {
  if (!message)
    return;

  MessageInfo msgInfo = ConvertMessage(message.get());

  // Add to messages cache
  {
    std::unique_lock<std::shared_mutex> lock(m_dataMutex);
    m_messages[msgInfo.chatId].push_back(msgInfo);
  }

  // LAZY LOADING: Only download thumbnails for current chat
  // Full media is downloaded on-demand when user interacts
  if (msgInfo.chatId == m_currentChatId) {
    // Only download thumbnail if available (small, fast)
    if (msgInfo.mediaThumbnailFileId != 0) {
      DownloadFile(msgInfo.mediaThumbnailFileId, 10, "Thumbnail", 0);
    }
    // For stickers without thumbnails, download the sticker (usually small)
    if (msgInfo.hasSticker && msgInfo.mediaFileId != 0 &&
        msgInfo.mediaThumbnailFileId == 0) {
      DownloadFile(msgInfo.mediaFileId, 12, "Sticker", msgInfo.mediaFileSize);
    }
  }

  // REACTIVE MVC: Add to new messages queue instead of posting callback
  {
    std::lock_guard<std::mutex> lock(m_newMessagesMutex);
    m_newMessages[msgInfo.chatId].push_back(msgInfo);
  }
  SetDirty(DirtyFlag::Messages);
}

void TelegramClient::OnMessageEdited(
    int64_t chatId, int64_t messageId,
    td_api::object_ptr<td_api::MessageContent> &content) {
  wxString newText = ExtractMessageText(content.get());
  wxString senderName;

  // Update in cache and get sender name
  {
    std::unique_lock<std::shared_mutex> lock(m_dataMutex);
    auto &messages = m_messages[chatId];
    for (auto &msg : messages) {
      if (msg.id == messageId) {
        msg.text = newText;
        msg.isEdited = true;
        senderName = msg.senderName;
        break;
      }
    }
  }

  // REACTIVE MVC: Add to updated messages queue
  {
    std::lock_guard<std::mutex> lock(m_updatedMessagesMutex);
    MessageInfo updatedMsg;
    updatedMsg.chatId = chatId;
    updatedMsg.id = messageId;
    updatedMsg.text = newText;
    updatedMsg.senderName = senderName;
    updatedMsg.isEdited = true;
    m_updatedMessages[chatId].push_back(updatedMsg);
  }
  SetDirty(DirtyFlag::Messages);
}

void TelegramClient::OnChatUpdate(td_api::object_ptr<td_api::chat> &chat) {
  if (!chat)
    return;

  ChatInfo info;
  info.id = chat->id_;
  info.title = wxString::FromUTF8(chat->title_);
  info.unreadCount = chat->unread_count_;
  info.lastReadInboxMessageId = chat->last_read_inbox_message_id_;
  info.lastReadOutboxMessageId = chat->last_read_outbox_message_id_;

  // Parse positions
  for (auto &pos : chat->positions_) {
    if (pos->list_->get_id() == td_api::chatListMain::ID) {
      info.isPinned = pos->is_pinned_;
      info.order = pos->order_;
      break;
    }
  }

  // Parse chat type
  if (chat->type_) {
    td_api::downcast_call(*chat->type_, [&info](auto &t) {
      using T = std::decay_t<decltype(t)>;
      if constexpr (std::is_same_v<T, td_api::chatTypePrivate>) {
        info.isPrivate = true;
        info.userId = t.user_id_;
      } else if constexpr (std::is_same_v<T, td_api::chatTypeBasicGroup>) {
        info.isGroup = true;
        info.basicGroupId = t.basic_group_id_;
      } else if constexpr (std::is_same_v<T, td_api::chatTypeSupergroup>) {
        info.isSupergroup = !t.is_channel_;
        info.isChannel = t.is_channel_;
        info.supergroupId = t.supergroup_id_;
      } else if constexpr (std::is_same_v<T, td_api::chatTypeSecret>) {
        info.isPrivate = true;
        info.userId = t.user_id_;
      }
    });
  }

  // Parse last message
  if (chat->last_message_) {
    info.lastMessage = ExtractMessageText(chat->last_message_->content_.get());
    info.lastMessageDate = chat->last_message_->date_;
  }

  // Check if it's a bot (for private chats)
  if (info.isPrivate && info.userId > 0) {
    auto userIt = m_users.find(info.userId);
    if (userIt != m_users.end()) {
      info.isBot = userIt->second.isBot;
    }
  }

  {
    std::unique_lock<std::shared_mutex> lock(m_dataMutex);
    m_chats[info.id] = info;
  }

  // REACTIVE MVC: Set dirty flag instead of posting callback
  SetDirty(DirtyFlag::ChatList);
}

void TelegramClient::OnUserUpdate(td_api::object_ptr<td_api::user> &user) {
  if (!user)
    return;

  UserInfo info;
  info.id = user->id_;
  info.firstName = wxString::FromUTF8(user->first_name_);
  info.lastName = wxString::FromUTF8(user->last_name_);
  // New API uses usernames_ object
  if (user->usernames_) {
    info.username = wxString::FromUTF8(user->usernames_->editable_username_);
  }
  info.phoneNumber = wxString::FromUTF8(user->phone_number_);
  info.isBot = user->type_->get_id() == td_api::userTypeBot::ID;
  // New API uses verification_status_ object instead of is_verified_
  info.isVerified = user->verification_status_ != nullptr;

  // Parse online status
  if (user->status_) {
    td_api::downcast_call(*user->status_, [&info](auto &s) {
      using T = std::decay_t<decltype(s)>;
      if constexpr (std::is_same_v<T, td_api::userStatusOnline>) {
        info.isOnline = true;
        info.onlineExpires = s.expires_;
      } else if constexpr (std::is_same_v<T, td_api::userStatusOffline>) {
        info.isOnline = false;
        info.lastSeenTime = s.was_online_;
        info.onlineExpires = 0;
      } else {
        info.isOnline = false;
        info.onlineExpires = 0;
      }
    });
  }

  {
    std::unique_lock<std::shared_mutex> lock(m_dataMutex);
    m_users[info.id] = info;

    // Update chat info if this user has a private chat
    for (auto &[chatId, chat] : m_chats) {
      if (chat.isPrivate && chat.userId == info.id) {
        chat.isBot = info.isBot;
        chat.title = info.GetDisplayName();
      }
    }
  }
}

void TelegramClient::OnUserStatusUpdate(
    int64_t userId, td_api::object_ptr<td_api::UserStatus> &status) {
  // Guard against invalid inputs
  if (!status || userId == 0)
    return;

  bool isOnline = false;
  int64_t lastSeenTime = 0;
  int64_t onlineExpires = 0;

  try {
    td_api::downcast_call(
        *status, [&isOnline, &lastSeenTime, &onlineExpires](auto &s) {
          using T = std::decay_t<decltype(s)>;
          if constexpr (std::is_same_v<T, td_api::userStatusOnline>) {
            isOnline = true;
            onlineExpires = s.expires_; // When this online status expires
          } else if constexpr (std::is_same_v<T, td_api::userStatusOffline>) {
            isOnline = false;
            lastSeenTime = s.was_online_;
            onlineExpires = 0;
          } else if constexpr (std::is_same_v<T, td_api::userStatusRecently>) {
            isOnline = false;
            lastSeenTime = 0; // Will show "last seen recently"
            onlineExpires = 0;
          } else if constexpr (std::is_same_v<T, td_api::userStatusLastWeek>) {
            isOnline = false;
            // Approximate to 7 days ago
            lastSeenTime =
                static_cast<int64_t>(std::time(nullptr)) - (7 * 24 * 60 * 60);
            onlineExpires = 0;
          } else if constexpr (std::is_same_v<T, td_api::userStatusLastMonth>) {
            isOnline = false;
            // Approximate to 30 days ago
            lastSeenTime =
                static_cast<int64_t>(std::time(nullptr)) - (30 * 24 * 60 * 60);
            onlineExpires = 0;
          } else {
            isOnline = false;
            onlineExpires = 0;
          }
        });
  } catch (...) {
    // If downcast fails, assume offline with unknown last seen
    isOnline = false;
    lastSeenTime = 0;
  }

  // Update cached user info
  {
    std::unique_lock<std::shared_mutex> lock(m_dataMutex);
    auto it = m_users.find(userId);
    if (it != m_users.end()) {
      it->second.isOnline = isOnline;
      it->second.onlineExpires = onlineExpires;
      if (lastSeenTime > 0) {
        it->second.lastSeenTime = lastSeenTime;
      }
    }
  }

  // REACTIVE MVC: Set dirty flag and notify UI immediately for responsive
  // online indicators
  SetDirty(DirtyFlag::UserStatus);
  NotifyUIRefresh();
}

void TelegramClient::OnFileUpdate(td_api::object_ptr<td_api::file> &file) {
  if (!file)
    return;

  // Guard against null local info
  if (!file->local_)
    return;

  int32_t fileId = file->id_;
  if (fileId == 0)
    return; // Invalid file ID

  bool isDownloading = file->local_->is_downloading_active_;
  bool isComplete = file->local_->is_downloading_completed_;
  wxString localPath = wxString::FromUTF8(file->local_->path_);
  int64_t downloadedSize = file->local_->downloaded_size_;

  int64_t totalSize = file->size_ > 0 ? file->size_ : file->expected_size_;

  // Update our download tracking
  {
    std::lock_guard<std::mutex> lock(m_downloadsMutex);
    auto it = m_activeDownloads.find(fileId);
    if (it != m_activeDownloads.end()) {
      if (isComplete) {
        it->second.state = DownloadState::Completed;
        it->second.localPath = localPath;
        it->second.downloadedSize = downloadedSize;
        TDLOG("OnFileUpdate: Download COMPLETED for fileId=%d path=%s", fileId,
              localPath.ToStdString().c_str());
      } else if (isDownloading) {
        it->second.state = DownloadState::Downloading;
        it->second.downloadedSize = downloadedSize;
        it->second.totalSize = totalSize;
        // Update progress time to prevent false timeout
        it->second.lastProgressTime = wxGetUTCTime();
      }
    } else {
      // File update for a file we're not tracking - could be auto-download
      if (isComplete) {
        TDLOG("OnFileUpdate: Untracked file COMPLETED fileId=%d path=%s",
              fileId, localPath.ToStdString().c_str());
      }
    }
  }

  // REACTIVE MVC: Add to queues instead of posting callbacks
  // UI will poll these when it refreshes
  if (isComplete && !localPath.IsEmpty()) {
    // Add to completed downloads queue
    {
      std::lock_guard<std::mutex> lock(m_completedDownloadsMutex);
      FileDownloadResult result;
      result.fileId = fileId;
      result.localPath = localPath;
      result.success = true;
      m_completedDownloads.push_back(result);
    }
    SetDirty(DirtyFlag::Downloads);
  } else if (isDownloading) {
    // Throttle progress updates to reduce UI refresh frequency
    // Only update if progress changed significantly (every 2% or 100KB)
    bool shouldUpdate = false;
    {
      std::lock_guard<std::mutex> lock(m_downloadsMutex);
      auto it = m_activeDownloads.find(fileId);
      if (it != m_activeDownloads.end()) {
        int64_t lastReported = it->second.downloadedSize;
        int64_t delta = downloadedSize - lastReported;

        // Update if: first update, or 2%+ change, or 100KB+ change
        if (lastReported == 0 || delta < 0) {
          shouldUpdate = true;
        } else if (totalSize > 0) {
          double percentChange = (delta * 100.0) / totalSize;
          shouldUpdate = (percentChange >= 2.0) || (delta >= 102400);
        } else {
          shouldUpdate = (delta >= 102400); // 100KB for unknown size
        }
      } else {
        shouldUpdate = true; // Not tracking, always update
      }
    }

    if (shouldUpdate) {
      // Add to progress updates with actual byte counts for status bar
      {
        std::lock_guard<std::mutex> lock(m_downloadProgressMutex);
        FileDownloadProgress progress;
        progress.fileId = fileId;
        progress.downloadedSize = downloadedSize;
        progress.totalSize = totalSize;
        m_downloadProgressUpdates.push_back(progress);
      }
      SetDirty(DirtyFlag::Downloads);
    }
  }
}

void TelegramClient::OnChatLastMessage(
    int64_t chatId, td_api::object_ptr<td_api::message> &message) {
  std::unique_lock<std::shared_mutex> lock(m_dataMutex);
  auto it = m_chats.find(chatId);
  if (it == m_chats.end())
    return;

  if (message) {
    it->second.lastMessage = ExtractMessageText(message->content_.get());
    it->second.lastMessageDate = message->date_;
  } else {
    it->second.lastMessage.Clear();
    it->second.lastMessageDate = 0;
  }
}

void TelegramClient::OnChatReadInbox(int64_t chatId,
                                     int64_t lastReadInboxMessageId,
                                     int32_t unreadCount) {
  {
    std::unique_lock<std::shared_mutex> lock(m_dataMutex);
    auto it = m_chats.find(chatId);
    if (it != m_chats.end()) {
      it->second.lastReadInboxMessageId = lastReadInboxMessageId;
      it->second.unreadCount = unreadCount;
    }
  }
  // REACTIVE MVC: Set dirty flag instead of posting callback
  SetDirty(DirtyFlag::ChatList);
}

void TelegramClient::OnChatReadOutbox(int64_t chatId, int64_t maxMessageId) {
  {
    std::unique_lock<std::shared_mutex> lock(m_dataMutex);
    auto it = m_chats.find(chatId);
    if (it != m_chats.end()) {
      it->second.lastReadOutboxMessageId = maxMessageId;
      it->second.lastReadOutboxTime =
          std::time(nullptr); // Record when we learned it was read
    }
  }
  // Set dirty flag and trigger immediate UI update
  SetDirty(DirtyFlag::Messages);
  NotifyUIRefresh();
}

void TelegramClient::OnChatPosition(
    int64_t chatId, td_api::object_ptr<td_api::chatPosition> &position) {
  std::unique_lock<std::shared_mutex> lock(m_dataMutex);
  auto it = m_chats.find(chatId);
  if (it == m_chats.end())
    return;

  if (position && position->list_->get_id() == td_api::chatListMain::ID) {
    it->second.isPinned = position->is_pinned_;
    it->second.order = position->order_;
  }
}

MessageInfo TelegramClient::ConvertMessage(td_api::message *msg) {
  MessageInfo info;
  if (!msg)
    return info;

  info.id = msg->id_;
  info.chatId = msg->chat_id_;
  info.date = msg->date_;
  info.editDate = msg->edit_date_;
  info.isOutgoing = msg->is_outgoing_;
  info.isEdited = msg->edit_date_ > 0;

  // New API uses reply_to_ object with MessageReplyTo type
  if (msg->reply_to_) {
    td_api::downcast_call(*msg->reply_to_, [this, &info, msg](auto &r) {
      using T = std::decay_t<decltype(r)>;
      if constexpr (std::is_same_v<T, td_api::messageReplyToMessage>) {
        info.replyToMessageId = r.message_id_;

        // Try to get quote text first (TDLib provides this for convenience)
        if (r.quote_ && r.quote_->text_) {
          info.replyToText = wxString::FromUTF8(r.quote_->text_->text_);
        }

        // If no quote, try to find the original message in our cache
        if (info.replyToText.IsEmpty() && r.message_id_ != 0) {
          std::shared_lock<std::shared_mutex> lock(m_dataMutex);
          auto chatIt = m_messages.find(msg->chat_id_);
          if (chatIt != m_messages.end()) {
            for (const auto &cachedMsg : chatIt->second) {
              if (cachedMsg.id == r.message_id_) {
                // Found the original message
                if (!cachedMsg.text.IsEmpty()) {
                  // Truncate long replies
                  if (cachedMsg.text.Length() > 50) {
                    info.replyToText = cachedMsg.senderName + ": " +
                                       cachedMsg.text.Left(50) + "…";
                  } else {
                    info.replyToText =
                        cachedMsg.senderName + ": " + cachedMsg.text;
                  }
                } else if (cachedMsg.hasPhoto) {
                  info.replyToText = cachedMsg.senderName + ": 📷 Photo";
                } else if (cachedMsg.hasVideo) {
                  info.replyToText = cachedMsg.senderName + ": 🎬 Video";
                } else if (cachedMsg.hasDocument) {
                  info.replyToText =
                      cachedMsg.senderName + ": 📎 " + cachedMsg.mediaFileName;
                } else if (cachedMsg.hasVoice) {
                  info.replyToText = cachedMsg.senderName + ": 🎤 Voice";
                } else if (cachedMsg.hasSticker) {
                  info.replyToText = cachedMsg.senderName + ": " +
                                     cachedMsg.mediaCaption + " Sticker";
                } else if (cachedMsg.hasAnimation) {
                  info.replyToText = cachedMsg.senderName + ": GIF";
                }
                break;
              }
            }
          }
        }
      }
    });
  }

  // Get sender info
  if (msg->sender_id_) {
    td_api::downcast_call(*msg->sender_id_, [this, &info](auto &s) {
      using T = std::decay_t<decltype(s)>;
      if constexpr (std::is_same_v<T, td_api::messageSenderUser>) {
        info.senderId = s.user_id_;
        bool found = false;
        UserInfo user = GetUser(s.user_id_, &found);
        if (found) {
          info.senderName = user.GetDisplayName();
        }
      } else if constexpr (std::is_same_v<T, td_api::messageSenderChat>) {
        info.senderId = s.chat_id_;
        bool found = false;
        ChatInfo chat = GetChat(s.chat_id_, &found);
        if (found) {
          info.senderName = chat.title;
        }
      }
    });
  }

  // Get forward info
  if (msg->forward_info_ && msg->forward_info_->origin_) {
    info.isForwarded = true;
    td_api::downcast_call(*msg->forward_info_->origin_, [this, &info](auto &o) {
      using T = std::decay_t<decltype(o)>;
      if constexpr (std::is_same_v<T, td_api::messageOriginUser>) {
        bool found = false;
        UserInfo user = GetUser(o.sender_user_id_, &found);
        if (found) {
          info.forwardedFrom = user.GetDisplayName();
        }
      } else if constexpr (std::is_same_v<T, td_api::messageOriginHiddenUser>) {
        info.forwardedFrom = wxString::FromUTF8(o.sender_name_);
      } else if constexpr (std::is_same_v<T, td_api::messageOriginChat>) {
        bool found = false;
        ChatInfo chat = GetChat(o.sender_chat_id_, &found);
        if (found) {
          info.forwardedFrom = chat.title;
        }
      } else if constexpr (std::is_same_v<T, td_api::messageOriginChannel>) {
        bool found = false;
        ChatInfo chat = GetChat(o.chat_id_, &found);
        if (found) {
          info.forwardedFrom = chat.title;
        }
      }
    });
  }

  // Parse content
  if (msg->content_) {
    info.text = ExtractMessageText(msg->content_.get());

    td_api::downcast_call(*msg->content_, [this, &info](auto &c) {
      using T = std::decay_t<decltype(c)>;

      if constexpr (std::is_same_v<T, td_api::messagePhoto>) {
        info.hasPhoto = true;
        if (c.caption_) {
          info.mediaCaption = wxString::FromUTF8(c.caption_->text_);
        }
        // Get smallest photo size for thumbnail (first), largest for full
        // (last)
        if (c.photo_ && !c.photo_->sizes_.empty()) {
          // Use smallest size for quick thumbnail preview
          auto &thumbSize = c.photo_->sizes_.front();
          auto &fullSize = c.photo_->sizes_.back();

          if (fullSize->photo_) {
            info.mediaFileId = fullSize->photo_->id_;
            info.mediaFileSize = fullSize->photo_->size_;
            info.width = fullSize->width_;
            info.height = fullSize->height_;

            if (IsFileAvailableLocally(fullSize->photo_.get())) {
              info.mediaLocalPath =
                  wxString::FromUTF8(fullSize->photo_->local_->path_);
            } else if (ShouldDownloadFile(fullSize->photo_.get())) {
              // Auto-download full photo
              this->DownloadFile(fullSize->photo_->id_, 5, "Photo",
                                 fullSize->photo_->size_);
            }
          }

          // Track thumbnail separately
          if (thumbSize->photo_) {
            info.mediaThumbnailFileId = thumbSize->photo_->id_;
            if (IsFileAvailableLocally(thumbSize->photo_.get())) {
              info.mediaThumbnailPath =
                  wxString::FromUTF8(thumbSize->photo_->local_->path_);
            } else if (ShouldDownloadFile(thumbSize->photo_.get())) {
              // Auto-download thumbnail
              this->DownloadFile(thumbSize->photo_->id_, 8, "Thumbnail", 0);
            }
          }
        }
      } else if constexpr (std::is_same_v<T, td_api::messageVideo>) {
        info.hasVideo = true;
        if (c.caption_) {
          info.mediaCaption = wxString::FromUTF8(c.caption_->text_);
        }
        if (c.video_) {
          // Extract duration
          info.mediaDuration = c.video_->duration_;

          if (c.video_->video_) {
            info.mediaFileId = c.video_->video_->id_;
            info.mediaFileName = wxString::FromUTF8(c.video_->file_name_);
            info.mediaFileSize = c.video_->video_->size_;
            info.width = c.video_->width_;
            info.height = c.video_->height_;

            // Check if actual video file is downloaded
            if (IsFileAvailableLocally(c.video_->video_.get())) {
              info.mediaLocalPath =
                  wxString::FromUTF8(c.video_->video_->local_->path_);
            }
          }

          // Always track thumbnail separately (don't put thumbnail path in
          // mediaLocalPath)
          if (c.video_->thumbnail_ && c.video_->thumbnail_->file_) {
            info.mediaThumbnailFileId = c.video_->thumbnail_->file_->id_;
            if (IsFileAvailableLocally(c.video_->thumbnail_->file_.get())) {
              info.mediaThumbnailPath = wxString::FromUTF8(
                  c.video_->thumbnail_->file_->local_->path_);
            } else if (ShouldDownloadFile(c.video_->thumbnail_->file_.get())) {
              // Auto-download video thumbnail
              this->DownloadFile(c.video_->thumbnail_->file_->id_, 8,
                                 "Video Thumbnail", 0);
            }
          }
        }
      } else if constexpr (std::is_same_v<T, td_api::messageDocument>) {
        info.hasDocument = true;
        if (c.caption_) {
          info.mediaCaption = wxString::FromUTF8(c.caption_->text_);
        }
        if (c.document_ && c.document_->document_) {
          info.mediaFileId = c.document_->document_->id_;
          info.mediaFileName = wxString::FromUTF8(c.document_->file_name_);
          info.mediaFileSize = c.document_->document_->size_;

          // Also check for local availability for documents
          if (IsFileAvailableLocally(c.document_->document_.get())) {
            info.mediaLocalPath =
                wxString::FromUTF8(c.document_->document_->local_->path_);
          }
        }
      } else if constexpr (std::is_same_v<T, td_api::messageVoiceNote>) {
        info.hasVoice = true;
        if (c.caption_) {
          info.mediaCaption = wxString::FromUTF8(c.caption_->text_);
        }
        if (c.voice_note_) {
          // Extract duration
          info.mediaDuration = c.voice_note_->duration_;

          // Extract waveform (5-bit values packed into bytes)
          if (!c.voice_note_->waveform_.empty()) {
            info.mediaWaveform.assign(c.voice_note_->waveform_.begin(),
                                      c.voice_note_->waveform_.end());
          }

          if (c.voice_note_->voice_) {
            info.mediaFileId = c.voice_note_->voice_->id_;
            info.mediaFileSize = c.voice_note_->voice_->size_;

            // Check if voice file is already downloaded
            if (IsFileAvailableLocally(c.voice_note_->voice_.get())) {
              info.mediaLocalPath =
                  wxString::FromUTF8(c.voice_note_->voice_->local_->path_);
            }
          }
        }
      } else if constexpr (std::is_same_v<T, td_api::messageVideoNote>) {
        info.hasVideoNote = true;
        if (c.video_note_) {
          // Extract duration
          info.mediaDuration = c.video_note_->duration_;

          if (c.video_note_->video_) {
            info.mediaFileId = c.video_note_->video_->id_;
            info.mediaFileSize = c.video_note_->video_->size_;
            // Video notes are usually square and somewhat small
            info.width = c.video_note_->length_;
            info.height = c.video_note_->length_;

            if (IsFileAvailableLocally(c.video_note_->video_.get())) {
              info.mediaLocalPath =
                  wxString::FromUTF8(c.video_note_->video_->local_->path_);
            }
          }

          // Always track thumbnail separately
          if (c.video_note_->thumbnail_ && c.video_note_->thumbnail_->file_) {
            info.mediaThumbnailFileId = c.video_note_->thumbnail_->file_->id_;
            if (IsFileAvailableLocally(
                    c.video_note_->thumbnail_->file_.get())) {
              info.mediaThumbnailPath = wxString::FromUTF8(
                  c.video_note_->thumbnail_->file_->local_->path_);
            } else if (ShouldDownloadFile(
                           c.video_note_->thumbnail_->file_.get())) {
              // Auto-download video note thumbnail
              this->DownloadFile(c.video_note_->thumbnail_->file_->id_, 8,
                                 "Video Note", 0);
            }
          }
        }
      } else if constexpr (std::is_same_v<T, td_api::messageSticker>) {
        info.hasSticker = true;
        if (c.sticker_) {
          info.mediaCaption = wxString::FromUTF8(c.sticker_->emoji_);
          if (c.sticker_->sticker_) {
            info.mediaFileId = c.sticker_->sticker_->id_;
            info.width = c.sticker_->width_;
            info.height = c.sticker_->height_;

            if (IsFileAvailableLocally(c.sticker_->sticker_.get())) {
              info.mediaLocalPath =
                  wxString::FromUTF8(c.sticker_->sticker_->local_->path_);
            } else if (ShouldDownloadFile(c.sticker_->sticker_.get())) {
              // Auto-download sticker with high priority
              this->DownloadFile(c.sticker_->sticker_->id_, 10, "Sticker", 0);
            }
          }

          // Track thumbnail separately for animated sticker preview
          // Thumbnails are usually WebP/JPEG which we can display
          if (c.sticker_->thumbnail_ && c.sticker_->thumbnail_->file_) {
            info.mediaThumbnailFileId = c.sticker_->thumbnail_->file_->id_;

            if (IsFileAvailableLocally(c.sticker_->thumbnail_->file_.get())) {
              info.mediaThumbnailPath = wxString::FromUTF8(
                  c.sticker_->thumbnail_->file_->local_->path_);
            } else if (ShouldDownloadFile(
                           c.sticker_->thumbnail_->file_.get())) {
              // Auto-download sticker thumbnail (higher priority for preview)
              this->DownloadFile(c.sticker_->thumbnail_->file_->id_, 10,
                                 "Sticker Thumbnail", 0);
            }
          }
        }
      } else if constexpr (std::is_same_v<T, td_api::messageAnimatedEmoji>) {
        // Animated emoji - treat like a sticker for popup display
        info.hasSticker = true;
        info.mediaCaption = wxString::FromUTF8(c.emoji_);

        if (c.animated_emoji_ && c.animated_emoji_->sticker_) {
          auto &sticker = c.animated_emoji_->sticker_;
          if (sticker->sticker_) {
            info.mediaFileId = sticker->sticker_->id_;

            if (IsFileAvailableLocally(sticker->sticker_.get())) {
              info.mediaLocalPath =
                  wxString::FromUTF8(sticker->sticker_->local_->path_);
            } else if (ShouldDownloadFile(sticker->sticker_.get())) {
              this->DownloadFile(sticker->sticker_->id_, 10, "Animated Emoji",
                                 0);
            }
          }

          if (sticker->thumbnail_ && sticker->thumbnail_->file_) {
            info.mediaThumbnailFileId = sticker->thumbnail_->file_->id_;

            if (IsFileAvailableLocally(sticker->thumbnail_->file_.get())) {
              info.mediaThumbnailPath =
                  wxString::FromUTF8(sticker->thumbnail_->file_->local_->path_);
            } else if (ShouldDownloadFile(sticker->thumbnail_->file_.get())) {
              this->DownloadFile(sticker->thumbnail_->file_->id_, 10,
                                 "Animated Emoji Thumbnail", 0);
            }
          }
        }
      } else if constexpr (std::is_same_v<T, td_api::messageAnimation>) {
        info.hasAnimation = true;
        if (c.caption_) {
          info.mediaCaption = wxString::FromUTF8(c.caption_->text_);
        }
        if (c.animation_) {
          if (c.animation_->animation_) {
            info.mediaFileId = c.animation_->animation_->id_;
            info.mediaFileName = wxString::FromUTF8(c.animation_->file_name_);
            info.mediaFileSize = c.animation_->animation_->size_;
            info.width = c.animation_->width_;
            info.height = c.animation_->height_;

            if (IsFileAvailableLocally(c.animation_->animation_.get())) {
              info.mediaLocalPath =
                  wxString::FromUTF8(c.animation_->animation_->local_->path_);
            }
          }

          // Always track thumbnail separately
          if (c.animation_->thumbnail_ && c.animation_->thumbnail_->file_) {
            info.mediaThumbnailFileId = c.animation_->thumbnail_->file_->id_;
            if (IsFileAvailableLocally(c.animation_->thumbnail_->file_.get())) {
              info.mediaThumbnailPath = wxString::FromUTF8(
                  c.animation_->thumbnail_->file_->local_->path_);
            } else if (ShouldDownloadFile(
                           c.animation_->thumbnail_->file_.get())) {
              // Auto-download GIF thumbnail
              this->DownloadFile(c.animation_->thumbnail_->file_->id_, 8,
                                 "GIF Thumbnail", 0);
            }
          }
        }
      }
    });
  }

  // Parse reactions from interaction_info
  if (msg->interaction_info_ && msg->interaction_info_->reactions_) {
    for (auto &reaction : msg->interaction_info_->reactions_->reactions_) {
      if (!reaction)
        continue;

      wxString emoji;
      if (reaction->type_) {
        td_api::downcast_call(*reaction->type_, [&emoji](auto &r) {
          using T = std::decay_t<decltype(r)>;
          if constexpr (std::is_same_v<T, td_api::reactionTypeEmoji>) {
            emoji = wxString::FromUTF8(r.emoji_);
          } else if constexpr (std::is_same_v<
                                   T, td_api::reactionTypeCustomEmoji>) {
            emoji = wxString::FromUTF8("⭐");
          } else if constexpr (std::is_same_v<T, td_api::reactionTypePaid>) {
            emoji = wxString::FromUTF8("⭐");
          }
        });
      }

      if (emoji.IsEmpty())
        continue;

      std::vector<wxString> senders;
      for (auto &sender : reaction->recent_sender_ids_) {
        if (!sender)
          continue;

        td_api::downcast_call(*sender, [this, &senders](auto &s) {
          using T = std::decay_t<decltype(s)>;
          if constexpr (std::is_same_v<T, td_api::messageSenderUser>) {
            wxString name = GetUserDisplayName(s.user_id_);
            if (!name.IsEmpty()) {
              senders.push_back(name);
            }
          }
        });
      }

      if (senders.empty() && reaction->total_count_ > 0) {
        senders.push_back(wxString::Format("%d", reaction->total_count_));
      }

      if (!senders.empty()) {
        info.reactions[emoji] = senders;
      }
    }
  }

  return info;
}

wxString TelegramClient::ExtractMessageText(td_api::MessageContent *content) {
  if (!content)
    return "";

  wxString text;

  td_api::downcast_call(*content, [&text](auto &c) {
    using T = std::decay_t<decltype(c)>;

    if constexpr (std::is_same_v<T, td_api::messageText>) {
      if (c.text_) {
        text = wxString::FromUTF8(c.text_->text_);
      }
    } else if constexpr (std::is_same_v<T, td_api::messagePhoto>) {
      text = "[Photo]";
      if (c.caption_ && !c.caption_->text_.empty()) {
        text += " " + wxString::FromUTF8(c.caption_->text_);
      }
    } else if constexpr (std::is_same_v<T, td_api::messageVideo>) {
      text = "[Video]";
      if (c.caption_ && !c.caption_->text_.empty()) {
        text += " " + wxString::FromUTF8(c.caption_->text_);
      }
    } else if constexpr (std::is_same_v<T, td_api::messageDocument>) {
      text = "[File] " +
             wxString::FromUTF8(c.document_ ? c.document_->file_name_ : "");
    } else if constexpr (std::is_same_v<T, td_api::messageVoiceNote>) {
      text = "[Voice Message]";
    } else if constexpr (std::is_same_v<T, td_api::messageVideoNote>) {
      text = "[Video Message]";
    } else if constexpr (std::is_same_v<T, td_api::messageSticker>) {
      text = "[Sticker] " +
             wxString::FromUTF8(c.sticker_ ? c.sticker_->emoji_ : "");
    } else if constexpr (std::is_same_v<T, td_api::messageAnimatedEmoji>) {
      // Animated emoji is just a fancy single emoji - display as plain text
      text = wxString::FromUTF8(c.emoji_);
    } else if constexpr (std::is_same_v<T, td_api::messageAnimation>) {
      text = "[GIF]";
    } else if constexpr (std::is_same_v<T, td_api::messageAudio>) {
      text = "[Audio]";
    } else if constexpr (std::is_same_v<T, td_api::messageContact>) {
      text = "[Contact]";
    } else if constexpr (std::is_same_v<T, td_api::messageLocation>) {
      text = "[Location]";
    } else if constexpr (std::is_same_v<T, td_api::messagePoll>) {
      text = "[Poll]";
    } else if constexpr (std::is_same_v<T, td_api::messageChatAddMembers>) {
      text = "[User joined]";
    } else if constexpr (std::is_same_v<T, td_api::messageChatDeleteMember>) {
      text = "[User left]";
    } else if constexpr (std::is_same_v<T, td_api::messageChatChangeTitle>) {
      text = "[Title changed]";
    } else if constexpr (std::is_same_v<T, td_api::messageChatChangePhoto>) {
      text = "[Photo changed]";
    } else if constexpr (std::is_same_v<T, td_api::messagePinMessage>) {
      text = "[Message pinned]";
    } else if constexpr (std::is_same_v<T, td_api::messageCall>) {
      // Handle call messages
      bool isVideo = c.is_video_;
      int duration = c.duration_;
      wxString callType = isVideo ? "Video call" : "Call";

      if (c.discard_reason_) {
        td_api::downcast_call(*c.discard_reason_, [&text, &callType,
                                                   duration](auto &reason) {
          using R = std::decay_t<decltype(reason)>;
          if constexpr (std::is_same_v<R, td_api::callDiscardReasonMissed>) {
            text = "[Missed " + callType + "]";
          } else if constexpr (std::is_same_v<
                                   R, td_api::callDiscardReasonDeclined>) {
            text = "[Declined " + callType + "]";
          } else if constexpr (std::is_same_v<
                                   R, td_api::callDiscardReasonDisconnected>) {
            text = "[" + callType + " disconnected]";
          } else if constexpr (std::is_same_v<
                                   R, td_api::callDiscardReasonHungUp>) {
            if (duration > 0) {
              int mins = duration / 60;
              int secs = duration % 60;
              text = wxString::Format("[%s - %d:%02d]", callType, mins, secs);
            } else {
              text = "[" + callType + "]";
            }
          } else {
            text = "[" + callType + "]";
          }
        });
      } else {
        if (duration > 0) {
          int mins = duration / 60;
          int secs = duration % 60;
          text = wxString::Format("[%s - %d:%02d]", callType, mins, secs);
        } else {
          text = "[" + callType + "]";
        }
      }
    } else if constexpr (std::is_same_v<T, td_api::messageScreenshotTaken>) {
      text = "[Screenshot taken]";
    } else if constexpr (std::is_same_v<T, td_api::messageGame>) {
      text = "[Game: " +
             wxString::FromUTF8(c.game_ ? c.game_->title_ : "Unknown") + "]";
    } else if constexpr (std::is_same_v<T, td_api::messageInvoice>) {
      text = "[Invoice: " +
             wxString::FromUTF8(c.product_info_ ? c.product_info_->title_
                                                : "Payment") +
             "]";
    } else if constexpr (std::is_same_v<T, td_api::messageContactRegistered>) {
      text = "[Contact joined Telegram]";
    } else if constexpr (std::is_same_v<T,
                                        td_api::messageSupergroupChatCreate>) {
      text = "[Group created]";
    } else if constexpr (std::is_same_v<T,
                                        td_api::messageBasicGroupChatCreate>) {
      text = "[Group created]";
    } else if constexpr (std::is_same_v<
                             T, td_api::messageChatSetMessageAutoDeleteTime>) {
      text = "[Auto-delete timer changed]";
    } else if constexpr (std::is_same_v<T, td_api::messageExpiredPhoto>) {
      text = "[Photo expired]";
    } else if constexpr (std::is_same_v<T, td_api::messageExpiredVideo>) {
      text = "[Video expired]";
    } else if constexpr (std::is_same_v<T,
                                        td_api::messageCustomServiceAction>) {
      text = "[" + wxString::FromUTF8(c.text_) + "]";
    } else if constexpr (std::is_same_v<T, td_api::messageUnsupported>) {
      text = "[Unsupported message]";
    } else {
      text = "[Message]";
    }
  });

  return text;
}

void TelegramClient::PostToMainThread(std::function<void()> func) {
  {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_mainThreadQueue.push(std::move(func));
  }

  // Post event to main thread via wxTheApp for proper event loop integration
  wxThreadEvent *event = new wxThreadEvent(wxEVT_TDLIB_UPDATE);
  if (wxTheApp) {
    wxQueueEvent(wxTheApp, event);
  } else {
    delete event;
  }
}

void TelegramClient::OnTdlibUpdate(wxThreadEvent &event) {
  // Clear the refresh pending flag FIRST so subsequent SetDirty calls can post
  // new events This avoids a race condition where updates occurring during
  // processing would be missed
  m_uiRefreshPending.store(false);

  // REACTIVE MVC: First, tell MainFrame to poll dirty flags
  // This handles all the frequent updates (messages, downloads, chat list)
  if (m_mainFrame) {
    m_mainFrame->ReactiveRefresh();
  }

  // Process any legacy callbacks (auth flow, errors, etc.) in batches
  std::queue<std::function<void()>> toProcess;
  {
    std::lock_guard<std::mutex> lock(m_queueMutex);

    // Process all queued callbacks - these should be rare now
    std::swap(toProcess, m_mainThreadQueue);
  }

  // Process callbacks
  while (!toProcess.empty()) {
    auto func = std::move(toProcess.front());
    toProcess.pop();
    if (func) {
      try {
        func();
      } catch (const std::exception &e) {
        TDLOG("OnTdlibUpdate: exception in callback: %s", e.what());
      } catch (...) {
        TDLOG("OnTdlibUpdate: unknown exception in callback");
      }
    }
  }
}

// ===== REACTIVE MVC API IMPLEMENTATION =====

void TelegramClient::SetDirty(DirtyFlag flag) {
  // Atomically set the dirty flag
  m_dirtyFlags.fetch_or(static_cast<uint32_t>(flag));

  // Notify UI to refresh (coalesced - only one event in flight)
  NotifyUIRefresh();
}

void TelegramClient::NotifyUIRefresh() {
  // Only post one refresh event at a time - coalesce multiple updates
  bool expected = false;
  if (m_uiRefreshPending.compare_exchange_strong(expected, true)) {
    // We successfully set the flag from false to true, so post the event
    wxThreadEvent *event = new wxThreadEvent(wxEVT_TDLIB_UPDATE);
    if (wxTheApp) {
      wxQueueEvent(wxTheApp, event);
    } else {
      delete event;
      m_uiRefreshPending.store(false);
    }
  }
  // If already pending, do nothing - the pending event will handle it
}

DirtyFlag TelegramClient::GetAndClearDirtyFlags() {
  uint32_t flags = m_dirtyFlags.exchange(0);
  return static_cast<DirtyFlag>(flags);
}

bool TelegramClient::IsDirty(DirtyFlag flag) const {
  uint32_t flags = m_dirtyFlags.load();
  return (flags & static_cast<uint32_t>(flag)) != 0;
}

std::vector<FileDownloadStarted> TelegramClient::GetStartedDownloads() {
  std::lock_guard<std::mutex> lock(m_startedDownloadsMutex);
  std::vector<FileDownloadStarted> result;
  result.swap(m_startedDownloads);
  return result;
}

std::vector<FileDownloadResult> TelegramClient::GetCompletedDownloads() {
  std::lock_guard<std::mutex> lock(m_completedDownloadsMutex);
  std::vector<FileDownloadResult> result;
  result.swap(m_completedDownloads);
  return result;
}

std::vector<MessageInfo> TelegramClient::GetNewMessages(int64_t chatId) {
  std::lock_guard<std::mutex> lock(m_newMessagesMutex);
  std::vector<MessageInfo> result;
  auto it = m_newMessages.find(chatId);
  if (it != m_newMessages.end()) {
    result.swap(it->second);
    m_newMessages.erase(it);
  }
  return result;
}

std::vector<MessageInfo> TelegramClient::GetUpdatedMessages(int64_t chatId) {
  std::lock_guard<std::mutex> lock(m_updatedMessagesMutex);
  std::vector<MessageInfo> result;
  auto it = m_updatedMessages.find(chatId);
  if (it != m_updatedMessages.end()) {
    result.swap(it->second);
    m_updatedMessages.erase(it);
  }
  return result;
}

std::vector<FileDownloadProgress> TelegramClient::GetDownloadProgressUpdates() {
  std::lock_guard<std::mutex> lock(m_downloadProgressMutex);
  std::vector<FileDownloadProgress> result;
  result.swap(m_downloadProgressUpdates);
  return result;
}

std::vector<int64_t> TelegramClient::GetDeletedMessages(int64_t chatId) {
  std::lock_guard<std::mutex> lock(m_deletedMessagesMutex);
  std::vector<int64_t> result;
  auto it = m_deletedMessages.find(chatId);
  if (it != m_deletedMessages.end()) {
    result.swap(it->second);
    m_deletedMessages.erase(it);
  }
  return result;
}

std::map<wxString, wxString> TelegramClient::GetTypingUsers() {
  std::lock_guard<std::mutex> lock(m_typingMutex);

  // Filter out stale typing indicators (older than 6 seconds)
  // Telegram typing events are sent every 5 seconds, so 6 gives some margin
  const int64_t TYPING_TIMEOUT_SECONDS = 6;
  int64_t now = wxGetUTCTime();

  std::map<wxString, wxString> result;

  for (auto it = m_typingUsers.begin(); it != m_typingUsers.end();) {
    int64_t age = now - it->second.second;
    if (age > TYPING_TIMEOUT_SECONDS) {
      // Remove stale entry
      it = m_typingUsers.erase(it);
    } else {
      // Copy to result (just action text, not timestamp)
      result[it->first] = it->second.first;
      ++it;
    }
  }

  return result;
}

std::vector<std::pair<int64_t, wxString>>
TelegramClient::GetSendFailures(int64_t chatId) {
  std::lock_guard<std::mutex> lock(m_sendFailedMutex);
  std::vector<std::pair<int64_t, wxString>> result;
  auto it = m_sendFailedMessages.find(chatId);
  if (it != m_sendFailedMessages.end()) {
    result.swap(it->second);
    m_sendFailedMessages.erase(it);
  }
  return result;
}
