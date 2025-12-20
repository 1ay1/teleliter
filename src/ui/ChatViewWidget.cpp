#include "ChatViewWidget.h"
#include "MainFrame.h"
#include "MessageFormatter.h"
#include "MediaPopup.h"
#include <iostream>

#define CVWLOG(msg) std::cerr << "[ChatViewWidget] " << msg << std::endl
// #define CVWLOG(msg) do {} while(0)
#include "FileDropTarget.h"
#include "../telegram/Types.h"
#include "../telegram/TelegramClient.h"
#include <wx/filename.h>
#include <wx/clipbrd.h>
#include <wx/stattext.h>
#include <ctime>

ChatViewWidget::ChatViewWidget(wxWindow* parent, MainFrame* mainFrame)
    : wxPanel(parent, wxID_ANY),
      m_mainFrame(mainFrame),
      m_chatArea(nullptr),
      m_messageFormatter(nullptr),
      m_mediaPopup(nullptr),
      m_editHistoryPopup(nullptr),
      m_newMessageButton(nullptr),
      m_topicBar(nullptr),
      m_topicText(nullptr),
      m_downloadBar(nullptr),
      m_downloadLabel(nullptr),
      m_downloadGauge(nullptr),
      m_downloadHideTimer(this),
      m_hoverTimer(this),
      m_hideTimer(this),
      m_lastHoveredTextPos(-1),
      m_isOverMediaSpan(false),
      m_wasAtBottom(true),
      m_newMessageCount(0),
      m_isLoading(false),
      m_isReloading(false),
      m_batchUpdateDepth(0),
      m_lastDisplayedTimestamp(0),
      m_lastDisplayedMessageId(0),
      m_contextMenuPos(-1)
{
    // Bind timer events
    Bind(wxEVT_TIMER, &ChatViewWidget::OnHoverTimer, this, m_hoverTimer.GetId());
    Bind(wxEVT_TIMER, &ChatViewWidget::OnHideTimer, this, m_hideTimer.GetId());
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { HideDownloadProgress(); }, m_downloadHideTimer.GetId());

    // Bind size event for repositioning the new message button
    Bind(wxEVT_SIZE, &ChatViewWidget::OnSize, this);

    CreateLayout();
    SetupDisplayControl();

    // Create media popup (hidden initially)
    m_mediaPopup = new MediaPopup(this);
    
    // Set click callback to open media when popup is clicked
    m_mediaPopup->SetClickCallback([this](const MediaInfo& info) {
        OpenMedia(info);
        HideMediaPopup();
    });

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

    // Topic bar at top (HexChat-style) - shows chat name and info
    m_topicBar = new wxPanel(this, wxID_ANY);
    m_topicBar->SetBackgroundColour(wxColour(0x3C, 0x3C, 0x3C));  // Slightly lighter than chat bg

    wxBoxSizer* topicSizer = new wxBoxSizer(wxHORIZONTAL);

    m_topicText = new wxStaticText(m_topicBar, wxID_ANY, "");
    m_topicText->SetForegroundColour(wxColour(0xD3, 0xD7, 0xCF));
    m_topicText->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));

    topicSizer->Add(m_topicText, 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 8);
    m_topicBar->SetSizer(topicSizer);
    m_topicBar->SetMinSize(wxSize(-1, 28));
    m_topicBar->Hide();  // Hidden until a chat is selected

    sizer->Add(m_topicBar, 0, wxEXPAND);

    // Download progress is now shown in status bar, not here
    m_downloadBar = nullptr;
    m_downloadLabel = nullptr;
    m_downloadGauge = nullptr;

    // ChatArea for display - uses same formatting as WelcomeChat
    m_chatArea = new ChatArea(this);
    sizer->Add(m_chatArea, 1, wxEXPAND);
    
    SetSizer(sizer);

    // Create the "New Messages" button (hidden initially)
    CreateNewMessageButton();
}

void ChatViewWidget::SetTopicText(const wxString& chatName, const wxString& info)
{
    if (!m_topicBar || !m_topicText) return;

    wxString topic;
    if (!chatName.IsEmpty()) {
        topic = chatName;
        if (!info.IsEmpty()) {
            topic += "  -  " + info;
        }
        m_topicText->SetLabel(topic);
        m_topicBar->Show();
    } else {
        m_topicBar->Hide();
    }
    Layout();
}

void ChatViewWidget::ClearTopicText()
{
    if (m_topicBar) {
        m_topicBar->Hide();
    }
    if (m_topicText) {
        m_topicText->SetLabel("");
    }
    Layout();
}

void ChatViewWidget::CreateNewMessageButton()
{
    m_newMessageButton = new wxButton(this, ID_NEW_MESSAGE_BUTTON,
        "v New Messages",
        wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);

    m_newMessageButton->SetBackgroundColour(wxColour(0x72, 0x9F, 0xCF));
    m_newMessageButton->SetForegroundColour(wxColour(0xFF, 0xFF, 0xFF));
    m_newMessageButton->SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    m_newMessageButton->Hide();

    Bind(wxEVT_BUTTON, &ChatViewWidget::OnNewMessageButtonClick, this, ID_NEW_MESSAGE_BUTTON);
}

void ChatViewWidget::SetupDisplayControl()
{
    wxRichTextCtrl* display = m_chatArea->GetDisplay();
    if (!display) return;

    // Bind mouse events for hover detection and context menu
    display->Bind(wxEVT_MOTION, &ChatViewWidget::OnMouseMove, this);
    display->Bind(wxEVT_LEAVE_WINDOW, &ChatViewWidget::OnMouseLeave, this);
    display->Bind(wxEVT_LEFT_DOWN, &ChatViewWidget::OnLeftDown, this);
    display->Bind(wxEVT_RIGHT_DOWN, &ChatViewWidget::OnRightDown, this);
    display->Bind(wxEVT_KEY_DOWN, &ChatViewWidget::OnKeyDown, this);
    display->Bind(wxEVT_SCROLLWIN_THUMBRELEASE, &ChatViewWidget::OnScroll, this);
    display->Bind(wxEVT_SCROLLWIN_LINEDOWN, &ChatViewWidget::OnScroll, this);
    display->Bind(wxEVT_SCROLLWIN_LINEUP, &ChatViewWidget::OnScroll, this);
    display->Bind(wxEVT_SCROLLWIN_PAGEDOWN, &ChatViewWidget::OnScroll, this);
    display->Bind(wxEVT_SCROLLWIN_PAGEUP, &ChatViewWidget::OnScroll, this);

    // Set up drag and drop for file uploads
    if (m_mainFrame) {
        FileDropTarget* dropTarget = new FileDropTarget(
            [this](const wxArrayString& files) {
                if (m_mainFrame) {
                    m_mainFrame->OnFilesDropped(files);
                }
            });
        display->SetDropTarget(dropTarget);
    }

    // Create message formatter using ChatArea
    m_messageFormatter = new MessageFormatter(m_chatArea);

    // Set up link span callback
    m_messageFormatter->SetLinkSpanCallback(
        [this](long startPos, long endPos, const wxString& url) {
            AddLinkSpan(startPos, endPos, url);
        });
}

void ChatViewWidget::EnsureMediaDownloaded(const MediaInfo& info)
{
    // Auto-download visible media if not already downloaded
    if (m_mainFrame && info.fileId != 0 && info.localPath.IsEmpty()) {
        // Check if we are already downloading this file
        if (!HasPendingDownload(info.fileId)) {
            TelegramClient* client = m_mainFrame->GetTelegramClient();
            if (client) {
                // Determine priority (higher for smaller files/images)
                int priority = 5;
                if (info.type == MediaType::Photo || info.type == MediaType::Sticker) {
                    priority = 10;
                }
                
                wxString displayName = info.fileName.IsEmpty() ? "Auto-download" : info.fileName;
                client->DownloadFile(info.fileId, priority, displayName, 0);
                AddPendingDownload(info.fileId, info);
            }
        }
    }
}

void ChatViewWidget::DisplayMessage(const MessageInfo& msg)
{
    if (!m_messageFormatter) return;

    // Track message ID to detect duplicates and out-of-order messages
    if (msg.id != 0) {
        // Skip if already displayed
        if (m_displayedMessageIds.count(msg.id) > 0) {
            CVWLOG("DisplayMessage: skipping duplicate message id=" << msg.id);
            return;
        }
        m_displayedMessageIds.insert(msg.id);
        
        // Track the last displayed message ID
        if (msg.id > m_lastDisplayedMessageId) {
            m_lastDisplayedMessageId = msg.id;
        }
    }

    wxString timestamp = FormatTimestamp(msg.date);
    wxString sender = msg.senderName.IsEmpty() ? "Unknown" : msg.senderName;

    // Date separator feature disabled for now - causing display issues
    // TODO: Fix date separator logic to only show when day actually changes

    // Handle forwarded messages
    if (msg.isForwarded && !msg.forwardedFrom.IsEmpty()) {
        m_messageFormatter->AppendForwardMessage(timestamp, sender,
            msg.forwardedFrom, msg.text);
        m_messageFormatter->SetLastMessage(sender, msg.date);
        m_lastDisplayedSender = sender;
        m_lastDisplayedTimestamp = msg.date;
        return;
    }

    // Handle reply messages
    if (msg.replyToMessageId != 0 && !msg.replyToText.IsEmpty()) {
        m_messageFormatter->AppendReplyMessage(timestamp, sender,
            msg.replyToText, msg.text);
        m_messageFormatter->SetLastMessage(sender, msg.date);
        m_lastDisplayedSender = sender;
        m_lastDisplayedTimestamp = msg.date;
        return;
    }

    // Handle media messages - helper lambda to update state after media
    auto updateStateAfterMedia = [&]() {
        m_messageFormatter->SetLastMessage(sender, msg.date);
        m_lastDisplayedSender = sender;
        m_lastDisplayedTimestamp = msg.date;
    };

    if (msg.hasPhoto) {
        MediaInfo info;
        info.type = MediaType::Photo;
        info.fileId = msg.mediaFileId;
        info.localPath = msg.mediaLocalPath;
        info.caption = msg.mediaCaption;
        info.thumbnailFileId = msg.mediaThumbnailFileId;
        info.thumbnailPath = msg.mediaThumbnailPath;

        EnsureMediaDownloaded(info);

        long startPos = m_chatArea->GetLastPosition();
        m_messageFormatter->AppendMediaMessage(timestamp, sender, info, msg.mediaCaption);
        long endPos = m_chatArea->GetLastPosition();
        AddMediaSpan(startPos, endPos, info);
        updateStateAfterMedia();
        return;
    }

    if (msg.hasVideo) {
        MediaInfo info;
        info.type = MediaType::Video;
        info.fileId = msg.mediaFileId;
        info.localPath = msg.mediaLocalPath;
        info.fileName = msg.mediaFileName;
        info.caption = msg.mediaCaption;
        info.thumbnailFileId = msg.mediaThumbnailFileId;
        info.thumbnailPath = msg.mediaThumbnailPath;

        // For video, ensure thumbnail is downloaded first
        if (info.thumbnailFileId != 0 && info.thumbnailPath.IsEmpty()) {
            MediaInfo thumbInfo = info;
            thumbInfo.fileId = info.thumbnailFileId;
            thumbInfo.localPath = ""; // Force download check for thumb
            EnsureMediaDownloaded(thumbInfo);
        }

        long startPos = m_chatArea->GetLastPosition();
        m_messageFormatter->AppendMediaMessage(timestamp, sender, info, msg.mediaCaption);
        long endPos = m_chatArea->GetLastPosition();
        AddMediaSpan(startPos, endPos, info);
        updateStateAfterMedia();
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

        long startPos = m_chatArea->GetLastPosition();
        m_messageFormatter->AppendMediaMessage(timestamp, sender, info, msg.mediaCaption);
        long endPos = m_chatArea->GetLastPosition();
        AddMediaSpan(startPos, endPos, info);
        updateStateAfterMedia();
        return;
    }

    if (msg.hasVoice) {
        MediaInfo info;
        info.type = MediaType::Voice;
        info.fileId = msg.mediaFileId;
        info.localPath = msg.mediaLocalPath;

        long startPos = m_chatArea->GetLastPosition();
        m_messageFormatter->AppendMediaMessage(timestamp, sender, info, "");
        long endPos = m_chatArea->GetLastPosition();
        AddMediaSpan(startPos, endPos, info);
        updateStateAfterMedia();
        return;
    }

    if (msg.hasVideoNote) {
        MediaInfo info;
        info.type = MediaType::VideoNote;
        info.fileId = msg.mediaFileId;
        info.localPath = msg.mediaLocalPath;
        info.thumbnailFileId = msg.mediaThumbnailFileId;
        info.thumbnailPath = msg.mediaThumbnailPath;

        long startPos = m_chatArea->GetLastPosition();
        m_messageFormatter->AppendMediaMessage(timestamp, sender, info, "");
        long endPos = m_chatArea->GetLastPosition();
        AddMediaSpan(startPos, endPos, info);
        updateStateAfterMedia();
        return;
    }

    if (msg.hasSticker) {
        MediaInfo info;
        info.type = MediaType::Sticker;
        info.fileId = msg.mediaFileId;
        info.localPath = msg.mediaLocalPath;
        info.emoji = msg.mediaCaption;  // Sticker emoji is stored in mediaCaption
        info.thumbnailFileId = msg.mediaThumbnailFileId;
        info.thumbnailPath = msg.mediaThumbnailPath;

        EnsureMediaDownloaded(info);

        long startPos = m_chatArea->GetLastPosition();
        m_messageFormatter->AppendMediaMessage(timestamp, sender, info, "");
        long endPos = m_chatArea->GetLastPosition();
        AddMediaSpan(startPos, endPos, info);
        updateStateAfterMedia();
        return;
    }

    if (msg.hasAnimation) {
        MediaInfo info;
        info.type = MediaType::GIF;
        info.fileId = msg.mediaFileId;
        info.localPath = msg.mediaLocalPath;
        info.caption = msg.mediaCaption;
        info.thumbnailFileId = msg.mediaThumbnailFileId;
        info.thumbnailPath = msg.mediaThumbnailPath;

        EnsureMediaDownloaded(info);

        long startPos = m_chatArea->GetLastPosition();
        m_messageFormatter->AppendMediaMessage(timestamp, sender, info, msg.mediaCaption);
        long endPos = m_chatArea->GetLastPosition();
        AddMediaSpan(startPos, endPos, info);
        updateStateAfterMedia();
        return;
    }

    // Check for action messages (/me)
    if (msg.text.StartsWith("/me ")) {
        wxString action = msg.text.Mid(4);
        m_messageFormatter->AppendActionMessage(timestamp, sender, action);
        m_messageFormatter->SetLastMessage(sender, msg.date);
        m_lastDisplayedSender = sender;
        m_lastDisplayedTimestamp = msg.date;
        return;
    }

    // Handle edited messages - just show (edited) marker
    // Note: TDLib doesn't provide original message text, so no hover popup
    if (msg.isEdited) {
        m_messageFormatter->AppendEditedMessage(timestamp, sender, msg.text, nullptr, nullptr);
        m_messageFormatter->SetLastMessage(sender, msg.date);
        m_lastDisplayedSender = sender;
        m_lastDisplayedTimestamp = msg.date;
        return;
    }

    // Check for mentions/highlights (HexChat-style)
    bool isMentioned = false;
    if (!m_currentUsername.IsEmpty() && !msg.text.IsEmpty() && !msg.isOutgoing) {
        wxString lowerText = msg.text.Lower();
        wxString lowerUsername = m_currentUsername.Lower();
        // Check for @username mention or just username in text
        if (lowerText.Contains("@" + lowerUsername) || 
            lowerText.Contains(lowerUsername)) {
            isMentioned = true;
        }
    }

    // Regular text message - always show full timestamp and sender (HexChat/WelcomeChat style)
    if (isMentioned) {
        // Highlighted message - someone mentioned you
        m_messageFormatter->AppendHighlightMessage(timestamp, sender, msg.text);
    } else {
        // Full message with nick and timestamp
        m_messageFormatter->AppendMessage(timestamp, sender, msg.text);
    }

    // Update grouping state
    m_messageFormatter->SetLastMessage(sender, msg.date);
    m_lastDisplayedSender = sender;
    m_lastDisplayedTimestamp = msg.date;
}

void ChatViewWidget::DisplayMessages(const std::vector<MessageInfo>& messages)
{
    BeginBatchUpdate();
    for (const auto& msg : messages) {
        DisplayMessage(msg);
    }
    EndBatchUpdate();
    ScrollToBottomIfAtBottom();
}

void ChatViewWidget::BeginBatchUpdate()
{
    if (m_batchUpdateDepth == 0 && m_chatArea) {
        m_chatArea->BeginBatchUpdate();
    }
    m_batchUpdateDepth++;
}

void ChatViewWidget::EndBatchUpdate()
{
    if (m_batchUpdateDepth > 0) {
        m_batchUpdateDepth--;
        if (m_batchUpdateDepth == 0 && m_chatArea) {
            m_chatArea->EndBatchUpdate();
        }
    }
}

void ChatViewWidget::ClearMessages()
{
    if (m_chatArea) {
        m_chatArea->Clear();
    }
    ClearMediaSpans();
    ClearEditSpans();
    ClearLinkSpans();

    // Reset message grouping state and marker tracking
    if (m_messageFormatter) {
        m_messageFormatter->ResetGroupingState();
        // Reset marker tracking (marker was deleted with the clear, just reset tracking)
        m_messageFormatter->ResetUnreadMarker();
    }
    m_lastDisplayedSender.Clear();
    m_lastDisplayedTimestamp = 0;
    
    // Reset message ID tracking
    m_displayedMessageIds.clear();
    m_lastDisplayedMessageId = 0;
}

bool ChatViewWidget::IsMessageOutOfOrder(int64_t messageId) const
{
    // A message is out of order if:
    // 1. We have displayed messages and this message ID is less than the last one
    // 2. This means it should have been inserted earlier, not appended
    if (m_lastDisplayedMessageId == 0) {
        return false;  // No messages displayed yet, so not out of order
    }
    
    // If the message ID is less than the last displayed, it's out of order
    // (Telegram message IDs are monotonically increasing)
    return messageId < m_lastDisplayedMessageId;
}

void ChatViewWidget::ScrollToBottom()
{
    if (m_chatArea) {
        m_chatArea->ScrollToBottom();
        m_wasAtBottom = true;
        HideNewMessageIndicator();
    }
}

void ChatViewWidget::ScrollToBottomIfAtBottom()
{
    if (m_wasAtBottom) {
        ScrollToBottom();
    } else {
        // User is scrolled up, show new message indicator
        m_newMessageCount++;
        ShowNewMessageIndicator();
    }
}

bool ChatViewWidget::IsAtBottom() const
{
    if (!m_chatArea) return true;
    return m_chatArea->IsAtBottom();
}

void ChatViewWidget::ShowNewMessageIndicator()
{
    if (!m_newMessageButton) return;

    // Update button text with count
    wxString label = wxString::Format("v %d New Message%s",
        m_newMessageCount, m_newMessageCount == 1 ? "" : "s");
    m_newMessageButton->SetLabel(label);

    // Position button at bottom center of chat display
    if (m_chatArea) {
        wxSize displaySize = m_chatArea->GetSize();
        wxSize btnSize = m_newMessageButton->GetBestSize();
        int x = (displaySize.GetWidth() - btnSize.GetWidth()) / 2;
        int y = displaySize.GetHeight() - btnSize.GetHeight() - 10;
        m_newMessageButton->SetPosition(wxPoint(x, y));
    }

    m_newMessageButton->Show();
    m_newMessageButton->Raise();
}

void ChatViewWidget::HideNewMessageIndicator()
{
    if (m_newMessageButton) {
        m_newMessageButton->Hide();
    }
    m_newMessageCount = 0;
}

void ChatViewWidget::SetLoading(bool loading)
{
    m_isLoading = loading;
    // Could add a loading indicator here in the future
}

void ChatViewWidget::AddMediaSpan(long startPos, long endPos, const MediaInfo& info)
{
    CVWLOG("AddMediaSpan: fileId=" << info.fileId << " thumbId=" << info.thumbnailFileId 
           << " pos=" << startPos << "-" << endPos);
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

void ChatViewWidget::UpdateMediaSpanPath(int32_t fileId, const wxString& localPath, bool isThumbnail)
{
    if (fileId == 0 || localPath.IsEmpty()) return;
    
    CVWLOG("UpdateMediaSpanPath: fileId=" << fileId << " path=" << localPath.ToStdString() 
           << " isThumbnailHint=" << isThumbnail << " totalSpans=" << m_mediaSpans.size());
    
    int updatedCount = 0;
    
    // Update all media spans that reference this fileId
    // Check both main file and thumbnail - a fileId could match either
    for (auto& span : m_mediaSpans) {
        // Check if this is the main file
        if (span.info.fileId == fileId) {
            CVWLOG("UpdateMediaSpanPath: updating localPath for span with fileId=" << fileId);
            span.info.localPath = localPath;
            span.info.isDownloading = false;
            updatedCount++;
        }
        // Also check if this is a thumbnail (same fileId could be thumbnail for another media)
        if (span.info.thumbnailFileId == fileId) {
            CVWLOG("UpdateMediaSpanPath: updating thumbnailPath for span with thumbnailFileId=" << fileId);
            span.info.thumbnailPath = localPath;
            updatedCount++;
        }
    }
    
    CVWLOG("UpdateMediaSpanPath: updated " << updatedCount << " spans");
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

void ChatViewWidget::SetUserColors(const wxColour* colors)
{
    if (m_chatArea) {
        m_chatArea->SetUserColors(colors);
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

void ChatViewWidget::ShowDownloadProgress(const wxString& fileName, int percent)
{
    // Download progress is now shown in status bar via MainFrame
    // This method is kept for API compatibility but is a no-op
    (void)fileName;
    (void)percent;
}

void ChatViewWidget::UpdateDownloadProgress(int percent)
{
    // Download progress is now shown in status bar via MainFrame
    // This method is kept for API compatibility but is a no-op
    (void)percent;
}

void ChatViewWidget::HideDownloadProgress()
{
    // Download progress is now shown in status bar via MainFrame
    // This method is kept for API compatibility but is a no-op
}

bool ChatViewWidget::IsSameMedia(const MediaInfo& a, const MediaInfo& b) const
{
    // Compare by fileId if both have valid IDs
    if (a.fileId != 0 && b.fileId != 0) {
        return a.fileId == b.fileId && a.type == b.type;
    }
    // Fall back to comparing by localPath
    if (!a.localPath.IsEmpty() && !b.localPath.IsEmpty()) {
        return a.localPath == b.localPath && a.type == b.type;
    }
    // Compare by thumbnail if available
    if (a.thumbnailFileId != 0 && b.thumbnailFileId != 0) {
        return a.thumbnailFileId == b.thumbnailFileId && a.type == b.type;
    }
    return false;
}

void ChatViewWidget::ShowMediaPopup(const MediaInfo& info, const wxPoint& position)
{
    if (!m_mediaPopup) {
        CVWLOG("ShowMediaPopup: no popup widget");
        return;
    }
    
    // Validate input - must have either a fileId or a localPath
    if (info.fileId == 0 && info.localPath.IsEmpty() && info.thumbnailPath.IsEmpty()) {
        CVWLOG("ShowMediaPopup: no valid media reference (fileId=0, no paths)");
        return;
    }

    // Cancel any pending hide
    m_hideTimer.Stop();

    // Don't re-show if already showing the same media (prevents flickering and video restart)
    // Check both if popup is shown AND if we're tracking the same media
    bool alreadyShowingSame = IsSameMedia(m_currentlyShowingMedia, info);
    
    if (alreadyShowingSame) {
        // Check if paths have changed (download completed since last show)
        bool localPathChanged = (m_currentlyShowingMedia.localPath != info.localPath && 
                                 !info.localPath.IsEmpty() && wxFileExists(info.localPath));
        bool thumbnailPathChanged = (m_currentlyShowingMedia.thumbnailPath != info.thumbnailPath && 
                                     !info.thumbnailPath.IsEmpty() && wxFileExists(info.thumbnailPath));
        
        if (!localPathChanged && !thumbnailPathChanged) {
            // Same media, no path changes, don't reload
            CVWLOG("ShowMediaPopup: same media already showing/tracked, no path changes, skipping");
            return;
        }
        CVWLOG("ShowMediaPopup: same media but paths changed, allowing reload. localPathChanged=" 
               << localPathChanged << " thumbnailPathChanged=" << thumbnailPathChanged);
    }
    
    // Hide any existing popup before showing NEW media (not same media)
    if (m_mediaPopup->IsShown() && !alreadyShowingSame) {
        m_mediaPopup->StopAllPlayback();
        m_mediaPopup->Hide();
    }
    
    // Also hide edit history popup
    HideEditHistoryPopup();
    
    CVWLOG("ShowMediaPopup: fileId=" << info.fileId << " type=" << static_cast<int>(info.type)
           << " localPath=" << info.localPath.ToStdString()
           << " thumbnailPath=" << info.thumbnailPath.ToStdString());

    // For stickers, download the thumbnail if not already available
    if (info.type == MediaType::Sticker) {
        if (info.thumbnailFileId != 0 && (info.thumbnailPath.IsEmpty() || !wxFileExists(info.thumbnailPath))) {
            if (m_mainFrame) {
                TelegramClient* client = m_mainFrame->GetTelegramClient();
                if (client && !client->IsDownloading(info.thumbnailFileId) && !HasPendingDownload(info.thumbnailFileId)) {
                    CVWLOG("ShowMediaPopup: downloading sticker thumbnail, thumbnailFileId=" << info.thumbnailFileId);
                    wxString displayName = info.fileName.IsEmpty() ? "Sticker" : info.fileName;
                    client->DownloadFile(info.thumbnailFileId, 10, displayName, 0);
                    AddPendingDownload(info.thumbnailFileId, info);
                }
            }
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
    // But only if we haven't already requested this download (prevent duplicates)
    if (info.localPath.IsEmpty() || !wxFileExists(info.localPath) || needsVideoDownload) {
        if (info.fileId != 0 && m_mainFrame && !HasPendingDownload(info.fileId)) {
            TelegramClient* client = m_mainFrame->GetTelegramClient();
            if (client && !client->IsDownloading(info.fileId)) {
                // Start download with high priority for preview
                wxString displayName = info.fileName;
                if (displayName.IsEmpty()) {
                    switch (info.type) {
                        case MediaType::Photo: displayName = "Photo"; break;
                        case MediaType::Video: displayName = "Video"; break;
                        case MediaType::GIF: displayName = "GIF"; break;
                        case MediaType::VideoNote: displayName = "Video Note"; break;
                        case MediaType::Sticker: displayName = "Sticker"; break;
                        case MediaType::Voice: displayName = "Voice Message"; break;
                        default: displayName = "Media"; break;
                    }
                }
                CVWLOG("ShowMediaPopup: downloading media file, fileId=" << info.fileId << " name=" << displayName.ToStdString());
                client->DownloadFile(info.fileId, 10, displayName, info.fileSize.IsEmpty() ? 0 : wxAtol(info.fileSize));
                // Track this as a pending download so we can update popup when complete
                AddPendingDownload(info.fileId, info);
            }
        }
    }

    // Show popup - ShowMedia handles all cases:
    // - If video file exists and is playable, it will play
    // - If only thumbnail exists, it will show thumbnail
    // - If nothing exists, it will show placeholder/loading
    m_currentlyShowingMedia = info;
    
    // Show the popup directly - don't use CallAfter as it adds latency
    // and can cause the popup to not appear if mouse moves away quickly
    try {
        m_mediaPopup->ShowMedia(info, position);
    } catch (const std::exception& e) {
        CVWLOG("ShowMediaPopup: exception in ShowMedia: " << e.what());
    } catch (...) {
        CVWLOG("ShowMediaPopup: unknown exception in ShowMedia");
    }
}

void ChatViewWidget::HideMediaPopup()
{
    // Clear tracking state
    m_currentlyShowingMedia = MediaInfo();
    m_isOverMediaSpan = false;

    if (m_mediaPopup) {
        try {
            // Always stop playback - even if popup isn't visible yet
            // (video might be loading in background)
            m_mediaPopup->StopAllPlayback();
            
            if (m_mediaPopup->IsShown()) {
                m_mediaPopup->Hide();
            }
        } catch (const std::exception& e) {
            CVWLOG("HideMediaPopup: exception: " << e.what());
        } catch (...) {
            CVWLOG("HideMediaPopup: unknown exception");
        }
    }
}

void ChatViewWidget::ScheduleHideMediaPopup()
{
    // Only schedule hide if not already scheduled and popup is visible
    if (!m_hideTimer.IsRunning() && m_mediaPopup && m_mediaPopup->IsShown()) {
        m_hideTimer.StartOnce(HIDE_DELAY_MS);
    }
}

void ChatViewWidget::OnHideTimer(wxTimerEvent& event)
{
    // Only hide if we're not over a media span anymore
    if (!m_isOverMediaSpan) {
        HideMediaPopup();
    }
}

void ChatViewWidget::UpdateMediaPopup(int32_t fileId, const wxString& localPath)
{
    CVWLOG("UpdateMediaPopup called: fileId=" << fileId << " path=" << localPath.ToStdString());
    
    // Validate input
    if (fileId == 0 || localPath.IsEmpty()) {
        CVWLOG("UpdateMediaPopup: invalid fileId or path");
        return;
    }
    
    // First, check if this was a user-initiated download to open
    OnMediaDownloadComplete(fileId, localPath);

    if (!m_mediaPopup) {
        CVWLOG("UpdateMediaPopup: no popup exists");
        return;
    }
    
    if (!m_mediaPopup->IsShown()) {
        CVWLOG("UpdateMediaPopup: popup not shown, skipping update");
        return;
    }

    // Check if this file ID matches the current popup's media or thumbnail
    const MediaInfo& currentInfo = m_mediaPopup->GetMediaInfo();
    CVWLOG("UpdateMediaPopup: current popup fileId=" << currentInfo.fileId 
           << " thumbnailFileId=" << currentInfo.thumbnailFileId
           << " currentPath=" << currentInfo.localPath.ToStdString());

    // Check if this is a thumbnail download (for stickers or other media)
    if (currentInfo.thumbnailFileId == fileId) {
        CVWLOG("UpdateMediaPopup: matched thumbnail, updating with path=" << localPath.ToStdString());
        MediaInfo updatedInfo = currentInfo;
        updatedInfo.thumbnailPath = localPath;
        updatedInfo.isDownloading = false;
        wxPoint pos = m_mediaPopup->GetPosition();
        // Update our tracking so subsequent hovers don't reset
        m_currentlyShowingMedia = updatedInfo;
        m_mediaPopup->ShowMedia(updatedInfo, pos);
        return;
    }

    // Check if this is the main file download
    if (currentInfo.fileId == fileId) {
        CVWLOG("UpdateMediaPopup: matched main file, updating with path=" << localPath.ToStdString());
        MediaInfo updatedInfo = currentInfo;
        updatedInfo.localPath = localPath;
        updatedInfo.isDownloading = false;
        wxPoint pos = m_mediaPopup->GetPosition();
        // Update our tracking so subsequent hovers don't reset
        m_currentlyShowingMedia = updatedInfo;
        // Re-show with updated info - ShowMedia will handle format detection
        m_mediaPopup->ShowMedia(updatedInfo, pos);
        return;
    }
    
    // Also check pending downloads - the popup might be showing a different file 
    // but we should update tracking
    if (HasPendingDownload(fileId)) {
        CVWLOG("UpdateMediaPopup: fileId not matching current popup but found in pending downloads");
    } else {
        CVWLOG("UpdateMediaPopup: fileId=" << fileId << " does not match current popup (fileId=" 
               << currentInfo.fileId << ", thumbnailFileId=" << currentInfo.thumbnailFileId << ")");
    }
}

void ChatViewWidget::OpenMedia(const MediaInfo& info)
{
    // Validate that we have something to open
    if (info.fileId == 0 && info.localPath.IsEmpty()) {
        CVWLOG("OpenMedia: no valid media to open");
        return;
    }
    
    if (!info.localPath.IsEmpty() && wxFileExists(info.localPath)) {
        // Open with default application
        try {
            wxLaunchDefaultApplication(info.localPath);
        } catch (const std::exception& e) {
            CVWLOG("OpenMedia: exception launching application: " << e.what());
        }
    } else if (info.fileId != 0 && m_mainFrame) {
        // Need to download first, then open
        TelegramClient* client = m_mainFrame->GetTelegramClient();
        if (client) {
            // Mark this as a pending open (not just preview)
            MediaInfo infoWithOpenFlag = info;
            infoWithOpenFlag.isDownloading = true;  // Use this to track pending open
            AddPendingDownload(info.fileId, infoWithOpenFlag);
            
            // Get display name for download indicator
            wxString displayName = info.fileName;
            if (displayName.IsEmpty()) {
                switch (info.type) {
                    case MediaType::Photo: displayName = "Photo"; break;
                    case MediaType::Video: displayName = "Video"; break;
                    case MediaType::GIF: displayName = "GIF"; break;
                    case MediaType::File: displayName = "File"; break;
                    default: displayName = "Media"; break;
                }
            }
            client->DownloadFile(info.fileId, 10, displayName, info.fileSize.IsEmpty() ? 0 : wxAtol(info.fileSize));  // High priority for user-initiated download
        }
    }
}

void ChatViewWidget::OnMediaDownloadComplete(int32_t fileId, const wxString& localPath)
{
    // Check if this was a user-initiated download (click to open)
    if (HasPendingDownload(fileId)) {
        MediaInfo info = GetPendingDownload(fileId);
        RemovePendingDownload(fileId);

        // If isDownloading flag was set, user clicked to open - so open it now
        if (info.isDownloading && !localPath.IsEmpty() && wxFileExists(localPath)) {
            wxLaunchDefaultApplication(localPath);
        }
    }
}

wxString ChatViewWidget::FormatTimestamp(int64_t unixTime)
{
    if (unixTime <= 0) {
        return wxString();
    }
    time_t t = static_cast<time_t>(unixTime);
    wxDateTime dt(t);
    return dt.Format("%H:%M");
}

wxString ChatViewWidget::FormatSmartTimestamp(int64_t unixTime)
{
    if (unixTime <= 0) {
        return wxString();
    }

    time_t t = static_cast<time_t>(unixTime);
    wxDateTime dt(t);
    wxDateTime now = wxDateTime::Now();
    wxDateTime today = now.GetDateOnly();
    wxDateTime yesterday = today - wxDateSpan::Day();
    wxDateTime msgDate = dt.GetDateOnly();

    wxString timeStr = dt.Format("%H:%M");

    if (msgDate == today) {
        return timeStr;  // Just time for today
    } else if (msgDate == yesterday) {
        return "Yesterday " + timeStr;
    } else if (msgDate > today - wxDateSpan::Week()) {
        return dt.Format("%a ") + timeStr;  // Day name for last week
    } else {
        return dt.Format("%b %d ") + timeStr;  // Month day for older
    }
}

void ChatViewWidget::OnScroll(wxScrollWinEvent& event)
{
    event.Skip();

    // Update scroll position tracking
    m_wasAtBottom = IsAtBottom();

    // Hide new message indicator if scrolled to bottom
    if (m_wasAtBottom) {
        HideNewMessageIndicator();
    }
}

void ChatViewWidget::OnSize(wxSizeEvent& event)
{
    event.Skip();

    // Reposition the new message button
    if (m_newMessageButton && m_newMessageButton->IsShown() && m_chatArea) {
        wxSize displaySize = m_chatArea->GetSize();
        wxSize btnSize = m_newMessageButton->GetBestSize();
        int x = (displaySize.GetWidth() - btnSize.GetWidth()) / 2;
        int y = displaySize.GetHeight() - btnSize.GetHeight() - 10;
        m_newMessageButton->SetPosition(wxPoint(x, y));
    }
}

void ChatViewWidget::OnNewMessageButtonClick(wxCommandEvent& event)
{
    ScrollToBottom();
}

void ChatViewWidget::OnKeyDown(wxKeyEvent& event)
{
    int keyCode = event.GetKeyCode();

    switch (keyCode) {
        case WXK_HOME:
            if (m_chatArea && m_chatArea->GetDisplay()) {
                m_chatArea->GetDisplay()->ShowPosition(0);
                m_wasAtBottom = false;
            }
            break;

        case WXK_END:
            ScrollToBottom();
            break;

        case WXK_PAGEUP:
        case WXK_PAGEDOWN:
            event.Skip();  // Let default handling work
            // Update scroll state after
            CallAfter([this]() {
                m_wasAtBottom = IsAtBottom();
                if (m_wasAtBottom) {
                    HideNewMessageIndicator();
                }
            });
            break;

        case 'C':
        case 'c':
            if (event.ControlDown() || event.CmdDown()) {
                // Copy selected text
                wxRichTextCtrl* display = m_chatArea ? m_chatArea->GetDisplay() : nullptr;
                if (display && display->CanCopy()) {
                    display->Copy();
                }
            } else {
                event.Skip();
            }
            break;

        default:
            event.Skip();
            break;
    }
}

void ChatViewWidget::OnRightDown(wxMouseEvent& event)
{
    wxPoint pos = event.GetPosition();
    long textPos;

    wxRichTextCtrl* display = m_chatArea ? m_chatArea->GetDisplay() : nullptr;
    if (!display) {
        event.Skip();
        return;
    }

    wxTextCtrlHitTestResult hit = display->HitTest(pos, &textPos);

    if (hit == wxTE_HT_ON_TEXT || hit == wxTE_HT_BEFORE) {
        m_contextMenuPos = textPos;

        // Check what's at this position
        m_contextMenuLink = GetLinkAtPosition(textPos);

        MediaSpan* mediaSpan = GetMediaSpanAtPosition(textPos);
        if (mediaSpan) {
            m_contextMenuMedia = mediaSpan->info;
        } else {
            m_contextMenuMedia = MediaInfo();
        }

        ShowContextMenu(display->ClientToScreen(pos));
    } else {
        event.Skip();
    }
}

wxString ChatViewWidget::GetSelectedText() const
{
    if (m_chatArea && m_chatArea->GetDisplay()) {
        return m_chatArea->GetDisplay()->GetStringSelection();
    }
    return wxString();
}

wxString ChatViewWidget::GetLinkAtPosition(long pos) const
{
    for (const auto& span : m_linkSpans) {
        if (pos >= span.startPos && pos < span.endPos) {
            return span.url;
        }
    }
    return wxString();
}

void ChatViewWidget::ShowContextMenu(const wxPoint& pos)
{
    wxMenu menu;

    // Copy option (always available if text is selected)
    wxString selectedText = GetSelectedText();
    if (!selectedText.IsEmpty()) {
        menu.Append(ID_COPY_TEXT, "Copy\tCtrl+C");
    }

    // Link options
    if (!m_contextMenuLink.IsEmpty()) {
        menu.AppendSeparator();
        menu.Append(ID_OPEN_LINK, "Open Link");
        menu.Append(ID_COPY_LINK, "Copy Link");
    }

    // Media options
    if (m_contextMenuMedia.fileId != 0 || !m_contextMenuMedia.localPath.IsEmpty()) {
        menu.AppendSeparator();
        if (!m_contextMenuMedia.localPath.IsEmpty() && wxFileExists(m_contextMenuMedia.localPath)) {
            menu.Append(ID_OPEN_MEDIA, "Open Media");
            menu.Append(ID_SAVE_MEDIA, "Save As...");
        }
    }

    // Bind handlers
    Bind(wxEVT_MENU, &ChatViewWidget::OnCopyText, this, ID_COPY_TEXT);
    Bind(wxEVT_MENU, &ChatViewWidget::OnCopyLink, this, ID_COPY_LINK);
    Bind(wxEVT_MENU, &ChatViewWidget::OnOpenLink, this, ID_OPEN_LINK);
    Bind(wxEVT_MENU, &ChatViewWidget::OnSaveMedia, this, ID_SAVE_MEDIA);
    Bind(wxEVT_MENU, &ChatViewWidget::OnOpenMedia, this, ID_OPEN_MEDIA);

    PopupMenu(&menu);

    // Unbind handlers
    Unbind(wxEVT_MENU, &ChatViewWidget::OnCopyText, this, ID_COPY_TEXT);
    Unbind(wxEVT_MENU, &ChatViewWidget::OnCopyLink, this, ID_COPY_LINK);
    Unbind(wxEVT_MENU, &ChatViewWidget::OnOpenLink, this, ID_OPEN_LINK);
    Unbind(wxEVT_MENU, &ChatViewWidget::OnSaveMedia, this, ID_SAVE_MEDIA);
    Unbind(wxEVT_MENU, &ChatViewWidget::OnOpenMedia, this, ID_OPEN_MEDIA);
}

void ChatViewWidget::OnCopyText(wxCommandEvent& event)
{
    wxRichTextCtrl* display = m_chatArea ? m_chatArea->GetDisplay() : nullptr;
    if (display && display->CanCopy()) {
        display->Copy();
    }
}

void ChatViewWidget::OnCopyLink(wxCommandEvent& event)
{
    if (!m_contextMenuLink.IsEmpty()) {
        if (wxTheClipboard->Open()) {
            wxTheClipboard->SetData(new wxTextDataObject(m_contextMenuLink));
            wxTheClipboard->Close();
        }
    }
}

void ChatViewWidget::OnOpenLink(wxCommandEvent& event)
{
    if (!m_contextMenuLink.IsEmpty()) {
        wxLaunchDefaultBrowser(m_contextMenuLink);
    }
}

void ChatViewWidget::OnSaveMedia(wxCommandEvent& event)
{
    if (m_contextMenuMedia.localPath.IsEmpty() || !wxFileExists(m_contextMenuMedia.localPath)) {
        return;
    }

    wxFileName fn(m_contextMenuMedia.localPath);
    wxString defaultName = m_contextMenuMedia.fileName.IsEmpty() ?
        fn.GetFullName() : m_contextMenuMedia.fileName;

    wxFileDialog saveDialog(this, "Save Media As", "", defaultName,
        "All files (*.*)|*.*", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

    if (saveDialog.ShowModal() == wxID_OK) {
        wxCopyFile(m_contextMenuMedia.localPath, saveDialog.GetPath());
    }
}

void ChatViewWidget::OnOpenMedia(wxCommandEvent& event)
{
    if (!m_contextMenuMedia.localPath.IsEmpty() && wxFileExists(m_contextMenuMedia.localPath)) {
        wxLaunchDefaultApplication(m_contextMenuMedia.localPath);
    }
}

void ChatViewWidget::OnMouseMove(wxMouseEvent& event)
{
    wxPoint pos = event.GetPosition();
    long textPos;

    wxRichTextCtrl* ctrl = m_chatArea ? m_chatArea->GetDisplay() : nullptr;
    if (!ctrl) {
        event.Skip();
        return;
    }

    // Get text position from screen coordinates
    wxTextCtrlHitTestResult hit = ctrl->HitTest(pos, &textPos);

    if (hit == wxTE_HT_ON_TEXT || hit == wxTE_HT_BEFORE || hit == wxTE_HT_BELOW) {
        MediaSpan* span = GetMediaSpanAtPosition(textPos);
        if (span) {
            // Change cursor to hand for ALL media types (clickable)
            ctrl->SetCursor(wxCursor(wxCURSOR_HAND));

            // Show preview for visual media types
            if (span->info.type == MediaType::Photo ||
                span->info.type == MediaType::Sticker ||
                span->info.type == MediaType::GIF ||
                span->info.type == MediaType::Video ||
                span->info.type == MediaType::VideoNote) {

                // We're over a media span
                m_isOverMediaSpan = true;
                m_hideTimer.Stop();  // Cancel any pending hide

                // Check if we're already showing or about to show this exact media
                if (IsSameMedia(m_currentlyShowingMedia, span->info) ||
                    IsSameMedia(m_pendingHoverMedia, span->info)) {
                    // Already showing or pending this media, do nothing
                    event.Skip();
                    return;
                }

                // Check if we're hovering a new/different media span
                if (!IsSameMedia(m_pendingHoverMedia, span->info)) {
                    // New media span - start debounce timer
                    m_hoverTimer.Stop();
                    m_pendingHoverMedia = span->info;
                    m_pendingHoverPos = ctrl->ClientToScreen(pos);
                    m_lastHoveredTextPos = textPos;
                    m_hoverTimer.StartOnce(HOVER_DELAY_MS);
                }
            } else {
                // Non-visual media type (Voice, File) - still clickable but no popup preview
                m_isOverMediaSpan = false;
                m_hoverTimer.Stop();
                m_pendingHoverMedia = MediaInfo();
                ScheduleHideMediaPopup();
            }
        } else {
            // Not over a media span
            m_isOverMediaSpan = false;
            m_hoverTimer.Stop();
            m_lastHoveredTextPos = -1;
            m_pendingHoverMedia = MediaInfo();

            // Check for link span
            LinkSpan* linkSpan = GetLinkSpanAtPosition(textPos);
            if (linkSpan) {
                ctrl->SetCursor(wxCursor(wxCURSOR_HAND));
                ScheduleHideMediaPopup();
                HideEditHistoryPopup();
            } else {
                // Check for edit span
                EditSpan* editSpan = GetEditSpanAtPosition(textPos);
                if (editSpan) {
                    ctrl->SetCursor(wxCursor(wxCURSOR_HAND));
                    // Show edit history popup
                    ShowEditHistoryPopup(*editSpan, ctrl->ClientToScreen(pos));
                    ScheduleHideMediaPopup();
                } else {
                    ctrl->SetCursor(wxCursor(wxCURSOR_IBEAM));
                    ScheduleHideMediaPopup();
                    HideEditHistoryPopup();
                }
            }
        }
    } else {
        // Not over text - cancel everything
        m_isOverMediaSpan = false;
        m_hoverTimer.Stop();
        m_lastHoveredTextPos = -1;
        m_pendingHoverMedia = MediaInfo();
        ctrl->SetCursor(wxCursor(wxCURSOR_DEFAULT));
        ScheduleHideMediaPopup();
    }

    event.Skip();
}

void ChatViewWidget::OnHoverTimer(wxTimerEvent& event)
{
    // Timer fired - show the popup if we still have pending media and still over media span
    if (m_isOverMediaSpan &&
        (m_pendingHoverMedia.fileId != 0 || !m_pendingHoverMedia.localPath.IsEmpty())) {
        // Clear pending state before showing to prevent re-triggering
        MediaInfo mediaToShow = m_pendingHoverMedia;
        wxPoint posToShow = m_pendingHoverPos;
        m_pendingHoverMedia = MediaInfo();
        
        try {
            ShowMediaPopup(mediaToShow, posToShow);
        } catch (const std::exception& e) {
            CVWLOG("OnHoverTimer: exception showing popup: " << e.what());
        } catch (...) {
            CVWLOG("OnHoverTimer: unknown exception showing popup");
        }
    }
}

void ChatViewWidget::OnMouseLeave(wxMouseEvent& event)
{
    // Cancel any pending hover
    m_hoverTimer.Stop();
    m_lastHoveredTextPos = -1;
    m_isOverMediaSpan = false;
    m_pendingHoverMedia = MediaInfo();

    // Use delayed hide for smoother UX when mouse briefly leaves
    ScheduleHideMediaPopup();
    HideEditHistoryPopup();
    wxRichTextCtrl* display = m_chatArea ? m_chatArea->GetDisplay() : nullptr;
    if (display) {
        display->SetCursor(wxCursor(wxCURSOR_IBEAM));
    }
    event.Skip();
}

void ChatViewWidget::OnLeftDown(wxMouseEvent& event)
{
    wxPoint pos = event.GetPosition();
    long textPos;

    wxRichTextCtrl* ctrl = m_chatArea ? m_chatArea->GetDisplay() : nullptr;
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
