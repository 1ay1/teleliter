#ifndef MESSAGEFORMATTER_H
#define MESSAGEFORMATTER_H

#include <wx/wx.h>
#include <wx/richtext/richtextctrl.h>
#include <functional>
#include "MediaTypes.h"

// Forward declarations
struct MessageInfo;

// Callback type for when a link span is created
using LinkSpanCallback = std::function<void(long startPos, long endPos, const wxString& url)>;

// Handles HexChat-style message formatting for the chat display
class MessageFormatter
{
public:
    MessageFormatter(wxRichTextCtrl* display);
    ~MessageFormatter() = default;
    
    // Set colors
    void SetTimestampColor(const wxColour& color) { m_timestampColor = color; }
    void SetTextColor(const wxColour& color) { m_textColor = color; }
    void SetServiceColor(const wxColour& color) { m_serviceColor = color; }
    void SetActionColor(const wxColour& color) { m_actionColor = color; }
    void SetMediaColor(const wxColour& color) { m_mediaColor = color; }
    void SetEditedColor(const wxColour& color) { m_editedColor = color; }
    void SetForwardColor(const wxColour& color) { m_forwardColor = color; }
    void SetReplyColor(const wxColour& color) { m_replyColor = color; }
    void SetHighlightColor(const wxColour& color) { m_highlightColor = color; }
    void SetNoticeColor(const wxColour& color) { m_noticeColor = color; }
    void SetLinkColor(const wxColour& color) { m_linkColor = color; }
    void SetUserColors(const wxColour colors[16]);
    
    // Set callback for link span tracking
    void SetLinkSpanCallback(LinkSpanCallback callback) { m_linkSpanCallback = callback; }
    
    // Message formatting methods (HexChat-style)
    void AppendMessage(const wxString& timestamp, const wxString& sender,
                       const wxString& message);
    void AppendActionMessage(const wxString& timestamp, const wxString& sender,
                             const wxString& action);
    void AppendServiceMessage(const wxString& timestamp, const wxString& message);
    void AppendNoticeMessage(const wxString& timestamp, const wxString& source,
                             const wxString& message);
    void AppendJoinMessage(const wxString& timestamp, const wxString& user);
    void AppendLeaveMessage(const wxString& timestamp, const wxString& user);
    void AppendKickMessage(const wxString& timestamp, const wxString& user,
                           const wxString& by, const wxString& reason = "");
    void AppendModeMessage(const wxString& timestamp, const wxString& user,
                           const wxString& mode);
    void AppendMediaMessage(const wxString& timestamp, const wxString& sender,
                            const MediaInfo& media, const wxString& caption = "");
    void AppendReplyMessage(const wxString& timestamp, const wxString& sender,
                            const wxString& replyTo, const wxString& message);
    void AppendForwardMessage(const wxString& timestamp, const wxString& sender,
                              const wxString& forwardFrom, const wxString& message);
    void AppendEditedMessage(const wxString& timestamp, const wxString& sender,
                             const wxString& message,
                             long* editSpanStart = nullptr, long* editSpanEnd = nullptr);
    void AppendHighlightMessage(const wxString& timestamp, const wxString& sender,
                                const wxString& message);
    
    // HexChat-style unread marker line
    void AppendUnreadMarker();
    
    // Display a MessageInfo from TDLib
    void DisplayMessage(const MessageInfo& msg, const wxString& timestamp);
    
    // Get the last media span start/end positions (for hover tracking)
    long GetLastMediaSpanStart() const { return m_lastMediaSpanStart; }
    long GetLastMediaSpanEnd() const { return m_lastMediaSpanEnd; }
    
    // Helper to get user color
    wxColour GetUserColor(const wxString& username);
    
    // Write text with clickable links detected and formatted
    void WriteTextWithLinks(const wxString& text);
    
private:
    wxRichTextCtrl* m_display;
    
    // Colors (HexChat-style)
    wxColour m_timestampColor;
    wxColour m_textColor;
    wxColour m_serviceColor;
    wxColour m_actionColor;
    wxColour m_mediaColor;
    wxColour m_editedColor;
    wxColour m_forwardColor;
    wxColour m_replyColor;
    wxColour m_highlightColor;
    wxColour m_noticeColor;
    wxColour m_linkColor;
    wxColour m_userColors[16];
    
    // Last media span positions
    long m_lastMediaSpanStart;
    long m_lastMediaSpanEnd;
    
    // Callback for link spans
    LinkSpanCallback m_linkSpanCallback;
};

#endif // MESSAGEFORMATTER_H