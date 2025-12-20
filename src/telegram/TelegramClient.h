#ifndef TELEGRAMCLIENT_H
#define TELEGRAMCLIENT_H

#include <wx/wx.h>
#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

#include <memory>
#include <map>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>
#include <queue>

#include "Types.h"

// Forward declarations
class MainFrame;
class WelcomeChat;

namespace td_api = td::td_api;

// Custom event for TDLib updates (to be processed on main thread)
wxDECLARE_EVENT(wxEVT_TDLIB_UPDATE, wxThreadEvent);

// TDLib client wrapper
class TelegramClient : public wxEvtHandler
{
public:
    TelegramClient();
    virtual ~TelegramClient();
    
    void SetMainFrame(MainFrame* frame) { m_mainFrame = frame; }
    void SetWelcomeChat(WelcomeChat* welcomeChat) { m_welcomeChat = welcomeChat; }
    
    void Start();
    void Stop();
    bool IsRunning() const { return m_running; }
    
    AuthState GetAuthState() const { return m_authState; }
    void SetPhoneNumber(const wxString& phoneNumber);
    void SetAuthCode(const wxString& code);
    void SetPassword(const wxString& password);
    void LogOut();
    
    const UserInfo& GetCurrentUser() const { return m_currentUser; }
    bool IsLoggedIn() const { return m_authState == AuthState::Ready; }
    
    void LoadChats(int limit = 100);
    const std::map<int64_t, ChatInfo>& GetChats() const { return m_chats; }
    ChatInfo* GetChat(int64_t chatId);
    
    void LoadMessages(int64_t chatId, int64_t fromMessageId = 0, int limit = 50);
    void SendMessage(int64_t chatId, const wxString& text);
    void SendFile(int64_t chatId, const wxString& filePath, const wxString& caption = "");
    
    void DownloadFile(int32_t fileId, int priority = 1);
    void CancelDownload(int32_t fileId);
    
    UserInfo* GetUser(int64_t userId);
    wxString GetUserDisplayName(int64_t userId);
    
    void MarkChatAsRead(int64_t chatId);
    
private:
    std::unique_ptr<td::ClientManager> m_clientManager;
    int32_t m_clientId;
    
    std::thread m_receiveThread;
    std::atomic<bool> m_running;
    
    AuthState m_authState;
    UserInfo m_currentUser;
    
    std::map<int64_t, ChatInfo> m_chats;
    std::map<int64_t, UserInfo> m_users;
    std::map<int64_t, std::vector<MessageInfo>> m_messages;
    
    std::uint64_t m_currentQueryId;
    std::map<std::uint64_t, std::function<void(td_api::object_ptr<td_api::Object>)>> m_handlers;
    std::mutex m_handlersMutex;
    
    MainFrame* m_mainFrame;
    WelcomeChat* m_welcomeChat;
    
    void Send(td_api::object_ptr<td_api::Function> f, 
              std::function<void(td_api::object_ptr<td_api::Object>)> handler = nullptr);
    
    void ProcessResponse(td::ClientManager::Response response);
    void ProcessUpdate(td_api::object_ptr<td_api::Object> update);
    
    void OnAuthStateUpdate(td_api::object_ptr<td_api::AuthorizationState>& state);
    void HandleAuthWaitTdlibParameters();
    void HandleAuthWaitPhoneNumber();
    void HandleAuthWaitCode();
    void HandleAuthWaitPassword();
    void HandleAuthReady();
    void HandleAuthClosed();
    
    void OnNewMessage(td_api::object_ptr<td_api::message>& message);
    void OnMessageEdited(int64_t chatId, int64_t messageId, 
                         td_api::object_ptr<td_api::MessageContent>& content);
    void OnChatUpdate(td_api::object_ptr<td_api::chat>& chat);
    void OnUserUpdate(td_api::object_ptr<td_api::user>& user);
    void OnFileUpdate(td_api::object_ptr<td_api::file>& file);
    void OnChatLastMessage(int64_t chatId, td_api::object_ptr<td_api::message>& message);
    void OnChatReadInbox(int64_t chatId, int64_t lastReadInboxMessageId, int32_t unreadCount);
    void OnChatPosition(int64_t chatId, td_api::object_ptr<td_api::chatPosition>& position);
    
    MessageInfo ConvertMessage(td_api::message* msg);
    wxString ExtractMessageText(td_api::MessageContent* content);
    
    void PostToMainThread(std::function<void()> func);
    void OnTdlibUpdate(wxThreadEvent& event);
    
    void ReceiveLoop();
    
    std::queue<std::function<void()>> m_mainThreadQueue;
    std::mutex m_queueMutex;
};

#endif // TELEGRAMCLIENT_H