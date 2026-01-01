#ifndef CHATVIEWWIDGET_H
#define CHATVIEWWIDGET_H

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <vector>
#include <wx/gauge.h>
#include <wx/popupwin.h>
#include <wx/timer.h>
#include <wx/wx.h>
#include <wx/statbmp.h>

#include "../telegram/Types.h"
#include "ChatArea.h"
#include "MediaTypes.h"

// Forward declarations
class MainFrame;
class MessageFormatter;
class MediaPopup;
class TelegramClient;

class ChatViewWidget : public wxPanel {
public:
  ChatViewWidget(wxWindow *parent, MainFrame *mainFrame);
  virtual ~ChatViewWidget();

  // Message display - messages are stored and rendered in sorted order
  void DisplayMessage(const MessageInfo &msg);
  void DisplayMessages(const std::vector<MessageInfo> &messages);
  void ClearMessages();
  
  void ScrollToBottom();
  void ForceScrollToBottom(); // Force scroll and set m_wasAtBottom = true

  // Refresh the display from the stored message vector
  // This re-renders all messages in proper sorted order
  void RefreshDisplay();

  // Schedule a debounced refresh (coalesces multiple rapid updates)
  void ScheduleRefresh();

  // Add a message to storage without immediately rendering
  // Call RefreshDisplay() after adding messages to update the view
  void AddMessage(const MessageInfo &msg);

  // Update an existing message (e.g., when media info becomes available)
  void UpdateMessage(const MessageInfo &msg);

  // Remove a message (e.g., when deleted)
  void RemoveMessage(int64_t messageId);

  // Get stored messages (for debugging/inspection)
  const std::vector<MessageInfo> &GetMessages() const { return m_messages; }
  size_t GetMessageCount() const { return m_messages.size(); }

  // Message ordering
  bool IsMessageOutOfOrder(int64_t messageId) const;
  int64_t GetLastDisplayedMessageId() const { return m_lastDisplayedMessageId; }
  bool HasMessage(int64_t messageId) const;

  // Reloading state - when true, new messages are ignored until reload
  // completes
  void SetReloading(bool reloading) { m_isReloading = reloading; }
  bool IsReloading() const { return m_isReloading; }

  // Smart scrolling - only auto-scroll if at bottom
  void ScrollToBottomIfAtBottom();
  bool IsAtBottom() const;
  bool IsNearTop() const;  // For lazy loading older messages
  void ShowNewMessageIndicator();
  void HideNewMessageIndicator();
  
  // Lazy loading for older messages
  void SetLoadOlderCallback(std::function<void(int64_t)> callback) { m_loadOlderCallback = callback; }
  void SetHasMoreMessages(bool hasMore) { m_hasMoreMessages = hasMore; }
  void SetIsLoadingOlder(bool loading);
  bool IsLoadingOlder() const { return m_isLoadingOlder; }
  int64_t GetOldestMessageId() const;
  
  // Loading indicator for older messages
  void ShowLoadingOlderIndicator();
  void HideLoadingOlderIndicator();

  // Batch updates (freeze/thaw for performance)
  void BeginBatchUpdate();
  void EndBatchUpdate();

  // Topic bar (HexChat-style header showing chat name and info)
  void SetTopicText(const wxString &chatName, const wxString &info = "");
  void SetTopicUserInfo(const UserInfo &user);  // Enhanced topic bar for private chats
  void ClearTopicText();
  
  // Set Telegram client for user lookups
  void SetTelegramClient(TelegramClient *client) { m_telegramClient = client; }

  // Media span tracking
  void AddMediaSpan(long startPos, long endPos, const MediaInfo &info,
                    int64_t messageId);
  MediaSpan *GetMediaSpanAtPosition(long pos);
  void ClearMediaSpans();
  void UpdateMediaPath(int32_t fileId, const wxString &localPath);

  // Message lookup - single source of truth
  MessageInfo *GetMessageById(int64_t messageId);
  const MessageInfo *GetMessageById(int64_t messageId) const;
  MessageInfo *GetMessageByFileId(int32_t fileId);
  MediaInfo GetMediaInfoForSpan(const MediaSpan &span) const;

  // Edit span tracking (for showing original text on hover)
  void AddEditSpan(long startPos, long endPos, int64_t messageId,
                   const wxString &originalText, int64_t editDate);
  EditSpan *GetEditSpanAtPosition(long pos);
  void ClearEditSpans();

  // Link span tracking (for clickable URLs)
  void AddLinkSpan(long startPos, long endPos, const wxString &url);
  LinkSpan *GetLinkSpanAtPosition(long pos);
  void ClearLinkSpans();



  // Access to ChatArea and underlying display control
  ChatArea *GetChatArea() { return m_chatArea; }
  wxRichTextCtrl *GetDisplayCtrl() {
    return m_chatArea ? m_chatArea->GetDisplay() : nullptr;
  }
  MessageFormatter *GetMessageFormatter() { return m_messageFormatter; }

  // User colors (delegates to ChatArea)
  void SetUserColors(const wxColour *colors);

  // Current user for mention/highlight detection (HexChat-style)
  void SetCurrentUsername(const wxString &username) {
    m_currentUsername = username;
    if (m_chatArea) {
      m_chatArea->SetCurrentUsername(username);
    }
  }
  wxString GetCurrentUsername() const { return m_currentUsername; }

  // Pending downloads - tracks fileIds currently being downloaded
  void AddPendingDownload(int32_t fileId);
  bool HasPendingDownload(int32_t fileId) const;
  void RemovePendingDownload(int32_t fileId);

  // Pending opens - tracks fileIds that should be opened when download
  // completes
  void AddPendingOpen(int32_t fileId);
  bool HasPendingOpen(int32_t fileId) const;
  void RemovePendingOpen(int32_t fileId);

  // Media popup
  void ShowMediaPopup(const MediaInfo &info, const wxPoint &position,
                      int parentBottom = -1);
  void HideMediaPopup();
  void UpdateMediaPopup(int32_t fileId, const wxString &localPath);

  // Edit history popup
  void ShowEditHistoryPopup(const EditSpan &span, const wxPoint &position);
  void HideEditHistoryPopup();

  // Open media (external viewer or download)
  void OpenMedia(const MediaInfo &info);

  // Called when a media download completes
  void OnMediaDownloadComplete(int32_t fileId, const wxString &localPath);

  // Loading state
  void SetLoading(bool loading);
  bool IsLoading() const { return m_isLoading; }

  // Read receipts
  void SetReadStatus(int64_t lastReadOutboxId, int64_t readTime = 0);

  // Download progress bar
  void ShowDownloadProgress(const wxString &fileName, int percent);
  void HideDownloadProgress();
  void UpdateDownloadProgress(int percent);

private:
  void CreateLayout();
  void SetupDisplayControl();
  void CreateNewMessageButton();
  wxString FormatTimestamp(int64_t unixTime);
  wxString FormatSmartTimestamp(int64_t unixTime);

  // Helper to ensure media is downloaded
  void EnsureMediaDownloaded(const MediaInfo &info);

  // Helper to track read markers
  void RecordReadMarker(long startPos, long endPos, int64_t messageId);

  // Render a single message to the display (internal - assumes display is
  // ready)
  void RenderMessageToDisplay(const MessageInfo &msg);
  void DoRenderMessage(const MessageInfo &msg);

  // Sort messages by ID (primary) and date (secondary)
  void SortMessages();

  // Timer callback for debounced refresh
  void OnRefreshTimer(wxTimerEvent &event);

  // Timer callback for highlight animation (fade effect on newly read messages)
  void OnHighlightTimer(wxTimerEvent &event);

  // Event handlers
  void OnMouseMove(wxMouseEvent &event);
  void OnMouseLeave(wxMouseEvent &event);
  void OnLeftDown(wxMouseEvent &event);
  void OnRightDown(wxMouseEvent &event);
  void OnKeyDown(wxKeyEvent &event);
  void OnScroll(wxScrollWinEvent &event);
  void OnMouseWheel(wxMouseEvent &event);
  
  // Lazy loading helpers
  void CheckAndTriggerLazyLoad();
  void ScheduleLazyLoadCheck();
  void OnLazyLoadTimer(wxTimerEvent &event);
  void OnNewMessageButtonClick(wxCommandEvent &event);
  void OnSize(wxSizeEvent &event);

  // Context menu handlers
  void OnCopyText(wxCommandEvent &event);
  void OnCopyLink(wxCommandEvent &event);
  void OnOpenLink(wxCommandEvent &event);
  void OnSaveMedia(wxCommandEvent &event);
  void OnOpenMedia(wxCommandEvent &event);

  // Context menu helpers
  void ShowContextMenu(const wxPoint &pos);
  wxString GetSelectedText() const;
  wxString GetLinkAtPosition(long pos) const;

  // Helper to check if two MediaInfo refer to the same media
  bool IsSameMedia(const MediaInfo &a, const MediaInfo &b) const;

  // Core components
  MainFrame *m_mainFrame;
  ChatArea *m_chatArea;
  MessageFormatter *m_messageFormatter;
  MediaPopup *m_mediaPopup;
  wxPopupWindow *m_editHistoryPopup;
  wxButton *m_newMessageButton;

  // Topic bar (HexChat-style)
  wxPanel *m_topicBar;
  wxStaticText *m_topicText;
  
  // Enhanced topic bar for private chats with user details
  wxPanel *m_userDetailsBar;
  wxStaticBitmap *m_userPhoto;
  wxStaticText *m_userName;
  wxStaticText *m_userUsername;
  wxStaticText *m_userPhone;
  wxStaticText *m_userStatus;
  wxBitmap m_userPhotoBitmap;
  int64_t m_currentUserId = 0;
  TelegramClient *m_telegramClient = nullptr;
  
  // User details bar event handlers
  void OnUserDetailsClick(wxMouseEvent &event);
  void CreateUserDetailsBar();
  void UpdateUserPhoto(const wxString &photoPath);
  wxBitmap CreateCircularBitmap(const wxBitmap &source, int size);
  wxBitmap CreateInitialsAvatar(const wxString &name, int size);

  // Download progress bar (legacy - now shown in status bar)
  wxPanel *m_downloadBar;
  wxStaticText *m_downloadLabel;
  wxGauge *m_downloadGauge;
  wxTimer m_downloadHideTimer;

  // Media spans for clickable media
  std::vector<MediaSpan> m_mediaSpans;

  // Fast lookup index: fileId -> indices in m_mediaSpans
  // Enables O(1) updates when file downloads complete instead of O(n) scan
  std::map<int32_t, std::vector<size_t>> m_fileIdToSpanIndex;

  // Edit spans for showing original text
  std::vector<EditSpan> m_editSpans;

  // Link spans for clickable URLs
  std::vector<LinkSpan> m_linkSpans;



  // Pending downloads - just tracks which fileIds are being downloaded
  std::set<int32_t> m_pendingDownloads;
  mutable std::mutex m_pendingDownloadsMutex;  // Protects m_pendingDownloads

  // Pending opens - fileIds that should be opened when download completes
  std::set<int32_t> m_pendingOpens;
  mutable std::mutex m_pendingOpensMutex;  // Protects m_pendingOpens

  // Debounced refresh timer - coalesces multiple rapid message updates
  wxTimer m_refreshTimer;
  bool m_refreshPending;
  static const int REFRESH_DEBOUNCE_MS = 200; // 200ms debounce - longer to reduce jitter

  // Popup management (click-only, no hover)
  MediaInfo m_currentlyShowingMedia;

  // Smart scrolling state
  bool m_wasAtBottom;
  int m_newMessageCount;
  bool m_isLoading;

  // Highlight timer for fade animation on newly read messages
  wxTimer m_highlightTimer;
  static const int HIGHLIGHT_DURATION_SECONDS = 3;
  static const int HIGHLIGHT_TIMER_ID = wxID_HIGHEST + 300;

  bool m_isReloading; // True when reloading messages due to out-of-order
                      // detection
  int m_batchUpdateDepth;

  // Message grouping state (HexChat-style)
  wxString m_lastDisplayedSender;
  int64_t m_lastDisplayedTimestamp;

  // Stored messages - the source of truth for display
  // Messages are kept sorted by ID for consistent ordering
  std::vector<MessageInfo> m_messages;
  mutable std::mutex m_messagesMutex; // Protects m_messages

  // Fast lookup: message ID -> index in m_messages for O(1) access
  std::map<int64_t, size_t> m_messageIdToIndex;

  // Track displayed message IDs for ordering (derived from m_messages)
  std::set<int64_t> m_displayedMessageIds;
  int64_t m_lastDisplayedMessageId;

  // Current username for highlight detection
  wxString m_currentUsername;

  // Read receipts
  int64_t m_lastReadOutboxId = 0;
  int64_t m_lastReadOutboxTime = 0;

  // Per-message read times - records when each message was marked as read
  // Key: messageId, Value: Unix timestamp when we learned it was read
  std::map<int64_t, int64_t> m_messageReadTimes;

  // Recently read messages for highlight animation (fade effect)
  // Key: messageId, Value: Unix timestamp when it was marked as read
  // Used to show bright green ✓✓ for ~3 seconds after read receipt arrives
  std::map<int64_t, int64_t> m_recentlyReadMessages;

  // Track [R] marker positions for tooltips
  struct ReadMarkerSpan {
    long startPos;
    long endPos;
    int64_t messageId;
    int64_t readTime; // When this specific message was read
  };
  std::vector<ReadMarkerSpan> m_readMarkerSpans;

  // Map to track character position ranges for each message
  // Key: messageId, Value: {startPos, endPos}
  // Used for anchor scrolling (keeping the same message visible after refresh)
  std::map<int64_t, std::pair<long, long>> m_messageRangeMap;

  // Context menu state
  long m_contextMenuPos;
  wxString m_contextMenuLink;
  MediaInfo m_contextMenuMedia;

  // Lazy loading for older messages
  std::function<void(int64_t)> m_loadOlderCallback;
  bool m_hasMoreMessages = true;
  bool m_isLoadingOlder = false;
  wxTimer m_lazyLoadTimer;
  static constexpr int LAZY_LOAD_DEBOUNCE_MS = 500;  // Much longer debounce - wait for scrolling to settle
  
  // Loading indicator for older messages
  wxPanel *m_loadingOlderPanel = nullptr;
  wxStaticText *m_loadingOlderText = nullptr;

  // Menu IDs
  enum {
    ID_COPY_TEXT = wxID_HIGHEST + 1000,
    ID_COPY_LINK,
    ID_OPEN_LINK,
    ID_SAVE_MEDIA,
    ID_OPEN_MEDIA,
    ID_NEW_MESSAGE_BUTTON
  };
};

#endif // CHATVIEWWIDGET_H