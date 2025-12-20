#ifndef MESSAGEFORMATTER_H
#define MESSAGEFORMATTER_H

#include <wx/wx.h>
#include <wx/richtext/richtextctrl.h>
#include "MediaTypes.h"

// Forward declarations
struct MessageInfo;

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
    void SetMediaColor(const wxColour& color) { m_mediaColor = color; }
    void SetEditedColor(const wxColour& color) { m_editedColor = color; }
    void SetForwardColor(const wxColour& color) { m_forwardColor = color; }
    void SetReplyColor(const wxColour& color) { m_replyColor = color; }
    void SetUserColors(const wxColour colors[16]);
    
    // Message formatting methods
    void AppendMessage(const wxString& timestamp, const wxString& sender,
                       const wxString& message);
    void AppendServiceMessage(const wxString& timestamp, const wxString& message);
    void AppendJoinMessage(const wxString& timestamp, const wxString& user);
    void AppendLeaveMessage(const wxString& timestamp, const wxString& user);
    void AppendMediaMessage(const wxString& timestamp, const wxString& sender,
                            const MediaInfo& media, const wxString& caption = "");
    void AppendReplyMessage(const wxString& timestamp, const wxString& sender,
                            const wxString& replyTo, const wxString& message);
    void AppendForwardMessage(const wxString& timestamp, const wxString& sender,
                              const wxString& forwardFrom, const wxString& message);
    void AppendEditedMessage(const wxString& timestamp, const wxString& sender,
                             const wxString& message);
    
    // Display a MessageInfo from TDLib
    void DisplayMessage(const MessageInfo& msg, const wxString& timestamp);
    
    // Get the last media span start/end positions (for hover tracking)
    long GetLastMediaSpanStart() const { return m_lastMediaSpanStart; }
    long GetLastMediaSpanEnd() const { return m_lastMediaSpanEnd; }
    
    // Helper to get user color
    wxColour GetUserColor(const wxString& username);
    
private:
    wxRichTextCtrl* m_display;
    
    // Colors
    wxColour m_timestampColor;
    wxColour m_textColor;
    wxColour m_serviceColor;
    wxColour m_mediaColor;
    wxColour m_editedColor;
    wxColour m_forwardColor;
    wxColour m_replyColor;
    wxColour m_userColors[16];
    
    // Last media span positions
    long m_lastMediaSpanStart;
    long m_lastMediaSpanEnd;
};

#endif // MESSAGEFORMATTER_H