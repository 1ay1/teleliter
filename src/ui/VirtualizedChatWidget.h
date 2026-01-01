#ifndef VIRTUALIZEDCHATWIDGET_H
#define VIRTUALIZEDCHATWIDGET_H

#include <wx/wx.h>
#include <wx/dcbuffer.h>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <functional>

#include "../telegram/Types.h"
#include "MediaTypes.h"

// Forward declarations
class MainFrame;

// Cached layout information for a single message
struct MessageLayout {
    int64_t messageId = 0;
    int yPosition = 0;          // Y position in virtual space
    int height = 0;             // Total height of this message
    int textHeight = 0;         // Height of text portion
    bool needsRecalc = true;    // Flag to recalculate on next paint
    
    // Cached wrapped lines for the message text
    std::vector<wxString> wrappedLines;
    
    // Cached positions for clickable areas
    struct ClickableArea {
        wxRect rect;
        enum Type { Link, Media, Edit } type;
        wxString data;  // URL for links, file path for media, etc.
    };
    std::vector<ClickableArea> clickableAreas;
};

// Configuration for rendering
struct ChatRenderConfig {
    // Fonts
    wxFont timestampFont;
    wxFont usernameFont;
    wxFont messageFont;
    wxFont emojiFont;
    
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
    wxColour dateSeparatorColor;
    
    // Spacing
    int horizontalPadding = 10;
    int verticalPadding = 2;
    int messagePadding = 4;
    int timestampWidth = 70;
    int usernameWidth = 120;
    int lineSpacing = 2;
    
    // Behavior
    int scrollSpeed = 3;  // Lines per scroll tick
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
    size_t GetMessageCount() const { return m_messages.size(); }
    
    // Scrolling
    void ScrollToBottom();
    void ScrollToMessage(int64_t messageId);
    void ScrollByLines(int lines);
    void ScrollByPixels(int pixels);
    bool IsAtBottom() const;
    
    // Selection
    wxString GetSelectedText() const;
    void ClearSelection();
    
    // Configuration
    void SetConfig(const ChatRenderConfig& config);
    const ChatRenderConfig& GetConfig() const { return m_config; }
    
    // Lazy loading callbacks
    using LoadMoreCallback = std::function<void(int64_t oldestMessageId)>;
    void SetLoadMoreCallback(LoadMoreCallback callback) { m_loadMoreCallback = callback; }
    
    // Media click callback
    using MediaClickCallback = std::function<void(const MediaInfo& media)>;
    void SetMediaClickCallback(MediaClickCallback callback) { m_mediaClickCallback = callback; }
    
    // Link click callback
    using LinkClickCallback = std::function<void(const wxString& url)>;
    void SetLinkClickCallback(LinkClickCallback callback) { m_linkClickCallback = callback; }
    
    // Loading state
    void SetLoadingHistory(bool loading) { m_isLoadingHistory = loading; }
    bool IsLoadingHistory() const { return m_isLoadingHistory; }
    void SetAllHistoryLoaded(bool loaded) { m_allHistoryLoaded = loaded; }
    bool IsAllHistoryLoaded() const { return m_allHistoryLoaded; }
    
    // Current user for highlighting mentions
    void SetCurrentUsername(const wxString& username) { m_currentUsername = username; }
    
    // Topic bar
    void SetTopicText(const wxString& topic);
    void ClearTopicText();

protected:
    // Event handlers
    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnMouseWheel(wxMouseEvent& event);
    void OnMouseDown(wxMouseEvent& event);
    void OnMouseUp(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    void OnKeyDown(wxKeyEvent& event);
    void OnScrollTimer(wxTimerEvent& event);

private:
    // Layout calculation
    void RecalculateAllLayouts();
    void RecalculateLayout(size_t index);
    void RecalculateLayoutsFrom(size_t startIndex);
    int CalculateMessageHeight(const MessageInfo& msg, int availableWidth);
    std::vector<wxString> WrapText(const wxString& text, wxDC& dc, int maxWidth);
    
    // Rendering
    void RenderMessage(wxDC& dc, const MessageInfo& msg, const MessageLayout& layout, int yOffset);
    void RenderTimestamp(wxDC& dc, const wxString& timestamp, int x, int y);
    void RenderUsername(wxDC& dc, const wxString& username, bool isOwn, int x, int y);
    void RenderMessageText(wxDC& dc, const std::vector<wxString>& lines, int x, int y);
    void RenderDateSeparator(wxDC& dc, const wxString& dateText, int y, int width);
    void RenderMediaPlaceholder(wxDC& dc, const MediaInfo& media, int x, int y, int width);
    void RenderLoadingIndicator(wxDC& dc, int y, int width);
    
    // Visibility calculations
    int GetFirstVisibleMessageIndex() const;
    int GetLastVisibleMessageIndex() const;
    int GetTotalVirtualHeight() const;
    
    // Scroll management
    void UpdateScrollPosition(int newPos);
    void EnsureScrollInBounds();
    void CheckAndTriggerLoadMore();
    
    // Hit testing
    int HitTestMessage(int y) const;  // Returns message index at y coordinate
    MessageLayout::ClickableArea* HitTestClickable(int x, int y);
    
    // Sorting
    void SortMessages();
    
    // Helpers
    wxString FormatTimestamp(int64_t unixTime) const;
    bool NeedsDateSeparator(size_t index) const;
    wxString GetDateSeparatorText(int64_t unixTime) const;
    
    // Core data
    MainFrame* m_mainFrame;
    std::vector<MessageInfo> m_messages;
    std::vector<MessageLayout> m_layouts;
    mutable std::mutex m_messagesMutex;
    
    // Index for fast lookup
    std::map<int64_t, size_t> m_messageIdToIndex;
    
    // Scroll state
    int m_scrollPosition = 0;      // Current scroll position in pixels
    int m_totalHeight = 0;         // Total virtual height
    bool m_wasAtBottom = true;     // Track if we were at bottom before update
    
    // Selection state
    bool m_isSelecting = false;
    wxPoint m_selectionStart;
    wxPoint m_selectionEnd;
    int m_selectionStartMsg = -1;
    int m_selectionEndMsg = -1;
    
    // Hover state
    int m_hoverMessageIndex = -1;
    MessageLayout::ClickableArea* m_hoverClickable = nullptr;
    
    // Loading state
    bool m_isLoadingHistory = false;
    bool m_allHistoryLoaded = false;
    
    // Smooth scrolling
    wxTimer m_scrollTimer;
    int m_scrollVelocity = 0;
    
    // Configuration
    ChatRenderConfig m_config;
    wxString m_currentUsername;
    wxString m_topicText;
    
    // Callbacks
    LoadMoreCallback m_loadMoreCallback;
    MediaClickCallback m_mediaClickCallback;
    LinkClickCallback m_linkClickCallback;
    
    // Layout cache validity
    int m_lastLayoutWidth = 0;  // Track width to invalidate cache on resize
    
    // Date separator tracking
    std::set<int64_t> m_dateSeparatorDays;  // Unix days that have separators
    
    // Constants
    static const int SCROLL_TIMER_INTERVAL = 16;  // ~60fps for smooth scrolling
    static const int LOAD_MORE_THRESHOLD = 200;   // Pixels from top to trigger load
    static const int DATE_SEPARATOR_HEIGHT = 30;
    
    wxDECLARE_EVENT_TABLE();
};

#endif // VIRTUALIZEDCHATWIDGET_H