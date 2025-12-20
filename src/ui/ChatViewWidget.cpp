#include "ChatViewWidget.h"
#include "MainFrame.h"
#include <iostream>
#include "MessageFormatter.h"
#include "MediaPopup.h"
#include "FileDropTarget.h"
#include "../telegram/Types.h"
#include "../telegram/TelegramClient.h"
#include <wx/filename.h>
#include <ctime>

ChatViewWidget::ChatViewWidget(wxWindow* parent, MainFrame* mainFrame)
    : wxPanel(parent, wxID_ANY),
      m_mainFrame(mainFrame),
      m_chatDisplay(nullptr),
      m_messageFormatter(nullptr),
      m_mediaPopup(nullptr),
      m_editHistoryPopup(nullptr),
      m_hoverTimer(this),
      m_lastShownMediaId(0),
      m_lastHoveredTextPos(-1),
      m_bgColor(0x2B, 0x2B, 0x2B),
      m_fgColor(0xD3, 0xD7, 0xCF),
      m_timestampColor(0x88, 0x88, 0x88),
      m_textColor(0xD3, 0xD7, 0xCF),
      m_serviceColor(0x88, 0x88, 0x88),
      m_actionColor(0xCE, 0x5C, 0x00),
      m_mediaColor(0x72, 0x9F, 0xCF),
      m_editedColor(0x88, 0x88, 0x88),
      m_forwardColor(0xAD, 0x7F, 0xA8),
      m_replyColor(0x72, 0x9F, 0xCF),
      m_highlightColor(0xFC, 0xAF, 0x3E),
      m_noticeColor(0xAD, 0x7F, 0xA8)
{
    // Bind hover timer event
    Bind(wxEVT_TIMER, &ChatViewWidget::OnHoverTimer, this, m_hoverTimer.GetId());
    // Initialize default user colors
    m_userColors[0]  = wxColour(0xCC, 0xCC, 0xCC);
    m_userColors[1]  = wxColour(0x35, 0x36, 0xB2);
    m_userColors[2]  = wxColour(0x2A, 0x8C, 0x2A);
    m_userColors[3]  = wxColour(0xC3, 0x38, 0x38);
    m_userColors[4]  = wxColour(0xC7, 0x38, 0x38);
    m_userColors[5]  = wxColour(0x80, 0x00, 0x80);
    m_userColors[6]  = wxColour(0xFF, 0x80, 0x00);
    m_userColors[7]  = wxColour(0x80, 0x80, 0x00);
    m_userColors[8]  = wxColour(0x33, 0xCC, 0x33);
    m_userColors[9]  = wxColour(0x00, 0x80, 0x80);
    m_userColors[10] = wxColour(0x33, 0xCC, 0xCC);
    m_userColors[11] = wxColour(0x66, 0x66, 0xFF);
    m_userColors[12] = wxColour(0xFF, 0x00, 0xFF);
    m_userColors[13] = wxColour(0x80, 0x80, 0x80);
    m_userColors[14] = wxColour(0xCC, 0xCC, 0xCC);
    m_userColors[15] = wxColour(0x72, 0x9F, 0xCF);
    
    CreateLayout();
    SetupDisplayControl();
    
    // Create media popup (hidden initially)
    m_mediaPopup = new MediaPopup(this);
    
    // Create edit history popup (hidden initially)
    m_editHistoryPopup = nullptr;  // Created on demand
}

ChatViewWidget::~ChatViewWidget()
{
    delete m_messageFormatter;
    m_messageFormatter = nullptr;
}

void ChatViewWidget::CreateLayout()
{
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    
    // Rich text control for chat display
    m_chatDisplay = new wxRichTextCtrl(this, wxID_ANY,
        wxEmptyString, wxDefaultPosition, wxDefaultSize,
        wxRE_MULTILINE | wxRE_READONLY | wxBORDER_NONE | wxVSCROLL);
    
    sizer->Add(m_chatDisplay, 1, wxEXPAND);
    SetSizer(sizer);
}

void ChatViewWidget::SetupDisplayControl()
{
    m_chatDisplay->SetBackgroundColour(m_bgColor);
    if (m_font.IsOk()) {
        m_chatDisplay->SetFont(m_font);
    }
    
    wxRichTextAttr defaultStyle;
    defaultStyle.SetTextColour(m_textColor);
    defaultStyle.SetBackgroundColour(m_bgColor);
    defaultStyle.SetFont(m_font);
    m_chatDisplay->SetDefaultStyle(defaultStyle);
    m_chatDisplay->SetBasicStyle(defaultStyle);
    
    // Bind mouse events for hover detection
    m_chatDisplay->Bind(wxEVT_MOTION, &ChatViewWidget::OnMouseMove, this);
    m_chatDisplay->Bind(wxEVT_LEAVE_WINDOW, &ChatViewWidget::OnMouseLeave, this);
    m_chatDisplay->Bind(wxEVT_LEFT_DOWN, &ChatViewWidget::OnLeftDown, this);
    
    // Set up drag and drop for file uploads
    if (m_mainFrame) {
        FileDropTarget* dropTarget = new FileDropTarget(m_mainFrame, 
            [this](const wxArrayString& files) {
                if (m_mainFrame) {
                    m_mainFrame->OnFilesDropped(files);
                }
            });
        m_chatDisplay->SetDropTarget(dropTarget);
    }
    
    // Create message formatter for the chat display (HexChat-style)
    m_messageFormatter = new MessageFormatter(m_chatDisplay);
    m_messageFormatter->SetTimestampColor(m_timestampColor);
    m_messageFormatter->SetTextColor(m_textColor);
    m_messageFormatter->SetServiceColor(m_serviceColor);
    m_messageFormatter->SetActionColor(m_actionColor);
    m_messageFormatter->SetMediaColor(m_mediaColor);
    m_messageFormatter->SetEditedColor(m_editedColor);
    m_messageFormatter->SetForwardColor(m_forwardColor);
    m_messageFormatter->SetReplyColor(m_replyColor);
    m_messageFormatter->SetHighlightColor(m_highlightColor);
    m_messageFormatter->SetNoticeColor(m_noticeColor);
    m_messageFormatter->SetLinkColor(wxColour(0x72, 0x9F, 0xCF));  // Blue for links
    m_messageFormatter->SetUserColors(m_userColors);
    
    // Set up link span callback
    m_messageFormatter->SetLinkSpanCallback(
        [this](long startPos, long endPos, const wxString& url) {
            AddLinkSpan(startPos, endPos, url);
        });
}

void ChatViewWidget::DisplayMessage(const MessageInfo& msg)
{
    if (!m_messageFormatter) return;
    
    wxString timestamp = FormatTimestamp(msg.date);
    wxString sender = msg.senderName.IsEmpty() ? "Unknown" : msg.senderName;
    
    // Handle forwarded messages
    if (msg.isForwarded && !msg.forwardedFrom.IsEmpty()) {
        m_messageFormatter->AppendForwardMessage(timestamp, sender, 
            msg.forwardedFrom, msg.text);
        return;
    }
    
    // Handle reply messages
    if (msg.replyToMessageId != 0 && !msg.replyToText.IsEmpty()) {
        m_messageFormatter->AppendReplyMessage(timestamp, sender, 
            msg.replyToText, msg.text);
        return;
    }
    
    // Handle media messages
    if (msg.hasPhoto) {
        MediaInfo info;
        info.type = MediaType::Photo;
        info.fileId = msg.mediaFileId;
        info.localPath = msg.mediaLocalPath;
        info.caption = msg.mediaCaption;
        
        long startPos = m_chatDisplay->GetLastPosition();
        m_messageFormatter->AppendMediaMessage(timestamp, sender, info, msg.mediaCaption);
        long endPos = m_chatDisplay->GetLastPosition();
        AddMediaSpan(startPos, endPos, info);
        return;
    }
    
    if (msg.hasVideo) {
        MediaInfo info;
        info.type = MediaType::Video;
        info.fileId = msg.mediaFileId;
        info.localPath = msg.mediaLocalPath;
        info.fileName = msg.mediaFileName;
        info.caption = msg.mediaCaption;
        
        long startPos = m_chatDisplay->GetLastPosition();
        m_messageFormatter->AppendMediaMessage(timestamp, sender, info, msg.mediaCaption);
        long endPos = m_chatDisplay->GetLastPosition();
        AddMediaSpan(startPos, endPos, info);
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
        
        long startPos = m_chatDisplay->GetLastPosition();
        m_messageFormatter->AppendMediaMessage(timestamp, sender, info, msg.mediaCaption);
        long endPos = m_chatDisplay->GetLastPosition();
        AddMediaSpan(startPos, endPos, info);
        return;
    }
    
    if (msg.hasVoice) {
        MediaInfo info;
        info.type = MediaType::Voice;
        info.fileId = msg.mediaFileId;
        info.localPath = msg.mediaLocalPath;
        
        long startPos = m_chatDisplay->GetLastPosition();
        m_messageFormatter->AppendMediaMessage(timestamp, sender, info, "");
        long endPos = m_chatDisplay->GetLastPosition();
        AddMediaSpan(startPos, endPos, info);
        return;
    }
    
    if (msg.hasVideoNote) {
        MediaInfo info;
        info.type = MediaType::VideoNote;
        info.fileId = msg.mediaFileId;
        info.localPath = msg.mediaLocalPath;
        
        long startPos = m_chatDisplay->GetLastPosition();
        m_messageFormatter->AppendMediaMessage(timestamp, sender, info, "");
        long endPos = m_chatDisplay->GetLastPosition();
        AddMediaSpan(startPos, endPos, info);
        return;
    }
    
    if (msg.hasSticker) {
        std::cerr << "[Sticker] hasSticker=true" << std::endl;
        std::cerr << "[Sticker]   mediaFileId=" << msg.mediaFileId << std::endl;
        std::cerr << "[Sticker]   mediaCaption(emoji)=" << msg.mediaCaption.ToStdString() << std::endl;
        std::cerr << "[Sticker]   mediaLocalPath=" << msg.mediaLocalPath.ToStdString() << std::endl;
        std::cerr << "[Sticker]   text=" << msg.text.ToStdString() << std::endl;
        
        MediaInfo info;
        info.type = MediaType::Sticker;
        info.fileId = msg.mediaFileId;
        info.localPath = msg.mediaLocalPath;
        info.emoji = msg.mediaCaption;  // Sticker emoji is stored in mediaCaption
        
        long startPos = m_chatDisplay->GetLastPosition();
        m_messageFormatter->AppendMediaMessage(timestamp, sender, info, "");
        long endPos = m_chatDisplay->GetLastPosition();
        AddMediaSpan(startPos, endPos, info);
        std::cerr << "[Sticker]   Added media span from " << startPos << " to " << endPos << std::endl;
        return;
    }
    
    if (msg.hasAnimation) {
        MediaInfo info;
        info.type = MediaType::GIF;
        info.fileId = msg.mediaFileId;
        info.localPath = msg.mediaLocalPath;
        
        long startPos = m_chatDisplay->GetLastPosition();
        m_messageFormatter->AppendMediaMessage(timestamp, sender, info, msg.mediaCaption);
        long endPos = m_chatDisplay->GetLastPosition();
        AddMediaSpan(startPos, endPos, info);
        return;
    }
    
    // Check for action messages (/me)
    if (msg.text.StartsWith("/me ")) {
        wxString action = msg.text.Mid(4);
        m_messageFormatter->AppendActionMessage(timestamp, sender, action);
        return;
    }
    
    // Handle edited messages with span tracking
    if (msg.isEdited) {
        long editSpanStart = 0, editSpanEnd = 0;
        m_messageFormatter->AppendEditedMessage(timestamp, sender, msg.text, 
                                                 &editSpanStart, &editSpanEnd);
        // Add edit span for hover popup
        AddEditSpan(editSpanStart, editSpanEnd, msg.id, msg.originalText, msg.editDate);
        return;
    }
    
    // Regular text message
    m_messageFormatter->AppendMessage(timestamp, sender, msg.text);
}

void ChatViewWidget::DisplayMessages(const std::vector<MessageInfo>& messages)
{
    for (const auto& msg : messages) {
        DisplayMessage(msg);
    }
    ScrollToBottom();
}

void ChatViewWidget::ClearMessages()
{
    if (m_chatDisplay) {
        m_chatDisplay->Clear();
    }
    ClearMediaSpans();
    ClearEditSpans();
    ClearLinkSpans();
}

void ChatViewWidget::ScrollToBottom()
{
    if (m_chatDisplay) {
        m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
        m_chatDisplay->Refresh();
        m_chatDisplay->Update();
    }
}

void ChatViewWidget::AddMediaSpan(long startPos, long endPos, const MediaInfo& info)
{
    MediaSpan span;
    span.startPos = startPos;
    span.endPos = endPos;
    span.info = info;
    m_mediaSpans.push_back(span);
}

MediaSpan* ChatViewWidget::GetMediaSpanAtPosition(long pos)
{
    for (auto& span : m_mediaSpans) {
        if (pos >= span.startPos && pos < span.endPos) {
            return &span;
        }
    }
    return nullptr;
}

void ChatViewWidget::ClearMediaSpans()
{
    m_mediaSpans.clear();
}

void ChatViewWidget::AddEditSpan(long startPos, long endPos, int64_t messageId,
                                  const wxString& originalText, int64_t editDate)
{
    EditSpan span;
    span.startPos = startPos;
    span.endPos = endPos;
    span.messageId = messageId;
    span.originalText = originalText;
    span.editDate = editDate;
    m_editSpans.push_back(span);
}

EditSpan* ChatViewWidget::GetEditSpanAtPosition(long pos)
{
    for (auto& span : m_editSpans) {
        if (pos >= span.startPos && pos < span.endPos) {
            return &span;
        }
    }
    return nullptr;
}

void ChatViewWidget::ClearEditSpans()
{
    m_editSpans.clear();
}

void ChatViewWidget::AddLinkSpan(long startPos, long endPos, const wxString& url)
{
    LinkSpan span;
    span.startPos = startPos;
    span.endPos = endPos;
    span.url = url;
    m_linkSpans.push_back(span);
}

LinkSpan* ChatViewWidget::GetLinkSpanAtPosition(long pos)
{
    for (auto& span : m_linkSpans) {
        if (span.Contains(pos)) {
            return &span;
        }
    }
    return nullptr;
}

void ChatViewWidget::ClearLinkSpans()
{
    m_linkSpans.clear();
}

void ChatViewWidget::ShowEditHistoryPopup(const EditSpan& span, const wxPoint& position)
{
    // Create popup on demand
    if (!m_editHistoryPopup) {
        m_editHistoryPopup = new wxPopupWindow(this, wxBORDER_SIMPLE);
    }
    
    // Create content panel
    m_editHistoryPopup->DestroyChildren();
    
    wxPanel* panel = new wxPanel(m_editHistoryPopup);
    panel->SetBackgroundColour(wxColour(0x1A, 0x1A, 0x1A));
    
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    
    // Header
    wxStaticText* header = new wxStaticText(panel, wxID_ANY, "Original message:");
    header->SetForegroundColour(wxColour(0x88, 0x88, 0x88));
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
    
    wxStaticText* textLabel = new wxStaticText(panel, wxID_ANY, originalText);
    textLabel->SetForegroundColour(wxColour(0xD3, 0xD7, 0xCF));
    sizer->Add(textLabel, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    
    // Edit time
    if (span.editDate > 0) {
        time_t t = static_cast<time_t>(span.editDate);
        wxDateTime dt(t);
        wxString editTimeStr = "Edited: " + dt.Format("%Y-%m-%d %H:%M:%S");
        wxStaticText* timeLabel = new wxStaticText(panel, wxID_ANY, editTimeStr);
        timeLabel->SetForegroundColour(wxColour(0x66, 0x66, 0x66));
        timeLabel->SetFont(timeLabel->GetFont().Smaller());
        sizer->Add(timeLabel, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    }
    
    panel->SetSizer(sizer);
    sizer->Fit(panel);
    
    wxBoxSizer* popupSizer = new wxBoxSizer(wxVERTICAL);
    popupSizer->Add(panel, 1, wxEXPAND);
    m_editHistoryPopup->SetSizer(popupSizer);
    popupSizer->Fit(m_editHistoryPopup);
    
    // Position popup near cursor
    m_editHistoryPopup->SetPosition(position);
    m_editHistoryPopup->Show();
}

void ChatViewWidget::HideEditHistoryPopup()
{
    if (m_editHistoryPopup && m_editHistoryPopup->IsShown()) {
        m_editHistoryPopup->Hide();
    }
}

void ChatViewWidget::SetColors(const wxColour& bg, const wxColour& fg,
                               const wxColour& timestamp, const wxColour& text,
                               const wxColour& service, const wxColour& action,
                               const wxColour& media, const wxColour& edited,
                               const wxColour& forward, const wxColour& reply,
                               const wxColour& highlight, const wxColour& notice)
{
    m_bgColor = bg;
    m_fgColor = fg;
    m_timestampColor = timestamp;
    m_textColor = text;
    m_serviceColor = service;
    m_actionColor = action;
    m_mediaColor = media;
    m_editedColor = edited;
    m_forwardColor = forward;
    m_replyColor = reply;
    m_highlightColor = highlight;
    m_noticeColor = notice;
    
    if (m_chatDisplay) {
        m_chatDisplay->SetBackgroundColour(m_bgColor);
        
        wxRichTextAttr defaultStyle;
        defaultStyle.SetTextColour(m_textColor);
        defaultStyle.SetBackgroundColour(m_bgColor);
        m_chatDisplay->SetDefaultStyle(defaultStyle);
        m_chatDisplay->SetBasicStyle(defaultStyle);
        m_chatDisplay->Refresh();
    }
    
    if (m_messageFormatter) {
        m_messageFormatter->SetTimestampColor(m_timestampColor);
        m_messageFormatter->SetTextColor(m_textColor);
        m_messageFormatter->SetServiceColor(m_serviceColor);
        m_messageFormatter->SetActionColor(m_actionColor);
        m_messageFormatter->SetMediaColor(m_mediaColor);
        m_messageFormatter->SetEditedColor(m_editedColor);
        m_messageFormatter->SetForwardColor(m_forwardColor);
        m_messageFormatter->SetReplyColor(m_replyColor);
        m_messageFormatter->SetHighlightColor(m_highlightColor);
        m_messageFormatter->SetNoticeColor(m_noticeColor);
    }
}

void ChatViewWidget::SetUserColors(const wxColour* colors)
{
    for (int i = 0; i < 16; i++) {
        m_userColors[i] = colors[i];
    }
    
    if (m_messageFormatter) {
        m_messageFormatter->SetUserColors(m_userColors);
    }
}

void ChatViewWidget::SetChatFont(const wxFont& font)
{
    m_font = font;
    
    if (m_chatDisplay) {
        m_chatDisplay->SetFont(m_font);
        
        wxRichTextAttr defaultStyle;
        defaultStyle.SetFont(m_font);
        defaultStyle.SetTextColour(m_textColor);
        defaultStyle.SetBackgroundColour(m_bgColor);
        m_chatDisplay->SetDefaultStyle(defaultStyle);
        m_chatDisplay->SetBasicStyle(defaultStyle);
        m_chatDisplay->Refresh();
    }
}

void ChatViewWidget::AddPendingDownload(int32_t fileId, const MediaInfo& info)
{
    m_pendingDownloads[fileId] = info;
}

bool ChatViewWidget::HasPendingDownload(int32_t fileId) const
{
    return m_pendingDownloads.find(fileId) != m_pendingDownloads.end();
}

MediaInfo ChatViewWidget::GetPendingDownload(int32_t fileId) const
{
    auto it = m_pendingDownloads.find(fileId);
    if (it != m_pendingDownloads.end()) {
        return it->second;
    }
    return MediaInfo();
}

void ChatViewWidget::RemovePendingDownload(int32_t fileId)
{
    m_pendingDownloads.erase(fileId);
}

void ChatViewWidget::ShowMediaPopup(const MediaInfo& info, const wxPoint& position)
{
    if (!m_mediaPopup) return;
    
    // Don't re-show if already showing the same media (prevents flickering and video restart)
    if (m_mediaPopup->IsShown()) {
        const MediaInfo& currentInfo = m_mediaPopup->GetMediaInfo();
        if (currentInfo.fileId == info.fileId && currentInfo.fileId != 0 && currentInfo.type == info.type) {
            // Same media already showing, just update position if needed
            return;
        }
    }
    
    // For videos/GIFs, we need to check if the actual video is downloaded
    // (not just the thumbnail). If only thumbnail exists, trigger video download.
    bool needsVideoDownload = false;
    if (info.type == MediaType::Video || info.type == MediaType::GIF || 
        info.type == MediaType::VideoNote) {
        // Check if localPath is a video file or just a thumbnail (image)
        if (!info.localPath.IsEmpty() && wxFileExists(info.localPath)) {
            wxFileName fn(info.localPath);
            wxString ext = fn.GetExt().Lower();
            bool isVideoFile = (ext == "mp4" || ext == "webm" || ext == "avi" || 
                                ext == "mov" || ext == "mkv" || ext == "gif" ||
                                ext == "m4v" || ext == "ogv");
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
    if (info.localPath.IsEmpty() || !wxFileExists(info.localPath) || needsVideoDownload) {
        if (info.fileId != 0 && m_mainFrame) {
            TelegramClient* client = m_mainFrame->GetTelegramClient();
            if (client) {
                std::cerr << "[ChatViewWidget] Starting download for fileId=" << info.fileId << std::endl;
                // Start download with high priority for preview
                client->DownloadFile(info.fileId, 10);
                // Track this as a pending download so we can update popup when complete
                AddPendingDownload(info.fileId, info);
            }
        }
    }
    
    // Show popup - ShowMedia handles all cases:
    // - If video file exists and is playable, it will play
    // - If only thumbnail exists, it will show thumbnail
    // - If nothing exists, it will show placeholder/loading
    m_mediaPopup->ShowMedia(info, position);
}

void ChatViewWidget::HideMediaPopup()
{
    // Clear tracking state
    m_lastShownMediaId = 0;
    
    if (m_mediaPopup) {
        // Stop video playback when hiding
        m_mediaPopup->StopVideo();
        m_mediaPopup->Hide();
    }
}

void ChatViewWidget::UpdateMediaPopup(int32_t fileId, const wxString& localPath)
{
    std::cerr << "[ChatViewWidget] UpdateMediaPopup called: fileId=" << fileId << " path=" << localPath.ToStdString() << std::endl;
    
    if (!m_mediaPopup || !m_mediaPopup->IsShown()) {
        std::cerr << "[ChatViewWidget] UpdateMediaPopup: popup not shown, skipping" << std::endl;
        return;
    }
    
    // Check if this file ID matches the current popup's media
    const MediaInfo& currentInfo = m_mediaPopup->GetMediaInfo();
    std::cerr << "[ChatViewWidget] UpdateMediaPopup: popup fileId=" << currentInfo.fileId << " comparing with " << fileId << std::endl;
    
    if (currentInfo.fileId == fileId) {
        // Check if this is a video/GIF that should be played
        wxFileName fn(localPath);
        wxString ext = fn.GetExt().Lower();
        bool isVideo = (ext == "mp4" || ext == "webm" || ext == "avi" || 
                        ext == "mov" || ext == "mkv" || ext == "gif" ||
                        ext == "m4v" || ext == "ogv");
        
        if (isVideo && (currentInfo.type == MediaType::Video || 
                        currentInfo.type == MediaType::GIF ||
                        currentInfo.type == MediaType::VideoNote)) {
            // Play video/GIF
            bool shouldLoop = (currentInfo.type == MediaType::GIF);
            bool shouldMute = (currentInfo.type == MediaType::GIF || currentInfo.type == MediaType::VideoNote);
            m_mediaPopup->PlayVideo(localPath, shouldLoop, shouldMute);
        } else {
            // Update the popup with the downloaded image
            m_mediaPopup->SetImage(localPath);
        }
    }
}

void ChatViewWidget::OpenMedia(const MediaInfo& info)
{
    if (!info.localPath.IsEmpty() && wxFileExists(info.localPath)) {
        // Open with default application
        wxLaunchDefaultApplication(info.localPath);
    } else if (info.fileId != 0 && m_mainFrame) {
        // Need to download first
        TelegramClient* client = m_mainFrame->GetTelegramClient();
        if (client) {
            AddPendingDownload(info.fileId, info);
            client->DownloadFile(info.fileId);
        }
    }
}

wxString ChatViewWidget::FormatTimestamp(int64_t unixTime)
{
    if (unixTime <= 0) {
        return wxDateTime::Now().Format("%H:%M");
    }
    
    time_t t = static_cast<time_t>(unixTime);
    wxDateTime dt(t);
    return dt.Format("%H:%M");
}

void ChatViewWidget::OnMouseMove(wxMouseEvent& event)
{
    wxPoint pos = event.GetPosition();
    long textPos;
    
    wxRichTextCtrl* ctrl = m_chatDisplay;
    if (!ctrl) {
        event.Skip();
        return;
    }
    
    // Get text position from screen coordinates
    wxTextCtrlHitTestResult hit = ctrl->HitTest(pos, &textPos);
    
    if (hit == wxTE_HT_ON_TEXT || hit == wxTE_HT_BEFORE || hit == wxTE_HT_BELOW) {
        MediaSpan* span = GetMediaSpanAtPosition(textPos);
        if (span) {
            // Change cursor to hand
            ctrl->SetCursor(wxCursor(wxCURSOR_HAND));
            
            // Show preview for all visual media types
            if (span->info.type == MediaType::Photo || 
                span->info.type == MediaType::Sticker ||
                span->info.type == MediaType::GIF ||
                span->info.type == MediaType::Video ||
                span->info.type == MediaType::VideoNote) {
                
                // Check if we're still hovering the same media
                if (m_lastShownMediaId == span->info.fileId && m_mediaPopup && m_mediaPopup->IsShown()) {
                    // Already showing this media, just update position slightly if needed
                    event.Skip();
                    return;
                }
                
                // Check if we're hovering a new media span
                if (m_lastHoveredTextPos != textPos || m_pendingHoverMedia.fileId != span->info.fileId) {
                    // New media span - start debounce timer
                    m_hoverTimer.Stop();
                    m_pendingHoverMedia = span->info;
                    m_pendingHoverPos = ctrl->ClientToScreen(pos);
                    m_lastHoveredTextPos = textPos;
                    m_hoverTimer.StartOnce(HOVER_DELAY_MS);
                }
            }
        } else {
            // Not over a media span - cancel pending hover and hide popup
            m_hoverTimer.Stop();
            m_lastHoveredTextPos = -1;
            m_pendingHoverMedia = MediaInfo();
            
            // Check for link span
            LinkSpan* linkSpan = GetLinkSpanAtPosition(textPos);
            if (linkSpan) {
                ctrl->SetCursor(wxCursor(wxCURSOR_HAND));
                HideMediaPopup();
                HideEditHistoryPopup();
            } else {
                // Check for edit span
                EditSpan* editSpan = GetEditSpanAtPosition(textPos);
                if (editSpan) {
                    ctrl->SetCursor(wxCursor(wxCURSOR_HAND));
                    // Show edit history popup
                    ShowEditHistoryPopup(*editSpan, ctrl->ClientToScreen(pos));
                } else {
                    ctrl->SetCursor(wxCursor(wxCURSOR_IBEAM));
                    HideMediaPopup();
                    HideEditHistoryPopup();
                }
            }
        }
    } else {
        // Not over text - cancel everything
        m_hoverTimer.Stop();
        m_lastHoveredTextPos = -1;
        m_pendingHoverMedia = MediaInfo();
        ctrl->SetCursor(wxCursor(wxCURSOR_DEFAULT));
        HideMediaPopup();
    }
    
    event.Skip();
}

void ChatViewWidget::OnHoverTimer(wxTimerEvent& event)
{
    // Timer fired - show the popup if we still have pending media
    if (m_pendingHoverMedia.fileId != 0 || !m_pendingHoverMedia.localPath.IsEmpty()) {
        m_lastShownMediaId = m_pendingHoverMedia.fileId;
        ShowMediaPopup(m_pendingHoverMedia, m_pendingHoverPos);
    }
}

void ChatViewWidget::OnMouseLeave(wxMouseEvent& event)
{
    // Cancel any pending hover
    m_hoverTimer.Stop();
    m_lastHoveredTextPos = -1;
    m_lastShownMediaId = 0;
    m_pendingHoverMedia = MediaInfo();
    
    HideMediaPopup();
    HideEditHistoryPopup();
    if (m_chatDisplay) {
        m_chatDisplay->SetCursor(wxCursor(wxCURSOR_IBEAM));
    }
    event.Skip();
}

void ChatViewWidget::OnLeftDown(wxMouseEvent& event)
{
    wxPoint pos = event.GetPosition();
    long textPos;
    
    wxRichTextCtrl* ctrl = m_chatDisplay;
    if (!ctrl) {
        event.Skip();
        return;
    }
    
    wxTextCtrlHitTestResult hit = ctrl->HitTest(pos, &textPos);
    
    if (hit == wxTE_HT_ON_TEXT || hit == wxTE_HT_BEFORE) {
        MediaSpan* span = GetMediaSpanAtPosition(textPos);
        if (span) {
            OpenMedia(span->info);
            return; // Don't skip - we handled the click
        }
        
        // Check for link click
        LinkSpan* linkSpan = GetLinkSpanAtPosition(textPos);
        if (linkSpan) {
            // Open URL in default browser
            wxLaunchDefaultBrowser(linkSpan->url);
            return; // Don't skip - we handled the click
        }
    }
    
    event.Skip();
}