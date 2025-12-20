#include "StatusBarManager.h"
#include "../telegram/TelegramClient.h"

StatusBarManager::StatusBarManager(wxFrame* parent)
    : m_parent(parent),
      m_statusBar(nullptr),
      m_connectionLabel(nullptr),
      m_progressGauge(nullptr),
      m_progressLabel(nullptr),
      m_transferAnimFrame(0),
      m_lastTransferredBytes(0),
      m_currentSpeed(0.0),
      m_hasActiveTransfers(false),
      m_activeTransferCount(0),
      m_isOnline(false),
      m_isLoggedIn(false),
      m_currentChatId(0),
      m_currentChatMemberCount(0),
      m_totalChats(0),
      m_unreadChats(0),
      m_bgColor(0x2B, 0x2B, 0x2B),
      m_fgColor(0xD3, 0xD7, 0xCF),
      m_onlineColor(0x4E, 0xC9, 0x4E),      // Green
      m_connectingColor(0xFC, 0xAF, 0x3E),  // Yellow/Orange
      m_offlineColor(0xCC, 0x00, 0x00),     // Red
      m_successColor(0x4E, 0xC9, 0x4E),     // Green
      m_errorColor(0xCC, 0x00, 0x00),       // Red
      m_telegramClient(nullptr)
{
    m_transferTimer.Start();
    m_sessionTimer.Start();
}

StatusBarManager::~StatusBarManager()
{
}

void StatusBarManager::Setup()
{
    if (!m_parent) return;
    
    // Create status bar with 3 fields:
    // [chat info / transfer progress] [session time] [connection status]
    m_statusBar = m_parent->CreateStatusBar(3);
    m_statusBar->SetBackgroundColour(m_bgColor);
    
    // Field widths: main area (flexible), session time (fixed), connection (fixed)
    int widths[] = {-1, 80, 120};
    m_statusBar->SetStatusWidths(3, widths);
    
    // Bind size event to reposition widgets when status bar resizes
    m_statusBar->Bind(wxEVT_SIZE, &StatusBarManager::OnStatusBarResize, this);
    
    // Create connection status label (in field 2) with color support
    m_connectionLabel = new wxStaticText(m_statusBar, wxID_ANY, "");
    m_connectionLabel->SetBackgroundColour(m_bgColor);
    
    wxRect connRect;
    m_statusBar->GetFieldRect(2, connRect);
    m_connectionLabel->SetPosition(wxPoint(connRect.x + 4, connRect.y + 2));
    m_connectionLabel->SetSize(connRect.width - 8, connRect.height - 4);
    
    // No separate progress label/gauge - we use the main status text field
    m_progressLabel = nullptr;
    m_progressGauge = nullptr;
    
    // Initial status
    m_parent->SetStatusText("Not logged in", 0);
    m_parent->SetStatusText("00:00:00", 1);
}

void StatusBarManager::RepositionWidgets()
{
    if (!m_statusBar) return;
    
    // Reposition connection label
    if (m_connectionLabel) {
        wxRect connRect;
        m_statusBar->GetFieldRect(2, connRect);
        m_connectionLabel->SetPosition(wxPoint(connRect.x + 4, connRect.y + 2));
        m_connectionLabel->SetSize(connRect.width - 8, connRect.height - 4);
    }
}

void StatusBarManager::OnStatusBarResize(wxSizeEvent& event)
{
    RepositionWidgets();
    event.Skip();
}

void StatusBarManager::UpdateStatusBar()
{
    if (!m_parent || !m_statusBar) return;
    
    // Field 0: Chat info (only update if no active transfer)
    if (!m_hasActiveTransfers) {
        wxString chatInfo;
        if (m_isLoggedIn) {
            if (m_currentChatId != 0) {
                chatInfo = m_currentChatTitle;
                if (m_currentChatMemberCount > 0) {
                    chatInfo += wxString::Format(" • %d members", m_currentChatMemberCount);
                }
            } else {
                chatInfo = wxString::Format("%d chats", m_totalChats);
                if (m_unreadChats > 0) {
                    chatInfo += wxString::Format(" • %d unread", m_unreadChats);
                }
            }
            chatInfo += wxString::Format(" • @%s", m_currentUser);
        } else {
            chatInfo = "Not logged in";
        }
        m_parent->SetStatusText(chatInfo, 0);
    }
    
    // Field 1: Session time
    long elapsed = m_sessionTimer.Time() / 1000;
    int hours = elapsed / 3600;
    int minutes = (elapsed % 3600) / 60;
    int seconds = elapsed % 60;
    wxString sessionTime = wxString::Format("%02d:%02d:%02d", hours, minutes, seconds);
    m_parent->SetStatusText(sessionTime, 1);
    
    // Field 2: Connection status with colors
    if (m_connectionLabel) {
        wxString connStatus;
        wxColour connColor;
        
        if (m_isOnline) {
            connStatus = "● Online";
            if (!m_connectionDC.IsEmpty()) {
                connStatus += " " + m_connectionDC;
            }
            connColor = m_onlineColor;
        } else if (m_telegramClient && m_telegramClient->IsRunning()) {
            // Animated connecting indicator
            static const wxString spinners[] = {"◐", "◓", "◑", "◒"};
            static int spinFrame = 0;
            spinFrame = (spinFrame + 1) % 4;
            connStatus = spinners[spinFrame] + wxString(" Connecting...");
            connColor = m_connectingColor;
        } else {
            connStatus = "○ Offline";
            connColor = m_offlineColor;
        }
        
        m_connectionLabel->SetForegroundColour(connColor);
        m_connectionLabel->SetLabel(connStatus);
    }
}

void StatusBarManager::UpdateTransferProgress(const TransferInfo& info)
{
    if (!m_parent || !m_statusBar) return;
    
    m_hasActiveTransfers = true;
    
    // Animated spinner characters
    static const wxString spinners[] = {"◐", "◓", "◑", "◒"};
    m_transferAnimFrame = (m_transferAnimFrame + 1) % 4;
    wxString spinner = spinners[m_transferAnimFrame];
    
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
        auto& oldest = m_speedSamples.front();
        auto& newest = m_speedSamples.back();
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
    
    // Build ASCII progress bar
    int percent = info.GetProgressPercent();
    wxString progressBar = BuildProgressBar(percent, 10);
    
    // Direction symbol
    wxString dirSymbol = info.direction == TransferDirection::Upload ? "⬆" : "⬇";
    
    // Truncate filename if too long
    wxString fileName = info.fileName;
    if (fileName.length() > 20) {
        fileName = fileName.Left(17) + "...";
    }
    
    // Build final label: "◐ ⬇ file.jpg [██████░░░░] 45% 1.2MB/s ~5s"
    wxString label = wxString::Format("%s %s %s [%s] %d%%",
        spinner, dirSymbol, fileName, progressBar, percent);
    
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

void StatusBarManager::OnTransferComplete(const TransferInfo& info)
{
    if (!m_parent || !m_statusBar) return;
    
    // Reset speed tracking
    m_speedSamples.clear();
    m_currentSpeed = 0.0;
    m_lastTransferredBytes = 0;
    
    // Direction symbol
    wxString dirSymbol = info.direction == TransferDirection::Upload ? "⬆" : "⬇";
    
    // Show completion with checkmark
    wxString label = wxString::Format("✓ %s %s [██████████] Done!", dirSymbol, info.fileName);
    m_parent->SetStatusText(label, 0);
}

void StatusBarManager::OnTransferError(const TransferInfo& info)
{
    if (!m_parent || !m_statusBar) return;
    
    // Reset speed tracking
    m_speedSamples.clear();
    m_currentSpeed = 0.0;
    
    // Direction symbol
    wxString dirSymbol = info.direction == TransferDirection::Upload ? "⬆" : "⬇";
    
    // Show error with X mark
    wxString label = wxString::Format("✗ %s %s Failed: %s", dirSymbol, info.fileName, info.error);
    m_parent->SetStatusText(label, 0);
}

void StatusBarManager::HideTransferProgress()
{
    m_hasActiveTransfers = false;
    // The next UpdateStatusBar() call will restore the chat info
}

wxString StatusBarManager::FormatSpeed(double bytesPerSecond) const
{
    if (bytesPerSecond >= 1024.0 * 1024.0) {
        return wxString::Format("%.1fMB/s", bytesPerSecond / (1024.0 * 1024.0));
    } else if (bytesPerSecond >= 1024.0) {
        return wxString::Format("%.0fKB/s", bytesPerSecond / 1024.0);
    } else if (bytesPerSecond > 0) {
        return wxString::Format("%.0fB/s", bytesPerSecond);
    }
    return "";
}

wxString StatusBarManager::FormatSize(int64_t bytes) const
{
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

wxString StatusBarManager::FormatSizeProgress(int64_t transferred, int64_t total) const
{
    double t = static_cast<double>(transferred);
    double tot = static_cast<double>(total);
    
    if (tot >= 1024.0 * 1024.0) {
        return wxString::Format("%.1f/%.1fMB", t / (1024.0 * 1024.0), tot / (1024.0 * 1024.0));
    } else if (tot >= 1024.0) {
        return wxString::Format("%.0f/%.0fKB", t / 1024.0, tot / 1024.0);
    }
    return wxString::Format("%lld/%lldB", transferred, total);
}

wxString StatusBarManager::FormatETA(int64_t remaining, double speed) const
{
    if (speed <= 0) return "";
    
    int seconds = static_cast<int>(remaining / speed);
    if (seconds < 60) {
        return wxString::Format(" ~%ds", seconds);
    } else if (seconds < 3600) {
        return wxString::Format(" ~%dm%ds", seconds / 60, seconds % 60);
    } else {
        return wxString::Format(" ~%dh%dm", seconds / 3600, (seconds % 3600) / 60);
    }
}

wxString StatusBarManager::BuildProgressBar(int percent, int width) const
{
    int filled = (percent * width) / 100;
    int empty = width - filled;
    
    wxString bar;
    for (int i = 0; i < filled; i++) bar += "█";
    for (int i = 0; i < empty; i++) bar += "░";
    
    return bar;
}