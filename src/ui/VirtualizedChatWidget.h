#ifndef VIRTUALIZEDCHATWIDGET_H
#define VIRTUALIZEDCHATWIDGET_H

#include <wx/wx.h>
#include <wx/dcbuffer.h>
#include <wx/clipbrd.h>
#include <wx/tooltip.h>
#include <wx/display.h>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <functional>
#include <unordered_map>

#include "../telegram/Types.h"
#include "MediaTypes.h"
#include "ChatArea.h"  // For MessageStatus enum

// Forward declarations
class MainFrame;
class MediaPopup;

// Text segment with styling information
struct TextSegment {
    wxString text;
    enum class Type {
        Plain,
        Bold,
        Italic,
        BoldItalic,
        Code,
        Pre,
        Link,
        Mention,
        Hashtag,
        Email,
        Phone,
        Strikethrough,
        Underline,
        Spoiler
    } type = Type::Plain;
    wxString url;  // For links
    int startChar = 0;  // Character offset in original text
    int length = 0;
    
    TextSegment() = default;
    TextSegment(const wxString& t, Type tp = Type::Plain) : text(t), type(tp), length(t.length()) {}
};

// Clickable area within a message
struct ClickableArea {
    wxRect rect;
    enum class Type { 
        Link, 
        Media, 
        Edit, 
        Mention,
        Reaction,
        ReadMarker 
    } type;
    wxString data;  // URL for links, username for mentions, etc.
    int64_t messageId = 0;
    MediaInfo mediaInfo;  // For media type
    
    bool Contains(const wxPoint& pt) const { return rect.Contains(pt); }
};

// Cached layout information for a single message
struct MessageLayout {
    int64_t messageId = 0;
    int yPosition = 0;          // Y position in virtual space
    int height = 0;             // Total height of this message
    int textHeight = 0;         // Height of text portion
    int mediaHeight = 0;        // Height of media section
    int reactionsHeight = 0;    // Height of reactions line
    bool needsRecalc = true;    // Flag to recalculate on next paint
    bool hasDateSeparator = false;  // Show date separator above this message
    
    // Cached parsed text segments (with formatting)
    std::vector<TextSegment> textSegments;
    
    // Cached wrapped lines for each segment
    struct WrappedLine {
        wxString text;
        TextSegment::Type type = TextSegment::Type::Plain;
        wxString url;
        int xOffset = 0;  // X offset within the line (for inline segments)
    };
    std::vector<std::vector<WrappedLine>> wrappedLines;  // Lines of segments
    
    // Simple wrapped lines for display (combined)
    std::vector<wxString> displayLines;
    
    // Clickable areas in screen coordinates (relative to message top)
    std::vector<ClickableArea> clickableAreas;
    
    // Media info if present
    bool hasMedia = false;
    MediaInfo mediaInfo;
    wxRect mediaRect;
    
    // Reactions if present
    std::vector<std::pair<wxString, std::vector<wxString>>> reactions;  // emoji -> users
    wxRect reactionsRect;
    
    // Edit marker if edited
    bool isEdited = false;
    wxRect editMarkerRect;
    
    // Status marker for outgoing
    MessageStatus status = MessageStatus::None;
    wxRect statusRect;
};

// Configuration for rendering
struct ChatRenderConfig {
    // Fonts
    wxFont timestampFont;
    wxFont usernameFont;
    wxFont messageFont;
    wxFont emojiFont;
    wxFont boldFont;
    wxFont italicFont;
    wxFont boldItalicFont;
    wxFont codeFont;
    
    // Colors
    wxColour backgroundColor;
    wxColour textColor;
    wxColour timestampColor;
    wxColour ownUsernameColor;
    wxColour otherUsernameColor;
    wxColour linkColor;
    wxColour mentionColor;
    wxColour systemMessageColor;
    wxColour selectionColor;
    wxColour selectionTextColor;
    wxColour dateSeparatorColor;
    wxColour dateSeparatorLineColor;
    wxColour mediaColor;
    wxColour codeBackgroundColor;
    wxColour codeTextColor;
    wxColour highlightColor;
    wxColour reactionColor;
    wxColour editedColor;
    wxColour readTickColor;
    wxColour sentTickColor;
    wxColour spoilerColor;
    
    // Spacing
    int horizontalPadding = 10;
    int verticalPadding = 2;
    int messagePadding = 4;
    int timestampWidth = 75;
    int usernameWidth = 120;
    int lineSpacing = 3;
    int mediaPlaceholderHeight = 24;
    int reactionHeight = 20;
    int dateSeparatorHeight = 30;
    
    // Behavior
    int scrollSpeed = 3;  // Lines per scroll tick
    int messageGroupTimeWindow = 120;  // Seconds for message grouping
};

// Read marker span for tracking read receipts
struct ReadMarkerSpan {
    int64_t messageId;
    wxRect rect;
    int64_t readTime;
};

// High-performance virtualized chat widget
// Only renders visible messages - O(visible) instead of O(all)
class VirtualizedChatWidget : public wxPanel {
public:
    VirtualizedChatWidget(wxWindow* parent, MainFrame* mainFrame);
    virtual ~VirtualizedChatWidget();
    
    // Message management
    void AddMessage(const MessageInfo& msg);
    void AddMessages(const std::vector<MessageInfo>& messages);
    void PrependMessages(const std::vector<MessageInfo>& messages);  // For history loading
    void UpdateMessage(const MessageInfo& msg);
    void RemoveMessage(int64_t messageId);
    void ClearMessages();
    
    // Get message by ID
    const MessageInfo* GetMessageById(int64_t messageId) const;
    MessageInfo* GetMessageById(int64_t messageId);
    MessageInfo* GetMessageByFileId(int32_t fileId);
    size_t GetMessageCount() const { return m_messages.size(); }
    
    // Scrolling
    void ScrollToBottom();
    void ScrollToMessage(int64_t messageId);
    void ScrollByLines(int lines);
    void ScrollByPixels(int pixels);
    bool IsAtBottom() const;
    void ForceScrollToBottom();
    
    // Selection
    wxString GetSelectedText() const;
    void ClearSelection();
    void SelectAll();
    
    // Configuration
    void SetConfig(const ChatRenderConfig& config);
    const ChatRenderConfig& GetConfig() const { return m_config; }
    
    // Lazy loading callbacks
    using LoadMoreCallback = std::function<void(int64_t oldestMessageId)>;
    void SetLoadMoreCallback(LoadMoreCallback callback) { m_loadMoreCallback = callback; }
    
    // Media callbacks
    using MediaClickCallback = std::function<void(const MediaInfo& media)>;
    void SetMediaClickCallback(MediaClickCallback callback) { m_mediaClickCallback = callback; }
    
    using MediaDownloadCallback = std::function<void(int32_t fileId, int priority)>;
    void SetMediaDownloadCallback(MediaDownloadCallback callback) { m_mediaDownloadCallback = callback; }
    
    // Link click callback
    using LinkClickCallback = std::function<void(const wxString& url)>;
    void SetLinkClickCallback(LinkClickCallback callback) { m_linkClickCallback = callback; }
    
    // Loading state
    void SetLoadingHistory(bool loading);
    bool IsLoadingHistory() const { return m_isLoadingHistory; }
    void SetAllHistoryLoaded(bool loaded) { m_allHistoryLoaded = loaded; }
    bool IsAllHistoryLoaded() const { return m_allHistoryLoaded; }
    
    // Current user for highlighting mentions
    void SetCurrentUsername(const wxString& username) { m_currentUsername = username; }
    wxString GetCurrentUsername() const { return m_currentUsername; }
    
    // Topic bar
    void SetTopicText(const wxString& topic);
    void ClearTopicText();
    wxString GetTopicText() const { return m_topicText; }
    
    // Read status
    void SetLastReadOutboxId(int64_t messageId) { m_lastReadOutboxId = messageId; }
    void SetReadStatus(int64_t messageId, int64_t readTime);
    
    // Media popup control
    void ShowMediaPopup(const MediaInfo& info, const wxPoint& pos, int bottomBoundary);
    void HideMediaPopup();
    void UpdateMediaPath(int32_t fileId, const wxString& localPath);
    
    // Download handling
    void OnMediaDownloadComplete(int32_t fileId, const wxString& localPath);
    void AddPendingDownload(int32_t fileId);
    bool HasPendingDownload(int32_t fileId) const;
    void RemovePendingDownload(int32_t fileId);
    void AddPendingOpen(int32_t fileId);
    bool HasPendingOpen(int32_t fileId) const;
    void RemovePendingOpen(int32_t fileId);
    
    // Context menu
    void ShowContextMenu(const wxPoint& pos);
    
    // Unread marker
    void ShowUnreadMarker();
    void HideUnreadMarker();
    bool HasUnreadMarker() const { return m_hasUnreadMarker; }
    
    // New message indicator
    void ShowNewMessageIndicator();
    void HideNewMessageIndicator();
    
    // Batch updates
    void BeginBatchUpdate();
    void EndBatchUpdate();

protected:
    // Event handlers
    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnMouseWheel(wxMouseEvent& event);
    void OnMouseDown(wxMouseEvent& event);
    void OnMouseUp(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    void OnRightDown(wxMouseEvent& event);
    void OnKeyDown(wxKeyEvent& event);
    void OnScrollTimer(wxTimerEvent& event);
    void OnChar(wxKeyEvent& event);

private:
    // Text parsing and formatting
    std::vector<TextSegment> ParseMessageText(const wxString& text, const MessageInfo& msg);
    std::vector<TextSegment> DetectUrls(const wxString& text);
    std::vector<TextSegment> ApplyEntities(const wxString& text, const std::vector<TextEntity>& entities);
    
    // Layout calculation
    void RecalculateAllLayouts();
    void RecalculateLayout(size_t index);
    void RecalculateLayoutsFrom(size_t startIndex);
    int CalculateMessageHeight(const MessageInfo& msg, MessageLayout& layout, int availableWidth);
    std::vector<wxString> WrapText(const wxString& text, wxDC& dc, int maxWidth);
    std::vector<wxString> WrapTextWithSegments(const std::vector<TextSegment>& segments, wxDC& dc, int maxWidth);
    void CalculateClickableAreas(MessageLayout& layout, const MessageInfo& msg, int textX, int textY, int textWidth);
    
    // Rendering
    void RenderMessage(wxDC& dc, const MessageInfo& msg, MessageLayout& layout, int yOffset);
    void RenderTimestamp(wxDC& dc, const wxString& timestamp, int x, int y);
    void RenderUsername(wxDC& dc, const wxString& username, bool isOwn, int x, int y, int maxWidth);
    void RenderMessageText(wxDC& dc, MessageLayout& layout, int x, int y, int maxWidth, const MessageInfo& msg);
    void RenderTextSegment(wxDC& dc, const TextSegment& segment, int x, int y);
    void RenderDateSeparator(wxDC& dc, const wxString& dateText, int y, int width);
    void RenderMediaPlaceholder(wxDC& dc, const MediaInfo& media, int x, int y, int width, MessageLayout& layout);
    void RenderReactions(wxDC& dc, const std::map<wxString, std::vector<wxString>>& reactions, int x, int y, int width, MessageLayout& layout);
    void RenderStatusTicks(wxDC& dc, MessageStatus status, int x, int y, MessageLayout& layout);
    void RenderEditMarker(wxDC& dc, int x, int y, MessageLayout& layout);
    void RenderLoadingIndicator(wxDC& dc, int y, int width);
    void RenderSelection(wxDC& dc, const MessageLayout& layout, int yOffset);
    void RenderUnreadMarker(wxDC& dc, int y, int width);
    
    // Visibility calculations
    int GetFirstVisibleMessageIndex() const;
    int GetLastVisibleMessageIndex() const;
    int GetTotalVirtualHeight() const;
    
    // Scroll management
    void UpdateScrollPosition(int newPos);
    void EnsureScrollInBounds();
    void CheckAndTriggerLoadMore();
    void ApplySmoothScroll(int targetDelta);
    
    // Hit testing
    int HitTestMessage(int y) const;  // Returns message index at y coordinate
    ClickableArea* HitTestClickable(int x, int y);
    const ClickableArea* HitTestClickable(int x, int y) const;
    int HitTestCharacter(int x, int y, int& charIndex) const;  // Returns message index, sets char index
    
    // Selection helpers
    void UpdateSelection(const wxPoint& start, const wxPoint& end);
    wxString GetTextInRange(int startMsg, int startChar, int endMsg, int endChar) const;
    
    // Sorting
    void SortMessages();
    void RebuildIndex();
    
    // Helpers
    wxString FormatTimestamp(int64_t unixTime) const;
    wxString FormatSmartTimestamp(int64_t unixTime) const;
    bool NeedsDateSeparator(size_t index) const;
    wxString GetDateSeparatorText(int64_t unixTime) const;
    bool ShouldGroupWithPrevious(size_t index) const;
    wxString GetMediaEmoji(MediaType type) const;
    wxString GenerateAsciiWaveform(const std::vector<uint8_t>& waveformData, int targetLength) const;
    MediaInfo BuildMediaInfo(const MessageInfo& msg) const;
    void OpenMedia(const MediaInfo& info);
    void CopyToClipboard(const wxString& text);
    
    // Core data
    MainFrame* m_mainFrame;
    std::vector<MessageInfo> m_messages;
    std::vector<MessageLayout> m_layouts;
    mutable std::mutex m_messagesMutex;
    
    // Index for fast lookup
    std::map<int64_t, size_t> m_messageIdToIndex;
    std::map<int32_t, size_t> m_fileIdToIndex;  // For media updates
    
    // Scroll state
    int m_scrollPosition = 0;      // Current scroll position in pixels
    int m_totalHeight = 0;         // Total virtual height
    bool m_wasAtBottom = true;     // Track if we were at bottom before update
    int m_targetScrollPosition = 0;  // For smooth scrolling
    bool m_smoothScrolling = false;
    
    // Selection state
    bool m_isSelecting = false;
    bool m_hasSelection = false;
    wxPoint m_selectionAnchor;  // Where selection started (screen coords)
    wxPoint m_selectionEnd;     // Where selection currently ends
    int m_selectionStartMsg = -1;
    int m_selectionStartChar = -1;
    int m_selectionEndMsg = -1;
    int m_selectionEndChar = -1;
    
    // Hover state
    int m_hoverMessageIndex = -1;
    ClickableArea* m_hoverClickable = nullptr;
    wxStockCursor m_currentCursor = wxCURSOR_ARROW;
    
    // Loading state
    bool m_isLoadingHistory = false;
    bool m_allHistoryLoaded = false;
    wxLongLong m_lastLoadTime = 0;
    static const int LOAD_COOLDOWN_MS = 800;
    
    // Smooth scrolling
    wxTimer m_scrollTimer;
    int m_scrollVelocity = 0;
    float m_scrollFriction = 0.92f;
    
    // Configuration
    ChatRenderConfig m_config;
    wxString m_currentUsername;
    wxString m_topicText;
    
    // Callbacks
    LoadMoreCallback m_loadMoreCallback;
    MediaClickCallback m_mediaClickCallback;
    MediaDownloadCallback m_mediaDownloadCallback;
    LinkClickCallback m_linkClickCallback;
    
    // Layout cache validity
    int m_lastLayoutWidth = 0;  // Track width to invalidate cache on resize
    
    // Date separator tracking
    std::set<int64_t> m_dateSeparatorDays;  // Unix days that have separators
    
    // Read status tracking
    int64_t m_lastReadOutboxId = 0;
    std::map<int64_t, int64_t> m_messageReadTimes;  // messageId -> readTime
    std::vector<ReadMarkerSpan> m_readMarkerSpans;
    
    // Pending downloads/opens
    std::set<int32_t> m_pendingDownloads;
    std::set<int32_t> m_pendingOpens;
    
    // Media popup
    MediaPopup* m_mediaPopup = nullptr;
    
    // Batch update mode
    int m_batchUpdateDepth = 0;
    bool m_needsLayoutRecalc = false;
    
    // Unread marker
    bool m_hasUnreadMarker = false;
    int64_t m_unreadMarkerAfterMessageId = 0;
    
    // New message button
    bool m_showNewMessageIndicator = false;
    int m_newMessageCount = 0;
    
    // Context menu state
    wxPoint m_contextMenuPos;
    MediaInfo m_contextMenuMedia;
    wxString m_contextMenuLink;
    int64_t m_contextMenuMessageId = 0;
    
    // Message grouping state
    wxString m_lastDisplayedSender;
    int64_t m_lastDisplayedTimestamp = 0;
    
    // Constants
    static const int SCROLL_TIMER_INTERVAL = 16;  // ~60fps for smooth scrolling
    static const int LOAD_MORE_THRESHOLD = 300;   // Pixels from top to trigger load
    static const int DATE_SEPARATOR_HEIGHT = 30;
    static const int MIN_SELECTION_DISTANCE = 3;  // Minimum pixels to start selection
    
    wxDECLARE_EVENT_TABLE();
};

#endif // VIRTUALIZEDCHATWIDGET_H