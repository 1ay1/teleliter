#include "ServiceMessageLog.h"
#include "WelcomeChat.h"
#include "StatusBarManager.h"
#include "../telegram/TelegramClient.h"
#include <algorithm>
#include <iterator>

ServiceMessageLog::ServiceMessageLog()
    : wxEvtHandler(), m_welcomeChat(nullptr), m_statusBar(nullptr),
      m_telegramClient(nullptr), m_maxMessages(500),
      m_rotationTimer(this, wxID_ANY), m_currentRotationIndex(0),
      m_rotationIntervalMs(4000), m_isRunning(false),
      m_logToWelcomeChat(true), m_showInStatusBar(true) {
  
  // Enable all message types by default
  m_enabledTypes.insert(ServiceMessageType::UserOnline);
  m_enabledTypes.insert(ServiceMessageType::UserOffline);
  m_enabledTypes.insert(ServiceMessageType::UserTyping);
  m_enabledTypes.insert(ServiceMessageType::UserAction);
  m_enabledTypes.insert(ServiceMessageType::ChatRead);
  m_enabledTypes.insert(ServiceMessageType::MessageDeleted);
  m_enabledTypes.insert(ServiceMessageType::MessageEdited);
  m_enabledTypes.insert(ServiceMessageType::NewMessage);
  m_enabledTypes.insert(ServiceMessageType::ConnectionState);
  m_enabledTypes.insert(ServiceMessageType::Download);
  m_enabledTypes.insert(ServiceMessageType::Upload);
  m_enabledTypes.insert(ServiceMessageType::Reaction);
  m_enabledTypes.insert(ServiceMessageType::Join);
  m_enabledTypes.insert(ServiceMessageType::Leave);
  m_enabledTypes.insert(ServiceMessageType::System);
  m_enabledTypes.insert(ServiceMessageType::Error);
  
  // Bind timer event
  Bind(wxEVT_TIMER, &ServiceMessageLog::OnRotationTimer, this,
       m_rotationTimer.GetId());
}

ServiceMessageLog::~ServiceMessageLog() {
  Stop();
}

void ServiceMessageLog::Start() {
  if (!m_isRunning) {
    m_isRunning = true;
    m_rotationTimer.Start(m_rotationIntervalMs);
  }
}

void ServiceMessageLog::Stop() {
  if (m_isRunning) {
    m_isRunning = false;
    m_rotationTimer.Stop();
  }
}

void ServiceMessageLog::Log(ServiceMessageType type, const wxString& text,
                            const wxString& detail, int64_t relatedId) {
  if (!IsTypeEnabled(type)) {
    return;
  }
  
  ServiceMessage msg(type, text, detail, relatedId);
  
  {
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    m_messages.push_back(msg);
    
    // Trim old messages
    while (m_messages.size() > m_maxMessages) {
      m_messages.pop_front();
    }
  }
  
  // Log to WelcomeChat
  if (m_logToWelcomeChat) {
    LogToWelcomeChat(msg);
  }
  
  // Show in status bar immediately for important messages
  if (m_showInStatusBar && m_statusBar) {
    // For high-priority messages, show immediately
    if (type == ServiceMessageType::ConnectionState ||
        type == ServiceMessageType::Error ||
        type == ServiceMessageType::NewMessage) {
      wxString statusText = FormatForStatusBar(msg);
      if (statusText != m_lastStatusMessage) {
        m_statusBar->SetOverrideStatus(statusText);
        m_lastStatusMessage = statusText;
        
        // Reset rotation to show this message longer
        m_currentRotationIndex = m_messages.size() - 1;
      }
    }
  }
}

void ServiceMessageLog::LogUserOnline(const wxString& username, int64_t userId) {
  // Coalesce rapid online events
  wxDateTime now = wxDateTime::Now();
  if (m_lastOnlineLog.IsValid() && 
      (now - m_lastOnlineLog).GetMilliseconds() < COALESCE_INTERVAL_MS) {
    return;
  }
  m_lastOnlineLog = now;
  
  // Deduplicate: don't log same user online repeatedly
  if (userId != 0) {
    if (m_loggedUserOnlineIds.find(userId) != m_loggedUserOnlineIds.end()) {
      return;
    }
    m_loggedUserOnlineIds.insert(userId);
    CleanupTrackedIds();
  }
  
  Log(ServiceMessageType::UserOnline, 
      username + " came online", username, userId);
}

void ServiceMessageLog::LogUserOffline(const wxString& username, 
                                        const wxString& lastSeen, int64_t userId) {
  // Remove from online tracking when they go offline
  if (userId != 0) {
    m_loggedUserOnlineIds.erase(userId);
  }
  
  wxString text = username + " went offline";
  if (!lastSeen.IsEmpty()) {
    text += " (" + lastSeen + ")";
  }
  Log(ServiceMessageType::UserOffline, text, username, userId);
}

void ServiceMessageLog::LogUserTyping(const wxString& username, 
                                       const wxString& chatName, int64_t chatId) {
  // Coalesce rapid typing events
  wxDateTime now = wxDateTime::Now();
  if (m_lastTypingLog.IsValid() && 
      (now - m_lastTypingLog).GetMilliseconds() < COALESCE_INTERVAL_MS) {
    return;
  }
  m_lastTypingLog = now;
  
  wxString text = username + " is typing";
  if (!chatName.IsEmpty()) {
    text += " in " + chatName;
  }
  Log(ServiceMessageType::UserTyping, text, chatName, chatId);
}

void ServiceMessageLog::LogUserAction(const wxString& username, const wxString& action,
                                       const wxString& chatName, int64_t chatId) {
  wxString text = username + " is " + action;
  if (!chatName.IsEmpty()) {
    text += " in " + chatName;
  }
  Log(ServiceMessageType::UserAction, text, chatName, chatId);
}

void ServiceMessageLog::LogMessageRead(const wxString& username, 
                                        const wxString& chatName, int64_t chatId) {
  wxString text = username + " read messages";
  if (!chatName.IsEmpty()) {
    text += " in " + chatName;
  }
  Log(ServiceMessageType::ChatRead, text, chatName, chatId);
}

void ServiceMessageLog::LogNewMessage(const wxString& sender, const wxString& chatName,
                                       const wxString& preview, int64_t chatId,
                                       int64_t messageId) {
  // Deduplicate: don't log same message twice
  if (messageId != 0) {
    if (m_loggedMessageIds.find(messageId) != m_loggedMessageIds.end()) {
      return;
    }
    m_loggedMessageIds.insert(messageId);
    CleanupTrackedIds();
  }
  
  wxString text = "New message from " + sender;
  if (!chatName.IsEmpty() && chatName != sender) {
    text += " in " + chatName;
  }
  if (!preview.IsEmpty()) {
    // Truncate preview
    wxString shortPreview = preview;
    if (shortPreview.length() > 50) {
      shortPreview = shortPreview.Left(47) + "...";
    }
    text += ": " + shortPreview;
  }
  Log(ServiceMessageType::NewMessage, text, chatName, chatId);
}

void ServiceMessageLog::CleanupTrackedIds() {
  // Prevent unbounded growth of tracking sets
  if (m_loggedMessageIds.size() > MAX_TRACKED_IDS) {
    // Clear oldest entries by clearing half
    auto it = m_loggedMessageIds.begin();
    std::advance(it, m_loggedMessageIds.size() / 2);
    m_loggedMessageIds.erase(m_loggedMessageIds.begin(), it);
  }
  
  if (m_loggedUserOnlineIds.size() > MAX_TRACKED_IDS) {
    auto it = m_loggedUserOnlineIds.begin();
    std::advance(it, m_loggedUserOnlineIds.size() / 2);
    m_loggedUserOnlineIds.erase(m_loggedUserOnlineIds.begin(), it);
  }
}

void ServiceMessageLog::LogConnectionState(const wxString& state) {
  Log(ServiceMessageType::ConnectionState, "Connection: " + state);
}

void ServiceMessageLog::LogDownloadStarted(const wxString& fileName, int64_t fileSize) {
  wxString text = "Downloading " + fileName;
  if (fileSize > 0) {
    if (fileSize >= 1024 * 1024) {
      text += wxString::Format(" (%.1f MB)", fileSize / (1024.0 * 1024.0));
    } else if (fileSize >= 1024) {
      text += wxString::Format(" (%.0f KB)", fileSize / 1024.0);
    }
  }
  Log(ServiceMessageType::Download, text, fileName);
}

void ServiceMessageLog::LogDownloadComplete(const wxString& fileName) {
  Log(ServiceMessageType::Download, "Downloaded " + fileName, fileName);
}

void ServiceMessageLog::LogDownloadFailed(const wxString& fileName, const wxString& error) {
  Log(ServiceMessageType::Error, "Download failed: " + fileName + " - " + error, fileName);
}

void ServiceMessageLog::LogUploadStarted(const wxString& fileName, int64_t fileSize) {
  wxString text = "Uploading " + fileName;
  if (fileSize > 0) {
    if (fileSize >= 1024 * 1024) {
      text += wxString::Format(" (%.1f MB)", fileSize / (1024.0 * 1024.0));
    } else if (fileSize >= 1024) {
      text += wxString::Format(" (%.0f KB)", fileSize / 1024.0);
    }
  }
  Log(ServiceMessageType::Upload, text, fileName);
}

void ServiceMessageLog::LogUploadComplete(const wxString& fileName) {
  Log(ServiceMessageType::Upload, "Uploaded " + fileName, fileName);
}

void ServiceMessageLog::LogUploadFailed(const wxString& fileName, const wxString& error) {
  Log(ServiceMessageType::Error, "Upload failed: " + fileName + " - " + error, fileName);
}

void ServiceMessageLog::LogReaction(const wxString& username, const wxString& emoji,
                                     const wxString& chatName, int64_t chatId) {
  wxString text = username + " reacted " + emoji;
  if (!chatName.IsEmpty()) {
    text += " in " + chatName;
  }
  Log(ServiceMessageType::Reaction, text, chatName, chatId);
}

void ServiceMessageLog::LogUserJoined(const wxString& username, 
                                       const wxString& chatName, int64_t chatId) {
  wxString text = username + " joined " + chatName;
  Log(ServiceMessageType::Join, text, chatName, chatId);
}

void ServiceMessageLog::LogUserLeft(const wxString& username,
                                     const wxString& chatName, int64_t chatId) {
  wxString text = username + " left " + chatName;
  Log(ServiceMessageType::Leave, text, chatName, chatId);
}

void ServiceMessageLog::LogSystem(const wxString& message) {
  Log(ServiceMessageType::System, message);
}

void ServiceMessageLog::LogError(const wxString& error) {
  Log(ServiceMessageType::Error, error);
}

std::vector<ServiceMessage> ServiceMessageLog::GetRecentMessages(size_t count) const {
  std::lock_guard<std::mutex> lock(m_messagesMutex);
  
  std::vector<ServiceMessage> result;
  size_t start = (m_messages.size() > count) ? m_messages.size() - count : 0;
  
  for (size_t i = start; i < m_messages.size(); i++) {
    result.push_back(m_messages[i]);
  }
  
  return result;
}

void ServiceMessageLog::Clear() {
  std::lock_guard<std::mutex> lock(m_messagesMutex);
  m_messages.clear();
  m_currentRotationIndex = 0;
}

void ServiceMessageLog::SetTypeEnabled(ServiceMessageType type, bool enabled) {
  if (enabled) {
    m_enabledTypes.insert(type);
  } else {
    m_enabledTypes.erase(type);
  }
}

bool ServiceMessageLog::IsTypeEnabled(ServiceMessageType type) const {
  return m_enabledTypes.find(type) != m_enabledTypes.end();
}

void ServiceMessageLog::OnRotationTimer(wxTimerEvent& event) {
  RotateStatusMessage();
}

void ServiceMessageLog::RotateStatusMessage() {
  if (!m_showInStatusBar || !m_statusBar) {
    return;
  }
  
  std::lock_guard<std::mutex> lock(m_messagesMutex);
  
  if (m_messages.empty()) {
    return;
  }
  
  // Prioritize messages for display:
  // 1. Recent high-priority messages (errors, new messages, connection changes)
  // 2. Recent general messages
  // 3. Older interesting messages
  
  wxDateTime now = wxDateTime::Now();
  std::vector<size_t> priorityIndices;
  std::vector<size_t> recentIndices;
  
  // Scan messages from newest to oldest
  for (size_t i = m_messages.size(); i > 0; --i) {
    size_t idx = i - 1;
    const ServiceMessage& msg = m_messages[idx];
    
    // How old is this message?
    wxTimeSpan age = now - msg.timestamp;
    int ageMinutes = age.GetMinutes();
    
    // High priority: errors, new messages, connection within last 5 minutes
    if (ageMinutes < 5) {
      if (msg.type == ServiceMessageType::Error ||
          msg.type == ServiceMessageType::NewMessage ||
          msg.type == ServiceMessageType::ConnectionState ||
          msg.type == ServiceMessageType::UserOnline) {
        priorityIndices.push_back(idx);
        if (priorityIndices.size() >= 10) break;
      } else if (recentIndices.size() < 15) {
        recentIndices.push_back(idx);
      }
    } else if (ageMinutes < 30 && recentIndices.size() < 15) {
      recentIndices.push_back(idx);
    }
    
    // Limit scan depth
    if (priorityIndices.size() + recentIndices.size() >= 20) break;
  }
  
  // Combine priority and recent, priority first
  std::vector<size_t> displayIndices;
  displayIndices.insert(displayIndices.end(), priorityIndices.begin(), priorityIndices.end());
  displayIndices.insert(displayIndices.end(), recentIndices.begin(), recentIndices.end());
  
  if (displayIndices.empty()) {
    // Fallback: show most recent message
    displayIndices.push_back(m_messages.size() - 1);
  }
  
  // Rotate through the display indices
  m_currentRotationIndex = (m_currentRotationIndex + 1) % displayIndices.size();
  size_t actualIndex = displayIndices[m_currentRotationIndex];
  
  if (actualIndex < m_messages.size()) {
    const ServiceMessage& msg = m_messages[actualIndex];
    wxString statusText = FormatForStatusBar(msg);
    
    // Only update if different
    if (statusText != m_lastStatusMessage) {
      m_statusBar->SetOverrideStatus(statusText);
      m_lastStatusMessage = statusText;
    }
  }
}

void ServiceMessageLog::LogToWelcomeChat(const ServiceMessage& msg) {
  if (!m_welcomeChat) {
    return;
  }
  
  ChatArea* chatArea = m_welcomeChat->GetChatArea();
  if (!chatArea) {
    return;
  }
  
  wxString timestamp = msg.timestamp.Format("%H:%M:%S");
  wxString icon = GetTypeIcon(msg.type);
  
  // Use appropriate ChatArea method based on message type
  switch (msg.type) {
    case ServiceMessageType::Error:
      chatArea->AppendError(msg.text);
      break;
      
    case ServiceMessageType::ConnectionState:
      if (msg.text.Contains("Ready") || msg.text.Contains("Online")) {
        chatArea->AppendSuccess(icon + " " + msg.text);
      } else {
        chatArea->AppendInfo(icon + " " + msg.text);
      }
      break;
      
    case ServiceMessageType::UserOnline:
      chatArea->AppendJoin(timestamp, msg.detail + " came online");
      break;
      
    case ServiceMessageType::UserOffline:
      chatArea->AppendLeave(timestamp, msg.detail + " went offline");
      break;
      
    case ServiceMessageType::Join:
      chatArea->AppendJoin(timestamp, msg.text);
      break;
      
    case ServiceMessageType::Leave:
      chatArea->AppendLeave(timestamp, msg.text);
      break;
      
    case ServiceMessageType::NewMessage:
      // Use service color for new message notifications
      chatArea->AppendService(icon + " " + msg.text);
      break;
      
    case ServiceMessageType::Download:
    case ServiceMessageType::Upload:
      if (msg.text.StartsWith("Downloaded") || msg.text.StartsWith("Uploaded")) {
        chatArea->AppendSuccess(icon + " " + msg.text);
      } else {
        chatArea->AppendService(icon + " " + msg.text);
      }
      break;
      
    case ServiceMessageType::Reaction:
      chatArea->AppendAction(timestamp, "", icon + " " + msg.text);
      break;
      
    case ServiceMessageType::UserTyping:
    case ServiceMessageType::UserAction:
      // These are transient, use service style
      chatArea->AppendService(icon + " " + msg.text);
      break;
      
    case ServiceMessageType::MessageEdited:
    case ServiceMessageType::MessageDeleted:
    case ServiceMessageType::ChatRead:
      chatArea->AppendService(icon + " " + msg.text);
      break;
      
    case ServiceMessageType::System:
    default:
      chatArea->AppendInfo(icon + " " + msg.text);
      break;
  }
  
  chatArea->ScrollToBottomIfAtBottom();
}

wxString ServiceMessageLog::FormatForStatusBar(const ServiceMessage& msg) const {
  wxString icon = GetTypeIcon(msg.type);
  wxString time = FormatTimestamp(msg.timestamp);
  
  // Calculate how long ago this message was
  wxDateTime now = wxDateTime::Now();
  wxTimeSpan age = now - msg.timestamp;
  wxString ageStr;
  
  if (age.GetMinutes() < 1) {
    ageStr = "now";
  } else if (age.GetMinutes() < 60) {
    ageStr = wxString::Format("%dm ago", (int)age.GetMinutes());
  } else if (age.GetHours() < 24) {
    ageStr = wxString::Format("%dh ago", (int)age.GetHours());
  } else {
    ageStr = time;
  }
  
  return icon + " " + msg.text + " [" + ageStr + "]";
}

wxString ServiceMessageLog::FormatTimestamp(const wxDateTime& dt) const {
  return dt.Format("%H:%M");
}

wxString ServiceMessageLog::GetTypeIcon(ServiceMessageType type) const {
  switch (type) {
    case ServiceMessageType::UserOnline:
      return wxString::FromUTF8("\xE2\x97\x8F");  // U+25CF BLACK CIRCLE
    case ServiceMessageType::UserOffline:
      return wxString::FromUTF8("\xE2\x97\x8B");  // U+25CB WHITE CIRCLE
    case ServiceMessageType::UserTyping:
      return wxString::FromUTF8("\xE2\x9C\x8F");  // U+270F PENCIL
    case ServiceMessageType::UserAction:
      return wxString::FromUTF8("\xE2\x97\x90");  // U+25D0 CIRCLE WITH LEFT HALF BLACK
    case ServiceMessageType::ChatRead:
      return wxString::FromUTF8("\xE2\x9C\x93\xE2\x9C\x93"); // U+2713 CHECK MARK x2
    case ServiceMessageType::MessageDeleted:
      return wxString::FromUTF8("\xE2\x9C\x97");  // U+2717 BALLOT X
    case ServiceMessageType::MessageEdited:
      return wxString::FromUTF8("\xE2\x9C\x8E");  // U+270E LOWER RIGHT PENCIL
    case ServiceMessageType::NewMessage:
      return wxString::FromUTF8("\xE2\x9C\x89");  // U+2709 ENVELOPE
    case ServiceMessageType::ConnectionState:
      return wxString::FromUTF8("\xE2\x9A\xA1");  // U+26A1 HIGH VOLTAGE SIGN
    case ServiceMessageType::Download:
      return wxString::FromUTF8("\xE2\x86\x93");  // U+2193 DOWNWARDS ARROW
    case ServiceMessageType::Upload:
      return wxString::FromUTF8("\xE2\x86\x91");  // U+2191 UPWARDS ARROW
    case ServiceMessageType::Reaction:
      return wxString::FromUTF8("\xE2\x99\xA5");  // U+2665 BLACK HEART SUIT
    case ServiceMessageType::Join:
      return wxString::FromUTF8("\xE2\x86\x92");  // U+2192 RIGHTWARDS ARROW
    case ServiceMessageType::Leave:
      return wxString::FromUTF8("\xE2\x86\x90");  // U+2190 LEFTWARDS ARROW
    case ServiceMessageType::Error:
      return wxString::FromUTF8("\xE2\x9A\xA0");  // U+26A0 WARNING SIGN
    case ServiceMessageType::System:
    default:
      return "*";
  }
}