#ifndef MESSAGEFORMATTER_H
#define MESSAGEFORMATTER_H

#include <wx/wx.h>
#include <functional>
#include <ctime>
#include "MediaTypes.h"
#include "ChatArea.h"

// Forward declarations
struct MessageInfo;

// Callback type for when a link span is created
using LinkSpanCallback = std::function<void(long startPos, long endPos, const wxString& url)>;

// Handles HexChat-style message formatting for the chat display
// Uses ChatArea for consistent formatting across the application
class MessageFormatter
{
public:
    MessageFormatter(ChatArea* chatArea);
    ~MessageFormatter() = default;
    
    // Set callback for link span tracking
    void SetLinkSpanCallback(LinkSpanCallback callback) { m_linkSpanCallback = callback; }
    
    // Message formatting methods (HexChat-style)
    // These delegate to ChatArea but add link detection and media span tracking
    void AppendMessage(const wxString& timestamp, const wxString& sender,
                       const wxString& message, MessageStatus status = MessageStatus::None,
                       bool statusHighlight = false);
    void AppendActionMessage(const wxString& timestamp, const wxString& sender,
                             const wxString& action, MessageStatus status = MessageStatus::None,
                             bool statusHighlight = false);
    void AppendServiceMessage(const wxString& timestamp, const wxString& message);
    void AppendNoticeMessage(const wxString& timestamp, const wxString& source,
                             const wxString& message);
    void AppendUserJoinedMessage(const wxString& timestamp, const wxString& user);
    void AppendUserLeftMessage(const wxString& timestamp, const wxString& user);
    void AppendMediaMessage(const wxString& timestamp, const wxString& sender,
                            const MediaInfo& media, const wxString& caption = "",
                            MessageStatus status = MessageStatus::None,
                            bool statusHighlight = false);
    void AppendReplyMessage(const wxString& timestamp, const wxString& sender,
                            const wxString& replyTo, const wxString& message,
                            MessageStatus status = MessageStatus::None,
                            bool statusHighlight = false);
    void AppendForwardMessage(const wxString& timestamp, const wxString& sender,
                              const wxString& forwardFrom, const wxString& message,
                              MessageStatus status = MessageStatus::None,
                              bool statusHighlight = false);
    void AppendEditedMessage(const wxString& timestamp, const wxString& sender,
                             const wxString& message,
                             long* editSpanStart = nullptr, long* editSpanEnd = nullptr,
                             MessageStatus status = MessageStatus::None,
                             bool statusHighlight = false);
    void AppendHighlightMessage(const wxString& timestamp, const wxString& sender,
                                const wxString& message,
                                MessageStatus status = MessageStatus::None,
                                bool statusHighlight = false);
    
    // HexChat-style unread marker line
    void AppendUnreadMarker();
    
    // Remove the unread marker if present (call when new messages arrive while viewing)
    void RemoveUnreadMarker();
    
    // Reset marker tracking without trying to remove text (call when chat is cleared)
    void ResetUnreadMarker() { m_unreadMarkerStart = -1; m_unreadMarkerEnd = -1; }
    
    // Check if marker is present
    bool HasUnreadMarker() const { return m_unreadMarkerStart >= 0; }
    
    // Date separator (HexChat style: "--- Today ---", "--- Monday, Jan 15 ---")
    void AppendDateSeparator(const wxString& dateText);
    void AppendDateSeparatorForTime(int64_t unixTime);
    
    // Continuation message (same sender, no user/timestamp shown, just indented)
    void AppendContinuationMessage(const wxString& message);
    
    // Check if we should group with previous message (same sender within time window)
    bool ShouldGroupWithPrevious(const wxString& sender, int64_t timestamp) const;
    
    // Update last sender/timestamp for grouping decisions
    void SetLastMessage(const wxString& sender, int64_t timestamp);
    
    // Reset grouping state (e.g., when clearing messages or changing date)
    void ResetGroupingState();
    
    // Get date string for a timestamp (for date separator logic)
    static wxString GetDateString(int64_t unixTime);
    static bool IsSameDay(int64_t time1, int64_t time2);
    
    // Display a MessageInfo from TDLib
    void DisplayMessage(const MessageInfo& msg, const wxString& timestamp);
    
    // Get the last media span start/end positions (for hover tracking)
    long GetLastMediaSpanStart() const { return m_lastMediaSpanStart; }
    long GetLastMediaSpanEnd() const { return m_lastMediaSpanEnd; }
    
    // Write text with clickable links detected and formatted
    void WriteTextWithLinks(const wxString& text);
    
private:
    ChatArea* m_chatArea;
    
    // Additional colors not in ChatArea (for specialized formatting)
    wxColour m_mediaColor;
    wxColour m_editedColor;
    wxColour m_forwardColor;
    wxColour m_replyColor;
    wxColour m_highlightColor;
    
    // Last media span positions
    long m_lastMediaSpanStart;
    long m_lastMediaSpanEnd;
    
    // Unread marker position tracking
    long m_unreadMarkerStart;
    long m_unreadMarkerEnd;
    
    // Callback for link spans
    LinkSpanCallback m_linkSpanCallback;
    
    // Message grouping state (HexChat-style)
    wxString m_lastSender;
    int64_t m_lastTimestamp;
    int64_t m_lastDateDay;  // Day number for date separator logic
    static const int GROUP_TIME_WINDOW_SECONDS = 300;  // 5 minutes
};

#endif // MESSAGEFORMATTER_H