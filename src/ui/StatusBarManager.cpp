#include "StatusBarManager.h"
#include "../telegram/TelegramClient.h"
#include "../telegram/Types.h"
#include <wx/settings.h>

StatusBarManager::StatusBarManager(wxFrame *parent)
    : wxEvtHandler(), m_parent(parent), m_statusBar(nullptr),
      m_isTypingIndicator(false), m_typingAnimFrame(0),
      m_typingAnimTimer(this, wxID_ANY), m_connectionLabel(nullptr),
      m_typingLabel(nullptr), m_progressGauge(nullptr),
      m_progressLabel(nullptr), m_transferAnimFrame(0),
      m_lastTransferredBytes(0), m_currentSpeed(0.0),
      m_hasActiveTransfers(false), m_activeTransferCount(0), m_isOnline(false),
      m_isLoggedIn(false), m_currentChatId(0), m_currentChatMemberCount(0),
      m_totalChats(0), m_unreadChats(0),
      m_bgColor(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE)),
      m_fgColor(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT)),
      m_onlineColor(0x00, 0x80,
                    0x00), // Green (semantic - no system equivalent)
      m_connectingColor(wxSystemSettings::GetColour(wxSYS_COLOUR_HOTLIGHT)),
      m_offlineColor(0xCC, 0x00, 0x00), // Red (semantic - no system equivalent)
      m_successColor(0x00, 0x80,
                     0x00),           // Green (semantic - no system equivalent)
      m_errorColor(0xCC, 0x00, 0x00), // Red (semantic - no system equivalent)
      m_telegramClient(nullptr) {
  m_transferTimer.Start();
  m_sessionTimer.Start();

  // Bind typing animation timer
  Bind(wxEVT_TIMER, &StatusBarManager::OnTypingAnimTimer, this,
       m_typingAnimTimer.GetId());
}

StatusBarManager::~StatusBarManager() {}

void StatusBarManager::Setup() {
  if (!m_parent)
    return;

  // Create status bar with 3 fields:
  // [chat info / transfer progress] [session time] [connection status]
  m_statusBar = m_parent->CreateStatusBar(3);
  // Font will be set from MainFrame via SetFont()

  // Field widths: main area (flexible), session time (fixed), connection
  // (fixed)
  int widths[] = {-1, 130, 120};
  m_statusBar->SetStatusWidths(3, widths);

  // Bind size event to reposition widgets when status bar resizes
  m_statusBar->Bind(wxEVT_SIZE, &StatusBarManager::OnStatusBarResize, this);

  // Create connection status label (in field 2) with color support
  m_connectionLabel = new wxStaticText(m_statusBar, wxID_ANY, "");
  // Font will be set from MainFrame via SetFont()

  wxRect connRect;
  m_statusBar->GetFieldRect(2, connRect);
  m_connectionLabel->SetPosition(wxPoint(connRect.x + 4, connRect.y + 2));
  m_connectionLabel->SetSize(connRect.width - 8, connRect.height - 4);

  // Create typing indicator label (in field 0) with colored text for visibility
  m_typingLabel = new wxStaticText(m_statusBar, wxID_ANY, "");
  m_typingLabel->Hide(); // Initially hidden
  // Font will be set from MainFrame via SetFont()

  // No separate progress label/gauge - we use the main status text field
  m_progressLabel = nullptr;
  m_progressGauge = nullptr;

  // Initial status
  m_parent->SetStatusText("Not logged in", 0);
  m_parent->SetStatusText("00:00:00", 1);
}

void StatusBarManager::RepositionWidgets() {
  if (!m_statusBar)
    return;

  // Reposition connection label
  if (m_connectionLabel) {
    wxRect connRect;
    m_statusBar->GetFieldRect(2, connRect);
    m_connectionLabel->SetPosition(wxPoint(connRect.x + 4, connRect.y + 2));
    m_connectionLabel->SetSize(connRect.width - 8, connRect.height - 4);
  }

  // Reposition typing label - calculate proper vertical centering
  if (m_typingLabel) {
    wxRect mainRect;
    m_statusBar->GetFieldRect(0, mainRect);

    // Center the label vertically within the field
    wxSize labelSize = m_typingLabel->GetBestSize();
    int yOffset = (mainRect.height - labelSize.GetHeight()) / 2;
    if (yOffset < 0)
      yOffset = 0;

    m_typingLabel->SetPosition(wxPoint(mainRect.x + 4, mainRect.y + yOffset));
    m_typingLabel->SetSize(mainRect.width - 8, labelSize.GetHeight());
  }
}

void StatusBarManager::OnStatusBarResize(wxSizeEvent &event) {
  RepositionWidgets();
  event.Skip();
}

void StatusBarManager::SetFont(const wxFont &font) {
  if (m_statusBar && font.IsOk()) {
    m_statusBar->SetFont(font);
  }
  if (m_connectionLabel && font.IsOk()) {
    m_connectionLabel->SetFont(font);
  }
  if (m_typingLabel && font.IsOk()) {
    m_typingLabel->SetFont(font);
  }
}

void StatusBarManager::UpdateStatusBar() {
  if (!m_parent || !m_statusBar)
    return;

  // Field 0: Chat info (only update if no active transfer)
  // Handle typing indicator - use native SetStatusText for proper alignment
  if (m_isTypingIndicator && !m_overrideStatusText.IsEmpty()) {
    // Show typing indicator with animated dots
    static const wxString dotPatterns[] = {"", ".", "..", "..."};
    wxString dots = dotPatterns[m_typingAnimFrame % 4];

    // U+270F PENCIL
    wxString typingText = wxString::FromUTF8("\xE2\x9C\x8F ") + m_overrideStatusText + dots;

    // Use native SetStatusText for proper vertical alignment
    m_parent->SetStatusText(typingText, 0);
  } else if (!m_overrideStatusText.IsEmpty()) {
    if (m_typingLabel) {
      m_typingLabel->Hide();
    }
    m_parent->SetStatusText(m_overrideStatusText, 0);
  } else if (!m_hasActiveTransfers) {
    if (m_typingLabel) {
      m_typingLabel->Hide();
    }
    wxString chatInfo;
    if (m_isLoggedIn) {
      if (m_currentChatId != 0) {
        chatInfo = m_currentChatTitle;
        if (m_currentChatMemberCount > 0) {
          chatInfo +=
              wxString::Format(" - %d members", m_currentChatMemberCount);
        }
      } else {
        chatInfo = wxString::Format("%d chats", m_totalChats);
        if (m_unreadChats > 0) {
          chatInfo += wxString::Format(" - %d unread", m_unreadChats);
        }
      }
      chatInfo += " - @" + m_currentUser;
    } else {
      chatInfo = "Not logged in";
    }
    m_parent->SetStatusText(chatInfo, 0);
  } else {
    // Active transfer - hide typing label
    if (m_typingLabel) {
      m_typingLabel->Hide();
    }
  }

  // Field 1: Session time
  long elapsed = m_sessionTimer.Time() / 1000;
  int hours = elapsed / 3600;
  int minutes = (elapsed % 3600) / 60;
  int seconds = elapsed % 60;
  wxString sessionTime =
      wxString::Format("Uptime: %02d:%02d:%02d", hours, minutes, seconds);
  m_parent->SetStatusText(sessionTime, 1);

  // Field 2: Connection status with colors
  // Use ASCII-compatible characters for cross-platform support (Linux fonts may
  // not have Unicode bullets)
  if (m_connectionLabel) {
    wxString connStatus;
    wxColour connColor;

    // Use actual TDLib connection state for accurate status
    if (m_telegramClient) {
      ConnectionState state = m_telegramClient->GetConnectionState();

      switch (state) {
      case ConnectionState::Ready:
        connStatus = "[*] Online";
        if (!m_connectionDC.IsEmpty()) {
          connStatus += " " + m_connectionDC;
        }
        connColor = m_onlineColor;
        break;
      case ConnectionState::Updating:
        connStatus = "[~] Syncing...";
        connColor = m_connectingColor;
        break;
      case ConnectionState::Connecting:
      case ConnectionState::ConnectingToProxy: {
        // Animated connecting indicator using ASCII
        static const wxString spinners[] = {"|", "/", "-", "\\"};
        static int spinFrame = 0;
        spinFrame = (spinFrame + 1) % 4;
        connStatus = "[" + spinners[spinFrame] + "] Connecting...";
        connColor = m_connectingColor;
        break;
      }
      case ConnectionState::WaitingForNetwork:
        connStatus = "[!] No Network";
        connColor = m_offlineColor;
        break;
      default:
        connStatus = "[ ] Offline";
        connColor = m_offlineColor;
        break;
      }
    } else {
      connStatus = "[ ] Offline";
      connColor = m_offlineColor;
    }

    m_connectionLabel->SetForegroundColour(connColor);
    m_connectionLabel->SetLabel(connStatus);
  }
}

void StatusBarManager::UpdateTransferProgress(const TransferInfo &info) {
  if (!m_parent || !m_statusBar)
    return;

  m_hasActiveTransfers = true;

  // Animated spinner characters (ASCII for cross-platform support)
  static const wxString spinners[] = {"|", "/", "-", "\\"};
  m_transferAnimFrame = (m_transferAnimFrame + 1) % 4;
  wxString spinner = "[" + spinners[m_transferAnimFrame] + "]";

  // Calculate speed
  long currentTime = m_transferTimer.Time();
  m_speedSamples.push_back({currentTime, info.transferredBytes});

  // Keep only last 2 seconds of samples
  while (!m_speedSamples.empty() &&
         (currentTime - m_speedSamples.front().first) > 2000) {
    m_speedSamples.pop_front();
  }

  // Calculate speed from samples
  if (m_speedSamples.size() >= 2) {
    auto &oldest = m_speedSamples.front();
    auto &newest = m_speedSamples.back();
    long timeDelta = newest.first - oldest.first;
    int64_t bytesDelta = newest.second - oldest.second;
    if (timeDelta > 0) {
      m_currentSpeed = (bytesDelta * 1000.0) / timeDelta;
    }
  }

  // Format components
  wxString speedStr = FormatSpeed(m_currentSpeed);

  // Calculate ETA
  wxString etaStr;
  if (m_currentSpeed > 0 && info.totalBytes > info.transferredBytes) {
    int64_t remaining = info.totalBytes - info.transferredBytes;
    etaStr = FormatETA(remaining, m_currentSpeed);
  }

  // Build ASCII progress bar (using # and - for cross-platform support)
  int percent = info.GetProgressPercent();
  wxString progressBar = BuildProgressBar(percent, 10);

  // Direction symbol (ASCII for cross-platform support)
  wxString dirSymbol = info.direction == TransferDirection::Upload ? "^" : "v";

  // Truncate filename if too long
  wxString fileName = info.fileName;
  if (fileName.length() > 20) {
    fileName = fileName.Left(17) + "...";
  }

  // Build final label: "[|] v file.jpg [######----] 45% 1.2MB/s ~5s"
  wxString label = spinner + " " + dirSymbol + " " + fileName + " [" +
                   progressBar + "] " + wxString::Format("%d%%", percent);

  if (!speedStr.IsEmpty()) {
    label += " " + speedStr;
  }
  if (!etaStr.IsEmpty()) {
    label += etaStr;
  }

  // If multiple transfers, append count
  if (m_activeTransferCount > 1) {
    label += wxString::Format(" (+%d more)", m_activeTransferCount - 1);
  }

  // Update the main status field with transfer progress
  m_parent->SetStatusText(label, 0);
}

void StatusBarManager::OnTransferComplete(const TransferInfo &info) {
  if (!m_parent || !m_statusBar)
    return;

  // Reset speed tracking
  m_speedSamples.clear();
  m_currentSpeed = 0.0;
  m_lastTransferredBytes = 0;

  // Direction symbol (ASCII)
  wxString dirSymbol = info.direction == TransferDirection::Upload ? "^" : "v";

  // Show completion with checkmark
  wxString label =
      "[OK] " + dirSymbol + " " + info.fileName + " [==========] Done!";
  m_parent->SetStatusText(label, 0);
}

void StatusBarManager::OnTransferError(const TransferInfo &info) {
  if (!m_parent || !m_statusBar)
    return;

  // Reset speed tracking
  m_speedSamples.clear();
  m_currentSpeed = 0.0;

  // Direction symbol (ASCII)
  wxString dirSymbol = info.direction == TransferDirection::Upload ? "^" : "v";

  // Show error with X mark
  wxString label =
      "[FAIL] " + dirSymbol + " " + info.fileName + " Failed: " + info.error;
  m_parent->SetStatusText(label, 0);
}

void StatusBarManager::HideTransferProgress() {
  m_hasActiveTransfers = false;
  // The next UpdateStatusBar() call will restore the chat info
}

wxString StatusBarManager::FormatSpeed(double bytesPerSecond) const {
  if (bytesPerSecond >= 1024.0 * 1024.0) {
    return wxString::Format("%.1fMB/s", bytesPerSecond / (1024.0 * 1024.0));
  } else if (bytesPerSecond >= 1024.0) {
    return wxString::Format("%.0fKB/s", bytesPerSecond / 1024.0);
  } else if (bytesPerSecond > 0) {
    return wxString::Format("%.0fB/s", bytesPerSecond);
  }
  return "";
}

wxString StatusBarManager::FormatSize(int64_t bytes) const {
  double size = static_cast<double>(bytes);
  if (size >= 1024.0 * 1024.0 * 1024.0) {
    return wxString::Format("%.2fGB", size / (1024.0 * 1024.0 * 1024.0));
  } else if (size >= 1024.0 * 1024.0) {
    return wxString::Format("%.1fMB", size / (1024.0 * 1024.0));
  } else if (size >= 1024.0) {
    return wxString::Format("%.0fKB", size / 1024.0);
  }
  return wxString::Format("%lldB", bytes);
}

wxString StatusBarManager::FormatSizeProgress(int64_t transferred,
                                              int64_t total) const {
  double t = static_cast<double>(transferred);
  double tot = static_cast<double>(total);

  if (tot >= 1024.0 * 1024.0) {
    return wxString::Format("%.1f/%.1fMB", t / (1024.0 * 1024.0),
                            tot / (1024.0 * 1024.0));
  } else if (tot >= 1024.0) {
    return wxString::Format("%.0f/%.0fKB", t / 1024.0, tot / 1024.0);
  }
  return wxString::Format("%lld/%lldB", transferred, total);
}

wxString StatusBarManager::FormatETA(int64_t remaining, double speed) const {
  if (speed <= 0)
    return "";

  int seconds = static_cast<int>(remaining / speed);
  if (seconds < 60) {
    return wxString::Format(" ~%ds", seconds);
  } else if (seconds < 3600) {
    return wxString::Format(" ~%dm%ds", seconds / 60, seconds % 60);
  } else {
    return wxString::Format(" ~%dh%dm", seconds / 3600, (seconds % 3600) / 60);
  }
}

wxString StatusBarManager::BuildProgressBar(int percent, int width) const {
  int filled = (percent * width) / 100;
  int empty = width - filled;

  // Use ASCII characters for cross-platform support (Linux fonts may not have
  // block chars)
  wxString bar;
  for (int i = 0; i < filled; i++)
    bar += "#";
  for (int i = 0; i < empty; i++)
    bar += "-";

  return bar;
}

void StatusBarManager::SetTypingIndicator(const wxString &text) {
  m_overrideStatusText = text;
  m_isTypingIndicator = true;
  m_typingAnimFrame = 0;

  // Start animation timer if not running
  if (!m_typingAnimTimer.IsRunning()) {
    m_typingAnimTimer.Start(400); // Update dots every 400ms
  }

  UpdateStatusBar();
}

void StatusBarManager::ClearTypingIndicator() {
  m_isTypingIndicator = false;
  m_typingAnimTimer.Stop();

  if (m_typingLabel) {
    m_typingLabel->Hide();
  }

  // Restore service message if one is set
  if (!m_serviceMessageText.IsEmpty()) {
    m_overrideStatusText = m_serviceMessageText;
  } else {
    m_overrideStatusText.clear();
  }

  UpdateStatusBar();
}

void StatusBarManager::OnTypingAnimTimer(wxTimerEvent &event) {
  if (!m_isTypingIndicator) {
    m_typingAnimTimer.Stop();
    return;
  }

  m_typingAnimFrame++;
  UpdateStatusBar();
}