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
#include "../ui/MediaTypes.h"

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
    std::map<int64_t, ChatInfo> GetChats() const;
    ChatInfo GetChat(int64_t chatId, bool* found = nullptr) const;
    
    void OpenChat(int64_t chatId);
    void CloseChat(int64_t chatId);
    void OpenChatAndLoadMessages(int64_t chatId, int limit = 100);
    
    // Track current active chat for download prioritization
    void SetCurrentChatId(int64_t chatId) { m_currentChatId = chatId; }
    int64_t GetCurrentChatId() const { return m_currentChatId; }
    void LoadMessages(int64_t chatId, int64_t fromMessageId = 0, int limit = 100);
    void LoadMessagesWithRetry(int64_t chatId, int limit, int retryCount);
    void LoadMoreMessages(int64_t chatId, int64_t fromMessageId, int limit = 100);
    void SendMessage(int64_t chatId, const wxString& text);
    void SendFile(int64_t chatId, const wxString& filePath, const wxString& caption = "");
    
    void DownloadFile(int32_t fileId, int priority = 1, const wxString& fileName = "", int64_t fileSize = 0);
    void CancelDownload(int32_t fileId);
    void RetryDownload(int32_t fileId);
    bool IsDownloading(int32_t fileId) const;
    DownloadState GetDownloadState(int32_t fileId) const;
    
    // Re-fetch a message from TDLib to get updated file info (for incomplete stickers etc.)
    void RefetchMessage(int64_t chatId, int64_t messageId);
    
    // Smart auto-download for chat media
    // Call when opening a chat to preemptively download visible media
    void AutoDownloadChatMedia(int64_t chatId, int messageLimit = 50);
    
    // Boost download priority for a file (e.g., when user hovers)
    void BoostDownloadPriority(int32_t fileId);
    
    // Get download progress (0-100, or -1 if not downloading)
    int GetDownloadProgress(int32_t fileId) const;
    
    UserInfo GetUser(int64_t userId, bool* found = nullptr) const;
    wxString GetUserDisplayName(int64_t userId) const;
    
    void MarkChatAsRead(int64_t chatId);
    
    // Load members for a chat (groups/supergroups/channels)
    void LoadChatMembers(int64_t chatId, int limit = 200);
    
private:
    std::unique_ptr<td::ClientManager> m_clientManager;
    int32_t m_clientId;
    
    std::thread m_receiveThread;
    std::atomic<bool> m_running;
    
    AuthState m_authState;
    UserInfo m_currentUser;
    int64_t m_currentChatId = 0;  // Currently viewed chat for download prioritization
    
    std::map<int64_t, ChatInfo> m_chats;
    std::map<int64_t, UserInfo> m_users;
    std::map<int64_t, std::vector<MessageInfo>> m_messages;
    mutable std::mutex m_dataMutex;  // Protects m_chats, m_users, m_messages
    
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
    void ConfigureAutoDownload();
    
    void OnNewMessage(td_api::object_ptr<td_api::message>& message);
    void OnMessageEdited(int64_t chatId, int64_t messageId, 
                         td_api::object_ptr<td_api::MessageContent>& content);
    void OnChatUpdate(td_api::object_ptr<td_api::chat>& chat);
    void OnUserUpdate(td_api::object_ptr<td_api::user>& user);
    void OnUserStatusUpdate(int64_t userId, td_api::object_ptr<td_api::UserStatus>& status);
    void OnFileUpdate(td_api::object_ptr<td_api::file>& file);
    void OnDownloadError(int32_t fileId, const wxString& error);
    void CheckDownloadTimeouts();
    
    // Smart download helpers
    void DownloadMediaFromMessage(const MessageInfo& msg, int basePriority);
    bool ShouldAutoDownloadMedia(MediaType type, int64_t fileSize) const;
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
    
    // Download tracking
    std::map<int32_t, DownloadInfo> m_activeDownloads;
    mutable std::mutex m_downloadsMutex;
    wxTimer m_downloadTimeoutTimer;
    
    // Progress throttling - protected by mutex for thread safety
    std::map<int32_t, int64_t> m_lastProgressTime;
    std::mutex m_progressThrottleMutex;
    
    // Background download throttling - avoid flooding UI with notifications
    std::atomic<int64_t> m_lastBackgroundDownloadNotify{0};
    static constexpr int64_t BACKGROUND_DOWNLOAD_THROTTLE_MS = 50;  // Min ms between background download starts
    
    // Main thread queue processing limits to avoid UI stalls
    static constexpr size_t MAX_EVENTS_PER_BATCH = 10;  // Max events processed per OnTdlibUpdate call
    static constexpr int64_t PROGRESS_THROTTLE_MS = 200;  // 5 updates/sec max per file
    
    void OnDownloadTimeoutTimer(wxTimerEvent& event);
    void StartDownloadInternal(int32_t fileId, int priority);
};

#endif // TELEGRAMCLIENT_H