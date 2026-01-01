#include "ChatViewWidget.h"
#include "InputBoxWidget.h"
#include "MainFrame.h"
#include "MediaPopup.h"
#include "MessageFormatter.h"
#include <algorithm>
#include <iostream>
#include <unordered_map>
#include <wx/settings.h>

// Cached file existence check to reduce disk I/O
// Cache entries expire after 500ms to balance performance with freshness
static bool CachedFileExists(const wxString &path) {
  if (path.IsEmpty())
    return false;

  struct CacheEntry {
    bool exists;
    wxLongLong timestamp;
  };

  static std::unordered_map<std::string, CacheEntry> s_cache;
  static const wxLongLong CACHE_DURATION_MS = 500;

  std::string key = path.ToStdString();
  wxLongLong now = wxGetLocalTimeMillis();

  auto it = s_cache.find(key);
  if (it != s_cache.end() && (now - it->second.timestamp) < CACHE_DURATION_MS) {
    return it->second.exists;
  }

  // Cache miss or expired - do actual check
  bool exists = wxFileExists(path);
  s_cache[key] = {exists, now};

  // Periodically clean old entries to prevent unbounded growth
  if (s_cache.size() > 1000) {
    for (auto iter = s_cache.begin(); iter != s_cache.end();) {
      if ((now - iter->second.timestamp) > CACHE_DURATION_MS * 10) {
        iter = s_cache.erase(iter);
      } else {
        ++iter;
      }
    }
  }

  return exists;
}

// #define CVWLOG(msg) std::cerr << "[ChatViewWidget] " << msg << std::endl
#define CVWLOG(msg) do {} while(0)

// Global cache for per-chat read times (persists across chat switches)
// Key: chatId, Value: map of messageId -> readTime
static std::map<int64_t, std::map<int64_t, int64_t>> s_perChatReadTimes;
#include "../telegram/TelegramClient.h"
#include "../telegram/Types.h"
#include "FileDropTarget.h"
#include <ctime>
#include <wx/clipbrd.h>
#include <wx/filename.h>
#include <wx/stattext.h>

ChatViewWidget::ChatViewWidget(wxWindow *parent, MainFrame *mainFrame)
    : wxPanel(parent, wxID_ANY), m_mainFrame(mainFrame), m_chatArea(nullptr),
      m_messageFormatter(nullptr), m_mediaPopup(nullptr),
      m_editHistoryPopup(nullptr), m_newMessageButton(nullptr),
      m_topicBar(nullptr), m_topicText(nullptr), m_downloadBar(nullptr),
      m_downloadLabel(nullptr), m_downloadGauge(nullptr),
      m_downloadHideTimer(this), m_refreshTimer(this), m_refreshPending(false),
      m_wasAtBottom(true), m_newMessageCount(0), m_isLoading(false),
      m_isLoadingHistory(false), m_allHistoryLoaded(false),
      m_highlightTimer(this, HIGHLIGHT_TIMER_ID), m_isReloading(false),
      m_batchUpdateDepth(0), m_lastDisplayedTimestamp(0),
      m_loadingIndicator(nullptr), m_loadMoreButton(nullptr), 
      m_lastDisplayedMessageId(0), m_contextMenuPos(-1) {
  // Bind timer events
  Bind(
      wxEVT_TIMER, [this](wxTimerEvent &) { HideDownloadProgress(); },
      m_downloadHideTimer.GetId());
  Bind(wxEVT_TIMER, &ChatViewWidget::OnRefreshTimer, this,
       m_refreshTimer.GetId());
  Bind(wxEVT_TIMER, &ChatViewWidget::OnHighlightTimer, this,
       HIGHLIGHT_TIMER_ID);

  // Bind size event for repositioning the new message button
  Bind(wxEVT_SIZE, &ChatViewWidget::OnSize, this);

  CreateLayout();
  SetupDisplayControl();

  // Create media popup (hidden initially)
  m_mediaPopup = new MediaPopup(this);

  // Set click callback to open media when popup is clicked
  m_mediaPopup->SetClickCallback([this](const MediaInfo &info) {
    OpenMedia(info);
    HideMediaPopup();
  });

  // Create edit history popup (hidden initially)
  m_editHistoryPopup = nullptr; // Created on demand
}

ChatViewWidget::~ChatViewWidget() {
  delete m_messageFormatter;
  m_messageFormatter = nullptr;
}

void ChatViewWidget::CreateLayout() {
  wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

  // Loading indicator at top (shown while fetching older messages)
  m_loadingIndicator = new wxStaticText(this, wxID_ANY, wxString::FromUTF8("⟳ Loading older messages..."),
                                        wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
  m_loadingIndicator->SetForegroundColour(wxColour(128, 128, 128));
  m_loadingIndicator->Hide();
  mainSizer->Add(m_loadingIndicator, 0, wxEXPAND | wxALL, 4);

  // Legacy load more button - kept for compatibility but not used
  m_loadMoreButton = new wxButton(this, wxID_ANY, "Load older messages");
  m_loadMoreButton->Hide();
  m_loadMoreButton->Bind(wxEVT_BUTTON, &ChatViewWidget::OnLoadMoreButtonClick, this);
  // Don't add to sizer - we use m_loadingIndicator now

  // Topic bar at top (HexChat style)
  m_topicBar = new wxPanel(this, wxID_ANY);

  wxBoxSizer *topicSizer = new wxBoxSizer(wxHORIZONTAL);

  m_topicText = new wxStaticText(m_topicBar, wxID_ANY, "");
  // Let text use native colors and font

  topicSizer->Add(m_topicText, 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT,
                  8);
  m_topicBar->SetSizer(topicSizer);
  m_topicBar->SetMinSize(wxSize(-1, 28));
  m_topicBar->Hide(); // Hidden until a chat is selected

  mainSizer->Add(m_topicBar, 0, wxEXPAND);

  // Download progress is now shown in status bar, not here
  m_downloadBar = nullptr;
  m_downloadLabel = nullptr;
  m_downloadGauge = nullptr;

  // ChatArea for display - uses same formatting as WelcomeChat
  m_chatArea = new ChatArea(this);
  mainSizer->Add(m_chatArea, 1, wxEXPAND);

  SetSizer(mainSizer);

  // Create the "New Messages" button (hidden initially)
  CreateNewMessageButton();
}

void ChatViewWidget::SetTopicText(const wxString &chatName,
                                  const wxString &info) {
  if (!m_topicBar || !m_topicText)
    return;

  wxString topic;
  if (!chatName.IsEmpty()) {
    topic = chatName;
    if (!info.IsEmpty()) {
      topic += "  -  " + info;
    }
    m_topicText->SetLabel(topic);
    m_topicBar->Show();
  } else {
    m_topicBar->Hide();
  }
  Layout();
}

void ChatViewWidget::ClearTopicText() {
  if (m_topicBar) {
    m_topicBar->Hide();
  }
  if (m_topicText) {
    m_topicText->SetLabel("");
  }
  Layout();
}

void ChatViewWidget::CreateNewMessageButton() {
  m_newMessageButton = new wxButton(this, ID_NEW_MESSAGE_BUTTON,
                                    wxString::FromUTF8("↓ New Messages"),
                                    wxDefaultPosition, wxDefaultSize);

  // Use native button styling
  m_newMessageButton->Hide();

  Bind(wxEVT_BUTTON, &ChatViewWidget::OnNewMessageButtonClick, this,
       ID_NEW_MESSAGE_BUTTON);
}

void ChatViewWidget::SetupDisplayControl() {
  wxRichTextCtrl *display = m_chatArea->GetDisplay();
  if (!display)
    return;

  // Cursor handling is now done in ChatArea (single source of truth)
  // Bind mouse events for cursor updates and click handling
  display->Bind(wxEVT_MOTION, &ChatViewWidget::OnMouseMove, this);
  display->Bind(wxEVT_LEAVE_WINDOW, &ChatViewWidget::OnMouseLeave, this);
  display->Bind(wxEVT_LEFT_DOWN, &ChatViewWidget::OnLeftDown, this);
  display->Bind(wxEVT_RIGHT_DOWN, &ChatViewWidget::OnRightDown, this);
  display->Bind(wxEVT_KEY_DOWN, &ChatViewWidget::OnKeyDown, this);
  display->Bind(wxEVT_SCROLLWIN_THUMBRELEASE, &ChatViewWidget::OnScroll, this);
  display->Bind(wxEVT_SCROLLWIN_LINEDOWN, &ChatViewWidget::OnScroll, this);
  display->Bind(wxEVT_SCROLLWIN_LINEUP, &ChatViewWidget::OnScroll, this);
  display->Bind(wxEVT_SCROLLWIN_PAGEDOWN, &ChatViewWidget::OnScroll, this);
  display->Bind(wxEVT_SCROLLWIN_PAGEUP, &ChatViewWidget::OnScroll, this);
  display->Bind(wxEVT_MOUSEWHEEL, &ChatViewWidget::OnMouseWheel, this);

  // Set up drag and drop for file uploads
  if (m_mainFrame) {
    FileDropTarget *dropTarget =
        new FileDropTarget([this](const wxArrayString &files) {
          if (m_mainFrame) {
            m_mainFrame->OnFilesDropped(files);
          }
        });
    display->SetDropTarget(dropTarget);
  }

  // Create message formatter using ChatArea
  m_messageFormatter = new MessageFormatter(m_chatArea);

  // Set up link span callback
  m_messageFormatter->SetLinkSpanCallback(
      [this](long startPos, long endPos, const wxString &url) {
        AddLinkSpan(startPos, endPos, url);
      });
}

void ChatViewWidget::EnsureMediaDownloaded(const MediaInfo &info) {
  // Auto-download visible media if not already downloaded
  if (m_mainFrame && info.fileId != 0 && info.localPath.IsEmpty()) {
    // Check if we are already downloading this file
    if (!HasPendingDownload(info.fileId)) {
      TelegramClient *client = m_mainFrame->GetTelegramClient();
      if (client) {
        // Determine priority (higher for smaller files/images)
        int priority = 5;
        if (info.type == MediaType::Photo || info.type == MediaType::Sticker) {
          priority = 10;
        } else if (info.type == MediaType::Voice ||
                   info.type == MediaType::VideoNote) {
          priority = 12; // High priority for voice/video notes
        } else if (info.type == MediaType::GIF) {
          priority = 8;
        } else if (info.type == MediaType::Video) {
          priority = 6;
        }

        wxString displayName =
            info.fileName.IsEmpty() ? "Auto-download" : info.fileName;
        client->DownloadFile(info.fileId, priority, displayName, 0);
        AddPendingDownload(info.fileId);
      }
    }
  }
}

void ChatViewWidget::SortMessages() {
  // Sort messages by timestamp (date) primary, message ID secondary
  // Timestamps are more reliable for display order than message IDs
  // which can arrive out of order from the server
  std::sort(m_messages.begin(), m_messages.end(),
            [](const MessageInfo &a, const MessageInfo &b) {
              if (a.date != b.date) {
                return a.date < b.date;
              }
              return a.id < b.id;
            });

  // Rebuild the message ID to index map after sorting
  // This is necessary because sorting changes the indices
  m_messageIdToIndex.clear();
  for (size_t i = 0; i < m_messages.size(); ++i) {
    if (m_messages[i].id != 0) {
      m_messageIdToIndex[m_messages[i].id] = i;
    }
  }
}

bool ChatViewWidget::HasMessage(int64_t messageId) const {
  std::lock_guard<std::mutex> lock(m_messagesMutex);
  return m_displayedMessageIds.count(messageId) > 0;
}

void ChatViewWidget::AddMessage(const MessageInfo &msg) {
  std::lock_guard<std::mutex> lock(m_messagesMutex);

  // Skip duplicates
  if (msg.id != 0 && m_displayedMessageIds.count(msg.id) > 0) {
    CVWLOG("AddMessage: skipping duplicate message id=" << msg.id);
    return;
  }

  size_t index = m_messages.size();
  m_messages.push_back(msg);
  if (msg.id != 0) {
    m_displayedMessageIds.insert(msg.id);
    m_messageIdToIndex[msg.id] = index;
  }
}

void ChatViewWidget::ScheduleRefresh() {
  // If a refresh is already pending, don't schedule another
  if (m_refreshPending) {
    return;
  }

  m_refreshPending = true;

  // Start or restart the debounce timer
  if (m_refreshTimer.IsRunning()) {
    m_refreshTimer.Stop();
  }
  m_refreshTimer.StartOnce(REFRESH_DEBOUNCE_MS);
}

void ChatViewWidget::OnHighlightTimer(wxTimerEvent &event) {
  // Remove expired highlights (older than HIGHLIGHT_DURATION_SECONDS)
  int64_t now = wxGetUTCTime();
  bool hasActiveHighlights = false;
  bool removedAny = false;

  auto it = m_recentlyReadMessages.begin();
  while (it != m_recentlyReadMessages.end()) {
    if (now - it->second >= HIGHLIGHT_DURATION_SECONDS) {
      it = m_recentlyReadMessages.erase(it);
      removedAny = true;
    } else {
      hasActiveHighlights = true;
      ++it;
    }
  }

  // If we removed any highlights, refresh to show normal colors
  if (!hasActiveHighlights) {
    m_highlightTimer.Stop();
  }

  // Only refresh if we actually removed expired highlights
  if (removedAny) {
    ScheduleRefresh();
  }
}

void ChatViewWidget::OnRefreshTimer(wxTimerEvent &event) {
  m_refreshPending = false;
  RefreshDisplay();
}

void ChatViewWidget::RefreshDisplay() {
  if (!m_messageFormatter || !m_chatArea)
    return;

  // Clear pending flag since we're refreshing now
  m_refreshPending = false;
  if (m_refreshTimer.IsRunning()) {
    m_refreshTimer.Stop();
  }

  // Check ACTUAL scroll position right now - don't trust cached m_wasAtBottom
  // This prevents jumping back down when user is actively scrolling up
  bool actuallyAtBottom = IsAtBottom();
  bool wasLoadingHistory = m_isLoadingHistory;
  
  // Only scroll to bottom if we're actually at bottom AND not loading history
  // When loading history, user is scrolling up - never force scroll down
  bool shouldScrollToBottom = actuallyAtBottom && !wasLoadingHistory;

  // Clear display but keep message storage
  m_chatArea->Clear();
  ClearMediaSpans();
  ClearEditSpans();
  ClearLinkSpans();
  m_readMarkerSpans.clear();
  m_messageRangeMap.clear();

  // Reset formatting state
  m_messageFormatter->ResetGroupingState();
  m_messageFormatter->ResetUnreadMarker();
  m_lastDisplayedSender.Clear();
  m_lastDisplayedTimestamp = 0;
  m_lastDisplayedMessageId = 0;

  // Freeze display for batch update and suppress undo for performance
  BeginBatchUpdate();
  
  wxRichTextCtrl *display = m_chatArea ? m_chatArea->GetDisplay() : nullptr;
  if (display) {
    display->BeginSuppressUndo();
  }
  
  // Enable fast mode for bulk rendering - skips expensive URL detection
  m_messageFormatter->SetFastMode(true);

  // Single lock for all message operations to reduce lock contention
  {
    std::lock_guard<std::mutex> lock(m_messagesMutex);

    // Sort messages before rendering
    SortMessages();
    
    // VIRTUAL WINDOW: Only render up to MAX_DISPLAYED_MESSAGES for performance
    // This is the key optimization - like Telegram Desktop / Discord
    size_t totalMessages = m_messages.size();
    
    // Determine window bounds
    if (wasLoadingHistory) {
      // Loading older messages - keep window at the same relative position
      // but shifted to include some new messages
      if (totalMessages <= MAX_DISPLAYED_MESSAGES) {
        m_displayWindowStart = 0;
        m_displayWindowEnd = totalMessages;
      } else {
        // Keep end position relative to newest messages
        size_t oldWindowSize = m_displayWindowEnd - m_displayWindowStart;
        if (oldWindowSize == 0) oldWindowSize = MAX_DISPLAYED_MESSAGES;
        m_displayWindowEnd = totalMessages;
        m_displayWindowStart = totalMessages > oldWindowSize ? totalMessages - oldWindowSize : 0;
        // Ensure we don't exceed max
        if (m_displayWindowEnd - m_displayWindowStart > MAX_DISPLAYED_MESSAGES) {
          m_displayWindowStart = m_displayWindowEnd - MAX_DISPLAYED_MESSAGES;
        }
      }
    } else if (shouldScrollToBottom || m_displayWindowEnd == 0) {
      // At bottom or first load - show newest messages
      if (totalMessages <= MAX_DISPLAYED_MESSAGES) {
        m_displayWindowStart = 0;
        m_displayWindowEnd = totalMessages;
      } else {
        m_displayWindowEnd = totalMessages;
        m_displayWindowStart = totalMessages - MAX_DISPLAYED_MESSAGES;
      }
    }
    // Otherwise keep current window position

    // Calculate optimal username width for alignment (only for visible messages)
    std::vector<wxString> usernames;
    usernames.reserve(m_displayWindowEnd - m_displayWindowStart);
    for (size_t i = m_displayWindowStart; i < m_displayWindowEnd && i < totalMessages; i++) {
      if (!m_messages[i].senderName.IsEmpty()) {
        usernames.push_back(m_messages[i].senderName);
      }
    }
    m_messageFormatter->CalculateUsernameWidth(usernames);

    // Update tracking from sorted messages
    m_displayedMessageIds.clear();
    for (const auto &msg : m_messages) {
      if (msg.id != 0) {
        m_displayedMessageIds.insert(msg.id);
        if (msg.id > m_lastDisplayedMessageId) {
          m_lastDisplayedMessageId = msg.id;
        }
      }
    }

    // Render only messages in the virtual window
    for (size_t i = m_displayWindowStart; i < m_displayWindowEnd && i < totalMessages; i++) {
      RenderMessageToDisplay(m_messages[i]);
    }
    
    CVWLOG("RefreshDisplay: rendered " << (m_displayWindowEnd - m_displayWindowStart) 
           << " of " << totalMessages << " messages (window: " 
           << m_displayWindowStart << "-" << m_displayWindowEnd << ")");
  }

  // Remove trailing newline after the last message to avoid extra blank line
  if (m_chatArea && m_chatArea->GetDisplay()) {
    wxRichTextCtrl *display = m_chatArea->GetDisplay();
    long lastPos = display->GetLastPosition();
    if (lastPos > 0) {
      wxString lastChar = display->GetRange(lastPos - 1, lastPos);
      if (lastChar == "\n") {
        display->Remove(lastPos - 1, lastPos);
      }
    }
  }

  // Disable fast mode after bulk rendering
  m_messageFormatter->SetFastMode(false);
  
  if (display) {
    display->EndSuppressUndo();
  }
  
  EndBatchUpdate();

  // Scroll restoration - keep it simple
  // Note: BufferOlderMessages handles its own scroll restoration for history loading
  if (wasLoadingHistory) {
    m_isLoadingHistory = false;
  }
  
  if (shouldScrollToBottom) {
    // Only scroll to bottom if user was already there
    CallAfter([this]() {
      if (m_chatArea) {
        m_chatArea->ScrollToBottom();
      }
    });
  }
  // Otherwise: don't touch scroll position
}

void ChatViewWidget::RefreshDisplayWindow() {
  // Optimized refresh that only re-renders the current window
  // Used when shifting the virtual window during scroll
  RefreshDisplay();
}

void ChatViewWidget::AdjustDisplayWindow(bool scrollingUp) {
  // Shift the display window when user scrolls near edges
  // This enables smooth virtual scrolling
  
  std::lock_guard<std::mutex> lock(m_messagesMutex);
  size_t totalMessages = m_messages.size();
  
  if (totalMessages <= MAX_DISPLAYED_MESSAGES) {
    // All messages fit - no adjustment needed
    return;
  }
  
  const size_t SHIFT_AMOUNT = 30;  // How many messages to shift
  bool needsRefresh = false;
  
  if (scrollingUp) {
    // User scrolling up - need older messages
    if (m_displayWindowStart > 0) {
      size_t shift = std::min(SHIFT_AMOUNT, m_displayWindowStart);
      m_displayWindowStart -= shift;
      m_displayWindowEnd -= shift;
      // Keep window size consistent
      if (m_displayWindowEnd - m_displayWindowStart > MAX_DISPLAYED_MESSAGES) {
        m_displayWindowEnd = m_displayWindowStart + MAX_DISPLAYED_MESSAGES;
      }
      needsRefresh = true;
    }
  } else {
    // User scrolling down - need newer messages
    if (m_displayWindowEnd < totalMessages) {
      size_t shift = std::min(SHIFT_AMOUNT, totalMessages - m_displayWindowEnd);
      m_displayWindowStart += shift;
      m_displayWindowEnd += shift;
      // Keep window size consistent
      if (m_displayWindowEnd - m_displayWindowStart > MAX_DISPLAYED_MESSAGES) {
        m_displayWindowStart = m_displayWindowEnd - MAX_DISPLAYED_MESSAGES;
      }
      needsRefresh = true;
    }
  }
  
  if (needsRefresh) {
    // Use CallAfter to avoid blocking the scroll event
    CallAfter([this]() {
      RefreshDisplayWindow();
    });
  }
}

void ChatViewWidget::ForceScrollToBottom() {
  // Set the flag so next refresh will scroll, then scroll immediately
  m_wasAtBottom = true;
  ScrollToBottom();
}

void ChatViewWidget::RenderMessageToDisplay(const MessageInfo &msg) {
  if (!m_chatArea)
    return;

  long startPos = m_chatArea->GetLastPosition();
  DoRenderMessage(msg);
  long endPos = m_chatArea->GetLastPosition();

  if (msg.id != 0 && endPos > startPos) {
    m_messageRangeMap[msg.id] = {startPos, endPos};
  }
}

void ChatViewWidget::DoRenderMessage(const MessageInfo &msg) {
  if (!m_messageFormatter)
    return;

  wxString timestamp = FormatTimestamp(msg.date);

  // Check if we need a date separator (day changed)
  if (m_messageFormatter->NeedsDateSeparator(msg.date)) {
    m_messageFormatter->AppendDateSeparatorForTime(msg.date);
    // Reset grouping after date separator
    m_messageFormatter->ResetGroupingState();
    m_lastDisplayedSender.Clear();
    m_lastDisplayedTimestamp = 0;
  } else if (m_lastDisplayedTimestamp == 0 && msg.date > 0) {
    // First message - show date separator if it's not today
    time_t t = static_cast<time_t>(msg.date);
    wxDateTime msgDate(t);
    wxDateTime today = wxDateTime::Now().GetDateOnly();
    if (msgDate.GetDateOnly() != today) {
      m_messageFormatter->AppendDateSeparatorForTime(msg.date);
    }
  }

  // Determine message status for outgoing messages
  MessageStatus status = MessageStatus::None;
  bool statusHighlight = false;

  if (msg.isOutgoing) {
    if (msg.id == 0) {
      status = MessageStatus::Sending;
    } else if (m_lastReadOutboxId > 0 && msg.id <= m_lastReadOutboxId) {
      status = MessageStatus::Read;
      // Check if this message was recently read (for highlight animation)
      auto it = m_recentlyReadMessages.find(msg.id);
      if (it != m_recentlyReadMessages.end()) {
        int64_t now = wxGetUTCTime();
        if (now - it->second < 3) { // Highlight for 3 seconds
          statusHighlight = true;
        }
      }
    } else {
      status = MessageStatus::Sent;
    }
  }

  bool hasReadMarker = (status == MessageStatus::Read);

  wxString sender = msg.senderName.IsEmpty() ? "Unknown" : msg.senderName;

  // Handle forwarded messages
  if (msg.isForwarded && !msg.forwardedFrom.IsEmpty()) {
    long startPos = m_chatArea->GetLastPosition();
    m_messageFormatter->AppendForwardMessage(timestamp, sender,
                                             msg.forwardedFrom, msg.text,
                                             status, statusHighlight);
    if (hasReadMarker)
      RecordReadMarker(startPos, m_chatArea->GetLastPosition(), msg.id);
    if (!msg.reactions.empty()) {
      m_messageFormatter->AppendReactions(msg.reactions);
    }
    m_messageFormatter->SetLastMessage(sender, msg.date);
    m_lastDisplayedSender = sender;
    m_lastDisplayedTimestamp = msg.date;
    return;
  }

  // Handle reply messages
  if (msg.replyToMessageId != 0 && !msg.replyToText.IsEmpty()) {
    long startPos = m_chatArea->GetLastPosition();
    m_messageFormatter->AppendReplyMessage(timestamp, sender, msg.replyToText,
                                           msg.text, status, statusHighlight);
    if (hasReadMarker)
      RecordReadMarker(startPos, m_chatArea->GetLastPosition(), msg.id);
    if (!msg.reactions.empty()) {
      m_messageFormatter->AppendReactions(msg.reactions);
    }
    m_messageFormatter->SetLastMessage(sender, msg.date);
    m_lastDisplayedSender = sender;
    m_lastDisplayedTimestamp = msg.date;
    return;
  }

  // Handle media messages - helper lambda to update state after media
  auto updateStateAfterMedia = [&]() {
    if (!msg.reactions.empty()) {
      m_messageFormatter->AppendReactions(msg.reactions);
    }
    m_messageFormatter->SetLastMessage(sender, msg.date);
    m_lastDisplayedSender = sender;
    m_lastDisplayedTimestamp = msg.date;
  };

  if (msg.hasPhoto) {
    MediaInfo info;
    info.type = MediaType::Photo;
    info.fileId = msg.mediaFileId;
    info.localPath = msg.mediaLocalPath;
    info.caption = msg.mediaCaption;
    info.thumbnailFileId = msg.mediaThumbnailFileId;
    info.thumbnailPath = msg.mediaThumbnailPath;

    long startPos = m_chatArea->GetLastPosition();
    m_messageFormatter->AppendMediaMessage(
        timestamp, sender, info, msg.mediaCaption, status, statusHighlight);
    long endPos = m_chatArea->GetLastPosition();
    // Only add span if we have valid media reference
    if (info.fileId != 0 || info.thumbnailFileId != 0 ||
        !info.localPath.IsEmpty()) {
      AddMediaSpan(startPos, endPos, info, msg.id);
    }
    if (hasReadMarker)
      RecordReadMarker(startPos, endPos, msg.id);
    updateStateAfterMedia();
    return;
  }

  if (msg.hasVideo) {
    MediaInfo info;
    info.type = MediaType::Video;
    info.fileId = msg.mediaFileId;
    info.localPath = msg.mediaLocalPath;
    info.fileName = msg.mediaFileName;
    info.caption = msg.mediaCaption;
    info.thumbnailFileId = msg.mediaThumbnailFileId;
    info.thumbnailPath = msg.mediaThumbnailPath;
    info.duration = msg.mediaDuration;

    long startPos = m_chatArea->GetLastPosition();
    m_messageFormatter->AppendMediaMessage(
        timestamp, sender, info, msg.mediaCaption, status, statusHighlight);
    long endPos = m_chatArea->GetLastPosition();
    // Only add span if we have valid media reference
    if (info.fileId != 0 || info.thumbnailFileId != 0 ||
        !info.localPath.IsEmpty()) {
      AddMediaSpan(startPos, endPos, info, msg.id);
    }
    if (hasReadMarker)
      RecordReadMarker(startPos, endPos, msg.id);
    updateStateAfterMedia();
    return;
  }

  if (msg.hasDocument) {
    MediaInfo info;
    info.type = MediaType::File;
    info.fileId = msg.mediaFileId;
    info.localPath = msg.mediaLocalPath;
    info.fileName = msg.mediaFileName;
    info.fileSize = wxString::Format("%lld bytes", msg.mediaFileSize);
    info.caption = msg.mediaCaption;

    long startPos = m_chatArea->GetLastPosition();
    m_messageFormatter->AppendMediaMessage(
        timestamp, sender, info, msg.mediaCaption, status, statusHighlight);
    long endPos = m_chatArea->GetLastPosition();
    // Only add span if we have valid media reference
    if (info.fileId != 0 || info.thumbnailFileId != 0 ||
        !info.localPath.IsEmpty()) {
      AddMediaSpan(startPos, endPos, info, msg.id);
    }
    if (hasReadMarker)
      RecordReadMarker(startPos, endPos, msg.id);
    updateStateAfterMedia();
    return;
  }

  if (msg.hasVoice) {
    MediaInfo info;
    info.type = MediaType::Voice;
    info.fileId = msg.mediaFileId;
    info.localPath = msg.mediaLocalPath;
    info.duration = msg.mediaDuration;
    info.waveform = msg.mediaWaveform;

    long startPos = m_chatArea->GetLastPosition();
    m_messageFormatter->AppendMediaMessage(timestamp, sender, info, "", status,
                                           statusHighlight);
    long endPos = m_chatArea->GetLastPosition();
    AddMediaSpan(startPos, endPos, info, msg.id);
    if (hasReadMarker)
      RecordReadMarker(startPos, endPos, msg.id);
    updateStateAfterMedia();
    return;
  }

  if (msg.hasVideoNote) {
    MediaInfo info;
    info.type = MediaType::VideoNote;
    info.fileId = msg.mediaFileId;
    info.localPath = msg.mediaLocalPath;
    info.thumbnailFileId = msg.mediaThumbnailFileId;
    info.thumbnailPath = msg.mediaThumbnailPath;
    info.duration = msg.mediaDuration;

    long startPos = m_chatArea->GetLastPosition();
    m_messageFormatter->AppendMediaMessage(
        timestamp, sender, info, msg.mediaCaption, status, statusHighlight);
    long endPos = m_chatArea->GetLastPosition();
    // Only add span if we have valid media reference
    if (info.fileId != 0 || info.thumbnailFileId != 0 ||
        !info.localPath.IsEmpty()) {
      AddMediaSpan(startPos, endPos, info, msg.id);
    }
    if (hasReadMarker)
      RecordReadMarker(startPos, endPos, msg.id);
    updateStateAfterMedia();
    return;
  }

  if (msg.hasSticker) {
    MediaInfo info;
    info.type = MediaType::Sticker;
    info.fileId = msg.mediaFileId;
    info.localPath = msg.mediaLocalPath;
    info.emoji = msg.mediaCaption; // Sticker emoji is stored in mediaCaption
    info.thumbnailFileId = msg.mediaThumbnailFileId;
    info.thumbnailPath = msg.mediaThumbnailPath;

    long startPos = m_chatArea->GetLastPosition();
    m_messageFormatter->AppendMediaMessage(
        timestamp, sender, info, msg.mediaCaption, status, statusHighlight);
    long endPos = m_chatArea->GetLastPosition();
    // Only add span if we have valid media reference
    if (info.fileId != 0 || info.thumbnailFileId != 0 ||
        !info.localPath.IsEmpty()) {
      AddMediaSpan(startPos, endPos, info, msg.id);
    }
    if (hasReadMarker)
      RecordReadMarker(startPos, endPos, msg.id);
    updateStateAfterMedia();
    return;
  }

  if (msg.hasAnimation) {
    MediaInfo info;
    info.type = MediaType::GIF;
    info.fileId = msg.mediaFileId;
    info.localPath = msg.mediaLocalPath;
    info.caption = msg.mediaCaption;
    info.thumbnailFileId = msg.mediaThumbnailFileId;
    info.thumbnailPath = msg.mediaThumbnailPath;

    long startPos = m_chatArea->GetLastPosition();
    m_messageFormatter->AppendMediaMessage(
        timestamp, sender, info, msg.mediaCaption, status, statusHighlight);
    long endPos = m_chatArea->GetLastPosition();
    // Only add span if we have valid media reference
    if (info.fileId != 0 || info.thumbnailFileId != 0 ||
        !info.localPath.IsEmpty()) {
      AddMediaSpan(startPos, endPos, info, msg.id);
    }
    if (hasReadMarker)
      RecordReadMarker(startPos, endPos, msg.id);
    updateStateAfterMedia();
    return;
  }

  // Check for action messages (/me)
  if (msg.text.StartsWith("/me ")) {
    wxString action = msg.text.Mid(4);
    long startPos = m_chatArea->GetLastPosition();
    m_messageFormatter->AppendActionMessage(timestamp, sender, action, status,
                                            statusHighlight);
    if (hasReadMarker)
      RecordReadMarker(startPos, m_chatArea->GetLastPosition(), msg.id);
    if (!msg.reactions.empty()) {
      m_messageFormatter->AppendReactions(msg.reactions);
    }
    m_messageFormatter->SetLastMessage(sender, msg.date);
    m_lastDisplayedSender = sender;
    m_lastDisplayedTimestamp = msg.date;
    return;
  }

  // Handle edited messages - just show (edited) marker
  // Note: TDLib doesn't provide original message text, so no hover popup
  if (msg.isEdited) {
    long startPos = m_chatArea->GetLastPosition();
    m_messageFormatter->AppendEditedMessage(
        timestamp, sender, msg.text, nullptr, nullptr, status, statusHighlight);
    if (hasReadMarker)
      RecordReadMarker(startPos, m_chatArea->GetLastPosition(), msg.id);
    if (!msg.reactions.empty()) {
      m_messageFormatter->AppendReactions(msg.reactions);
    }
    m_messageFormatter->SetLastMessage(sender, msg.date);
    m_lastDisplayedSender = sender;
    m_lastDisplayedTimestamp = msg.date;
    return;
  }

  // Check for mentions/highlights (HexChat-style)
  bool isMentioned = false;
  if (!m_currentUsername.IsEmpty() && !msg.text.IsEmpty() && !msg.isOutgoing) {
    wxString lowerText = msg.text.Lower();
    wxString lowerUsername = m_currentUsername.Lower();
    // Check for @username mention or just username in text
    if (lowerText.Contains("@" + lowerUsername) ||
        lowerText.Contains(lowerUsername)) {
      isMentioned = true;
    }
  }

  // Regular text message
  long startPos = m_chatArea->GetLastPosition();

  if (isMentioned) {
    // Highlighted message - someone mentioned you (always full format)
    m_messageFormatter->AppendHighlightMessage(timestamp, sender, msg.text,
                                               status, statusHighlight);
  } else {
    // Full message with nick and timestamp
    m_messageFormatter->AppendMessage(timestamp, sender, msg.text, status,
                                      statusHighlight);
  }

  if (hasReadMarker)
    RecordReadMarker(startPos, m_chatArea->GetLastPosition(), msg.id);

  // Display reactions if any
  if (!msg.reactions.empty()) {
    m_messageFormatter->AppendReactions(msg.reactions);
  }

  // Update grouping state
  m_messageFormatter->SetLastMessage(sender, msg.date);
  m_lastDisplayedSender = sender;
  m_lastDisplayedTimestamp = msg.date;
}

void ChatViewWidget::DisplayMessage(const MessageInfo &msg) {
  if (!m_messageFormatter || !m_chatArea)
    return;

  // Skip duplicates
  if (msg.id != 0 && HasMessage(msg.id)) {
    CVWLOG("DisplayMessage: skipping duplicate message id=" << msg.id);
    return;
  }

  // Determine if we can append using the last displayed state
  // If this message is newer/same time as the last displayed one, we can append
  bool canAppend = false;
  if (m_lastDisplayedTimestamp == 0 || msg.date >= m_lastDisplayedTimestamp) {
    canAppend = true;
  }

  // Add to storage
  AddMessage(msg);

  // Trigger download for media if needed
  if (msg.hasPhoto || msg.hasSticker || msg.hasAnimation || msg.hasVoice ||
      msg.hasVideoNote || msg.hasVideo) {
    MediaInfo info;
    info.fileId = msg.mediaFileId;
    info.localPath = msg.mediaLocalPath;
    if (msg.hasPhoto)
      info.type = MediaType::Photo;
    else if (msg.hasSticker)
      info.type = MediaType::Sticker;
    else if (msg.hasAnimation)
      info.type = MediaType::GIF;
    else if (msg.hasVoice)
      info.type = MediaType::Voice;
    else if (msg.hasVideoNote)
      info.type = MediaType::VideoNote;
    else if (msg.hasVideo)
      info.type = MediaType::Video;
    EnsureMediaDownloaded(info);
  }

  if (canAppend) {
    // Append directly to the display without clearing
    BeginBatchUpdate();

    // Ensure we suppress undo to save memory/cpu
    if (m_chatArea && m_chatArea->GetDisplay()) {
      m_chatArea->GetDisplay()->BeginSuppressUndo();
      m_chatArea->GetDisplay()->SetInsertionPointEnd();

      // Ensure we start on a new line if not already
      // This prevents messages from being merged onto the same line
      long lastPos = m_chatArea->GetDisplay()->GetLastPosition();
      if (lastPos > 0) {
        wxString lastChar =
            m_chatArea->GetDisplay()->GetRange(lastPos - 1, lastPos);
        if (!lastChar.IsEmpty() && lastChar[0] != '\n' && lastChar[0] != '\r') {
          m_chatArea->GetDisplay()->WriteText("\n");
        }
      }
    }

    RenderMessageToDisplay(msg);

    // Remove trailing newline to keep layout tight (no extra gap at bottom)
    if (m_chatArea && m_chatArea->GetDisplay()) {
      wxRichTextCtrl *display = m_chatArea->GetDisplay();
      long lastPos = display->GetLastPosition();
      if (lastPos > 0) {
        wxString lastChar = display->GetRange(lastPos - 1, lastPos);
        if (lastChar == "\n") {
          display->Remove(lastPos - 1, lastPos);
        }
      }
      display->EndSuppressUndo();
    }

    EndBatchUpdate();
    ScrollToBottomIfAtBottom();
  } else {
    // Out of order message - must resort and refresh
    ScheduleRefresh();
  }
}

void ChatViewWidget::DisplayMessages(const std::vector<MessageInfo> &messages) {
  CVWLOG("DisplayMessages: adding " << messages.size() << " messages");

  // Add all messages to storage first
  {
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    for (const auto &msg : messages) {
      // Skip duplicates
      if (msg.id != 0 && m_displayedMessageIds.count(msg.id) > 0) {
        continue;
      }
      size_t index = m_messages.size();
      m_messages.push_back(msg);
      if (msg.id != 0) {
        m_displayedMessageIds.insert(msg.id);
        m_messageIdToIndex[msg.id] = index;
      }

      // Trigger download for media if needed
      if (msg.hasPhoto || msg.hasSticker || msg.hasAnimation || msg.hasVoice ||
          msg.hasVideoNote || msg.hasVideo) {
        MediaInfo info;
        info.fileId = msg.mediaFileId;
        info.localPath = msg.mediaLocalPath;
        if (msg.hasPhoto)
          info.type = MediaType::Photo;
        else if (msg.hasSticker)
          info.type = MediaType::Sticker;
        else if (msg.hasAnimation)
          info.type = MediaType::GIF;
        else if (msg.hasVoice)
          info.type = MediaType::Voice;
        else if (msg.hasVideoNote)
          info.type = MediaType::VideoNote;
        else if (msg.hasVideo)
          info.type = MediaType::Video;
        EnsureMediaDownloaded(info);
      }
    }
  }

  // Render all messages in proper order immediately (not debounced for bulk
  // loads)
  RefreshDisplay();
}

void ChatViewWidget::BufferOlderMessages(const std::vector<MessageInfo> &messages) {
  // Optimized: add messages to storage and shift virtual window to include them
  // Only renders MAX_DISPLAYED_MESSAGES so this is fast
  HideLoadingIndicator();
  
  if (messages.empty()) {
    m_isLoadingHistory = false;
    return;
  }

  // Get existing message IDs to avoid duplicates
  std::set<int64_t> existingIds;
  {
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    for (const auto &msg : m_messages) {
      existingIds.insert(msg.id);
    }
  }
  
  // Collect new messages
  std::vector<MessageInfo> newMessages;
  for (const auto &msg : messages) {
    if (msg.id != 0 && existingIds.find(msg.id) == existingIds.end()) {
      newMessages.push_back(msg);
    }
  }
  
  if (newMessages.empty()) {
    m_isLoadingHistory = false;
    return;
  }
  
  size_t addedCount = newMessages.size();
  CVWLOG("BufferOlderMessages: adding " << addedCount << " older messages");
  
  // Add new messages to storage
  {
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    for (const auto &msg : newMessages) {
      m_messages.push_back(msg);
      m_displayedMessageIds.insert(msg.id);
    }
    
    // Sort by ID (oldest first)
    std::sort(m_messages.begin(), m_messages.end(),
              [](const MessageInfo &a, const MessageInfo &b) {
                return a.id < b.id;
              });
    
    // Rebuild index
    m_messageIdToIndex.clear();
    for (size_t i = 0; i < m_messages.size(); i++) {
      if (m_messages[i].id != 0) {
        m_messageIdToIndex[m_messages[i].id] = i;
      }
    }
    
    // VIRTUAL WINDOW: Shift window to include the new older messages
    // New messages are prepended (after sort), so shift window start back
    size_t totalMessages = m_messages.size();
    
    if (totalMessages <= MAX_DISPLAYED_MESSAGES) {
      // All messages fit in window
      m_displayWindowStart = 0;
      m_displayWindowEnd = totalMessages;
    } else {
      // Shift window to show some of the new older messages
      // Keep the same messages visible, but now there are older ones available
      m_displayWindowStart += addedCount;  // Indices shifted because of prepend
      m_displayWindowEnd += addedCount;
      
      // Now shift back to show some older messages
      size_t shiftBack = std::min(addedCount, m_displayWindowStart);
      m_displayWindowStart -= shiftBack;
      m_displayWindowEnd -= shiftBack;
      
      // Clamp to valid range
      if (m_displayWindowEnd > totalMessages) {
        m_displayWindowEnd = totalMessages;
      }
      if (m_displayWindowEnd - m_displayWindowStart > MAX_DISPLAYED_MESSAGES) {
        m_displayWindowStart = m_displayWindowEnd - MAX_DISPLAYED_MESSAGES;
      }
    }
  }
  
  // Do a single refresh - only renders the virtual window (fast!)
  RefreshDisplay();
  
  // Scroll to show the newly loaded messages at the top
  CallAfter([this]() {
    if (m_chatArea && m_chatArea->GetDisplay()) {
      // Scroll to near top to show the new messages
      m_chatArea->GetDisplay()->ShowPosition(0);
    }
  });
  
  m_isLoadingHistory = false;
}

void ChatViewWidget::RenderBufferedMessages() {
  // Legacy function - now BufferOlderMessages renders immediately
  // Keep for API compatibility but it's a no-op since we don't buffer anymore
  CVWLOG("RenderBufferedMessages: no-op (scroll-based loading renders immediately)");
}

void ChatViewWidget::ShowLoadMoreIndicator(size_t count) {
  // Legacy function - repurposed to show loading indicator
  (void)count;
  ShowLoadingIndicator();
}

void ChatViewWidget::HideLoadMoreIndicator() {
  // Legacy function - repurposed to hide loading indicator
  HideLoadingIndicator();
}

void ChatViewWidget::ShowLoadingIndicator() {
  if (m_loadingIndicator && !m_loadingIndicator->IsShown()) {
    m_loadingIndicator->Show();
    if (GetSizer()) {
      GetSizer()->Layout();
    }
  }
}

void ChatViewWidget::HideLoadingIndicator() {
  if (m_loadingIndicator && m_loadingIndicator->IsShown()) {
    m_loadingIndicator->Hide();
    if (GetSizer()) {
      GetSizer()->Layout();
    }
  }
}

void ChatViewWidget::OnLoadMoreButtonClick(wxCommandEvent &event) {
  // No longer used - scroll-based loading renders immediately
  (void)event;
}

void ChatViewWidget::RemoveMessage(int64_t messageId) {
  if (messageId == 0)
    return;

  bool needsRefresh = false;
  {
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    auto indexIt = m_messageIdToIndex.find(messageId);
    if (indexIt != m_messageIdToIndex.end()) {
      size_t removedIndex = indexIt->second;
      if (removedIndex < m_messages.size()) {
        m_messages.erase(m_messages.begin() + removedIndex);
        m_displayedMessageIds.erase(messageId);
        m_messageIdToIndex.erase(indexIt);

        // Update indices for all messages after the removed one
        for (auto &pair : m_messageIdToIndex) {
          if (pair.second > removedIndex) {
            pair.second--;
          }
        }
        needsRefresh = true;
      }
    }
  }

  if (needsRefresh) {
    ScheduleRefresh();
  }
}

void ChatViewWidget::UpdateMessage(const MessageInfo &msg) {
  if (msg.id == 0)
    return;

  bool neededRefresh = false;
  int64_t oldId = msg.id;
  int64_t newId = (msg.serverMessageId != 0) ? msg.serverMessageId : msg.id;

  {
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    for (auto &existingMsg : m_messages) {
      if (existingMsg.id == msg.id) {
        // Check if meaningful changes occurred that require a redraw
        // Note: mediaLocalPath and mediaThumbnailPath changes do NOT need
        // refresh because they don't affect displayed text - paths are only
        // used for popup
        if (existingMsg.text != msg.text ||
            existingMsg.isEdited != msg.isEdited ||
            existingMsg.reactions != msg.reactions) {

          neededRefresh = true;
        } else if ((existingMsg.mediaFileId == 0 && msg.mediaFileId != 0) ||
                   (existingMsg.mediaThumbnailFileId == 0 &&
                    msg.mediaThumbnailFileId != 0)) {
          // ID appeared where there was none (completion of initial load)
          neededRefresh = true;
        }

        // If server assigned a new ID, update the message ID and track in
        // displayedMessageIds and messageIdToIndex
        if (msg.serverMessageId != 0 && existingMsg.id != msg.serverMessageId) {
          // Update the index map with the new ID
          auto indexIt = m_messageIdToIndex.find(existingMsg.id);
          if (indexIt != m_messageIdToIndex.end()) {
            size_t idx = indexIt->second;
            m_messageIdToIndex.erase(indexIt);
            m_messageIdToIndex[msg.serverMessageId] = idx;
          }
          m_displayedMessageIds.erase(existingMsg.id);
          existingMsg.id = msg.serverMessageId;
          m_displayedMessageIds.insert(msg.serverMessageId);
          neededRefresh = true; // Need to refresh to update media spans
        }

        // Update all fields
        existingMsg.mediaFileId = msg.mediaFileId;
        existingMsg.mediaThumbnailFileId = msg.mediaThumbnailFileId;
        existingMsg.mediaLocalPath = msg.mediaLocalPath;
        existingMsg.mediaThumbnailPath = msg.mediaThumbnailPath;
        existingMsg.mediaFileName = msg.mediaFileName;
        existingMsg.mediaFileSize = msg.mediaFileSize;
        existingMsg.text = msg.text;
        existingMsg.isEdited = msg.isEdited;
        existingMsg.editDate = msg.editDate;
        existingMsg.reactions = msg.reactions;

        break;
      }
    }
  }

  // Update media spans if message ID changed or file IDs became available
  if (msg.serverMessageId != 0 && oldId != newId) {
    for (auto &span : m_mediaSpans) {
      if (span.messageId == oldId) {
        span.messageId = newId;
        // Also update fileId and thumbnailFileId from the new message data
        span.fileId = msg.mediaFileId;
        span.thumbnailFileId = msg.mediaThumbnailFileId;
      }
    }
  }

  // Also update spans that have the same message ID but missing file IDs
  // This handles the case where the message was created before file info was
  // available
  for (auto &span : m_mediaSpans) {
    if (span.messageId == msg.id || span.messageId == newId) {
      if (span.fileId == 0 && msg.mediaFileId != 0) {
        span.fileId = msg.mediaFileId;
      }
      if (span.thumbnailFileId == 0 && msg.mediaThumbnailFileId != 0) {
        span.thumbnailFileId = msg.mediaThumbnailFileId;
      }
    }
  }

  if (neededRefresh) {
    ScheduleRefresh();
  }
}

void ChatViewWidget::BeginBatchUpdate() {
  if (m_batchUpdateDepth == 0 && m_chatArea) {
    m_chatArea->BeginBatchUpdate();
  }
  m_batchUpdateDepth++;
}

void ChatViewWidget::EndBatchUpdate() {
  if (m_batchUpdateDepth > 0) {
    m_batchUpdateDepth--;
    if (m_batchUpdateDepth == 0 && m_chatArea) {
      m_chatArea->EndBatchUpdate();
    }
  }
}

void ChatViewWidget::ClearMessages() {
  // Hide loading indicator if visible
  HideLoadingIndicator();
  
  CVWLOG("ClearMessages: clearing all messages");

  // Save read times to global cache before clearing (so they persist across
  // chat switches)
  if (m_mainFrame && !m_messageReadTimes.empty()) {
    int64_t currentChatId = m_mainFrame->GetCurrentChatId();
    if (currentChatId != 0) {
      // Merge with existing cache (don't overwrite existing entries)
      auto &chatCache = s_perChatReadTimes[currentChatId];
      for (const auto &[msgId, readTime] : m_messageReadTimes) {
        if (readTime > 0 && chatCache.find(msgId) == chatCache.end()) {
          chatCache[msgId] = readTime;
        }
      }
    }
  }

  // Clear message storage
  {
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    m_messages.clear();
    m_displayedMessageIds.clear();
    m_messageIdToIndex.clear();
  }

  // Clear per-message read times and read status (switching chats)
  m_messageReadTimes.clear();
  m_readMarkerSpans.clear();
  m_recentlyReadMessages.clear();
  m_lastReadOutboxId = 0;
  m_lastReadOutboxTime = 0;

  // Stop highlight timer if running
  if (m_highlightTimer.IsRunning()) {
    m_highlightTimer.Stop();
  }

  // Clear display
  if (m_chatArea) {
    m_chatArea->Clear();
  }
  ClearMediaSpans();
  ClearEditSpans();
  ClearLinkSpans();

  // Reset message grouping state and marker tracking
  if (m_messageFormatter) {
    m_messageFormatter->ResetGroupingState();
    // Reset marker tracking (marker was deleted with the clear, just reset
    // tracking)
    m_messageFormatter->ResetUnreadMarker();
  }
  m_lastDisplayedSender.Clear();
  m_lastDisplayedTimestamp = 0;
  m_lastDisplayedMessageId = 0;
  m_lastReadOutboxId = 0;
  m_lastReadOutboxTime = 0;
  m_isLoadingHistory = false;
  m_allHistoryLoaded = false;
}

bool ChatViewWidget::IsMessageOutOfOrder(int64_t messageId) const {
  // With the new vector-based storage and RefreshDisplay, messages are always
  // displayed in order. We no longer need to detect out-of-order messages
  // because RefreshDisplay sorts them before rendering.
  //
  // However, we keep this method for backwards compatibility - it now just
  // returns false since we handle ordering internally.
  return false;
}

void ChatViewWidget::ScrollToBottom() {
  if (m_chatArea) {
    m_chatArea->ScrollToBottom();
    m_wasAtBottom = true;
    HideNewMessageIndicator();
  }
}

void ChatViewWidget::ScrollToBottomIfAtBottom() {
  // Check ACTUAL scroll position, not cached flag
  // This prevents jumping down when user is actively scrolling up
  if (IsAtBottom()) {
    ScrollToBottom();
  } else {
    // User is scrolled up, show new message indicator
    m_newMessageCount++;
    ShowNewMessageIndicator();
  }
}

bool ChatViewWidget::IsAtBottom() const {
  if (!m_chatArea)
    return true;
  return m_chatArea->IsAtBottom();
}

void ChatViewWidget::ShowNewMessageIndicator() {
  if (!m_newMessageButton)
    return;

  // Update button text with count and down arrow emoji
  wxString arrow = wxString::FromUTF8("↓");
  wxString label;
  if (m_newMessageCount == 1) {
    label = wxString::Format("%s 1 New Message", arrow);
  } else if (m_newMessageCount < 100) {
    label = wxString::Format("%s %d New Messages", arrow, m_newMessageCount);
  } else {
    label = wxString::Format("%s 99+ New Messages", arrow);
  }
  m_newMessageButton->SetLabel(label);

  // Position button at bottom center of chat display
  if (m_chatArea) {
    wxSize displaySize = m_chatArea->GetSize();
    wxSize btnSize = m_newMessageButton->GetBestSize();
    int x = (displaySize.GetWidth() - btnSize.GetWidth()) / 2;
    int y = displaySize.GetHeight() - btnSize.GetHeight() - 10;
    m_newMessageButton->SetPosition(wxPoint(x, y));
  }

  m_newMessageButton->Show();
  m_newMessageButton->Raise();
}

void ChatViewWidget::HideNewMessageIndicator() {
  if (m_newMessageButton) {
    m_newMessageButton->Hide();
  }
  m_newMessageCount = 0;
}

void ChatViewWidget::SetLoading(bool loading) {
  m_isLoading = loading;
  // Could add a loading indicator here in the future
}

void ChatViewWidget::AddMediaSpan(long startPos, long endPos,
                                  const MediaInfo &info, int64_t messageId) {
   MediaSpan span;
  span.startPos = startPos;
  span.endPos = endPos;
  span.messageId = messageId;
  span.fileId = info.fileId;
  span.thumbnailFileId = info.thumbnailFileId;
  span.thumbnailFileId = info.thumbnailFileId;
  span.type = info.type;
  span.width = info.width;
  span.height = info.height;

  size_t index = m_mediaSpans.size();
  m_mediaSpans.push_back(span);

  // Update fast lookup index for O(1) updates on download complete
  if (info.fileId != 0) {
    m_fileIdToSpanIndex[info.fileId].push_back(index);
  }
  if (info.thumbnailFileId != 0) {
    m_fileIdToSpanIndex[info.thumbnailFileId].push_back(index);
  }
}

MessageInfo *ChatViewWidget::GetMessageById(int64_t messageId) {
  // Use O(1) map lookup instead of O(n) linear search
  auto it = m_messageIdToIndex.find(messageId);
  if (it != m_messageIdToIndex.end() && it->second < m_messages.size()) {
    return &m_messages[it->second];
  }
  return nullptr;
}

const MessageInfo *ChatViewWidget::GetMessageById(int64_t messageId) const {
  // Use O(1) map lookup instead of O(n) linear search
  auto it = m_messageIdToIndex.find(messageId);
  if (it != m_messageIdToIndex.end() && it->second < m_messages.size()) {
    return &m_messages[it->second];
  }
  return nullptr;
}

MessageInfo *ChatViewWidget::GetMessageByFileId(int32_t fileId) {
  for (auto &msg : m_messages) {
    if (msg.mediaFileId == fileId || msg.mediaThumbnailFileId == fileId) {
      return &msg;
    }
  }
  return nullptr;
}

MediaInfo ChatViewWidget::GetMediaInfoForSpan(const MediaSpan &span) const {
  MediaInfo info;
  info.type = span.type;

  // Always start with span's file IDs as fallback
  info.fileId = span.fileId;
  info.thumbnailFileId = span.thumbnailFileId;
  info.width = span.width;
  info.height = span.height;

  // Look up the message to get current file IDs and paths (single source of
  // truth) The message may have been updated with file IDs since the span was
  // created
  const MessageInfo *msg = GetMessageById(span.messageId);
  if (msg) {
    info.width = msg->width;
    info.height = msg->height;
    info.duration = msg->mediaDuration;
    info.waveform = msg->mediaWaveform;
    // Prefer message's file IDs over span's (message is updated, span is not)
    if (msg->mediaFileId != 0) {
      info.fileId = msg->mediaFileId;
    }
    if (msg->mediaThumbnailFileId != 0) {
      info.thumbnailFileId = msg->mediaThumbnailFileId;
    }
    info.localPath = msg->mediaLocalPath;
    info.thumbnailPath = msg->mediaThumbnailPath;
    info.fileName = msg->mediaFileName;
    info.caption = msg->mediaCaption;
    // isDownloading will be set by caller (ShowMediaPopup) when needed
    // We don't check TelegramClient here to avoid mutex contention on every
    // hover The caller can check download state when actually showing the popup
    info.isDownloading = info.localPath.IsEmpty() && info.fileId != 0;
  } else {
    // Message not found in m_messages - this can happen briefly for new
    // messages We already have span's file IDs from initialization above
  }

  return info;
}

MediaSpan *ChatViewWidget::GetMediaSpanAtPosition(long pos) {
  for (auto &span : m_mediaSpans) {
    if (pos >= span.startPos && pos < span.endPos) {
      return &span;
    }
  }
  return nullptr;
}

void ChatViewWidget::ClearMediaSpans() {
  m_mediaSpans.clear();
  m_fileIdToSpanIndex.clear();
}

void ChatViewWidget::UpdateMediaPath(int32_t fileId,
                                     const wxString &localPath) {
  if (fileId == 0 || localPath.IsEmpty())
    return;

  int updatedCount = 0;

  // Update m_messages - the SINGLE SOURCE OF TRUTH
  for (auto &msg : m_messages) {
    if (msg.mediaFileId == fileId) {
      msg.mediaLocalPath = localPath;
      updatedCount++;
      CVWLOG("UpdateMediaPath: updated message id="
             << msg.id << " mediaLocalPath=" << localPath.ToStdString());
    }
    if (msg.mediaThumbnailFileId == fileId) {
      msg.mediaThumbnailPath = localPath;
      updatedCount++;
      CVWLOG("UpdateMediaPath: updated message id="
             << msg.id << " mediaThumbnailPath=" << localPath.ToStdString());
    }
  }

  CVWLOG("UpdateMediaPath: fileId=" << fileId << " updated " << updatedCount
                                    << " messages");
}

void ChatViewWidget::AddEditSpan(long startPos, long endPos, int64_t messageId,
                                 const wxString &originalText,
                                 int64_t editDate) {
  EditSpan span;
  span.startPos = startPos;
  span.endPos = endPos;
  span.messageId = messageId;
  span.originalText = originalText;
  span.editDate = editDate;
  m_editSpans.push_back(span);
}

EditSpan *ChatViewWidget::GetEditSpanAtPosition(long pos) {
  for (auto &span : m_editSpans) {
    if (pos >= span.startPos && pos < span.endPos) {
      return &span;
    }
  }
  return nullptr;
}

void ChatViewWidget::ClearEditSpans() { m_editSpans.clear(); }

void ChatViewWidget::AddLinkSpan(long startPos, long endPos,
                                 const wxString &url) {
  LinkSpan span;
  span.startPos = startPos;
  span.endPos = endPos;
  span.url = url;
  m_linkSpans.push_back(span);
}

LinkSpan *ChatViewWidget::GetLinkSpanAtPosition(long pos) {
  for (auto &span : m_linkSpans) {
    if (span.Contains(pos)) {
      return &span;
    }
  }
  return nullptr;
}

void ChatViewWidget::ClearLinkSpans() { m_linkSpans.clear(); }

void ChatViewWidget::ShowEditHistoryPopup(const EditSpan &span,
                                          const wxPoint &position) {
  // Create popup on demand
  if (!m_editHistoryPopup) {
    m_editHistoryPopup = new wxPopupWindow(this, wxBORDER_SIMPLE);
  }

  // Create content panel
  m_editHistoryPopup->DestroyChildren();

  wxPanel *panel = new wxPanel(m_editHistoryPopup);
  panel->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_INFOBK));

  wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);

  // Header
  wxStaticText *header = new wxStaticText(panel, wxID_ANY, "Original message:");
  header->SetForegroundColour(
      wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
  header->SetFont(header->GetFont().Bold());
  sizer->Add(header, 0, wxALL, 8);

  // Original text
  wxString originalText = span.originalText;
  if (originalText.IsEmpty()) {
    originalText = "(Original text not available)";
  }

  // Wrap long text
  if (originalText.Length() > 60) {
    wxString wrapped;
    size_t pos = 0;
    while (pos < originalText.Length()) {
      wrapped += originalText.Mid(pos, 60);
      pos += 60;
      if (pos < originalText.Length()) {
        wrapped += "\n";
      }
    }
    originalText = wrapped;
  }

  wxStaticText *textLabel = new wxStaticText(panel, wxID_ANY, originalText);
  textLabel->SetForegroundColour(
      wxSystemSettings::GetColour(wxSYS_COLOUR_INFOTEXT));
  sizer->Add(textLabel, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

  // Edit time
  if (span.editDate > 0) {
    time_t t = static_cast<time_t>(span.editDate);
    wxDateTime dt(t);
    wxString editTimeStr = "Edited: " + dt.Format("%Y-%m-%d %H:%M:%S");
    wxStaticText *timeLabel = new wxStaticText(panel, wxID_ANY, editTimeStr);
    timeLabel->SetForegroundColour(
        wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    timeLabel->SetFont(timeLabel->GetFont().Smaller());
    sizer->Add(timeLabel, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
  }

  panel->SetSizer(sizer);
  sizer->Fit(panel);

  wxBoxSizer *popupSizer = new wxBoxSizer(wxVERTICAL);
  popupSizer->Add(panel, 1, wxEXPAND);
  m_editHistoryPopup->SetSizer(popupSizer);
  popupSizer->Fit(m_editHistoryPopup);

  // Position popup near cursor
  m_editHistoryPopup->SetPosition(position);
  m_editHistoryPopup->Show();
}

void ChatViewWidget::HideEditHistoryPopup() {
  if (m_editHistoryPopup && m_editHistoryPopup->IsShown()) {
    m_editHistoryPopup->Hide();
  }
}

void ChatViewWidget::SetUserColors(const wxColour *colors) {
  if (m_chatArea) {
    m_chatArea->SetUserColors(colors);
  }
}

void ChatViewWidget::AddPendingDownload(int32_t fileId) {
  m_pendingDownloads.insert(fileId);
}

bool ChatViewWidget::HasPendingDownload(int32_t fileId) const {
  return m_pendingDownloads.count(fileId) > 0;
}

void ChatViewWidget::RemovePendingDownload(int32_t fileId) {
  m_pendingDownloads.erase(fileId);
}

void ChatViewWidget::AddPendingOpen(int32_t fileId) {
  m_pendingOpens.insert(fileId);
}

bool ChatViewWidget::HasPendingOpen(int32_t fileId) const {
  return m_pendingOpens.count(fileId) > 0;
}

void ChatViewWidget::RemovePendingOpen(int32_t fileId) {
  m_pendingOpens.erase(fileId);
}

void ChatViewWidget::ShowDownloadProgress(const wxString &fileName,
                                          int percent) {
  // Download progress is now shown in status bar via MainFrame
  // This method is kept for API compatibility but is a no-op
  (void)fileName;
  (void)percent;
}

void ChatViewWidget::UpdateDownloadProgress(int percent) {
  // Download progress is now shown in status bar via MainFrame
  // This method is kept for API compatibility but is a no-op
  (void)percent;
}

void ChatViewWidget::HideDownloadProgress() {
  // Download progress is now shown in status bar via MainFrame
  // This method is kept for API compatibility but is a no-op
}

bool ChatViewWidget::IsSameMedia(const MediaInfo &a, const MediaInfo &b) const {
  // Compare by fileId if both have valid IDs
  if (a.fileId != 0 && b.fileId != 0) {
    return a.fileId == b.fileId && a.type == b.type;
  }
  // Fall back to comparing by localPath
  if (!a.localPath.IsEmpty() && !b.localPath.IsEmpty()) {
    return a.localPath == b.localPath && a.type == b.type;
  }
  // Compare by thumbnail if available
  if (a.thumbnailFileId != 0 && b.thumbnailFileId != 0) {
    return a.thumbnailFileId == b.thumbnailFileId && a.type == b.type;
  }
  return false;
}

void ChatViewWidget::ShowMediaPopup(const MediaInfo &info,
                                    const wxPoint &position, int parentBottom) {
  if (!m_mediaPopup) {
    CVWLOG("ShowMediaPopup: no popup widget");
    return;
  }

  // Store parent bottom for popup positioning
  m_mediaPopup->SetParentBottom(parentBottom);

  // Validate input - must have either a fileId, thumbnailFileId, or a local
  // path Stickers may only have thumbnailFileId initially before the main
  // sticker downloads
  if (info.fileId == 0 && info.thumbnailFileId == 0 &&
      info.localPath.IsEmpty() && info.thumbnailPath.IsEmpty()) {
    CVWLOG("ShowMediaPopup: no valid media reference (fileId=0, "
           "thumbnailFileId=0, no paths)");
    return;
  }

  // Don't re-show if already showing the same media (prevents flickering and
  // video restart) Check both if popup is shown AND if we're tracking the same
  // media
  bool alreadyShowingSame = IsSameMedia(m_currentlyShowingMedia, info);

  if (alreadyShowingSame) {
    // Check if paths have changed (download completed since last show)
    bool localPathChanged =
        (m_currentlyShowingMedia.localPath != info.localPath &&
         !info.localPath.IsEmpty() && CachedFileExists(info.localPath));
    bool thumbnailPathChanged =
        (m_currentlyShowingMedia.thumbnailPath != info.thumbnailPath &&
         !info.thumbnailPath.IsEmpty() && CachedFileExists(info.thumbnailPath));

    if (!localPathChanged && !thumbnailPathChanged) {
      // Same media, no path changes.
      // BUT we still need to update position because user might have clicked
      // same link in a different message!
      CVWLOG("ShowMediaPopup: same media already showing/tracked, updating "
             "position only");
      m_mediaPopup->ShowMedia(info, position);
      return;
    }
    CVWLOG("ShowMediaPopup: same media but paths changed, allowing reload. "
           "localPathChanged="
           << localPathChanged
           << " thumbnailPathChanged=" << thumbnailPathChanged);
  }

  // Hide any existing popup before showing NEW media (not same media)
  if (m_mediaPopup->IsShown() && !alreadyShowingSame) {
    m_mediaPopup->StopAllPlayback();
    m_mediaPopup->Hide();
  }

  // Also hide edit history popup
  HideEditHistoryPopup();

  CVWLOG("ShowMediaPopup: fileId="
         << info.fileId << " thumbnailFileId=" << info.thumbnailFileId
         << " type=" << static_cast<int>(info.type)
         << " localPath=" << info.localPath.ToStdString()
         << " thumbnailPath=" << info.thumbnailPath.ToStdString());

  // For stickers, download the thumbnail if not already available
  // Some stickers may only have thumbnailFileId (animated emoji, etc.)
  if (info.type == MediaType::Sticker) {
    // Try to download thumbnail first (for preview)
    if (info.thumbnailFileId != 0 && (info.thumbnailPath.IsEmpty() ||
                                      !CachedFileExists(info.thumbnailPath))) {
      if (m_mainFrame) {
        TelegramClient *client = m_mainFrame->GetTelegramClient();
        if (client) {
          bool clientDownloading = client->IsDownloading(info.thumbnailFileId);
          bool hasPending = HasPendingDownload(info.thumbnailFileId);

          // If we have a stale pending download, clean it up
          if (hasPending && !clientDownloading) {
            CVWLOG("ShowMediaPopup: clearing stale pending thumbnail download "
                   "for fileId="
                   << info.thumbnailFileId);
            RemovePendingDownload(info.thumbnailFileId);
            hasPending = false;
          }

          if (!clientDownloading && !hasPending) {
            CVWLOG("ShowMediaPopup: downloading sticker thumbnail, "
                   "thumbnailFileId="
                   << info.thumbnailFileId);
            wxString displayName =
                info.fileName.IsEmpty() ? "Sticker Thumbnail" : info.fileName;
            client->DownloadFile(info.thumbnailFileId, 12, displayName,
                                 0); // Higher priority for thumbnails
            AddPendingDownload(info.thumbnailFileId);
          } else if (clientDownloading) {
            // Already downloading - boost priority since user clicked
            client->BoostDownloadPriority(info.thumbnailFileId);
          }
        }
      }
    }

    // If fileId is 0 but we have thumbnailFileId, use thumbnail as main display
    // This handles animated emoji and other stickers where main file isn't
    // available
    if (info.fileId == 0 && info.thumbnailFileId != 0) {
      CVWLOG(
          "ShowMediaPopup: sticker has no main fileId, using thumbnail only");
      // The popup will show the thumbnail when it becomes available
    }
  }

  // For videos/GIFs, we need to check if the actual video is downloaded
  // (not just the thumbnail). If only thumbnail exists, trigger video download.
  bool needsVideoDownload = false;
  if (info.type == MediaType::Video || info.type == MediaType::GIF ||
      info.type == MediaType::VideoNote) {
    // Check if localPath is a video file or just a thumbnail (image)
    if (!info.localPath.IsEmpty() && CachedFileExists(info.localPath)) {
      wxFileName fn(info.localPath);
      wxString ext = fn.GetExt().Lower();
      bool isVideoFile =
          (ext == "mp4" || ext == "webm" || ext == "avi" || ext == "mov" ||
           ext == "mkv" || ext == "gif" || ext == "m4v" || ext == "ogv");
      if (!isVideoFile) {
        // It's a thumbnail, need to download the actual video
        needsVideoDownload = true;
      }
    } else {
      // No file at all, need download
      needsVideoDownload = true;
    }
  }

  // If the file isn't downloaded yet, trigger a download
  // But only if we haven't already requested this download (prevent duplicates)
  if (info.localPath.IsEmpty() || !CachedFileExists(info.localPath) ||
      needsVideoDownload) {
    if (info.fileId != 0 && m_mainFrame) {
      TelegramClient *client = m_mainFrame->GetTelegramClient();
      if (client) {
        bool clientDownloading = client->IsDownloading(info.fileId);
        bool hasPending = HasPendingDownload(info.fileId);

        // If we have a stale pending download (we think it's pending but
        // TelegramClient doesn't), clean it up
        if (hasPending && !clientDownloading) {
          CVWLOG("ShowMediaPopup: clearing stale pending download for fileId="
                 << info.fileId);
          RemovePendingDownload(info.fileId);
          hasPending = false;
        }

        if (!clientDownloading && !hasPending) {
          // Start download with high priority for preview
          wxString displayName = info.fileName;
          if (displayName.IsEmpty()) {
            switch (info.type) {
            case MediaType::Photo:
              displayName = "Photo";
              break;
            case MediaType::Video:
              displayName = "Video";
              break;
            case MediaType::GIF:
              displayName = "GIF";
              break;
            case MediaType::VideoNote:
              displayName = "Video Note";
              break;
            case MediaType::Sticker:
              displayName = "Sticker";
              break;
            case MediaType::Voice:
              displayName = "Voice Message";
              break;
            default:
              displayName = "Media";
              break;
            }
          }
          CVWLOG("ShowMediaPopup: downloading media file, fileId="
                 << info.fileId << " name=" << displayName.ToStdString());
          client->DownloadFile(info.fileId, 10, displayName,
                               info.fileSize.IsEmpty() ? 0
                                                       : wxAtol(info.fileSize));
          // Track this as a pending download so we can update popup when
          // complete
          AddPendingDownload(info.fileId);
        } else if (clientDownloading) {
          // Already downloading - boost priority since user clicked
          CVWLOG("ShowMediaPopup: boosting download priority for fileId="
                 << info.fileId);
          client->BoostDownloadPriority(info.fileId);
        }
      }
    }
  }

  // Also boost thumbnail download priority if it's in progress
  if (info.thumbnailFileId != 0 && m_mainFrame) {
    TelegramClient *client = m_mainFrame->GetTelegramClient();
    if (client && client->IsDownloading(info.thumbnailFileId)) {
      client->BoostDownloadPriority(info.thumbnailFileId);
    }
  }

  // Show popup - ShowMedia handles all cases:
  // - If video file exists and is playable, it will play
  // - If only thumbnail exists, it will show thumbnail
  // - If nothing exists, it will show placeholder/loading
  m_currentlyShowingMedia = info;

  // Show the popup directly - don't use CallAfter as it adds latency
  // and can cause the popup to not appear if mouse moves away quickly
  try {
    m_mediaPopup->ShowMedia(info, position);
  } catch (const std::exception &e) {
    CVWLOG("ShowMediaPopup: exception in ShowMedia: " << e.what());
  } catch (...) {
    CVWLOG("ShowMediaPopup: unknown exception in ShowMedia");
  }
}

void ChatViewWidget::HideMediaPopup() {
  // Clear tracking state
  m_currentlyShowingMedia = MediaInfo();

  if (m_mediaPopup) {
    try {
      // Always stop playback - even if popup isn't visible yet
      // (video might be loading in background)
      m_mediaPopup->StopAllPlayback();

      if (m_mediaPopup->IsShown()) {
        m_mediaPopup->Hide();
      }
    } catch (const std::exception &e) {
      CVWLOG("HideMediaPopup: exception: " << e.what());
    } catch (...) {
      CVWLOG("HideMediaPopup: unknown exception");
    }
  }
}

void ChatViewWidget::UpdateMediaPopup(int32_t fileId,
                                      const wxString &localPath) {
  CVWLOG("UpdateMediaPopup called: fileId=" << fileId << " path="
                                            << localPath.ToStdString());

  // Validate input
  if (fileId == 0 || localPath.IsEmpty()) {
    CVWLOG("UpdateMediaPopup: invalid fileId or path");
    return;
  }

  // First, check if this was a user-initiated download to open
  OnMediaDownloadComplete(fileId, localPath);

  if (!m_mediaPopup) {
    CVWLOG("UpdateMediaPopup: no popup exists");
    return;
  }

  // Clear any previous failure tracking for this path since download succeeded
  m_mediaPopup->ClearFailedPath(localPath);

  if (!m_mediaPopup->IsShown()) {
    CVWLOG("UpdateMediaPopup: popup not shown, skipping update");
    return;
  }

  // Check if this file ID matches the current popup's media or thumbnail
  const MediaInfo &currentInfo = m_mediaPopup->GetMediaInfo();
  CVWLOG("UpdateMediaPopup: current popup fileId="
         << currentInfo.fileId
         << " thumbnailFileId=" << currentInfo.thumbnailFileId
         << " currentPath=" << currentInfo.localPath.ToStdString());

  // Check if this is a thumbnail download (for stickers or other media)
  if (currentInfo.thumbnailFileId == fileId) {
    CVWLOG("UpdateMediaPopup: matched thumbnail, updating with path="
           << localPath.ToStdString());
    MediaInfo updatedInfo = currentInfo;
    updatedInfo.thumbnailPath = localPath;
    updatedInfo.isDownloading = false;
    wxPoint pos = m_mediaPopup->GetPosition();
    // Update our tracking so subsequent clicks don't reset
    m_currentlyShowingMedia = updatedInfo;
    m_mediaPopup->ShowMedia(updatedInfo, pos);
    return;
  }

  // Check if this is the main file download
  if (currentInfo.fileId == fileId) {
    CVWLOG("UpdateMediaPopup: matched main file, updating with path="
           << localPath.ToStdString());
    MediaInfo updatedInfo = currentInfo;
    updatedInfo.localPath = localPath;
    updatedInfo.isDownloading = false;
    wxPoint pos = m_mediaPopup->GetPosition();
    // Update our tracking so subsequent clicks don't reset
    m_currentlyShowingMedia = updatedInfo;
    // Re-show with updated info - ShowMedia will handle format detection
    m_mediaPopup->ShowMedia(updatedInfo, pos);
    return;
  }

  // Also check pending downloads - the popup might be showing a different file
  // but we should update tracking
  if (HasPendingDownload(fileId)) {
    CVWLOG("UpdateMediaPopup: fileId not matching current popup but found in "
           "pending downloads");
  } else {
    CVWLOG("UpdateMediaPopup: fileId="
           << fileId
           << " does not match current popup (fileId=" << currentInfo.fileId
           << ", thumbnailFileId=" << currentInfo.thumbnailFileId << ")");
  }
}

void ChatViewWidget::OpenMedia(const MediaInfo &info) {
  // Validate that we have something to open
  if (info.fileId == 0 && info.localPath.IsEmpty()) {
    CVWLOG("OpenMedia: no valid media to open");
    return;
  }

  if (!info.localPath.IsEmpty() && CachedFileExists(info.localPath)) {
    // Open with default application
    try {
      wxLaunchDefaultApplication(info.localPath);
    } catch (const std::exception &e) {
      CVWLOG("OpenMedia: exception launching application: " << e.what());
    }
  } else if (info.fileId != 0 && m_mainFrame) {
    // Need to download first, then open
    TelegramClient *client = m_mainFrame->GetTelegramClient();
    if (client) {
      // Mark this as a pending open (will open when download completes)
      AddPendingDownload(info.fileId);
      AddPendingOpen(info.fileId);

      // Get display name for download indicator
      wxString displayName = info.fileName;
      if (displayName.IsEmpty()) {
        switch (info.type) {
        case MediaType::Photo:
          displayName = "Photo";
          break;
        case MediaType::Video:
          displayName = "Video";
          break;
        case MediaType::GIF:
          displayName = "GIF";
          break;
        case MediaType::Voice:
          displayName = "Voice Message";
          break;
        case MediaType::VideoNote:
          displayName = "Video Note";
          break;
        case MediaType::File:
          displayName = "File";
          break;
        default:
          displayName = "Media";
          break;
        }
      }
      client->DownloadFile(
          info.fileId, 10, displayName,
          info.fileSize.IsEmpty()
              ? 0
              : wxAtol(info.fileSize)); // High priority for user-initiated
                                        // download
    }
  }
}

void ChatViewWidget::OnMediaDownloadComplete(int32_t fileId,
                                             const wxString &localPath) {
  // Clean up pending download tracking
  RemovePendingDownload(fileId);

  // Check if this was a user-initiated "open" request
  if (HasPendingOpen(fileId)) {
    RemovePendingOpen(fileId);

    // Open the file now that it's downloaded
    if (!localPath.IsEmpty() && CachedFileExists(localPath)) {
      wxLaunchDefaultApplication(localPath);
    }
  }
}

wxString ChatViewWidget::FormatTimestamp(int64_t unixTime) {
  if (unixTime <= 0) {
    return wxString();
  }
  time_t t = static_cast<time_t>(unixTime);
  wxDateTime dt(t);
  return dt.Format("%H:%M:%S");
}

wxString ChatViewWidget::FormatSmartTimestamp(int64_t unixTime) {
  if (unixTime <= 0) {
    return wxString();
  }

  time_t t = static_cast<time_t>(unixTime);
  wxDateTime dt(t);
  wxDateTime now = wxDateTime::Now();
  wxDateTime today = now.GetDateOnly();
  wxDateTime yesterday = today - wxDateSpan::Day();
  wxDateTime msgDate = dt.GetDateOnly();

  wxString timeStr = dt.Format("%H:%M:%S");

  if (msgDate == today) {
    return timeStr; // Just time for today
  } else if (msgDate == yesterday) {
    return "Yesterday " + timeStr;
  } else if (msgDate > today - wxDateSpan::Week()) {
    return dt.Format("%a ") + timeStr; // Day name for last week
  } else {
    return dt.Format("%b %d ") + timeStr; // Month day for older
  }
}

void ChatViewWidget::CheckAndTriggerHistoryLoad() {
  // Don't load if already loading
  if (m_isLoadingHistory) {
    return;
  }
  
  if (!m_chatArea || !m_chatArea->GetDisplay()) {
    return;
  }
  
  wxRichTextCtrl *display = m_chatArea->GetDisplay();
  int scrollPos = display->GetScrollPos(wxVERTICAL);
  int scrollRange = display->GetScrollRange(wxVERTICAL);
  int thumbSize = display->GetScrollThumb(wxVERTICAL);
  
  // Check if near the top of the display
  int maxScroll = scrollRange - thumbSize;
  bool nearTop = false;
  
  if (maxScroll > 0) {
    float scrollPercent = (float)scrollPos / (float)maxScroll;
    nearTop = scrollPercent < 0.15f;
  } else {
    // Very short content - check if actually at top
    nearTop = scrollPos <= 10;
  }
  
  if (!nearTop) {
    return;
  }
  
  // VIRTUAL WINDOW: First check if we have more messages in memory to show
  // before loading from TDLib
  {
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    
    // If window start is not at the beginning, we have older messages in memory
    if (m_displayWindowStart > 0) {
      // Shift window to show older messages from memory (no network needed)
      size_t shiftAmount = std::min(m_displayWindowStart, (size_t)30);
      m_displayWindowStart -= shiftAmount;
      m_displayWindowEnd -= shiftAmount;
      
      // Keep window size within limits
      if (m_displayWindowEnd - m_displayWindowStart > MAX_DISPLAYED_MESSAGES) {
        m_displayWindowEnd = m_displayWindowStart + MAX_DISPLAYED_MESSAGES;
      }
      
      // Refresh to show the shifted window
      CallAfter([this]() {
        RefreshDisplay();
      });
      return;
    }
  }
  
  // Window is at the beginning of stored messages - need to load from TDLib
  if (m_allHistoryLoaded) {
    return;
  }
  
  // Cooldown: 800ms between TDLib loads
  static wxLongLong lastLoadTime = 0;
  wxLongLong now = wxGetLocalTimeMillis();
  if (now - lastLoadTime < 800) {
    return;
  }
  
  // Find oldest stored message to load from
  int64_t oldestId = 0;
  {
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    if (!m_messages.empty()) {
      oldestId = m_messages.front().id;
    }
  }
  
  if (oldestId != 0 && m_mainFrame) {
    m_isLoadingHistory = true;
    lastLoadTime = wxGetLocalTimeMillis();
    ShowLoadingIndicator();
    m_mainFrame->LoadMoreMessages(oldestId);
  }
}

void ChatViewWidget::OnScroll(wxScrollWinEvent &event) {
  event.Skip();

  // Update scroll state
  m_wasAtBottom = IsAtBottom();
  if (m_wasAtBottom) {
    HideNewMessageIndicator();
  }
  
  // Check if we need to load more history
  CheckAndTriggerHistoryLoad();
}

void ChatViewWidget::OnMouseWheel(wxMouseEvent &event) {
  event.Skip();

  // Update scroll state after wheel event completes
  CallAfter([this]() {
    m_wasAtBottom = IsAtBottom();
    if (m_wasAtBottom) {
      HideNewMessageIndicator();
    }
    
    // Check if we need to load more history
    CheckAndTriggerHistoryLoad();
  });
}

void ChatViewWidget::OnSize(wxSizeEvent &event) {
  event.Skip();

  // Reposition the new message button
  if (m_newMessageButton && m_newMessageButton->IsShown() && m_chatArea) {
    wxSize displaySize = m_chatArea->GetSize();
    wxSize btnSize = m_newMessageButton->GetBestSize();
    int x = (displaySize.GetWidth() - btnSize.GetWidth()) / 2;
    int y = displaySize.GetHeight() - btnSize.GetHeight() - 10;
    m_newMessageButton->SetPosition(wxPoint(x, y));
  }
}

void ChatViewWidget::OnNewMessageButtonClick(wxCommandEvent &event) {
  // Hide the button
  HideNewMessageIndicator();

  // Scroll to bottom
  m_wasAtBottom = true;
  ScrollToBottom();
}

void ChatViewWidget::OnKeyDown(wxKeyEvent &event) {
  // Let the native control handle shortcuts like Copy (Cmd+C)
  event.Skip();
}

void ChatViewWidget::RecordReadMarker(long startPos, long endPos,
                                      int64_t messageId) {
  // Use the formatter's tracked status marker positions for accurate tooltip
  // placement
  long rMarkerStart = m_messageFormatter->GetLastStatusMarkerStart();
  long rMarkerEnd = m_messageFormatter->GetLastStatusMarkerEnd();

  // If formatter didn't record valid positions, fall back to end of message
  if (rMarkerStart < 0 || rMarkerEnd < 0 || rMarkerStart >= rMarkerEnd) {
    // Fallback: ticks are at end of message before newline
    // Format: ... ✓✓\n  (space + 2 ticks + newline = 4 chars from end)
    rMarkerStart = endPos - 3; // Position of first ✓
    rMarkerEnd = endPos - 1;   // Position after second ✓ (before newline)

    // Make sure we don't go before start of message
    if (rMarkerStart < startPos) {
      rMarkerStart = startPos;
    }
    if (rMarkerEnd <= rMarkerStart) {
      // Fallback to full span if calculation is off
      rMarkerStart = startPos;
      rMarkerEnd = endPos;
    }
  }

  ReadMarkerSpan span;
  span.startPos = rMarkerStart;
  span.endPos = rMarkerEnd;
  span.messageId = messageId;

  // Look up the per-message read time from our tracking map
  auto it = m_messageReadTimes.find(messageId);
  if (it != m_messageReadTimes.end()) {
    span.readTime = it->second;
  } else {
    // No recorded time for this specific message - we don't know when it was
    // read (it was already read before the current session started) Use 0 to
    // indicate unknown time, tooltip will just show "Seen"
    span.readTime = 0;
  }

  m_readMarkerSpans.push_back(span);
}

void ChatViewWidget::SetReadStatus(int64_t lastReadOutboxId, int64_t readTime) {
  // Restore read times from global cache if we're just opening this chat
  // (m_lastReadOutboxId == 0 means we just switched to this chat)
  if (m_lastReadOutboxId == 0 && m_mainFrame) {
    int64_t currentChatId = m_mainFrame->GetCurrentChatId();
    if (currentChatId != 0) {
      auto cacheIt = s_perChatReadTimes.find(currentChatId);
      if (cacheIt != s_perChatReadTimes.end()) {
        // Restore cached read times
        for (const auto &[msgId, cachedTime] : cacheIt->second) {
          if (m_messageReadTimes.find(msgId) == m_messageReadTimes.end()) {
            m_messageReadTimes[msgId] = cachedTime;
          }
        }
      }
    }
  }

  if (lastReadOutboxId <= m_lastReadOutboxId) {
    return;
  }

  // Record read times for all newly read messages
  // Messages between old m_lastReadOutboxId and new lastReadOutboxId are now
  // read
  int64_t now = wxGetUTCTime();
  bool hasNewlyReadMessages = false;

  {
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    for (const auto &msg : m_messages) {
      if (msg.isOutgoing && msg.id > 0 && msg.id > m_lastReadOutboxId &&
          msg.id <= lastReadOutboxId) {
        // This message was just marked as read - record the time
        if (m_messageReadTimes.find(msg.id) == m_messageReadTimes.end()) {
          // Only record time if we have a valid read time from the server
          // If readTime is 0, it means we don't know when it was read (loaded
          // as already-read) In that case, use 0 to indicate "unknown time"
          m_messageReadTimes[msg.id] = readTime > 0 ? readTime : 0;
        }
        // Track for highlight animation
        m_recentlyReadMessages[msg.id] = now;
        hasNewlyReadMessages = true;
      }
    }
  }

  m_lastReadOutboxId = lastReadOutboxId;

  if (readTime > 0) {
    m_lastReadOutboxTime = readTime;
  }

  // Start highlight timer to clear highlights after a few seconds
  if (hasNewlyReadMessages && !m_highlightTimer.IsRunning()) {
    m_highlightTimer.Start(1000); // Check every second
  }

  // Only trigger refresh if we have newly read messages to highlight
  if (hasNewlyReadMessages) {
    ScheduleRefresh();
  }
}

void ChatViewWidget::OnRightDown(wxMouseEvent &event) {
  // Get position in text control
  if (!m_chatArea || !m_chatArea->GetDisplay())
    return;

  wxRichTextCtrl *display = m_chatArea->GetDisplay();
  wxPoint pos = event.GetPosition();
  long charPos = 0;

  // Hit test to find character position
  wxTextCtrlHitTestResult hit = display->HitTest(pos, &charPos);
  if (hit == wxTE_HT_ON_TEXT || hit == wxTE_HT_BEFORE) {
    // Find what's at this position
    MediaSpan *mediaSpan = GetMediaSpanAtPosition(charPos);
    LinkSpan *linkSpan = GetLinkSpanAtPosition(charPos);

    if (linkSpan) {
      m_contextMenuLink = linkSpan->url;
    } else {
      m_contextMenuLink.Clear();
    }

    if (mediaSpan) {
      m_contextMenuMedia = GetMediaInfoForSpan(*mediaSpan);
    } else {
      m_contextMenuMedia = MediaInfo();
    }

    m_contextMenuPos = charPos;
    ShowContextMenu(event.GetPosition());
  }
}

wxString ChatViewWidget::GetSelectedText() const {
  if (m_chatArea && m_chatArea->GetDisplay()) {
    if (m_chatArea->GetDisplay()->HasSelection()) {
      return m_chatArea->GetDisplay()->GetStringSelection();
    }
  }
  return "";
}

wxString ChatViewWidget::GetLinkAtPosition(long pos) const {
  // This is used for right click - we've already done the lookup in OnRightDown
  if (pos == m_contextMenuPos && !m_contextMenuLink.IsEmpty()) {
    return m_contextMenuLink;
  }
  return "";
}

void ChatViewWidget::ShowContextMenu(const wxPoint &pos) {
  wxMenu menu;

  bool hasSelection = !GetSelectedText().IsEmpty();
  bool hasLink = !m_contextMenuLink.IsEmpty();
  bool hasMedia = (m_contextMenuMedia.fileId != 0 ||
                   !m_contextMenuMedia.localPath.IsEmpty());

  if (hasSelection) {
    menu.Append(ID_COPY_TEXT, "Copy");
    Bind(wxEVT_MENU, &ChatViewWidget::OnCopyText, this, ID_COPY_TEXT);
  }

  if (hasLink) {
    menu.Append(ID_COPY_LINK, "Copy Link");
    menu.Append(ID_OPEN_LINK, "Open Link");
    Bind(wxEVT_MENU, &ChatViewWidget::OnCopyLink, this, ID_COPY_LINK);
    Bind(wxEVT_MENU, &ChatViewWidget::OnOpenLink, this, ID_OPEN_LINK);
  }

  if (hasMedia) {
    menu.Append(ID_SAVE_MEDIA, "Save As...");
    menu.Append(ID_OPEN_MEDIA, "Open Media");
    Bind(wxEVT_MENU, &ChatViewWidget::OnSaveMedia, this, ID_SAVE_MEDIA);
    Bind(wxEVT_MENU, &ChatViewWidget::OnOpenMedia, this, ID_OPEN_MEDIA);
  }

  if (menu.GetMenuItemCount() > 0) {
    PopupMenu(&menu, pos);
  }
}

void ChatViewWidget::OnCopyText(wxCommandEvent &event) {
  wxString text = GetSelectedText();
  if (!text.IsEmpty()) {
    if (wxTheClipboard->Open()) {
      wxTheClipboard->SetData(new wxTextDataObject(text));
      wxTheClipboard->Close();
    }
  }
}

void ChatViewWidget::OnCopyLink(wxCommandEvent &event) {
  if (!m_contextMenuLink.IsEmpty()) {
    if (wxTheClipboard->Open()) {
      wxTheClipboard->SetData(new wxTextDataObject(m_contextMenuLink));
      wxTheClipboard->Close();
    }
  }
}

void ChatViewWidget::OnOpenLink(wxCommandEvent &event) {
  if (!m_contextMenuLink.IsEmpty()) {
    wxLaunchDefaultApplication(m_contextMenuLink);
  }
}

void ChatViewWidget::OnSaveMedia(wxCommandEvent &event) {
  if (m_contextMenuMedia.fileId == 0 &&
      m_contextMenuMedia.localPath.IsEmpty()) {
    return;
  }

  // TODO: Implement save dialog
  wxMessageBox("Save As not implemented yet", "Info", wxICON_INFORMATION);
}

void ChatViewWidget::OnOpenMedia(wxCommandEvent &event) {
  if (m_contextMenuMedia.fileId != 0 ||
      !m_contextMenuMedia.localPath.IsEmpty()) {
    OpenMedia(m_contextMenuMedia);
  }
}

void ChatViewWidget::OnMouseMove(wxMouseEvent &event) {
  if (!m_chatArea || !m_chatArea->GetDisplay()) {
    event.Skip();
    return;
  }

  // Throttle mouse move processing to reduce CPU usage
  // Only process every 50ms to avoid excessive work on rapid mouse movements
  static wxLongLong s_lastProcessTime = 0;
  static wxString s_lastTooltip;
  static wxStockCursor s_lastCursor = wxCURSOR_ARROW;

  wxLongLong now = wxGetLocalTimeMillis();
  if (now - s_lastProcessTime < 50) {
    event.Skip();
    return;
  }
  s_lastProcessTime = now;

  wxRichTextCtrl *display = m_chatArea->GetDisplay();
  wxPoint pos = event.GetPosition();
  long charPos = 0;

  // Helper to set cursor without flickering - updates ChatArea's tracked cursor
  // Also caches last cursor to avoid redundant calls
  auto setCursor = [this](wxStockCursor cursor) {
    if (cursor != s_lastCursor) {
      m_chatArea->SetCurrentCursor(cursor);
      s_lastCursor = cursor;
    }
  };

  // Helper to set tooltip only if changed
  auto setTooltip = [display](const wxString &tip) {
    if (tip != s_lastTooltip) {
      if (tip.IsEmpty()) {
        display->SetToolTip(NULL);
      } else {
        display->SetToolTip(tip);
      }
      s_lastTooltip = tip;
    }
  };

  // Hit test to find character position
  wxTextCtrlHitTestResult hit = display->HitTest(pos, &charPos);
  if (hit == wxTE_HT_ON_TEXT || hit == wxTE_HT_BEFORE) {
    // Check for read markers FIRST - they are the most specific (just 3 chars
    // for [R]) and located in the timestamp area, so they won't conflict with
    // message content
    bool foundReadMarker = false;
    for (const auto &span : m_readMarkerSpans) {
      if (charPos >= span.startPos && charPos < span.endPos) {
        setCursor(wxCURSOR_ARROW);

        wxString tooltip = "Seen";
        // Use the per-message read time stored in the span
        if (span.readTime > 0) {
          int64_t readTime = span.readTime;
          wxDateTime now = wxDateTime::Now();
          wxDateTime readDt((time_t)readTime);
          wxTimeSpan diff = now - readDt;

          long total_seconds = diff.GetSeconds().GetValue();
          int mins = total_seconds / 60;
          if (mins < 1) {
            tooltip = "Seen just now";
          } else if (mins < 60) {
            tooltip = wxString::Format("Seen %dm ago", mins);
          } else {
            int hours = mins / 60;
            if (hours < 24) {
              tooltip = wxString::Format("Seen %dh ago", hours);
            } else {
              int days = hours / 24;
              tooltip = wxString::Format("Seen %dd ago", days);
            }
          }
        }

        setTooltip(tooltip);
        foundReadMarker = true;
        break;
      }
    }

    if (foundReadMarker) {
      // Already handled above
    } else {
      // Check for links
      LinkSpan *linkSpan = GetLinkSpanAtPosition(charPos);
      if (linkSpan) {
        setCursor(wxCURSOR_HAND);
        setTooltip(linkSpan->url);
      } else {
        // Check for media
        MediaSpan *mediaSpan = GetMediaSpanAtPosition(charPos);
        if (mediaSpan) {
          setCursor(wxCURSOR_HAND);
          MediaInfo info = GetMediaInfoForSpan(*mediaSpan);
          if (!info.fileName.IsEmpty()) {
            setTooltip(info.fileName);
          } else {
            setTooltip("Click to view");
          }
        } else {
          // Check for edit markers
          EditSpan *editSpan = GetEditSpanAtPosition(charPos);
          if (editSpan) {
            setCursor(wxCURSOR_HAND);
            setTooltip("Click to see original message");
          } else {
            // Normal text - use I-beam for text selection
            setCursor(wxCURSOR_IBEAM);
            setTooltip(wxEmptyString);
          }
        }
      }
    }
  } else {
    // Not on text
    setCursor(wxCURSOR_ARROW);
    setTooltip(wxEmptyString);
  }

  event.Skip();
}

void ChatViewWidget::OnMouseLeave(wxMouseEvent &event) {
  if (m_chatArea) {
    m_chatArea->SetCurrentCursor(wxCURSOR_ARROW);
    if (m_chatArea->GetDisplay()) {
      m_chatArea->GetDisplay()->SetToolTip(NULL);
    }
  }
  event.Skip();
}

void ChatViewWidget::OnLeftDown(wxMouseEvent &event) {
  if (!m_chatArea || !m_chatArea->GetDisplay())
    return;

  wxRichTextCtrl *display = m_chatArea->GetDisplay();
  wxPoint pos = event.GetPosition();
  long charPos = 0;

  // Hit test to find character position
  wxTextCtrlHitTestResult hit = display->HitTest(pos, &charPos);
  if (hit == wxTE_HT_ON_TEXT || hit == wxTE_HT_BEFORE) {
    // Check for links
    LinkSpan *linkSpan = GetLinkSpanAtPosition(charPos);
    if (linkSpan) {
      wxLaunchDefaultApplication(linkSpan->url);
      return;
    }

    // Check for media
    MediaSpan *mediaSpan = GetMediaSpanAtPosition(charPos);
    if (mediaSpan) {
      MediaInfo info = GetMediaInfoForSpan(*mediaSpan);
      ShowMediaPopup(info, ClientToScreen(pos),
                     GetScreenRect().GetBottom()); // Use ChatViewWidget bottom
      return;
    }

    // Check for edit markers
    EditSpan *editSpan = GetEditSpanAtPosition(charPos);
    if (editSpan) {
      ShowEditHistoryPopup(*editSpan, ClientToScreen(pos));
      return;
    }
  }

  // Hide popups if clicking elsewhere
  HideMediaPopup();
  HideEditHistoryPopup();

  event.Skip();
}