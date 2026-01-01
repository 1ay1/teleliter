#ifndef SERVICEMESSAGELOG_H
#define SERVICEMESSAGELOG_H

#include <wx/wx.h>
#include <deque>
#include <mutex>
#include <functional>
#include <set>

// Forward declarations
class WelcomeChat;
class StatusBarManager;
class TelegramClient;

// Service message types for categorization and filtering
enum class ServiceMessageType {
  UserOnline,      // User came online
  UserOffline,     // User went offline
  UserTyping,      // User is typing
  UserAction,      // User is recording/uploading/etc.
  ChatRead,        // Someone read messages
  MessageDeleted,  // Message was deleted
  MessageEdited,   // Message was edited
  NewMessage,      // New message received (from non-current chat)
  ConnectionState, // Connection state changed
  Download,        // Download started/completed/failed
  Upload,          // Upload started/completed/failed
  Reaction,        // Someone reacted to a message
  Join,            // User joined a chat
  Leave,           // User left a chat
  System,          // Generic system message
  Error            // Error message
};

// A single service message entry
struct ServiceMessage {
  wxDateTime timestamp;
  ServiceMessageType type;
  wxString text;
  wxString detail;     // Additional detail (e.g., username, chat name)
  int64_t relatedId;   // Related user/chat/message ID (0 if none)
  
  ServiceMessage(ServiceMessageType t, const wxString& txt, 
                 const wxString& det = "", int64_t id = 0)
    : timestamp(wxDateTime::Now()), type(t), text(txt), 
      detail(det), relatedId(id) {}
};

// Service message log - central hub for all service events
// Displays rotating messages in status bar and logs to WelcomeChat
class ServiceMessageLog : public wxEvtHandler {
public:
  ServiceMessageLog();
  ~ServiceMessageLog();
  
  // Set references to UI components
  void SetWelcomeChat(WelcomeChat* welcomeChat) { m_welcomeChat = welcomeChat; }
  void SetStatusBarManager(StatusBarManager* statusBar) { m_statusBar = statusBar; }
  void SetTelegramClient(TelegramClient* client) { m_telegramClient = client; }
  
  // Start/stop the rotation timer
  void Start();
  void Stop();
  
  // Log a service message
  void Log(ServiceMessageType type, const wxString& text, 
           const wxString& detail = "", int64_t relatedId = 0);
  
  // Convenience methods for common message types
  void LogUserOnline(const wxString& username, int64_t userId = 0);
  void LogUserOffline(const wxString& username, const wxString& lastSeen = "", int64_t userId = 0);
  void LogUserTyping(const wxString& username, const wxString& chatName, int64_t chatId = 0);
  void LogUserAction(const wxString& username, const wxString& action, 
                     const wxString& chatName, int64_t chatId = 0);
  void LogMessageRead(const wxString& username, const wxString& chatName, int64_t chatId = 0);
  void LogNewMessage(const wxString& sender, const wxString& chatName, 
                     const wxString& preview = "", int64_t chatId = 0,
                     int64_t messageId = 0);
  void LogConnectionState(const wxString& state);
  void LogDownloadStarted(const wxString& fileName, int64_t fileSize = 0);
  void LogDownloadComplete(const wxString& fileName);
  void LogDownloadFailed(const wxString& fileName, const wxString& error);
  void LogUploadStarted(const wxString& fileName, int64_t fileSize = 0);
  void LogUploadComplete(const wxString& fileName);
  void LogUploadFailed(const wxString& fileName, const wxString& error);
  void LogReaction(const wxString& username, const wxString& emoji, 
                   const wxString& chatName, int64_t chatId = 0);
  void LogUserJoined(const wxString& username, const wxString& chatName, int64_t chatId = 0);
  void LogUserLeft(const wxString& username, const wxString& chatName, int64_t chatId = 0);
  void LogSystem(const wxString& message);
  void LogError(const wxString& error);
  
  // Get recent messages (for display)
  std::vector<ServiceMessage> GetRecentMessages(size_t count = 50) const;
  
  // Clear all messages
  void Clear();
  
  // Settings
  void SetMaxMessages(size_t max) { m_maxMessages = max; }
  void SetRotationInterval(int ms) { m_rotationIntervalMs = ms; }
  void SetLogToWelcomeChat(bool enable) { m_logToWelcomeChat = enable; }
  void SetShowInStatusBar(bool enable) { m_showInStatusBar = enable; }
  
  // Filter settings - control what types are shown
  void SetTypeEnabled(ServiceMessageType type, bool enabled);
  bool IsTypeEnabled(ServiceMessageType type) const;
  
private:
  void OnRotationTimer(wxTimerEvent& event);
  void RotateStatusMessage();
  void LogToWelcomeChat(const ServiceMessage& msg);
  wxString FormatForStatusBar(const ServiceMessage& msg) const;
  wxString FormatTimestamp(const wxDateTime& dt) const;
  wxString GetTypeIcon(ServiceMessageType type) const;
  
  // UI references
  WelcomeChat* m_welcomeChat;
  StatusBarManager* m_statusBar;
  TelegramClient* m_telegramClient;
  
  // Message storage
  std::deque<ServiceMessage> m_messages;
  mutable std::mutex m_messagesMutex;
  size_t m_maxMessages;
  
  // Rotation state
  wxTimer m_rotationTimer;
  size_t m_currentRotationIndex;
  int m_rotationIntervalMs;
  bool m_isRunning;
  
  // Settings
  bool m_logToWelcomeChat;
  bool m_showInStatusBar;
  std::set<ServiceMessageType> m_enabledTypes;
  
  // Track last shown message to avoid repeats
  wxString m_lastStatusMessage;
  
  // Coalesce rapid events (e.g., multiple users typing)
  wxDateTime m_lastTypingLog;
  wxDateTime m_lastOnlineLog;
  static constexpr int COALESCE_INTERVAL_MS = 2000;
  
  // Track logged message IDs to avoid duplicate notifications
  std::set<int64_t> m_loggedMessageIds;
  std::set<int64_t> m_loggedUserOnlineIds;
  static constexpr size_t MAX_TRACKED_IDS = 1000;
  
  // Clean up old tracked IDs periodically
  void CleanupTrackedIds();
};

#endif // SERVICEMESSAGELOG_H