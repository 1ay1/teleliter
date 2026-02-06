#ifndef STATUSBARMANAGER_H
#define STATUSBARMANAGER_H

#include "../telegram/TransferTypes.h"
#include <deque>
#include <wx/stopwatch.h>
#include <wx/wx.h>

// Forward declarations
class TelegramClient;

class StatusBarManager : public wxEvtHandler {
public:
  StatusBarManager(wxFrame *parent);
  ~StatusBarManager();

  // Initialize the status bar
  void Setup();

  // Update methods
  void UpdateStatusBar();
  void UpdateTransferProgress(const TransferInfo &info);
  void OnTransferComplete(const TransferInfo &info);
  void OnTransferError(const TransferInfo &info);

  // State setters
  void SetOnline(bool online) { m_isOnline = online; }
  void SetLoggedIn(bool loggedIn) { m_isLoggedIn = loggedIn; }
  void SetCurrentUser(const wxString &user) { m_currentUser = user; }
  void SetCurrentChatTitle(const wxString &title) {
    m_currentChatTitle = title;
  }
  void SetCurrentChatId(int64_t chatId) { m_currentChatId = chatId; }
  void SetCurrentChatMemberCount(int count) {
    m_currentChatMemberCount = count;
  }
  void SetTotalChats(int count) { m_totalChats = count; }
  void SetUnreadChats(int count) { m_unreadChats = count; }
  void SetConnectionDC(const wxString &dc) { m_connectionDC = dc; }
  void SetTelegramClient(TelegramClient *client) { m_telegramClient = client; }

  // Reset session timer (call on login)
  void ResetSessionTimer() { m_sessionTimer.Start(); }

  // Hide transfer progress
  void HideTransferProgress();

  // Check if has active transfers (for external queries)
  bool HasActiveTransfers() const { return m_hasActiveTransfers; }
  void SetHasActiveTransfers(bool active) { m_hasActiveTransfers = active; }

  // Get active transfer count (for external display)
  void SetActiveTransferCount(int count) { m_activeTransferCount = count; }

  // Override status functionality (for service messages)
  void SetOverrideStatus(const wxString &text) {
    m_serviceMessageText = text;
    // Only show service message if no typing indicator
    if (!m_isTypingIndicator) {
      m_overrideStatusText = text;
      UpdateStatusBar();
    }
  }
  void ClearOverrideStatus() {
    m_serviceMessageText.clear();
    if (!m_isTypingIndicator) {
      m_overrideStatusText.clear();
      UpdateStatusBar();
    }
  }
  
  // Get current service message (for display when typing stops)
  wxString GetServiceMessage() const { return m_serviceMessageText; }

  // Typing indicator with animation
  void SetTypingIndicator(const wxString &text);
  void ClearTypingIndicator();
  bool HasTypingIndicator() const { return m_isTypingIndicator; }

  // Font control
  void SetFont(const wxFont &font);
  
  // Theme support
  void RefreshTheme();

private:
  wxFrame *m_parent;
  wxStatusBar *m_statusBar;

  // Override status text (takes precedence over chat info)
  wxString m_overrideStatusText;
  
  // Service message text (persists when typing indicator is active)
  wxString m_serviceMessageText;
  bool m_isTypingIndicator;
  int m_typingAnimFrame;
  wxTimer m_typingAnimTimer;

  // Status bar widgets - custom labels for proper color control
  wxStaticText *m_mainLabel;      // Field 0: chat info / transfers
  wxStaticText *m_sessionLabel;   // Field 1: session time
  wxStaticText *m_connectionLabel; // Field 2: connection status
  
  // Typing indicator label (in field 0, colored for visibility)
  wxStaticText *m_typingLabel;

  // Legacy pointers (unused, kept for compatibility)
  wxGauge *m_progressGauge;
  wxStaticText *m_progressLabel;

  // Transfer display state
  int m_transferAnimFrame;
  wxStopWatch m_transferTimer;
  int64_t m_lastTransferredBytes;
  std::deque<std::pair<long, int64_t>> m_speedSamples;
  double m_currentSpeed;
  bool m_hasActiveTransfers;
  int m_activeTransferCount;

  // Session timer
  wxStopWatch m_sessionTimer;

  // State tracking
  bool m_isOnline;
  bool m_isLoggedIn;
  wxString m_currentUser;
  wxString m_currentChatTitle;
  int64_t m_currentChatId;
  int m_currentChatMemberCount;
  int m_totalChats;
  int m_unreadChats;
  wxString m_connectionDC;

  // Colors
  wxColour m_bgColor;
  wxColour m_fgColor;
  wxColour m_onlineColor;
  wxColour m_connectingColor;
  wxColour m_offlineColor;
  wxColour m_successColor;
  wxColour m_errorColor;

  // Reference to telegram client for connection state
  TelegramClient *m_telegramClient;

  // Helper methods
  wxString FormatSpeed(double bytesPerSecond) const;
  wxString FormatSize(int64_t bytes) const;
  wxString FormatSizeProgress(int64_t transferred, int64_t total) const;
  wxString FormatETA(int64_t remaining, double speed) const;
  wxString BuildProgressBar(int percent, int width = 10) const;

  // Layout helpers
  void RepositionWidgets();
  void OnStatusBarResize(wxSizeEvent &event);
  void OnTypingAnimTimer(wxTimerEvent &event);
};

#endif // STATUSBARMANAGER_H