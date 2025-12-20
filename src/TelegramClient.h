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

// Forward declarations
class MainFrame;
class WelcomeChat;

namespace td_api = td::td_api;

// Authentication state
enum class AuthState {
    WaitTdlibParameters,
    WaitPhoneNumber,
    WaitCode,
    WaitPassword,
    Ready,
    Closed,
    Error
};

// Chat info structure (extends the basic TelegramChat)
struct ChatInfo {
    int64_t id;
    wxString title;
    wxString lastMessage;
    int64_t lastMessageDate;
    int32_t unreadCount;
    bool isPinned;
    bool isMuted;
    int64_t order;  // For sorting
    
    // Chat type info
    bool isPrivate;
    bool isGroup;
    bool isSupergroup;
    bool isChannel;
    bool isBot;
    
    // For private chats
    int64_t userId;
    
    // For groups/supergroups/channels
    int64_t supergroupId;
    int64_t basicGroupId;
    
    ChatInfo() : id(0), lastMessageDate(0), unreadCount(0), isPinned(false),
                 isMuted(false), order(0), isPrivate(false), isGroup(false),
                 isSupergroup(false), isChannel(false), isBot(false),
                 userId(0), supergroupId(0), basicGroupId(0) {}
};

// Message info structure
struct MessageInfo {
    int64_t id;
    int64_t chatId;
    int64_t senderId;
    wxString senderName;
    wxString text;
    int64_t date;
    bool isOutgoing;
    bool isEdited;
    
    // Media info
    bool hasPhoto;
    bool hasVideo;
    bool hasDocument;
    bool hasVoice;
    bool hasVideoNote;
    bool hasSticker;
    bool hasAnimation;
    
    wxString mediaCaption;
    wxString mediaFileName;
    int32_t mediaFileId;
    wxString mediaLocalPath;
    int64_t mediaFileSize;
    
    // Reply info
    int64_t replyToMessageId;
    wxString replyToText;
    
    // Forward info
    bool isForwarded;
    wxString forwardedFrom;
    
    MessageInfo() : id(0), chatId(0), senderId(0), date(0), isOutgoing(false),
                    isEdited(false), hasPhoto(false), hasVideo(false),
                    hasDocument(false), hasVoice(false), hasVideoNote(false),
                    hasSticker(false), hasAnimation(false), mediaFileId(0),
                    mediaFileSize(0), replyToMessageId(0), isForwarded(false) {}
};

// User info structure
struct UserInfo {
    int64_t id;
    wxString firstName;
    wxString lastName;
    wxString username;
    wxString phoneNumber;
    bool isBot;
    bool isVerified;
    bool isSelf;
    
    // Online status
    bool isOnline;
    int64_t lastSeenTime;
    
    wxString GetDisplayName() const {
        wxString name = firstName;
        if (!lastName.IsEmpty()) {
            name += " " + lastName;
        }
        return name.IsEmpty() ? username : name;
    }
    
    UserInfo() : id(0), isBot(false), isVerified(false), isSelf(false),
                 isOnline(false), lastSeenTime(0) {}
};

// Callback types for async operations
using AuthCallback = std::function<void(AuthState state, const wxString& error)>;
using ChatsCallback = std::function<void(const std::vector<ChatInfo>& chats)>;
using MessagesCallback = std::function<void(const std::vector<MessageInfo>& messages)>;
using SendMessageCallback = std::function<void(bool success, int64_t messageId, const wxString& error)>;
using FileCallback = std::function<void(bool success, const wxString& localPath, const wxString& error)>;

// Custom event for TDLib updates (to be processed on main thread)
wxDECLARE_EVENT(wxEVT_TDLIB_UPDATE, wxThreadEvent);

// TDLib client wrapper
class TelegramClient : public wxEvtHandler
{
public:
    TelegramClient();
    virtual ~TelegramClient();
    
    // Initialization
    void SetMainFrame(MainFrame* frame) { m_mainFrame = frame; }
    void SetWelcomeChat(WelcomeChat* welcomeChat) { m_welcomeChat = welcomeChat; }
    
    // Start/stop the client
    void Start();
    void Stop();
    bool IsRunning() const { return m_running; }
    
    // Authentication
    AuthState GetAuthState() const { return m_authState; }
    void SetPhoneNumber(const wxString& phoneNumber);
    void SetAuthCode(const wxString& code);
    void SetPassword(const wxString& password);
    void LogOut();
    
    // Get current user info
    const UserInfo& GetCurrentUser() const { return m_currentUser; }
    bool IsLoggedIn() const { return m_authState == AuthState::Ready; }
    
    // Chat operations
    void LoadChats(int limit = 100);
    const std::map<int64_t, ChatInfo>& GetChats() const { return m_chats; }
    ChatInfo* GetChat(int64_t chatId);
    
    // Message operations
    void LoadMessages(int64_t chatId, int64_t fromMessageId = 0, int limit = 50);
    void SendMessage(int64_t chatId, const wxString& text);
    void SendFile(int64_t chatId, const wxString& filePath, const wxString& caption = "");
    
    // File operations
    void DownloadFile(int32_t fileId, int priority = 1);
    void CancelDownload(int32_t fileId);
    
    // User operations
    UserInfo* GetUser(int64_t userId);
    wxString GetUserDisplayName(int64_t userId);
    
    // Mark messages as read
    void MarkChatAsRead(int64_t chatId);
    
private:
    // TDLib client
    std::unique_ptr<td::ClientManager> m_clientManager;
    int32_t m_clientId;
    
    // Processing thread
    std::thread m_receiveThread;
    std::atomic<bool> m_running;
    
    // State
    AuthState m_authState;
    UserInfo m_currentUser;
    
    // Cached data
    std::map<int64_t, ChatInfo> m_chats;
    std::map<int64_t, UserInfo> m_users;
    std::map<int64_t, std::vector<MessageInfo>> m_messages;  // chatId -> messages
    
    // Pending requests
    std::uint64_t m_currentQueryId;
    std::map<std::uint64_t, std::function<void(td_api::object_ptr<td_api::Object>)>> m_handlers;
    std::mutex m_handlersMutex;
    
    // UI references
    MainFrame* m_mainFrame;
    WelcomeChat* m_welcomeChat;
    
    // TDLib API helpers
    void Send(td_api::object_ptr<td_api::Function> f, 
              std::function<void(td_api::object_ptr<td_api::Object>)> handler = nullptr);
    
    // Response processing
    void ProcessResponse(td::ClientManager::Response response);
    void ProcessUpdate(td_api::object_ptr<td_api::Object> update);
    
    // Auth state handlers
    void OnAuthStateUpdate(td_api::object_ptr<td_api::AuthorizationState>& state);
    void HandleAuthWaitTdlibParameters();
    void HandleAuthWaitPhoneNumber();
    void HandleAuthWaitCode();
    void HandleAuthWaitPassword();
    void HandleAuthReady();
    void HandleAuthClosed();
    
    // Update handlers
    void OnNewMessage(td_api::object_ptr<td_api::message>& message);
    void OnMessageEdited(int64_t chatId, int64_t messageId, 
                         td_api::object_ptr<td_api::MessageContent>& content);
    void OnChatUpdate(td_api::object_ptr<td_api::chat>& chat);
    void OnUserUpdate(td_api::object_ptr<td_api::user>& user);
    void OnFileUpdate(td_api::object_ptr<td_api::file>& file);
    void OnChatLastMessage(int64_t chatId, td_api::object_ptr<td_api::message>& message);
    void OnChatReadInbox(int64_t chatId, int64_t lastReadInboxMessageId, int32_t unreadCount);
    void OnChatPosition(int64_t chatId, td_api::object_ptr<td_api::chatPosition>& position);
    
    // Helper to convert TDLib message to MessageInfo
    MessageInfo ConvertMessage(td_api::message* msg);
    
    // Helper to extract text from message content
    wxString ExtractMessageText(td_api::MessageContent* content);
    
    // Thread-safe UI update dispatch
    void PostToMainThread(std::function<void()> func);
    void OnTdlibUpdate(wxThreadEvent& event);
    
    // Receive loop (runs in separate thread)
    void ReceiveLoop();
    
    // Queue for main thread processing
    std::queue<std::function<void()>> m_mainThreadQueue;
    std::mutex m_queueMutex;
};

#endif // TELEGRAMCLIENT_H