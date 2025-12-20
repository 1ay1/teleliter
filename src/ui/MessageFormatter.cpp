#include "MessageFormatter.h"
#include "../telegram/Types.h"
#include <wx/regex.h>
#include <wx/datetime.h>

MessageFormatter::MessageFormatter(ChatArea* chatArea)
    : m_chatArea(chatArea ? chatArea : nullptr),
      m_lastMediaSpanStart(0),
      m_lastMediaSpanEnd(0),
      m_unreadMarkerStart(-1),
      m_unreadMarkerEnd(-1),
      m_lastTimestamp(0),
      m_lastDateDay(0)
{
    // Additional colors not provided by ChatArea
    m_mediaColor = wxColour(0x72, 0x9F, 0xCF);      // Blue for media links
    m_editedColor = wxColour(0x88, 0x88, 0x88);     // Gray for (edited) marker
    m_forwardColor = wxColour(0xAD, 0x7F, 0xA8);    // Purple for forwards
    m_replyColor = wxColour(0x72, 0x9F, 0xCF);      // Blue for replies
    m_highlightColor = wxColour(0xFC, 0xAF, 0x3E);  // Yellow/orange for highlights
}

void MessageFormatter::ResetGroupingState()
{
    m_lastSender.Clear();
    m_lastTimestamp = 0;
    m_lastDateDay = 0;
}

void MessageFormatter::SetLastMessage(const wxString& sender, int64_t timestamp)
{
    m_lastSender = sender;
    m_lastTimestamp = timestamp;
    if (timestamp > 0) {
        time_t t = static_cast<time_t>(timestamp);
        wxDateTime dt(t);
        m_lastDateDay = dt.GetJulianDayNumber();
    }
}

bool MessageFormatter::ShouldGroupWithPrevious(const wxString& sender, int64_t timestamp) const
{
    if (m_lastSender.IsEmpty() || sender.IsEmpty()) {
        return false;
    }
    if (m_lastSender != sender) {
        return false;
    }
    if (m_lastTimestamp == 0 || timestamp == 0) {
        return false;
    }
    // Group if within time window
    int64_t diff = timestamp - m_lastTimestamp;
    return diff >= 0 && diff <= GROUP_TIME_WINDOW_SECONDS;
}

bool MessageFormatter::IsSameDay(int64_t time1, int64_t time2)
{
    if (time1 <= 0 || time2 <= 0) return false;
    wxDateTime dt1(static_cast<time_t>(time1));
    wxDateTime dt2(static_cast<time_t>(time2));
    return dt1.GetJulianDayNumber() == dt2.GetJulianDayNumber();
}

wxString MessageFormatter::GetDateString(int64_t unixTime)
{
    if (unixTime <= 0) return wxString();
    
    time_t t = static_cast<time_t>(unixTime);
    wxDateTime dt(t);
    wxDateTime now = wxDateTime::Now();
    wxDateTime today = now.GetDateOnly();
    wxDateTime yesterday = today - wxDateSpan::Day();
    wxDateTime msgDate = dt.GetDateOnly();
    
    if (msgDate == today) {
        return "Today";
    } else if (msgDate == yesterday) {
        return "Yesterday";
    } else if (msgDate > today - wxDateSpan::Week()) {
        return dt.Format("%A");  // Day name (Monday, Tuesday, etc.)
    } else if (dt.GetYear() == now.GetYear()) {
        return dt.Format("%A, %B %d");  // "Monday, January 15"
    } else {
        return dt.Format("%A, %B %d, %Y");  // "Monday, January 15, 2024"
    }
}

void MessageFormatter::AppendDateSeparator(const wxString& dateText)
{
    // HexChat-style date separator - simple and unobtrusive
    m_chatArea->BeginTextColour(wxColour(0x66, 0x66, 0x66));
    m_chatArea->WriteText("--- " + dateText + " ---\n");
    m_chatArea->EndTextColour();
}

void MessageFormatter::AppendDateSeparatorForTime(int64_t unixTime)
{
    wxString dateStr = GetDateString(unixTime);
    if (!dateStr.IsEmpty()) {
        AppendDateSeparator(dateStr);
    }
    
    // Update last date day
    if (unixTime > 0) {
        time_t t = static_cast<time_t>(unixTime);
        wxDateTime dt(t);
        m_lastDateDay = dt.GetJulianDayNumber();
    }
}

void MessageFormatter::AppendContinuationMessage(const wxString& message)
{
    if (!m_chatArea) return;
    
    // Continuation: just indented message, no timestamp or user
    m_chatArea->BeginTextColour(m_chatArea->GetFgColor());
    m_chatArea->WriteText("        ");  // Indent to align with message
    WriteTextWithLinks(message);
    m_chatArea->WriteText("\n");
    m_chatArea->EndTextColour();
}

void MessageFormatter::WriteTextWithLinks(const wxString& text)
{
    if (!m_chatArea) return;
    
    // Handle empty text
    if (text.IsEmpty()) return;
    
    // URL regex pattern - matches http://, https://, and www. URLs
    static wxRegEx urlRegex(
        "(https?://[^\\s<>\"'\\)\\]]+|www\\.[^\\s<>\"'\\)\\]]+)",
        wxRE_EXTENDED | wxRE_ICASE);
    
    if (!urlRegex.IsValid()) {
        // Fallback - just write plain text
        m_chatArea->WriteText(text);
        return;
    }
    
    wxString remaining = text;
    
    while (urlRegex.Matches(remaining)) {
        size_t matchStart, matchLen;
        if (!urlRegex.GetMatch(&matchStart, &matchLen, 0)) {
            break;
        }
        
        // Write text before the link
        if (matchStart > 0) {
            m_chatArea->WriteText(remaining.Left(matchStart));
        }
        
        // Extract the URL
        wxString url = remaining.Mid(matchStart, matchLen);
        
        // Track link span start
        long linkStart = m_chatArea->GetLastPosition();
        
        // Write the link with special formatting
        m_chatArea->BeginTextColour(m_chatArea->GetLinkColor());
        m_chatArea->BeginUnderline();
        m_chatArea->WriteText(url);
        m_chatArea->EndUnderline();
        m_chatArea->EndTextColour();
        
        // Track link span end
        long linkEnd = m_chatArea->GetLastPosition();
        
        // Notify callback about the link span
        if (m_linkSpanCallback) {
            // Prepend https:// to www. URLs
            wxString fullUrl = url;
            if (url.Lower().StartsWith("www.")) {
                fullUrl = "https://" + url;
            }
            m_linkSpanCallback(linkStart, linkEnd, fullUrl);
        }
        
        // Continue with remaining text
        remaining = remaining.Mid(matchStart + matchLen);
    }
    
    // Write any remaining text
    if (!remaining.IsEmpty()) {
        m_chatArea->WriteText(remaining);
    }
}

void MessageFormatter::AppendMessage(const wxString& timestamp, const wxString& sender,
                                      const wxString& message)
{
    if (!m_chatArea) return;
    
    // [HH:MM] <user> message
    m_chatArea->WriteTimestamp(timestamp);
    
    wxColour userColor = m_chatArea->GetUserColor(sender);
    m_chatArea->BeginTextColour(userColor);
    m_chatArea->WriteText("<");
    m_chatArea->BeginBold();
    m_chatArea->WriteText(sender);
    m_chatArea->EndBold();
    m_chatArea->WriteText("> ");
    m_chatArea->EndTextColour();
    
    m_chatArea->BeginTextColour(m_chatArea->GetFgColor());
    WriteTextWithLinks(message);
    m_chatArea->WriteText("\n");
    m_chatArea->EndTextColour();
}

void MessageFormatter::AppendServiceMessage(const wxString& timestamp, const wxString& message)
{
    if (!m_chatArea) return;
    
    m_chatArea->WriteTimestamp(timestamp);
    
    m_chatArea->BeginTextColour(m_chatArea->GetServiceColor());
    m_chatArea->WriteText("* " + message + "\n");
    m_chatArea->EndTextColour();
}

void MessageFormatter::AppendActionMessage(const wxString& timestamp, const wxString& sender,
                                           const wxString& action)
{
    if (!m_chatArea) return;
    
    // Action message: [HH:MM] * user does something
    m_chatArea->WriteTimestamp(timestamp);
    
    m_chatArea->BeginTextColour(m_chatArea->GetActionColor());
    m_chatArea->WriteText("* ");
    m_chatArea->BeginBold();
    m_chatArea->WriteText(sender);
    m_chatArea->EndBold();
    m_chatArea->WriteText(" " + action + "\n");
    m_chatArea->EndTextColour();
}

void MessageFormatter::AppendNoticeMessage(const wxString& timestamp, const wxString& source,
                                           const wxString& message)
{
    if (!m_chatArea) return;
    
    // HexChat-style notice: [HH:MM] -source- message
    m_chatArea->WriteTimestamp(timestamp);
    
    m_chatArea->BeginTextColour(m_chatArea->GetServiceColor());
    m_chatArea->WriteText("-" + source + "- " + message + "\n");
    m_chatArea->EndTextColour();
}

void MessageFormatter::AppendHighlightMessage(const wxString& timestamp, const wxString& sender,
                                              const wxString& message)
{
    if (!m_chatArea) return;
    
    // Highlighted message (when you are mentioned)
    m_chatArea->WriteTimestamp(timestamp);
    
    wxColour userColor = m_chatArea->GetUserColor(sender);
    m_chatArea->BeginTextColour(userColor);
    m_chatArea->WriteText("<");
    m_chatArea->BeginBold();
    m_chatArea->WriteText(sender);
    m_chatArea->EndBold();
    m_chatArea->WriteText("> ");
    m_chatArea->EndTextColour();
    
    m_chatArea->BeginTextColour(m_highlightColor);
    m_chatArea->BeginBold();
    WriteTextWithLinks(message);
    m_chatArea->EndBold();
    m_chatArea->WriteText("\n");
    m_chatArea->EndTextColour();
}

void MessageFormatter::AppendUserJoinedMessage(const wxString& timestamp, const wxString& user)
{
    if (!m_chatArea) return;
    
    m_chatArea->WriteTimestamp(timestamp);
    
    m_chatArea->BeginTextColour(m_chatArea->GetServiceColor());
    m_chatArea->WriteText("--> " + user + " joined the chat\n");
    m_chatArea->EndTextColour();
}

void MessageFormatter::AppendUserLeftMessage(const wxString& timestamp, const wxString& user)
{
    if (!m_chatArea) return;
    
    m_chatArea->WriteTimestamp(timestamp);
    
    m_chatArea->BeginTextColour(m_chatArea->GetServiceColor());
    m_chatArea->WriteText("<-- " + user + " left the chat\n");
    m_chatArea->EndTextColour();
}

void MessageFormatter::AppendMediaMessage(const wxString& timestamp, const wxString& sender,
                                          const MediaInfo& media, const wxString& caption)
{
    if (!m_chatArea) return;
    
    m_chatArea->WriteTimestamp(timestamp);
    
    wxColour userColor = m_chatArea->GetUserColor(sender);
    m_chatArea->BeginTextColour(userColor);
    m_chatArea->WriteText("<");
    m_chatArea->BeginBold();
    m_chatArea->WriteText(sender);
    m_chatArea->EndBold();
    m_chatArea->WriteText("> ");
    m_chatArea->EndTextColour();
    
    m_lastMediaSpanStart = m_chatArea->GetLastPosition();
    
    m_chatArea->BeginTextColour(m_mediaColor);
    m_chatArea->BeginUnderline();
    
    wxString mediaLabel;
    switch (media.type) {
        case MediaType::Photo:
            mediaLabel = "[Photo]";
            break;
        case MediaType::Video:
            mediaLabel = "[Video]";
            break;
        case MediaType::Sticker:
            mediaLabel = "[Sticker]";
            break;
        case MediaType::GIF:
            mediaLabel = "[GIF]";
            break;
        case MediaType::Voice:
            mediaLabel = "[Voice]";
            break;
        case MediaType::VideoNote:
            mediaLabel = "[Video Message]";
            break;
        case MediaType::File:
            mediaLabel = "[File: " + media.fileName + "]";
            break;
        default:
            mediaLabel = "[Media]";
            break;
    }
    
    m_chatArea->WriteText(mediaLabel);
    m_chatArea->EndUnderline();
    m_chatArea->EndTextColour();
    
    // For stickers, show emoji after the underlined text (emoji doesn't underline well)
    if (media.type == MediaType::Sticker && !media.emoji.IsEmpty()) {
        m_chatArea->WriteText(" " + media.emoji);
    }
    
    m_lastMediaSpanEnd = m_chatArea->GetLastPosition();
    
    if (!caption.IsEmpty() && media.type != MediaType::Sticker) {
        // Don't repeat caption for stickers since emoji is already shown
        m_chatArea->BeginTextColour(m_chatArea->GetFgColor());
        m_chatArea->WriteText(" " + caption);
        m_chatArea->EndTextColour();
    }
    
    m_chatArea->WriteText("\n");
}

void MessageFormatter::AppendReplyMessage(const wxString& timestamp, const wxString& sender,
                                          const wxString& replyToText, const wxString& message)
{
    if (!m_chatArea) return;
    
    m_chatArea->WriteTimestamp(timestamp);
    
    wxColour userColor = m_chatArea->GetUserColor(sender);
    m_chatArea->BeginTextColour(userColor);
    m_chatArea->WriteText("<");
    m_chatArea->BeginBold();
    m_chatArea->WriteText(sender);
    m_chatArea->EndBold();
    m_chatArea->WriteText("> ");
    m_chatArea->EndTextColour();
    
    // Reply quote in italics with a vertical bar prefix
    m_chatArea->BeginTextColour(m_replyColor);
    m_chatArea->BeginItalic();
    // Truncate long reply text
    wxString truncatedReply = replyToText;
    if (truncatedReply.length() > 50) {
        truncatedReply = truncatedReply.Left(47) + "...";
    }
    m_chatArea->WriteText("| " + truncatedReply);
    m_chatArea->EndItalic();
    m_chatArea->EndTextColour();
    
    m_chatArea->WriteText("\n");
    
    // The actual reply message on next line with indent
    m_chatArea->WriteText("        ");
    m_chatArea->BeginTextColour(m_chatArea->GetFgColor());
    WriteTextWithLinks(message);
    m_chatArea->WriteText("\n");
    m_chatArea->EndTextColour();
}

void MessageFormatter::AppendForwardMessage(const wxString& timestamp, const wxString& sender,
                                            const wxString& forwardFrom, const wxString& message)
{
    if (!m_chatArea) return;
    
    m_chatArea->WriteTimestamp(timestamp);
    
    wxColour userColor = m_chatArea->GetUserColor(sender);
    m_chatArea->BeginTextColour(userColor);
    m_chatArea->WriteText("<");
    m_chatArea->BeginBold();
    m_chatArea->WriteText(sender);
    m_chatArea->EndBold();
    m_chatArea->WriteText("> ");
    m_chatArea->EndTextColour();
    
    // Forward indicator with arrow
    m_chatArea->BeginTextColour(m_forwardColor);
    m_chatArea->BeginItalic();
    m_chatArea->WriteText(">> Forwarded from ");
    m_chatArea->BeginBold();
    m_chatArea->WriteText(forwardFrom);
    m_chatArea->EndBold();
    m_chatArea->EndItalic();
    m_chatArea->EndTextColour();
    
    m_chatArea->WriteText("\n");
    
    // The forwarded content on next line with indent
    m_chatArea->WriteText("        ");
    m_chatArea->BeginTextColour(m_chatArea->GetFgColor());
    WriteTextWithLinks(message);
    m_chatArea->WriteText("\n");
    m_chatArea->EndTextColour();
}

void MessageFormatter::AppendUnreadMarker()
{
    if (!m_chatArea) return;
    
    // Track start position of marker
    m_unreadMarkerStart = m_chatArea->GetLastPosition();
    
    // Ensure we start on a new line
    m_chatArea->WriteText("\n");
    
    // Visual marker showing where messages have been read up to
    m_chatArea->BeginTextColour(wxColour(0x4E, 0xC9, 0x4E)); // Green
    m_chatArea->WriteText("----------------------- ");
    m_chatArea->EndTextColour();
    
    m_chatArea->BeginTextColour(wxColour(0x6E, 0xE9, 0x6E)); // Lighter green for text
    m_chatArea->BeginBold();
    m_chatArea->WriteText("<- read up to here");
    m_chatArea->EndBold();
    m_chatArea->EndTextColour();
    
    m_chatArea->BeginTextColour(wxColour(0x4E, 0xC9, 0x4E)); // Green
    m_chatArea->WriteText(" -----------------------\n");
    m_chatArea->EndTextColour();
    
    // Track end position of marker
    m_unreadMarkerEnd = m_chatArea->GetLastPosition();
}

void MessageFormatter::RemoveUnreadMarker()
{
    if (!m_chatArea || m_unreadMarkerStart < 0 || m_unreadMarkerEnd < 0) return;
    
    wxRichTextCtrl* display = m_chatArea->GetDisplay();
    if (!display) return;
    
    // Delete the marker text range
    display->Remove(m_unreadMarkerStart, m_unreadMarkerEnd);
    
    // Reset marker tracking
    m_unreadMarkerStart = -1;
    m_unreadMarkerEnd = -1;
}

void MessageFormatter::AppendEditedMessage(const wxString& timestamp, const wxString& sender,
                                           const wxString& message,
                                           long* editSpanStart, long* editSpanEnd)
{
    if (!m_chatArea) return;
    
    m_chatArea->WriteTimestamp(timestamp);
    
    wxColour userColor = m_chatArea->GetUserColor(sender);
    m_chatArea->BeginTextColour(userColor);
    m_chatArea->WriteText("<");
    m_chatArea->BeginBold();
    m_chatArea->WriteText(sender);
    m_chatArea->EndBold();
    m_chatArea->WriteText("> ");
    m_chatArea->EndTextColour();
    
    m_chatArea->BeginTextColour(m_chatArea->GetFgColor());
    WriteTextWithLinks(message);
    m_chatArea->EndTextColour();
    
    // Simple (edited) marker
    m_chatArea->BeginTextColour(m_editedColor);
    m_chatArea->BeginItalic();
    m_chatArea->WriteText(" (edited)");
    m_chatArea->EndItalic();
    m_chatArea->EndTextColour();
    
    m_chatArea->WriteText("\n");
    
    // No span tracking - TDLib doesn't provide original message text
    if (editSpanStart) *editSpanStart = 0;
    if (editSpanEnd) *editSpanEnd = 0;
}

void MessageFormatter::DisplayMessage(const MessageInfo& msg, const wxString& timestamp)
{
    if (!m_chatArea) return;
    
    if (msg.isForwarded && !msg.forwardedFrom.IsEmpty()) {
        AppendForwardMessage(timestamp, msg.senderName, msg.forwardedFrom, msg.text);
    } else if (msg.replyToMessageId != 0 && !msg.replyToText.IsEmpty()) {
        AppendReplyMessage(timestamp, msg.senderName, msg.replyToText, msg.text);
    } else if (msg.isEdited) {
        AppendEditedMessage(timestamp, msg.senderName, msg.text);
    } else if (msg.hasPhoto || msg.hasVideo || msg.hasDocument || 
               msg.hasVoice || msg.hasVideoNote || msg.hasSticker || msg.hasAnimation) {
        MediaInfo media;
        if (msg.hasPhoto) {
            media.type = MediaType::Photo;
        } else if (msg.hasVideo) {
            media.type = MediaType::Video;
        } else if (msg.hasDocument) {
            media.type = MediaType::File;
            media.fileName = msg.mediaFileName;
        } else if (msg.hasVoice) {
            media.type = MediaType::Voice;
        } else if (msg.hasVideoNote) {
            media.type = MediaType::VideoNote;
        } else if (msg.hasSticker) {
            media.type = MediaType::Sticker;
        } else if (msg.hasAnimation) {
            media.type = MediaType::GIF;
        }
        media.localPath = msg.mediaLocalPath;
        AppendMediaMessage(timestamp, msg.senderName, media, msg.mediaCaption);
    } else {
        AppendMessage(timestamp, msg.senderName, msg.text);
    }
}