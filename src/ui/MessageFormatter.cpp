#include "MessageFormatter.h"
#include "../telegram/Types.h"
#include <wx/regex.h>
#include <wx/datetime.h>

MessageFormatter::MessageFormatter(wxRichTextCtrl* display)
    : m_display(display),
      m_lastMediaSpanStart(0),
      m_lastMediaSpanEnd(0),
      m_lastTimestamp(0),
      m_lastDateDay(0)
{
    // HexChat-style colors
    m_timestampColor = wxColour(0x87, 0x87, 0x87);  // Gray timestamps
    m_textColor = wxColour(0xD3, 0xD7, 0xCF);       // Light gray text
    m_serviceColor = wxColour(0x88, 0x88, 0x88);    // Gray for server/service messages
    m_actionColor = wxColour(0xCE, 0x5C, 0x00);     // Orange for /me actions
    m_mediaColor = wxColour(0x72, 0x9F, 0xCF);      // Blue for media links
    m_editedColor = wxColour(0x88, 0x88, 0x88);     // Gray for (edited) marker
    m_forwardColor = wxColour(0xAD, 0x7F, 0xA8);    // Purple for forwards
    m_replyColor = wxColour(0x72, 0x9F, 0xCF);      // Blue for replies
    m_highlightColor = wxColour(0xFC, 0xAF, 0x3E);  // Yellow/orange for highlights
    m_noticeColor = wxColour(0xAD, 0x7F, 0xA8);     // Purple for notices
    m_linkColor = wxColour(0x72, 0x9F, 0xCF);       // Blue for links
    
    // HexChat default nick colors
    m_userColors[0]  = wxColour(0x00, 0x00, 0x00);  // Not used (black)
    m_userColors[1]  = wxColour(0x00, 0x00, 0xCC);  // Blue
    m_userColors[2]  = wxColour(0x00, 0xCC, 0x00);  // Green
    m_userColors[3]  = wxColour(0xCC, 0x00, 0x00);  // Red
    m_userColors[4]  = wxColour(0xCC, 0x00, 0x00);  // Light red
    m_userColors[5]  = wxColour(0xCC, 0x00, 0xCC);  // Purple
    m_userColors[6]  = wxColour(0xCC, 0x66, 0x00);  // Orange
    m_userColors[7]  = wxColour(0xCC, 0xCC, 0x00);  // Yellow
    m_userColors[8]  = wxColour(0x00, 0xCC, 0x00);  // Light green
    m_userColors[9]  = wxColour(0x00, 0xCC, 0xCC);  // Cyan
    m_userColors[10] = wxColour(0x00, 0xCC, 0xCC);  // Light cyan
    m_userColors[11] = wxColour(0x00, 0x00, 0xFC);  // Light blue
    m_userColors[12] = wxColour(0xCC, 0x00, 0xCC);  // Light purple
    m_userColors[13] = wxColour(0x7F, 0x7F, 0x7F);  // Dark gray
    m_userColors[14] = wxColour(0xCC, 0xCC, 0xCC);  // Light gray
    m_userColors[15] = wxColour(0xD3, 0xD7, 0xCF);  // White
}

void MessageFormatter::SetUserColors(const wxColour colors[16])
{
    for (int i = 0; i < 16; i++) {
        m_userColors[i] = colors[i];
    }
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
    m_display->BeginTextColour(wxColour(0x66, 0x66, 0x66));
    m_display->WriteText("\t--- " + dateText + " ---\n");
    m_display->EndTextColour();
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
    // Continuation: just indented message, no timestamp or nick
    // Use tab to align with message column (HexChat-style)
    m_display->BeginTextColour(m_textColor);
    m_display->WriteText("\t");  // Tab to message column
    WriteTextWithLinks(message);
    m_display->WriteText("\n");
    m_display->EndTextColour();
}

wxColour MessageFormatter::GetUserColor(const wxString& username)
{
    unsigned long hash = 0;
    for (size_t i = 0; i < username.length(); i++) {
        hash = static_cast<unsigned long>(username[i].GetValue()) + (hash << 6) + (hash << 16) - hash;
    }
    return m_userColors[hash % 16];
}

void MessageFormatter::WriteTextWithLinks(const wxString& text)
{
    // URL regex pattern - matches http://, https://, and www. URLs
    static wxRegEx urlRegex(
        "(https?://[^\\s<>\"'\\)\\]]+|www\\.[^\\s<>\"'\\)\\]]+)",
        wxRE_EXTENDED | wxRE_ICASE);
    
    if (!urlRegex.IsValid()) {
        // Fallback - just write plain text
        m_display->WriteText(text);
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
            m_display->WriteText(remaining.Left(matchStart));
        }
        
        // Extract the URL
        wxString url = remaining.Mid(matchStart, matchLen);
        
        // Track link span start
        long linkStart = m_display->GetLastPosition();
        
        // Write the link with special formatting
        m_display->BeginTextColour(m_linkColor);
        m_display->BeginUnderline();
        m_display->WriteText(url);
        m_display->EndUnderline();
        m_display->EndTextColour();
        
        // Track link span end
        long linkEnd = m_display->GetLastPosition();
        
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
        m_display->WriteText(remaining);
    }
}

void MessageFormatter::AppendMessage(const wxString& timestamp, const wxString& sender,
                                      const wxString& message)
{
    // Column 1: [HH:MM] <nick>
    m_display->BeginTextColour(m_timestampColor);
    m_display->WriteText("[" + timestamp + "] ");
    m_display->EndTextColour();
    
    m_display->BeginTextColour(GetUserColor(sender));
    m_display->WriteText("<");
    m_display->BeginBold();
    m_display->WriteText(sender);
    m_display->EndBold();
    m_display->WriteText(">");
    m_display->EndTextColour();
    
    // Tab to column 2 for message text
    m_display->WriteText("\t");
    
    m_display->BeginTextColour(m_textColor);
    WriteTextWithLinks(message);
    m_display->WriteText("\n");
    m_display->EndTextColour();
}

void MessageFormatter::AppendServiceMessage(const wxString& timestamp, const wxString& message)
{
    m_display->BeginTextColour(m_timestampColor);
    m_display->WriteText("[" + timestamp + "] ");
    m_display->EndTextColour();
    
    m_display->BeginTextColour(m_serviceColor);
    m_display->WriteText("*\t" + message + "\n");
    m_display->EndTextColour();
}

void MessageFormatter::AppendActionMessage(const wxString& timestamp, const wxString& sender,
                                           const wxString& action)
{
    // HexChat-style action: [HH:MM] * nick does something
    m_display->BeginTextColour(m_timestampColor);
    m_display->WriteText("[" + timestamp + "] ");
    m_display->EndTextColour();
    
    m_display->BeginTextColour(m_actionColor);
    m_display->WriteText("*");
    m_display->EndTextColour();
    
    m_display->WriteText("\t");
    
    m_display->BeginTextColour(m_actionColor);
    m_display->BeginBold();
    m_display->WriteText(sender);
    m_display->EndBold();
    m_display->WriteText(" " + action + "\n");
    m_display->EndTextColour();
}

void MessageFormatter::AppendNoticeMessage(const wxString& timestamp, const wxString& source,
                                           const wxString& message)
{
    // HexChat-style notice: [HH:MM] -source- message
    m_display->BeginTextColour(m_timestampColor);
    m_display->WriteText("[" + timestamp + "] ");
    m_display->EndTextColour();
    
    m_display->BeginTextColour(m_noticeColor);
    m_display->WriteText("-" + source + "-");
    m_display->EndTextColour();
    
    m_display->WriteText("\t");
    
    m_display->BeginTextColour(m_noticeColor);
    m_display->WriteText(message + "\n");
    m_display->EndTextColour();
}

void MessageFormatter::AppendHighlightMessage(const wxString& timestamp, const wxString& sender,
                                              const wxString& message)
{
    // Highlighted message (when your nick is mentioned)
    m_display->BeginTextColour(m_timestampColor);
    m_display->WriteText("[" + timestamp + "] ");
    m_display->EndTextColour();
    
    m_display->BeginTextColour(GetUserColor(sender));
    m_display->WriteText("<");
    m_display->BeginBold();
    m_display->WriteText(sender);
    m_display->EndBold();
    m_display->WriteText(">");
    m_display->EndTextColour();
    
    m_display->WriteText("\t");
    
    m_display->BeginTextColour(m_highlightColor);
    m_display->BeginBold();
    WriteTextWithLinks(message);
    m_display->EndBold();
    m_display->WriteText("\n");
    m_display->EndTextColour();
}

void MessageFormatter::AppendJoinMessage(const wxString& timestamp, const wxString& user)
{
    m_display->BeginTextColour(m_timestampColor);
    m_display->WriteText("[" + timestamp + "] ");
    m_display->EndTextColour();
    
    m_display->BeginTextColour(m_serviceColor);
    m_display->WriteText("-->");
    m_display->EndTextColour();
    
    m_display->WriteText("\t");
    
    m_display->BeginTextColour(m_serviceColor);
    m_display->WriteText(user + " has joined\n");
    m_display->EndTextColour();
}

void MessageFormatter::AppendLeaveMessage(const wxString& timestamp, const wxString& user)
{
    m_display->BeginTextColour(m_timestampColor);
    m_display->WriteText("[" + timestamp + "] ");
    m_display->EndTextColour();
    
    m_display->BeginTextColour(m_serviceColor);
    m_display->WriteText("<--");
    m_display->EndTextColour();
    
    m_display->WriteText("\t");
    
    m_display->BeginTextColour(m_serviceColor);
    m_display->WriteText(user + " has left\n");
    m_display->EndTextColour();
}

void MessageFormatter::AppendKickMessage(const wxString& timestamp, const wxString& user,
                                         const wxString& by, const wxString& reason)
{
    m_display->BeginTextColour(m_timestampColor);
    m_display->WriteText("[" + timestamp + "] ");
    m_display->EndTextColour();
    
    m_display->BeginTextColour(m_serviceColor);
    m_display->WriteText("<--");
    m_display->EndTextColour();
    
    m_display->WriteText("\t");
    
    m_display->BeginTextColour(m_serviceColor);
    wxString msg = user + " was kicked by " + by;
    if (!reason.IsEmpty()) {
        msg += " (" + reason + ")";
    }
    m_display->WriteText(msg + "\n");
    m_display->EndTextColour();
}

void MessageFormatter::AppendModeMessage(const wxString& timestamp, const wxString& user,
                                         const wxString& mode)
{
    m_display->BeginTextColour(m_timestampColor);
    m_display->WriteText("[" + timestamp + "] ");
    m_display->EndTextColour();
    
    m_display->BeginTextColour(m_serviceColor);
    m_display->WriteText("*");
    m_display->EndTextColour();
    
    m_display->WriteText("\t");
    
    m_display->BeginTextColour(m_serviceColor);
    m_display->WriteText(user + " sets mode: " + mode + "\n");
    m_display->EndTextColour();
}

void MessageFormatter::AppendMediaMessage(const wxString& timestamp, const wxString& sender,
                                          const MediaInfo& media, const wxString& caption)
{
    m_display->BeginTextColour(m_timestampColor);
    m_display->WriteText("[" + timestamp + "] ");
    m_display->EndTextColour();
    
    m_display->BeginTextColour(GetUserColor(sender));
    m_display->WriteText("<");
    m_display->BeginBold();
    m_display->WriteText(sender);
    m_display->EndBold();
    m_display->WriteText(">");
    m_display->EndTextColour();
    
    m_display->WriteText("\t");
    
    m_lastMediaSpanStart = m_display->GetLastPosition();
    
    m_display->BeginTextColour(m_mediaColor);
    m_display->BeginUnderline();
    
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
    
    m_display->WriteText(mediaLabel);
    m_display->EndUnderline();
    m_display->EndTextColour();
    
    // For stickers, show emoji after the underlined text (emoji doesn't underline well)
    if (media.type == MediaType::Sticker && !media.emoji.IsEmpty()) {
        m_display->WriteText(" " + media.emoji);
    }
    
    m_lastMediaSpanEnd = m_display->GetLastPosition();
    
    if (!caption.IsEmpty() && media.type != MediaType::Sticker) {
        // Don't repeat caption for stickers since emoji is already shown
        m_display->BeginTextColour(m_textColor);
        m_display->WriteText(" " + caption);
        m_display->EndTextColour();
    }
    
    m_display->WriteText("\n");
}

void MessageFormatter::AppendReplyMessage(const wxString& timestamp, const wxString& sender,
                                          const wxString& replyTo, const wxString& message)
{
    m_display->BeginTextColour(m_timestampColor);
    m_display->WriteText("[" + timestamp + "] ");
    m_display->EndTextColour();
    
    m_display->BeginTextColour(GetUserColor(sender));
    m_display->WriteText("<");
    m_display->BeginBold();
    m_display->WriteText(sender);
    m_display->EndBold();
    m_display->WriteText(">");
    m_display->EndTextColour();
    
    m_display->WriteText("\t");
    
    // Reply quote in italics with a vertical bar prefix
    m_display->BeginTextColour(m_replyColor);
    m_display->BeginItalic();
    // Truncate long reply text
    wxString truncatedReply = replyTo;
    if (truncatedReply.length() > 50) {
        truncatedReply = truncatedReply.Left(47) + "...";
    }
    m_display->WriteText("│ " + truncatedReply);
    m_display->EndItalic();
    m_display->EndTextColour();
    
    m_display->WriteText("\n");
    
    // The actual reply message on next line with tab indent
    m_display->WriteText("\t");
    m_display->BeginTextColour(m_textColor);
    WriteTextWithLinks(message);
    m_display->WriteText("\n");
    m_display->EndTextColour();
}

void MessageFormatter::AppendForwardMessage(const wxString& timestamp, const wxString& sender,
                                            const wxString& forwardFrom, const wxString& message)
{
    m_display->BeginTextColour(m_timestampColor);
    m_display->WriteText("[" + timestamp + "] ");
    m_display->EndTextColour();
    
    m_display->BeginTextColour(GetUserColor(sender));
    m_display->WriteText("<");
    m_display->BeginBold();
    m_display->WriteText(sender);
    m_display->EndBold();
    m_display->WriteText(">");
    m_display->EndTextColour();
    
    m_display->WriteText("\t");
    
    // Forward indicator with arrow
    m_display->BeginTextColour(m_forwardColor);
    m_display->BeginItalic();
    m_display->WriteText("↪ Forwarded from ");
    m_display->BeginBold();
    m_display->WriteText(forwardFrom);
    m_display->EndBold();
    m_display->EndItalic();
    m_display->EndTextColour();
    
    m_display->WriteText("\n");
    
    // The forwarded content on next line with tab indent
    m_display->WriteText("\t");
    m_display->BeginTextColour(m_textColor);
    WriteTextWithLinks(message);
    m_display->WriteText("\n");
    m_display->EndTextColour();
}

void MessageFormatter::AppendUnreadMarker()
{
    // Ensure we start on a new line
    m_display->WriteText("\n");
    
    // HexChat-style red line to mark unread messages
    m_display->BeginTextColour(wxColour(0xCC, 0x00, 0x00)); // Red
    m_display->WriteText("─────────────────────── ");
    m_display->EndTextColour();
    
    m_display->BeginTextColour(wxColour(0xFF, 0x40, 0x40)); // Lighter red for text
    m_display->BeginBold();
    m_display->WriteText("New Messages");
    m_display->EndBold();
    m_display->EndTextColour();
    
    m_display->BeginTextColour(wxColour(0xCC, 0x00, 0x00)); // Red
    m_display->WriteText(" ───────────────────────\n");
    m_display->EndTextColour();
}

void MessageFormatter::AppendEditedMessage(const wxString& timestamp, const wxString& sender,
                                           const wxString& message,
                                           long* editSpanStart, long* editSpanEnd)
{
    m_display->BeginTextColour(m_timestampColor);
    m_display->WriteText("[" + timestamp + "] ");
    m_display->EndTextColour();
    
    m_display->BeginTextColour(GetUserColor(sender));
    m_display->WriteText("<");
    m_display->BeginBold();
    m_display->WriteText(sender);
    m_display->EndBold();
    m_display->WriteText(">");
    m_display->EndTextColour();
    
    m_display->WriteText("\t");
    
    m_display->BeginTextColour(m_textColor);
    WriteTextWithLinks(message);
    m_display->EndTextColour();
    
    // Simple (edited) marker - no span tracking needed since we don't have original text
    m_display->BeginTextColour(m_editedColor);
    m_display->BeginItalic();
    m_display->WriteText(" (edited)");
    m_display->EndItalic();
    m_display->EndTextColour();
    
    m_display->WriteText("\n");
    
    // No span tracking - TDLib doesn't provide original message text
    if (editSpanStart) *editSpanStart = 0;
    if (editSpanEnd) *editSpanEnd = 0;
}

void MessageFormatter::DisplayMessage(const MessageInfo& msg, const wxString& timestamp)
{
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