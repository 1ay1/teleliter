#include "MessageFormatter.h"
#include "../telegram/Types.h"

MessageFormatter::MessageFormatter(wxRichTextCtrl* display)
    : m_display(display),
      m_lastMediaSpanStart(0),
      m_lastMediaSpanEnd(0)
{
    // Default colors
    m_timestampColor = wxColour(0x87, 0x87, 0x87);
    m_textColor = wxColour(0xD0, 0xD0, 0xD0);
    m_serviceColor = wxColour(0x00, 0xAA, 0x00);
    m_mediaColor = wxColour(0x00, 0x99, 0xCC);
    m_editedColor = wxColour(0x99, 0x99, 0x99);
    m_forwardColor = wxColour(0xCC, 0x99, 0x00);
    m_replyColor = wxColour(0x66, 0x99, 0xCC);
    
    // Default user colors
    for (int i = 0; i < 16; i++) {
        m_userColors[i] = wxColour(0xCC, 0xCC, 0xCC);
    }
}

void MessageFormatter::SetUserColors(const wxColour colors[16])
{
    for (int i = 0; i < 16; i++) {
        m_userColors[i] = colors[i];
    }
}

wxColour MessageFormatter::GetUserColor(const wxString& username)
{
    unsigned long hash = 0;
    for (size_t i = 0; i < username.length(); i++) {
        hash = static_cast<unsigned long>(username[i].GetValue()) + (hash << 6) + (hash << 16) - hash;
    }
    return m_userColors[hash % 16];
}

void MessageFormatter::AppendMessage(const wxString& timestamp, const wxString& sender,
                                      const wxString& message)
{
    m_display->BeginTextColour(m_timestampColor);
    m_display->WriteText("[" + timestamp + "] ");
    m_display->EndTextColour();
    
    m_display->BeginTextColour(GetUserColor(sender));
    m_display->WriteText("<" + sender + "> ");
    m_display->EndTextColour();
    
    m_display->BeginTextColour(m_textColor);
    m_display->WriteText(message + "\n");
    m_display->EndTextColour();
}

void MessageFormatter::AppendServiceMessage(const wxString& timestamp, const wxString& message)
{
    m_display->BeginTextColour(m_timestampColor);
    m_display->WriteText("[" + timestamp + "] ");
    m_display->EndTextColour();
    
    m_display->BeginTextColour(m_serviceColor);
    m_display->WriteText("* " + message + "\n");
    m_display->EndTextColour();
}

void MessageFormatter::AppendJoinMessage(const wxString& timestamp, const wxString& user)
{
    m_display->BeginTextColour(m_timestampColor);
    m_display->WriteText("[" + timestamp + "] ");
    m_display->EndTextColour();
    
    m_display->BeginTextColour(m_serviceColor);
    m_display->WriteText("--> " + user + " joined the group\n");
    m_display->EndTextColour();
}

void MessageFormatter::AppendLeaveMessage(const wxString& timestamp, const wxString& user)
{
    m_display->BeginTextColour(m_timestampColor);
    m_display->WriteText("[" + timestamp + "] ");
    m_display->EndTextColour();
    
    m_display->BeginTextColour(m_serviceColor);
    m_display->WriteText("<-- " + user + " left the group\n");
    m_display->EndTextColour();
}

void MessageFormatter::AppendMediaMessage(const wxString& timestamp, const wxString& sender,
                                          const MediaInfo& media, const wxString& caption)
{
    m_display->BeginTextColour(m_timestampColor);
    m_display->WriteText("[" + timestamp + "] ");
    m_display->EndTextColour();
    
    m_display->BeginTextColour(GetUserColor(sender));
    m_display->WriteText("<" + sender + "> ");
    m_display->EndTextColour();
    
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
            mediaLabel = "[Sticker " + media.emoji + "]";
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
    
    m_lastMediaSpanEnd = m_display->GetLastPosition();
    
    if (!caption.IsEmpty()) {
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
    m_display->WriteText("<" + sender + "> ");
    m_display->EndTextColour();
    
    m_display->BeginTextColour(m_replyColor);
    m_display->WriteText("[> " + replyTo + "] ");
    m_display->EndTextColour();
    
    m_display->BeginTextColour(m_textColor);
    m_display->WriteText(message + "\n");
    m_display->EndTextColour();
}

void MessageFormatter::AppendForwardMessage(const wxString& timestamp, const wxString& sender,
                                            const wxString& forwardFrom, const wxString& message)
{
    m_display->BeginTextColour(m_timestampColor);
    m_display->WriteText("[" + timestamp + "] ");
    m_display->EndTextColour();
    
    m_display->BeginTextColour(GetUserColor(sender));
    m_display->WriteText("<" + sender + "> ");
    m_display->EndTextColour();
    
    m_display->BeginTextColour(m_forwardColor);
    m_display->WriteText("[Fwd: " + forwardFrom + "] ");
    m_display->EndTextColour();
    
    m_display->BeginTextColour(m_textColor);
    m_display->WriteText(message + "\n");
    m_display->EndTextColour();
}

void MessageFormatter::AppendEditedMessage(const wxString& timestamp, const wxString& sender,
                                           const wxString& message)
{
    m_display->BeginTextColour(m_timestampColor);
    m_display->WriteText("[" + timestamp + "] ");
    m_display->EndTextColour();
    
    m_display->BeginTextColour(GetUserColor(sender));
    m_display->WriteText("<" + sender + "> ");
    m_display->EndTextColour();
    
    m_display->BeginTextColour(m_textColor);
    m_display->WriteText(message + " ");
    m_display->EndTextColour();
    
    m_display->BeginTextColour(m_editedColor);
    m_display->WriteText("(edited)\n");
    m_display->EndTextColour();
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