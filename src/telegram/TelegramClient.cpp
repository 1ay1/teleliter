#include "TelegramClient.h"
#include "../ui/MainFrame.h"
#include "../ui/WelcomeChat.h"

#include <iostream>
#include <sstream>
#include <ctime>

// Debug logging - disabled for release
// #define TDLOG(msg) std::cerr << "[TDLib] " << msg << std::endl
#define TDLOG(msg) do {} while(0)

wxDEFINE_EVENT(wxEVT_TDLIB_UPDATE, wxThreadEvent);

// Static pointer to the singleton instance for event handling
static TelegramClient* s_instance = nullptr;

TelegramClient::TelegramClient()
    : m_clientManager(nullptr),
      m_clientId(0),
      m_running(false),
      m_authState(AuthState::WaitTdlibParameters),
      m_currentQueryId(0),
      m_mainFrame(nullptr),
      m_welcomeChat(nullptr)
{
    s_instance = this;
    // Bind to wxTheApp for proper main thread event handling
    if (wxTheApp) {
        wxTheApp->Bind(wxEVT_TDLIB_UPDATE, &TelegramClient::OnTdlibUpdate, this);
    }
}

TelegramClient::~TelegramClient()
{
    Stop();
    if (wxTheApp) {
        wxTheApp->Unbind(wxEVT_TDLIB_UPDATE, &TelegramClient::OnTdlibUpdate, this);
    }
    s_instance = nullptr;
}

void TelegramClient::Start()
{
    if (m_running) {
        TDLOG("Already running, skipping Start()");
        return;
    }
    
    TDLOG("Starting TelegramClient...");
    
    // Set TDLib log verbosity (2 = warnings, 1 = errors only)
    td::ClientManager::execute(td_api::make_object<td_api::setLogVerbosityLevel>(2));
    
    // Create client manager and client
    m_clientManager = std::make_unique<td::ClientManager>();
    m_clientId = m_clientManager->create_client_id();
    
    // Send initial request to start the client
    Send(td_api::make_object<td_api::getOption>("version"), [](td_api::object_ptr<td_api::Object> result) {
        if (result->get_id() == td_api::optionValueString::ID) {
            auto ver = td_api::move_object_as<td_api::optionValueString>(result);
            TDLOG("TDLib version: " << ver->value_);
        }
    });
    
    m_running = true;
    TDLOG("Client started, launching receive thread");
    
    // Start receive thread
    m_receiveThread = std::thread(&TelegramClient::ReceiveLoop, this);
}

void TelegramClient::Stop()
{
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

void TelegramClient::ReceiveLoop()
{
    TDLOG("Receive loop started");
    while (m_running) {
        auto response = m_clientManager->receive(1.0);  // 1 second timeout
        if (response.object) {
            TDLOG("Received response, id=" << response.object->get_id());
            ProcessResponse(std::move(response));
        }
    }
    TDLOG("Receive loop ended");
}

void TelegramClient::Send(td_api::object_ptr<td_api::Function> f,
                          std::function<void(td_api::object_ptr<td_api::Object>)> handler)
{
    auto queryId = ++m_currentQueryId;
    
    if (handler) {
        std::lock_guard<std::mutex> lock(m_handlersMutex);
        m_handlers[queryId] = std::move(handler);
    }
    
    m_clientManager->send(m_clientId, queryId, std::move(f));
}

void TelegramClient::ProcessResponse(td::ClientManager::Response response)
{
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

void TelegramClient::ProcessUpdate(td_api::object_ptr<td_api::Object> update)
{
    td_api::downcast_call(*update, [this](auto& u) {
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
            if (auto it = m_chats.find(u.chat_id_); it != m_chats.end()) {
                it->second.title = wxString::FromUTF8(u.title_);
                PostToMainThread([this]() {
                    if (m_mainFrame) m_mainFrame->RefreshChatList();
                });
            }
        } else if constexpr (std::is_same_v<T, td_api::updateChatLastMessage>) {
            OnChatLastMessage(u.chat_id_, u.last_message_);
        } else if constexpr (std::is_same_v<T, td_api::updateChatReadInbox>) {
            OnChatReadInbox(u.chat_id_, u.last_read_inbox_message_id_, u.unread_count_);
        } else if constexpr (std::is_same_v<T, td_api::updateChatPosition>) {
            OnChatPosition(u.chat_id_, u.position_);
        } else if constexpr (std::is_same_v<T, td_api::updateUser>) {
            OnUserUpdate(u.user_);
        } else if constexpr (std::is_same_v<T, td_api::updateFile>) {
            OnFileUpdate(u.file_);
        } else if constexpr (std::is_same_v<T, td_api::updateChatNotificationSettings>) {
            if (auto it = m_chats.find(u.chat_id_); it != m_chats.end()) {
                it->second.isMuted = u.notification_settings_->mute_for_ > 0;
            }
        }
    });
}

void TelegramClient::OnAuthStateUpdate(td_api::object_ptr<td_api::AuthorizationState>& state)
{
    td_api::downcast_call(*state, [this](auto& s) {
        using T = std::decay_t<decltype(s)>;
        
        if constexpr (std::is_same_v<T, td_api::authorizationStateWaitTdlibParameters>) {
            TDLOG("Auth state: WaitTdlibParameters");
            m_authState = AuthState::WaitTdlibParameters;
            HandleAuthWaitTdlibParameters();
        } else if constexpr (std::is_same_v<T, td_api::authorizationStateWaitPhoneNumber>) {
            TDLOG("Auth state: WaitPhoneNumber");
            m_authState = AuthState::WaitPhoneNumber;
            HandleAuthWaitPhoneNumber();
        } else if constexpr (std::is_same_v<T, td_api::authorizationStateWaitCode>) {
            TDLOG("Auth state: WaitCode");
            m_authState = AuthState::WaitCode;
            HandleAuthWaitCode();
        } else if constexpr (std::is_same_v<T, td_api::authorizationStateWaitPassword>) {
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
        } else if constexpr (std::is_same_v<T, td_api::authorizationStateLoggingOut>) {
            // Logging out, wait for closed state
        } else if constexpr (std::is_same_v<T, td_api::authorizationStateClosing>) {
            // Closing, wait for closed state
        }
    });
}

void TelegramClient::HandleAuthWaitTdlibParameters()
{
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
    request->database_encryption_key_ = "";  // Empty = no encryption
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
            TDLOG("setTdlibParameters error: " << error->message_);
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

void TelegramClient::HandleAuthWaitPhoneNumber()
{
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

void TelegramClient::HandleAuthWaitCode()
{
    PostToMainThread([this]() {
        if (m_welcomeChat) {
            m_welcomeChat->OnCodeRequested();
        }
    });
}

void TelegramClient::HandleAuthWaitPassword()
{
    PostToMainThread([this]() {
        if (m_welcomeChat) {
            m_welcomeChat->On2FARequested();
        }
    });
}

void TelegramClient::HandleAuthReady()
{
    // Get current user info
    Send(td_api::make_object<td_api::getMe>(), [this](td_api::object_ptr<td_api::Object> result) {
        if (result->get_id() == td_api::user::ID) {
            auto user = td_api::move_object_as<td_api::user>(result);
            
            m_currentUser.id = user->id_;
            m_currentUser.firstName = wxString::FromUTF8(user->first_name_);
            m_currentUser.lastName = wxString::FromUTF8(user->last_name_);
            // New API uses usernames_ object
            if (user->usernames_) {
                m_currentUser.username = wxString::FromUTF8(user->usernames_->editable_username_);
            }
            m_currentUser.phoneNumber = wxString::FromUTF8(user->phone_number_);
            m_currentUser.isBot = user->type_->get_id() == td_api::userTypeBot::ID;
            m_currentUser.isSelf = true;
            
            // Store in users map too
            m_users[user->id_] = m_currentUser;
            
            PostToMainThread([this]() {
                if (m_welcomeChat) {
                    m_welcomeChat->OnLoginSuccess(
                        m_currentUser.GetDisplayName(),
                        m_currentUser.phoneNumber
                    );
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

void TelegramClient::HandleAuthClosed()
{
    m_running = false;
    PostToMainThread([this]() {
        if (m_mainFrame) {
            m_mainFrame->OnLoggedOut();
        }
    });
}

void TelegramClient::SetPhoneNumber(const wxString& phoneNumber)
{
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

void TelegramClient::SetAuthCode(const wxString& code)
{
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

void TelegramClient::SetPassword(const wxString& password)
{
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

void TelegramClient::LogOut()
{
    Send(td_api::make_object<td_api::logOut>(), nullptr);
}

void TelegramClient::LoadChats(int limit)
{
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
            td_api::make_object<td_api::chatListMain>(), 100
        ), [this](td_api::object_ptr<td_api::Object> result) {
            if (result->get_id() == td_api::chats::ID) {
                auto chats = td_api::move_object_as<td_api::chats>(result);
                
                for (auto chatId : chats->chat_ids_) {
                    // Get full chat info
                    Send(td_api::make_object<td_api::getChat>(chatId),
                         [this](td_api::object_ptr<td_api::Object> result) {
                        if (result->get_id() == td_api::chat::ID) {
                            auto chat = td_api::move_object_as<td_api::chat>(result);
                            OnChatUpdate(chat);
                        }
                    });
                }
                
                PostToMainThread([this]() {
                    if (m_mainFrame) {
                        m_mainFrame->RefreshChatList();
                    }
                });
            }
        });
    });
}

ChatInfo* TelegramClient::GetChat(int64_t chatId)
{
    auto it = m_chats.find(chatId);
    if (it != m_chats.end()) {
        return &it->second;
    }
    return nullptr;
}

void TelegramClient::LoadMessages(int64_t chatId, int64_t fromMessageId, int limit)
{
    auto request = td_api::make_object<td_api::getChatHistory>();
    request->chat_id_ = chatId;
    request->from_message_id_ = fromMessageId;
    request->offset_ = 0;
    request->limit_ = limit;
    request->only_local_ = false;
    
    Send(std::move(request), [this, chatId](td_api::object_ptr<td_api::Object> result) {
        if (result->get_id() == td_api::messages::ID) {
            auto messages = td_api::move_object_as<td_api::messages>(result);
            
            std::vector<MessageInfo> msgList;
            for (auto& msg : messages->messages_) {
                if (msg) {
                    msgList.push_back(ConvertMessage(msg.get()));
                }
            }
            
            // Store messages (prepend for history loading)
            auto& chatMessages = m_messages[chatId];
            chatMessages.insert(chatMessages.begin(), msgList.begin(), msgList.end());
            
            PostToMainThread([this, chatId, msgList]() {
                if (m_mainFrame) {
                    m_mainFrame->OnMessagesLoaded(chatId, msgList);
                }
            });
        }
    });
}

void TelegramClient::SendMessage(int64_t chatId, const wxString& text)
{
    auto content = td_api::make_object<td_api::inputMessageText>();
    content->text_ = td_api::make_object<td_api::formattedText>();
    content->text_->text_ = text.ToStdString();
    
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

void TelegramClient::SendFile(int64_t chatId, const wxString& filePath, const wxString& caption)
{
    // Determine file type based on extension
    wxString ext = filePath.AfterLast('.').Lower();
    
    td_api::object_ptr<td_api::InputMessageContent> content;
    
    auto inputFile = td_api::make_object<td_api::inputFileLocal>();
    inputFile->path_ = filePath.ToStdString();
    
    auto formattedCaption = td_api::make_object<td_api::formattedText>();
    formattedCaption->text_ = caption.ToStdString();
    
    if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "gif" || ext == "webp") {
        auto photo = td_api::make_object<td_api::inputMessagePhoto>();
        photo->photo_ = std::move(inputFile);
        photo->caption_ = std::move(formattedCaption);
        content = std::move(photo);
    } else if (ext == "mp4" || ext == "mkv" || ext == "avi" || ext == "mov" || ext == "webm") {
        auto video = td_api::make_object<td_api::inputMessageVideo>();
        video->video_ = std::move(inputFile);
        video->caption_ = std::move(formattedCaption);
        content = std::move(video);
    } else if (ext == "mp3" || ext == "ogg" || ext == "wav" || ext == "flac" || ext == "m4a") {
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

void TelegramClient::DownloadFile(int32_t fileId, int priority)
{
    auto request = td_api::make_object<td_api::downloadFile>();
    request->file_id_ = fileId;
    request->priority_ = priority;
    request->synchronous_ = false;
    
    Send(std::move(request), nullptr);
}

void TelegramClient::CancelDownload(int32_t fileId)
{
    auto request = td_api::make_object<td_api::cancelDownloadFile>();
    request->file_id_ = fileId;
    request->only_if_pending_ = false;
    
    Send(std::move(request), nullptr);
}

UserInfo* TelegramClient::GetUser(int64_t userId)
{
    auto it = m_users.find(userId);
    if (it != m_users.end()) {
        return &it->second;
    }
    
    // Request user info if not cached
    Send(td_api::make_object<td_api::getUser>(userId),
         [this](td_api::object_ptr<td_api::Object> result) {
        if (result->get_id() == td_api::user::ID) {
            auto user = td_api::move_object_as<td_api::user>(result);
            OnUserUpdate(user);
        }
    });
    
    return nullptr;
}

wxString TelegramClient::GetUserDisplayName(int64_t userId)
{
    if (auto user = GetUser(userId)) {
        return user->GetDisplayName();
    }
    return wxString::Format("User %lld", userId);
}

void TelegramClient::MarkChatAsRead(int64_t chatId)
{
    auto chat = GetChat(chatId);
    if (!chat || chat->unreadCount == 0) {
        return;
    }
    
    // Get the last message ID
    auto& messages = m_messages[chatId];
    int64_t lastMessageId = 0;
    if (!messages.empty()) {
        lastMessageId = messages.back().id;
    }
    
    if (lastMessageId > 0) {
        auto request = td_api::make_object<td_api::viewMessages>();
        request->chat_id_ = chatId;
        request->message_ids_.push_back(lastMessageId);
        request->force_read_ = true;
        
        Send(std::move(request), nullptr);
    }
}

void TelegramClient::OnNewMessage(td_api::object_ptr<td_api::message>& message)
{
    if (!message) return;
    
    MessageInfo msgInfo = ConvertMessage(message.get());
    
    // Add to messages cache
    m_messages[msgInfo.chatId].push_back(msgInfo);
    
    PostToMainThread([this, msgInfo]() {
        if (m_mainFrame) {
            m_mainFrame->OnNewMessage(msgInfo);
        }
    });
}

void TelegramClient::OnMessageEdited(int64_t chatId, int64_t messageId,
                                      td_api::object_ptr<td_api::MessageContent>& content)
{
    wxString newText = ExtractMessageText(content.get());
    
    // Update in cache
    auto& messages = m_messages[chatId];
    for (auto& msg : messages) {
        if (msg.id == messageId) {
            msg.text = newText;
            msg.isEdited = true;
            break;
        }
    }
    
    PostToMainThread([this, chatId, messageId, newText]() {
        if (m_mainFrame) {
            m_mainFrame->OnMessageEdited(chatId, messageId, newText);
        }
    });
}

void TelegramClient::OnChatUpdate(td_api::object_ptr<td_api::chat>& chat)
{
    if (!chat) return;
    
    ChatInfo info;
    info.id = chat->id_;
    info.title = wxString::FromUTF8(chat->title_);
    info.unreadCount = chat->unread_count_;
    
    // Parse positions
    for (auto& pos : chat->positions_) {
        if (pos->list_->get_id() == td_api::chatListMain::ID) {
            info.isPinned = pos->is_pinned_;
            info.order = pos->order_;
            break;
        }
    }
    
    // Parse chat type
    if (chat->type_) {
        td_api::downcast_call(*chat->type_, [&info](auto& t) {
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
    
    m_chats[info.id] = info;
    
    PostToMainThread([this]() {
        if (m_mainFrame) {
            m_mainFrame->RefreshChatList();
        }
    });
}

void TelegramClient::OnUserUpdate(td_api::object_ptr<td_api::user>& user)
{
    if (!user) return;
    
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
        td_api::downcast_call(*user->status_, [&info](auto& s) {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, td_api::userStatusOnline>) {
                info.isOnline = true;
            } else if constexpr (std::is_same_v<T, td_api::userStatusOffline>) {
                info.isOnline = false;
                info.lastSeenTime = s.was_online_;
            } else {
                info.isOnline = false;
            }
        });
    }
    
    m_users[info.id] = info;
    
    // Update chat info if this user has a private chat
    for (auto& [chatId, chat] : m_chats) {
        if (chat.isPrivate && chat.userId == info.id) {
            chat.isBot = info.isBot;
            chat.title = info.GetDisplayName();
        }
    }
}

void TelegramClient::OnFileUpdate(td_api::object_ptr<td_api::file>& file)
{
    if (!file) return;
    
    int32_t fileId = file->id_;
    bool isDownloading = file->local_->is_downloading_active_;
    bool isComplete = file->local_->is_downloading_completed_;
    wxString localPath = wxString::FromUTF8(file->local_->path_);
    int64_t downloadedSize = file->local_->downloaded_size_;
    int64_t totalSize = file->size_;
    
    PostToMainThread([this, fileId, isDownloading, isComplete, localPath, downloadedSize, totalSize]() {
        if (m_mainFrame) {
            if (isComplete) {
                m_mainFrame->OnFileDownloaded(fileId, localPath);
            } else if (isDownloading) {
                m_mainFrame->OnFileProgress(fileId, downloadedSize, totalSize);
            }
        }
    });
}

void TelegramClient::OnChatLastMessage(int64_t chatId, td_api::object_ptr<td_api::message>& message)
{
    auto it = m_chats.find(chatId);
    if (it == m_chats.end()) return;
    
    if (message) {
        it->second.lastMessage = ExtractMessageText(message->content_.get());
        it->second.lastMessageDate = message->date_;
    } else {
        it->second.lastMessage.Clear();
        it->second.lastMessageDate = 0;
    }
}

void TelegramClient::OnChatReadInbox(int64_t chatId, int64_t lastReadInboxMessageId, int32_t unreadCount)
{
    auto it = m_chats.find(chatId);
    if (it != m_chats.end()) {
        it->second.unreadCount = unreadCount;
        
        PostToMainThread([this]() {
            if (m_mainFrame) {
                m_mainFrame->RefreshChatList();
            }
        });
    }
}

void TelegramClient::OnChatPosition(int64_t chatId, td_api::object_ptr<td_api::chatPosition>& position)
{
    auto it = m_chats.find(chatId);
    if (it == m_chats.end()) return;
    
    if (position && position->list_->get_id() == td_api::chatListMain::ID) {
        it->second.isPinned = position->is_pinned_;
        it->second.order = position->order_;
    }
}

MessageInfo TelegramClient::ConvertMessage(td_api::message* msg)
{
    MessageInfo info;
    if (!msg) return info;
    
    info.id = msg->id_;
    info.chatId = msg->chat_id_;
    info.date = msg->date_;
    info.isOutgoing = msg->is_outgoing_;
    info.isEdited = msg->edit_date_ > 0;
    
    // New API uses reply_to_ object with MessageReplyTo type
    if (msg->reply_to_) {
        td_api::downcast_call(*msg->reply_to_, [&info](auto& r) {
            using T = std::decay_t<decltype(r)>;
            if constexpr (std::is_same_v<T, td_api::messageReplyToMessage>) {
                info.replyToMessageId = r.message_id_;
            }
        });
    }
    
    // Get sender info
    if (msg->sender_id_) {
        td_api::downcast_call(*msg->sender_id_, [this, &info](auto& s) {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, td_api::messageSenderUser>) {
                info.senderId = s.user_id_;
                if (auto user = GetUser(s.user_id_)) {
                    info.senderName = user->GetDisplayName();
                }
            } else if constexpr (std::is_same_v<T, td_api::messageSenderChat>) {
                info.senderId = s.chat_id_;
                if (auto chat = GetChat(s.chat_id_)) {
                    info.senderName = chat->title;
                }
            }
        });
    }
    
    // Get forward info
    if (msg->forward_info_ && msg->forward_info_->origin_) {
        info.isForwarded = true;
        td_api::downcast_call(*msg->forward_info_->origin_, [this, &info](auto& o) {
            using T = std::decay_t<decltype(o)>;
            if constexpr (std::is_same_v<T, td_api::messageOriginUser>) {
                if (auto user = GetUser(o.sender_user_id_)) {
                    info.forwardedFrom = user->GetDisplayName();
                }
            } else if constexpr (std::is_same_v<T, td_api::messageOriginHiddenUser>) {
                info.forwardedFrom = wxString::FromUTF8(o.sender_name_);
            } else if constexpr (std::is_same_v<T, td_api::messageOriginChat>) {
                if (auto chat = GetChat(o.sender_chat_id_)) {
                    info.forwardedFrom = chat->title;
                }
            } else if constexpr (std::is_same_v<T, td_api::messageOriginChannel>) {
                if (auto chat = GetChat(o.chat_id_)) {
                    info.forwardedFrom = chat->title;
                }
            }
        });
    }
    
    // Parse content
    if (msg->content_) {
        info.text = ExtractMessageText(msg->content_.get());
        
        td_api::downcast_call(*msg->content_, [&info](auto& c) {
            using T = std::decay_t<decltype(c)>;
            
            if constexpr (std::is_same_v<T, td_api::messagePhoto>) {
                info.hasPhoto = true;
                if (c.caption_) {
                    info.mediaCaption = wxString::FromUTF8(c.caption_->text_);
                }
                // Get largest photo size
                if (c.photo_ && !c.photo_->sizes_.empty()) {
                    auto& size = c.photo_->sizes_.back();
                    if (size->photo_) {
                        info.mediaFileId = size->photo_->id_;
                        info.mediaFileSize = size->photo_->size_;
                        if (size->photo_->local_->is_downloading_completed_) {
                            info.mediaLocalPath = wxString::FromUTF8(size->photo_->local_->path_);
                        }
                    }
                }
            } else if constexpr (std::is_same_v<T, td_api::messageVideo>) {
                info.hasVideo = true;
                if (c.caption_) {
                    info.mediaCaption = wxString::FromUTF8(c.caption_->text_);
                }
                if (c.video_ && c.video_->video_) {
                    info.mediaFileId = c.video_->video_->id_;
                    info.mediaFileName = wxString::FromUTF8(c.video_->file_name_);
                    info.mediaFileSize = c.video_->video_->size_;
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
                }
            } else if constexpr (std::is_same_v<T, td_api::messageVoiceNote>) {
                info.hasVoice = true;
                if (c.caption_) {
                    info.mediaCaption = wxString::FromUTF8(c.caption_->text_);
                }
                if (c.voice_note_ && c.voice_note_->voice_) {
                    info.mediaFileId = c.voice_note_->voice_->id_;
                    info.mediaFileSize = c.voice_note_->voice_->size_;
                }
            } else if constexpr (std::is_same_v<T, td_api::messageVideoNote>) {
                info.hasVideoNote = true;
                if (c.video_note_ && c.video_note_->video_) {
                    info.mediaFileId = c.video_note_->video_->id_;
                    info.mediaFileSize = c.video_note_->video_->size_;
                }
            } else if constexpr (std::is_same_v<T, td_api::messageSticker>) {
                info.hasSticker = true;
                if (c.sticker_) {
                    info.mediaCaption = wxString::FromUTF8(c.sticker_->emoji_);
                    if (c.sticker_->sticker_) {
                        info.mediaFileId = c.sticker_->sticker_->id_;
                    }
                }
            } else if constexpr (std::is_same_v<T, td_api::messageAnimation>) {
                info.hasAnimation = true;
                if (c.caption_) {
                    info.mediaCaption = wxString::FromUTF8(c.caption_->text_);
                }
                if (c.animation_ && c.animation_->animation_) {
                    info.mediaFileId = c.animation_->animation_->id_;
                    info.mediaFileName = wxString::FromUTF8(c.animation_->file_name_);
                    info.mediaFileSize = c.animation_->animation_->size_;
                }
            }
        });
    }
    
    return info;
}

wxString TelegramClient::ExtractMessageText(td_api::MessageContent* content)
{
    if (!content) return "";
    
    wxString text;
    
    td_api::downcast_call(*content, [&text](auto& c) {
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
            text = "[File] " + wxString::FromUTF8(c.document_ ? c.document_->file_name_ : "");
        } else if constexpr (std::is_same_v<T, td_api::messageVoiceNote>) {
            text = "[Voice Message]";
        } else if constexpr (std::is_same_v<T, td_api::messageVideoNote>) {
            text = "[Video Message]";
        } else if constexpr (std::is_same_v<T, td_api::messageSticker>) {
            text = "[Sticker] " + wxString::FromUTF8(c.sticker_ ? c.sticker_->emoji_ : "");
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
        } else {
            text = "[Message]";
        }
    });
    
    return text;
}

void TelegramClient::PostToMainThread(std::function<void()> func)
{
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_mainThreadQueue.push(std::move(func));
    }
    
    // Post event to main thread via wxTheApp for proper event loop integration
    wxThreadEvent* event = new wxThreadEvent(wxEVT_TDLIB_UPDATE);
    if (wxTheApp) {
        wxQueueEvent(wxTheApp, event);
    } else {
        delete event;
    }
}

void TelegramClient::OnTdlibUpdate(wxThreadEvent& event)
{
    // Process all queued functions on main thread
    std::queue<std::function<void()>> toProcess;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        std::swap(toProcess, m_mainThreadQueue);
    }
    
    while (!toProcess.empty()) {
        auto func = std::move(toProcess.front());
        toProcess.pop();
        if (func) {
            func();
        }
    }
}