#include "VirtualizedChatWidget.h"
#include "MainFrame.h"
#include "MediaPopup.h"
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/clipbrd.h>
#include <wx/regex.h>
#include <algorithm>
#include <ctime>

// Cached file existence check
static bool CachedFileExists(const wxString& path) {
    if (path.IsEmpty()) return false;
    
    struct CacheEntry {
        bool exists;
        wxLongLong timestamp;
    };
    
    static std::unordered_map<std::string, CacheEntry> s_cache;
    static const wxLongLong CACHE_DURATION_MS = 500;
    
    std::string key = path.ToStdString();
    wxLongLong now = wxGetLocalTimeMillis();
    
    auto it = s_cache.find(key);
    if (it != s_cache.end() && (now - it->second.timestamp) < CACHE_DURATION_MS) {
        return it->second.exists;
    }
    
    bool exists = wxFileExists(path);
    s_cache[key] = {exists, now};
    
    if (s_cache.size() > 1000) {
        for (auto iter = s_cache.begin(); iter != s_cache.end();) {
            if ((now - iter->second.timestamp) > CACHE_DURATION_MS * 10) {
                iter = s_cache.erase(iter);
            } else {
                ++iter;
            }
        }
    }
    
    return exists;
}

wxBEGIN_EVENT_TABLE(VirtualizedChatWidget, wxPanel)
    EVT_PAINT(VirtualizedChatWidget::OnPaint)
    EVT_SIZE(VirtualizedChatWidget::OnSize)
    EVT_MOUSEWHEEL(VirtualizedChatWidget::OnMouseWheel)
    EVT_LEFT_DOWN(VirtualizedChatWidget::OnMouseDown)
    EVT_LEFT_UP(VirtualizedChatWidget::OnMouseUp)
    EVT_MOTION(VirtualizedChatWidget::OnMouseMove)
    EVT_LEAVE_WINDOW(VirtualizedChatWidget::OnMouseLeave)
    EVT_RIGHT_DOWN(VirtualizedChatWidget::OnRightDown)
    EVT_KEY_DOWN(VirtualizedChatWidget::OnKeyDown)
    EVT_CHAR(VirtualizedChatWidget::OnChar)
    EVT_TIMER(wxID_ANY, VirtualizedChatWidget::OnScrollTimer)
wxEND_EVENT_TABLE()

VirtualizedChatWidget::VirtualizedChatWidget(wxWindow* parent, MainFrame* mainFrame)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, 
              wxFULL_REPAINT_ON_RESIZE | wxWANTS_CHARS)
    , m_mainFrame(mainFrame)
    , m_scrollTimer(this)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    
    // Initialize default fonts
    m_config.timestampFont = wxFont(9, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    m_config.usernameFont = wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
    m_config.messageFont = wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    m_config.emojiFont = wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    m_config.boldFont = wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
    m_config.italicFont = wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_ITALIC, wxFONTWEIGHT_NORMAL);
    m_config.boldItalicFont = wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_ITALIC, wxFONTWEIGHT_BOLD);
    m_config.codeFont = wxFont(9, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    
    // Use system colors for native look
    m_config.backgroundColor = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
    m_config.textColor = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    m_config.timestampColor = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
    m_config.ownUsernameColor = wxColour(0, 128, 0);
    m_config.otherUsernameColor = wxSystemSettings::GetColour(wxSYS_COLOUR_HOTLIGHT);
    m_config.linkColor = wxSystemSettings::GetColour(wxSYS_COLOUR_HOTLIGHT);
    m_config.mentionColor = wxColour(255, 128, 0);
    m_config.systemMessageColor = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
    m_config.selectionColor = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT);
    m_config.selectionTextColor = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT);
    m_config.dateSeparatorColor = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
    m_config.dateSeparatorLineColor = wxColour(180, 180, 180);
    m_config.mediaColor = wxColour(0, 100, 180);
    m_config.codeBackgroundColor = wxColour(240, 240, 240);
    m_config.codeTextColor = wxColour(80, 80, 80);
    m_config.highlightColor = wxColour(255, 255, 200);
    m_config.reactionColor = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
    m_config.editedColor = wxColour(128, 128, 128);
    m_config.readTickColor = wxColour(0, 150, 0);
    m_config.sentTickColor = wxColour(128, 128, 128);
    m_config.spoilerColor = wxColour(100, 100, 100);
    
    SetBackgroundColour(m_config.backgroundColor);
    
    // Create media popup
    m_mediaPopup = new MediaPopup(this);
    m_mediaPopup->SetClickCallback([this](const MediaInfo& info) {
        OpenMedia(info);
        HideMediaPopup();
    });
}

VirtualizedChatWidget::~VirtualizedChatWidget() {
    if (m_scrollTimer.IsRunning()) {
        m_scrollTimer.Stop();
    }
}

// === Message Management ===

void VirtualizedChatWidget::AddMessage(const MessageInfo& msg) {
    bool wasBottom = IsAtBottom();
    
    {
        std::lock_guard<std::mutex> lock(m_messagesMutex);
        
        if (msg.id != 0 && m_messageIdToIndex.count(msg.id) > 0) {
            return;
        }
        
        size_t index = m_messages.size();
        m_messages.push_back(msg);
        m_layouts.push_back(MessageLayout());
        m_layouts.back().messageId = msg.id;
        m_layouts.back().needsRecalc = true;
        
        if (msg.id != 0) {
            m_messageIdToIndex[msg.id] = index;
        }
        
        // Track file ID for media updates
        if (msg.mediaFileId != 0) {
            m_fileIdToIndex[msg.mediaFileId] = index;
        }
    }
    
    SortMessages();
    
    if (m_batchUpdateDepth == 0) {
        RecalculateAllLayouts();
        if (wasBottom) {
            ScrollToBottom();
        }
        Refresh();
    } else {
        m_needsLayoutRecalc = true;
    }
}

void VirtualizedChatWidget::AddMessages(const std::vector<MessageInfo>& messages) {
    if (messages.empty()) return;
    
    bool wasBottom = IsAtBottom();
    
    {
        std::lock_guard<std::mutex> lock(m_messagesMutex);
        
        for (const auto& msg : messages) {
            if (msg.id != 0 && m_messageIdToIndex.count(msg.id) > 0) {
                continue;
            }
            
            size_t index = m_messages.size();
            m_messages.push_back(msg);
            m_layouts.push_back(MessageLayout());
            m_layouts.back().messageId = msg.id;
            m_layouts.back().needsRecalc = true;
            
            if (msg.id != 0) {
                m_messageIdToIndex[msg.id] = index;
            }
            if (msg.mediaFileId != 0) {
                m_fileIdToIndex[msg.mediaFileId] = index;
            }
        }
    }
    
    SortMessages();
    
    if (m_batchUpdateDepth == 0) {
        RecalculateAllLayouts();
        if (wasBottom) {
            ScrollToBottom();
        }
        Refresh();
    } else {
        m_needsLayoutRecalc = true;
    }
}

void VirtualizedChatWidget::PrependMessages(const std::vector<MessageInfo>& messages) {
    if (messages.empty()) {
        m_isLoadingHistory = false;
        Refresh();
        return;
    }
    
    // Find anchor
    int firstVisibleIndex = GetFirstVisibleMessageIndex();
    int64_t anchorMessageId = 0;
    int anchorOffset = 0;
    
    if (firstVisibleIndex >= 0 && firstVisibleIndex < (int)m_messages.size()) {
        std::lock_guard<std::mutex> lock(m_messagesMutex);
        anchorMessageId = m_messages[firstVisibleIndex].id;
        anchorOffset = m_scrollPosition - m_layouts[firstVisibleIndex].yPosition;
    }
    
    size_t addedCount = 0;
    {
        std::lock_guard<std::mutex> lock(m_messagesMutex);
        
        for (const auto& msg : messages) {
            if (msg.id != 0 && m_messageIdToIndex.count(msg.id) > 0) {
                continue;
            }
            
            size_t index = m_messages.size();
            m_messages.push_back(msg);
            m_layouts.push_back(MessageLayout());
            m_layouts.back().messageId = msg.id;
            m_layouts.back().needsRecalc = true;
            
            if (msg.id != 0) {
                m_messageIdToIndex[msg.id] = index;
            }
            if (msg.mediaFileId != 0) {
                m_fileIdToIndex[msg.mediaFileId] = index;
            }
            addedCount++;
        }
    }
    
    if (addedCount == 0) {
        m_isLoadingHistory = false;
        Refresh();
        return;
    }
    
    SortMessages();
    RecalculateAllLayouts();
    
    // Restore scroll anchor
    if (anchorMessageId != 0) {
        auto it = m_messageIdToIndex.find(anchorMessageId);
        if (it != m_messageIdToIndex.end() && it->second < m_layouts.size()) {
            int newScrollPos = m_layouts[it->second].yPosition + anchorOffset;
            UpdateScrollPosition(newScrollPos);
        }
    }
    
    m_isLoadingHistory = false;
    Refresh();
}

void VirtualizedChatWidget::UpdateMessage(const MessageInfo& msg) {
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    
    auto it = m_messageIdToIndex.find(msg.id);
    if (it == m_messageIdToIndex.end()) return;
    
    size_t index = it->second;
    if (index >= m_messages.size()) return;
    
    m_messages[index] = msg;
    m_layouts[index].needsRecalc = true;
    
    if (m_batchUpdateDepth == 0) {
        RecalculateLayout(index);
        Refresh();
    } else {
        m_needsLayoutRecalc = true;
    }
}

void VirtualizedChatWidget::RemoveMessage(int64_t messageId) {
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    
    auto it = m_messageIdToIndex.find(messageId);
    if (it == m_messageIdToIndex.end()) return;
    
    size_t index = it->second;
    if (index >= m_messages.size()) return;
    
    // Remove file ID mapping if present
    int32_t fileId = m_messages[index].mediaFileId;
    if (fileId != 0) {
        m_fileIdToIndex.erase(fileId);
    }
    
    m_messages.erase(m_messages.begin() + index);
    m_layouts.erase(m_layouts.begin() + index);
    
    RebuildIndex();
    
    if (m_batchUpdateDepth == 0) {
        RecalculateAllLayouts();
        Refresh();
    } else {
        m_needsLayoutRecalc = true;
    }
}

void VirtualizedChatWidget::ClearMessages() {
    {
        std::lock_guard<std::mutex> lock(m_messagesMutex);
        m_messages.clear();
        m_layouts.clear();
        m_messageIdToIndex.clear();
        m_fileIdToIndex.clear();
    }
    
    m_scrollPosition = 0;
    m_totalHeight = 0;
    m_wasAtBottom = true;
    m_hasSelection = false;
    m_isSelecting = false;
    m_selectionStartMsg = -1;
    m_selectionEndMsg = -1;
    m_hasUnreadMarker = false;
    m_lastDisplayedSender.Clear();
    m_lastDisplayedTimestamp = 0;
    m_isLoadingHistory = false;
    m_allHistoryLoaded = false;
    m_readMarkerSpans.clear();
    
    Refresh();
}

const MessageInfo* VirtualizedChatWidget::GetMessageById(int64_t messageId) const {
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    
    auto it = m_messageIdToIndex.find(messageId);
    if (it == m_messageIdToIndex.end() || it->second >= m_messages.size()) {
        return nullptr;
    }
    return &m_messages[it->second];
}

MessageInfo* VirtualizedChatWidget::GetMessageById(int64_t messageId) {
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    
    auto it = m_messageIdToIndex.find(messageId);
    if (it == m_messageIdToIndex.end() || it->second >= m_messages.size()) {
        return nullptr;
    }
    return &m_messages[it->second];
}

MessageInfo* VirtualizedChatWidget::GetMessageByFileId(int32_t fileId) {
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    
    auto it = m_fileIdToIndex.find(fileId);
    if (it == m_fileIdToIndex.end() || it->second >= m_messages.size()) {
        return nullptr;
    }
    return &m_messages[it->second];
}

// === Scrolling ===

void VirtualizedChatWidget::ScrollToBottom() {
    int viewHeight = GetClientSize().GetHeight();
    int maxScroll = std::max(0, m_totalHeight - viewHeight);
    m_scrollPosition = maxScroll;
    m_wasAtBottom = true;
    Refresh();
}

void VirtualizedChatWidget::ForceScrollToBottom() {
    ScrollToBottom();
}

void VirtualizedChatWidget::ScrollToMessage(int64_t messageId) {
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    
    auto it = m_messageIdToIndex.find(messageId);
    if (it == m_messageIdToIndex.end() || it->second >= m_layouts.size()) {
        return;
    }
    
    UpdateScrollPosition(m_layouts[it->second].yPosition);
    Refresh();
}

void VirtualizedChatWidget::ScrollByLines(int lines) {
    int lineHeight = m_config.messageFont.GetPixelSize().GetHeight() + m_config.lineSpacing;
    if (lineHeight <= 0) lineHeight = 16;
    ScrollByPixels(lines * lineHeight);
}

void VirtualizedChatWidget::ScrollByPixels(int pixels) {
    UpdateScrollPosition(m_scrollPosition + pixels);
    Refresh();
}

bool VirtualizedChatWidget::IsAtBottom() const {
    int viewHeight = GetClientSize().GetHeight();
    int maxScroll = std::max(0, m_totalHeight - viewHeight);
    return m_scrollPosition >= maxScroll - 15;
}

// === Selection ===

wxString VirtualizedChatWidget::GetSelectedText() const {
    if (!m_hasSelection || m_selectionStartMsg < 0 || m_selectionEndMsg < 0) {
        return wxEmptyString;
    }
    
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    
    wxString result;
    int startMsg = std::min(m_selectionStartMsg, m_selectionEndMsg);
    int endMsg = std::max(m_selectionStartMsg, m_selectionEndMsg);
    
    for (int i = startMsg; i <= endMsg && i < (int)m_messages.size(); i++) {
        const auto& msg = m_messages[i];
        if (!result.IsEmpty()) result += "\n";
        result += wxString::Format("[%s] <%s> %s", 
            FormatTimestamp(msg.date), msg.senderName, msg.text);
    }
    
    return result;
}

void VirtualizedChatWidget::ClearSelection() {
    m_isSelecting = false;
    m_hasSelection = false;
    m_selectionStartMsg = -1;
    m_selectionEndMsg = -1;
    m_selectionStartChar = -1;
    m_selectionEndChar = -1;
    Refresh();
}

void VirtualizedChatWidget::SelectAll() {
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    
    if (m_messages.empty()) return;
    
    m_hasSelection = true;
    m_selectionStartMsg = 0;
    m_selectionEndMsg = m_messages.size() - 1;
    m_selectionStartChar = 0;
    m_selectionEndChar = -1;  // -1 means end of message
    Refresh();
}

void VirtualizedChatWidget::CopyToClipboard(const wxString& text) {
    if (wxTheClipboard->Open()) {
        wxTheClipboard->SetData(new wxTextDataObject(text));
        wxTheClipboard->Close();
    }
}

// === Configuration ===

void VirtualizedChatWidget::SetConfig(const ChatRenderConfig& config) {
    m_config = config;
    SetBackgroundColour(m_config.backgroundColor);
    
    for (auto& layout : m_layouts) {
        layout.needsRecalc = true;
    }
    
    RecalculateAllLayouts();
    Refresh();
}

void VirtualizedChatWidget::SetTopicText(const wxString& topic) {
    m_topicText = topic;
    Refresh();
}

void VirtualizedChatWidget::ClearTopicText() {
    m_topicText.Clear();
    Refresh();
}

void VirtualizedChatWidget::SetLoadingHistory(bool loading) {
    m_isLoadingHistory = loading;
    if (loading) {
        m_lastLoadTime = wxGetLocalTimeMillis();
    }
    Refresh();
}

// === Read Status ===

void VirtualizedChatWidget::SetReadStatus(int64_t messageId, int64_t readTime) {
    m_messageReadTimes[messageId] = readTime;
    
    // Update layout for this message
    auto it = m_messageIdToIndex.find(messageId);
    if (it != m_messageIdToIndex.end() && it->second < m_layouts.size()) {
        m_layouts[it->second].needsRecalc = true;
    }
    
    Refresh();
}

// === Media Popup ===

void VirtualizedChatWidget::ShowMediaPopup(const MediaInfo& info, const wxPoint& pos, int bottomBoundary) {
    if (!m_mediaPopup) return;
    
    m_mediaPopup->SetParentBottom(bottomBoundary);
    m_mediaPopup->ShowMedia(info, pos);
}

void VirtualizedChatWidget::HideMediaPopup() {
    if (m_mediaPopup && m_mediaPopup->IsShown()) {
        m_mediaPopup->Hide();
    }
}

void VirtualizedChatWidget::UpdateMediaPath(int32_t fileId, const wxString& localPath) {
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    
    auto it = m_fileIdToIndex.find(fileId);
    if (it != m_fileIdToIndex.end() && it->second < m_messages.size()) {
        m_messages[it->second].mediaLocalPath = localPath;
        m_layouts[it->second].needsRecalc = true;
        Refresh();
    }
}

void VirtualizedChatWidget::OnMediaDownloadComplete(int32_t fileId, const wxString& localPath) {
    RemovePendingDownload(fileId);
    
    if (HasPendingOpen(fileId)) {
        RemovePendingOpen(fileId);
        if (!localPath.IsEmpty() && CachedFileExists(localPath)) {
            wxLaunchDefaultApplication(localPath);
        }
    }
    
    UpdateMediaPath(fileId, localPath);
}

void VirtualizedChatWidget::AddPendingDownload(int32_t fileId) {
    m_pendingDownloads.insert(fileId);
}

bool VirtualizedChatWidget::HasPendingDownload(int32_t fileId) const {
    return m_pendingDownloads.count(fileId) > 0;
}

void VirtualizedChatWidget::RemovePendingDownload(int32_t fileId) {
    m_pendingDownloads.erase(fileId);
}

void VirtualizedChatWidget::AddPendingOpen(int32_t fileId) {
    m_pendingOpens.insert(fileId);
}

bool VirtualizedChatWidget::HasPendingOpen(int32_t fileId) const {
    return m_pendingOpens.count(fileId) > 0;
}

void VirtualizedChatWidget::RemovePendingOpen(int32_t fileId) {
    m_pendingOpens.erase(fileId);
}

void VirtualizedChatWidget::OpenMedia(const MediaInfo& info) {
    if (info.fileId == 0 && info.localPath.IsEmpty()) return;
    
    if (!info.localPath.IsEmpty() && CachedFileExists(info.localPath)) {
        wxLaunchDefaultApplication(info.localPath);
    } else if (info.fileId != 0 && m_mediaDownloadCallback) {
        AddPendingDownload(info.fileId);
        AddPendingOpen(info.fileId);
        m_mediaDownloadCallback(info.fileId, 10);
    }
}

// === Unread Marker ===

void VirtualizedChatWidget::ShowUnreadMarker() {
    m_hasUnreadMarker = true;
    if (!m_messages.empty()) {
        m_unreadMarkerAfterMessageId = m_messages.back().id;
    }
    Refresh();
}

void VirtualizedChatWidget::HideUnreadMarker() {
    m_hasUnreadMarker = false;
    m_unreadMarkerAfterMessageId = 0;
    Refresh();
}

// === New Message Indicator ===

void VirtualizedChatWidget::ShowNewMessageIndicator() {
    m_showNewMessageIndicator = true;
    m_newMessageCount++;
    Refresh();
}

void VirtualizedChatWidget::HideNewMessageIndicator() {
    m_showNewMessageIndicator = false;
    m_newMessageCount = 0;
    Refresh();
}

// === Batch Updates ===

void VirtualizedChatWidget::BeginBatchUpdate() {
    m_batchUpdateDepth++;
}

void VirtualizedChatWidget::EndBatchUpdate() {
    if (m_batchUpdateDepth > 0) {
        m_batchUpdateDepth--;
    }
    
    if (m_batchUpdateDepth == 0 && m_needsLayoutRecalc) {
        RecalculateAllLayouts();
        m_needsLayoutRecalc = false;
        Refresh();
    }
}

// === Text Parsing ===

std::vector<TextSegment> VirtualizedChatWidget::ParseMessageText(const wxString& text, const MessageInfo& msg) {
    std::vector<TextSegment> segments;
    
    if (text.IsEmpty()) {
        return segments;
    }
    
    // If we have entities from TDLib, use those
    if (!msg.entities.empty()) {
        return ApplyEntities(text, msg.entities);
    }
    
    // Otherwise, detect URLs
    return DetectUrls(text);
}

std::vector<TextSegment> VirtualizedChatWidget::DetectUrls(const wxString& text) {
    std::vector<TextSegment> segments;
    
    static wxRegEx urlRegex(
        "(https?://[^\\s<>\"'\\)\\]]+|www\\.[^\\s<>\"'\\)\\]]+)",
        wxRE_EXTENDED | wxRE_ICASE);
    
    if (!urlRegex.IsValid()) {
        segments.push_back(TextSegment(text, TextSegment::Type::Plain));
        return segments;
    }
    
    wxString remaining = text;
    int offset = 0;
    
    while (urlRegex.Matches(remaining)) {
        size_t matchStart, matchLen;
        if (!urlRegex.GetMatch(&matchStart, &matchLen, 0)) break;
        
        // Text before URL
        if (matchStart > 0) {
            TextSegment seg(remaining.Left(matchStart), TextSegment::Type::Plain);
            seg.startChar = offset;
            seg.length = matchStart;
            segments.push_back(seg);
        }
        
        // The URL itself
        wxString url = remaining.Mid(matchStart, matchLen);
        TextSegment linkSeg(url, TextSegment::Type::Link);
        linkSeg.startChar = offset + matchStart;
        linkSeg.length = matchLen;
        if (url.Lower().StartsWith("www.")) {
            linkSeg.url = "https://" + url;
        } else {
            linkSeg.url = url;
        }
        segments.push_back(linkSeg);
        
        offset += matchStart + matchLen;
        remaining = remaining.Mid(matchStart + matchLen);
    }
    
    // Remaining text after last URL
    if (!remaining.IsEmpty()) {
        TextSegment seg(remaining, TextSegment::Type::Plain);
        seg.startChar = offset;
        seg.length = remaining.length();
        segments.push_back(seg);
    }
    
    if (segments.empty()) {
        segments.push_back(TextSegment(text, TextSegment::Type::Plain));
    }
    
    return segments;
}

std::vector<TextSegment> VirtualizedChatWidget::ApplyEntities(const wxString& text, const std::vector<TextEntity>& entities) {
    std::vector<TextSegment> segments;
    
    if (entities.empty()) {
        return DetectUrls(text);
    }
    
    // Sort entities by offset
    std::vector<TextEntity> sortedEntities = entities;
    std::sort(sortedEntities.begin(), sortedEntities.end(),
        [](const TextEntity& a, const TextEntity& b) { return a.offset < b.offset; });
    
    size_t currentPos = 0;
    
    for (const auto& entity : sortedEntities) {
        // Text before this entity
        if ((size_t)entity.offset > currentPos) {
            wxString beforeText = text.Mid(currentPos, entity.offset - currentPos);
            auto urlSegments = DetectUrls(beforeText);
            for (auto& seg : urlSegments) {
                seg.startChar += currentPos;
                segments.push_back(seg);
            }
        }
        
        // The entity itself
        wxString entityText = text.Mid(entity.offset, entity.length);
        TextSegment seg(entityText);
        seg.startChar = entity.offset;
        seg.length = entity.length;
        
        switch (entity.type) {
            case TextEntityType::Bold:
                seg.type = TextSegment::Type::Bold;
                break;
            case TextEntityType::Italic:
                seg.type = TextSegment::Type::Italic;
                break;
            case TextEntityType::Underline:
                seg.type = TextSegment::Type::Underline;
                break;
            case TextEntityType::Strikethrough:
                seg.type = TextSegment::Type::Strikethrough;
                break;
            case TextEntityType::Code:
                seg.type = TextSegment::Type::Code;
                break;
            case TextEntityType::Pre:
                seg.type = TextSegment::Type::Pre;
                break;
            case TextEntityType::TextUrl:
                seg.type = TextSegment::Type::Link;
                seg.url = entity.url;
                break;
            case TextEntityType::Url:
                seg.type = TextSegment::Type::Link;
                seg.url = entityText;
                break;
            case TextEntityType::Mention:
            case TextEntityType::MentionName:
                seg.type = TextSegment::Type::Mention;
                seg.url = entityText;
                break;
            case TextEntityType::Hashtag:
                seg.type = TextSegment::Type::Hashtag;
                break;
            case TextEntityType::EmailAddress:
                seg.type = TextSegment::Type::Email;
                seg.url = "mailto:" + entityText;
                break;
            case TextEntityType::PhoneNumber:
                seg.type = TextSegment::Type::Phone;
                seg.url = "tel:" + entityText;
                break;
            case TextEntityType::Spoiler:
                seg.type = TextSegment::Type::Spoiler;
                break;
            default:
                seg.type = TextSegment::Type::Plain;
                break;
        }
        
        segments.push_back(seg);
        currentPos = entity.offset + entity.length;
    }
    
    // Text after last entity
    if (currentPos < text.length()) {
        wxString afterText = text.Mid(currentPos);
        auto urlSegments = DetectUrls(afterText);
        for (auto& seg : urlSegments) {
            seg.startChar += currentPos;
            segments.push_back(seg);
        }
    }
    
    return segments;
}

// === Layout Calculation ===

void VirtualizedChatWidget::RecalculateAllLayouts() {
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    
    if (m_messages.empty()) {
        m_totalHeight = 0;
        return;
    }
    
    wxClientDC dc(this);
    int viewWidth = GetClientSize().GetWidth();
    if (viewWidth < 100) viewWidth = 400;
    
    m_lastLayoutWidth = viewWidth;
    int yPos = m_config.verticalPadding;
    
    // Reset date tracking
    m_dateSeparatorDays.clear();
    
    for (size_t i = 0; i < m_messages.size(); i++) {
        auto& layout = m_layouts[i];
        const auto& msg = m_messages[i];
        
        // Check for date separator
        layout.hasDateSeparator = NeedsDateSeparator(i);
        if (layout.hasDateSeparator) {
            yPos += DATE_SEPARATOR_HEIGHT;
        }
        
        // Check for unread marker
        if (m_hasUnreadMarker && i > 0 && m_messages[i-1].id == m_unreadMarkerAfterMessageId) {
            yPos += 25;  // Unread marker height
        }
        
        layout.yPosition = yPos;
        layout.messageId = msg.id;
        
        int textAreaWidth = viewWidth - m_config.horizontalPadding * 2 - 
                           m_config.timestampWidth - m_config.usernameWidth - 20;
        if (textAreaWidth < 100) textAreaWidth = 100;
        
        int height = CalculateMessageHeight(msg, layout, textAreaWidth);
        layout.height = height;
        
        yPos += height + m_config.messagePadding;
    }
    
    m_totalHeight = yPos + m_config.verticalPadding;
}

void VirtualizedChatWidget::RecalculateLayout(size_t index) {
    if (index >= m_layouts.size()) return;
    
    // Just recalculate from this index onwards
    RecalculateLayoutsFrom(index);
}

void VirtualizedChatWidget::RecalculateLayoutsFrom(size_t startIndex) {
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    
    if (startIndex >= m_messages.size()) return;
    
    wxClientDC dc(this);
    int viewWidth = GetClientSize().GetWidth();
    if (viewWidth < 100) viewWidth = 400;
    
    int yPos = (startIndex > 0) ? 
        m_layouts[startIndex - 1].yPosition + m_layouts[startIndex - 1].height + m_config.messagePadding :
        m_config.verticalPadding;
    
    for (size_t i = startIndex; i < m_messages.size(); i++) {
        auto& layout = m_layouts[i];
        const auto& msg = m_messages[i];
        
        layout.hasDateSeparator = NeedsDateSeparator(i);
        if (layout.hasDateSeparator) {
            yPos += DATE_SEPARATOR_HEIGHT;
        }
        
        layout.yPosition = yPos;
        
        int textAreaWidth = viewWidth - m_config.horizontalPadding * 2 - 
                           m_config.timestampWidth - m_config.usernameWidth - 20;
        if (textAreaWidth < 100) textAreaWidth = 100;
        
        int height = CalculateMessageHeight(msg, layout, textAreaWidth);
        layout.height = height;
        
        yPos += height + m_config.messagePadding;
    }
    
    m_totalHeight = yPos + m_config.verticalPadding;
}

int VirtualizedChatWidget::CalculateMessageHeight(const MessageInfo& msg, MessageLayout& layout, int availableWidth) {
    wxClientDC dc(this);
    dc.SetFont(m_config.messageFont);
    
    wxFontMetrics fm = dc.GetFontMetrics();
    int lineHeight = fm.height + m_config.lineSpacing;
    if (lineHeight <= 0) lineHeight = 16;
    
    // Parse text into segments
    layout.textSegments = ParseMessageText(msg.text, msg);
    
    // Wrap text
    layout.displayLines = WrapText(msg.text, dc, availableWidth);
    int textLines = layout.displayLines.size();
    if (textLines < 1) textLines = 1;
    
    layout.textHeight = textLines * lineHeight;
    int totalHeight = layout.textHeight;
    
    // Add media height
    layout.hasMedia = msg.hasPhoto || msg.hasVideo || msg.hasSticker || 
                      msg.hasAnimation || msg.hasVoice || msg.hasVideoNote || 
                      msg.hasDocument;
    if (layout.hasMedia) {
        layout.mediaInfo = BuildMediaInfo(msg);
        layout.mediaHeight = m_config.mediaPlaceholderHeight;
        totalHeight += layout.mediaHeight + m_config.lineSpacing;
    } else {
        layout.mediaHeight = 0;
    }
    
    // Add reactions height
    if (!msg.reactions.empty()) {
        layout.reactionsHeight = m_config.reactionHeight;
        totalHeight += layout.reactionsHeight + m_config.lineSpacing;
        
        // Convert reactions to layout format
        layout.reactions.clear();
        for (const auto& r : msg.reactions) {
            layout.reactions.push_back({r.first, r.second});
        }
    } else {
        layout.reactionsHeight = 0;
        layout.reactions.clear();
    }
    
    // Track edit status
    layout.isEdited = msg.isEdited;
    
    // Calculate status
    layout.status = MessageStatus::None;
    if (msg.isOutgoing) {
        if (msg.id == 0) {
            layout.status = MessageStatus::Sending;
        } else if (m_lastReadOutboxId > 0 && msg.id <= m_lastReadOutboxId) {
            layout.status = MessageStatus::Read;
        } else {
            layout.status = MessageStatus::Sent;
        }
    }
    
    totalHeight += m_config.verticalPadding * 2;
    
    return totalHeight;
}

std::vector<wxString> VirtualizedChatWidget::WrapText(const wxString& text, wxDC& dc, int maxWidth) {
    std::vector<wxString> lines;
    
    if (text.IsEmpty()) {
        lines.push_back(wxEmptyString);
        return lines;
    }
    
    // Split by explicit newlines first
    wxArrayString paragraphs = wxSplit(text, '\n');
    
    for (const auto& para : paragraphs) {
        if (para.IsEmpty()) {
            lines.push_back(wxEmptyString);
            continue;
        }
        
        wxString remaining = para;
        
        while (!remaining.IsEmpty()) {
            wxSize textSize = dc.GetTextExtent(remaining);
            
            if (textSize.GetWidth() <= maxWidth) {
                lines.push_back(remaining);
                break;
            }
            
            // Binary search for break point
            int low = 1, high = remaining.length();
            int best = 1;
            
            while (low <= high) {
                int mid = (low + high) / 2;
                wxString test = remaining.Left(mid);
                wxSize size = dc.GetTextExtent(test);
                
                if (size.GetWidth() <= maxWidth) {
                    best = mid;
                    low = mid + 1;
                } else {
                    high = mid - 1;
                }
            }
            
            // Try to break at word boundary
            int breakPos = best;
            for (int i = best; i > 0; i--) {
                if (remaining[i] == ' ' || remaining[i] == '-') {
                    breakPos = i + 1;
                    break;
                }
            }
            
            if (breakPos <= 0) breakPos = best;
            if (breakPos <= 0) breakPos = 1;
            
            lines.push_back(remaining.Left(breakPos));
            remaining = remaining.Mid(breakPos);
            
            // Skip leading spaces on continuation
            while (!remaining.IsEmpty() && remaining[0] == ' ') {
                remaining = remaining.Mid(1);
            }
        }
    }
    
    if (lines.empty()) {
        lines.push_back(wxEmptyString);
    }
    
    return lines;
}

void VirtualizedChatWidget::CalculateClickableAreas(MessageLayout& layout, const MessageInfo& msg, 
                                                     int textX, int textY, int textWidth) {
    layout.clickableAreas.clear();
    
    wxClientDC dc(this);
    dc.SetFont(m_config.messageFont);
    wxFontMetrics fm = dc.GetFontMetrics();
    int lineHeight = fm.height + m_config.lineSpacing;
    
    int currentY = textY;
    (void)currentY;  // Used for future clickable area positioning
    (void)lineHeight;
    
    // Add clickable areas for links in text segments
    for (const auto& segment : layout.textSegments) {
        if (segment.type == TextSegment::Type::Link || 
            segment.type == TextSegment::Type::Mention ||
            segment.type == TextSegment::Type::Email ||
            segment.type == TextSegment::Type::Phone) {
            
            // Find this segment's position in the wrapped text
            // This is approximate - for proper positioning we'd need to track
            // positions during wrapping
            wxSize segmentSize = dc.GetTextExtent(segment.text);
            
            ClickableArea area;
            area.rect = wxRect(textX, currentY, segmentSize.GetWidth(), lineHeight);
            
            switch (segment.type) {
                case TextSegment::Type::Link:
                    area.type = ClickableArea::Type::Link;
                    break;
                case TextSegment::Type::Mention:
                    area.type = ClickableArea::Type::Mention;
                    break;
                default:
                    area.type = ClickableArea::Type::Link;
                    break;
            }
            
            area.data = segment.url;
            area.messageId = msg.id;
            layout.clickableAreas.push_back(area);
        }
    }
    
    // Add media clickable area
    if (layout.hasMedia) {
        ClickableArea mediaArea;
        mediaArea.rect = layout.mediaRect;
        mediaArea.type = ClickableArea::Type::Media;
        mediaArea.mediaInfo = layout.mediaInfo;
        mediaArea.messageId = msg.id;
        layout.clickableAreas.push_back(mediaArea);
    }
    
    // Add edit marker clickable area
    if (layout.isEdited && !layout.editMarkerRect.IsEmpty()) {
        ClickableArea editArea;
        editArea.rect = layout.editMarkerRect;
        editArea.type = ClickableArea::Type::Edit;
        editArea.messageId = msg.id;
        layout.clickableAreas.push_back(editArea);
    }
}

// === Rendering ===

void VirtualizedChatWidget::OnPaint(wxPaintEvent& event) {
    wxAutoBufferedPaintDC dc(this);
    
    dc.SetBackground(wxBrush(m_config.backgroundColor));
    dc.Clear();
    
    wxSize clientSize = GetClientSize();
    int viewHeight = clientSize.GetHeight();
    int viewWidth = clientSize.GetWidth();
    
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    
    if (m_messages.empty()) {
        dc.SetFont(m_config.messageFont);
        dc.SetTextForeground(m_config.timestampColor);
        wxString placeholder = "No messages yet";
        wxSize textSize = dc.GetTextExtent(placeholder);
        dc.DrawText(placeholder, (viewWidth - textSize.GetWidth()) / 2, viewHeight / 2);
        return;
    }
    
    // Recalculate layouts if width changed
    if (viewWidth != m_lastLayoutWidth && viewWidth > 100) {
        const_cast<VirtualizedChatWidget*>(this)->RecalculateAllLayouts();
    }
    
    int firstVisible = GetFirstVisibleMessageIndex();
    int lastVisible = GetLastVisibleMessageIndex();
    
    if (firstVisible < 0) firstVisible = 0;
    if (lastVisible >= (int)m_messages.size()) lastVisible = m_messages.size() - 1;
    
    // Render loading indicator at top
    if (m_isLoadingHistory && firstVisible == 0) {
        RenderLoadingIndicator(dc, 10 - m_scrollPosition, viewWidth);
    }
    
    // Render visible messages
    for (int i = firstVisible; i <= lastVisible && i < (int)m_messages.size(); i++) {
        const auto& msg = m_messages[i];
        auto& layout = m_layouts[i];
        
        int yOffset = layout.yPosition - m_scrollPosition;
        
        // Render date separator if needed
        if (layout.hasDateSeparator) {
            wxString dateText = GetDateSeparatorText(msg.date);
            RenderDateSeparator(dc, dateText, yOffset - DATE_SEPARATOR_HEIGHT, viewWidth);
        }
        
        // Render unread marker if positioned after previous message
        if (m_hasUnreadMarker && i > 0 && m_messages[i-1].id == m_unreadMarkerAfterMessageId) {
            RenderUnreadMarker(dc, yOffset - 25, viewWidth);
        }
        
        // Render selection background first
        if (m_hasSelection) {
            RenderSelection(dc, layout, yOffset);
        }
        
        RenderMessage(dc, msg, layout, yOffset);
    }
    
    // Draw custom scrollbar
    if (m_totalHeight > viewHeight) {
        int scrollbarHeight = std::max(30, viewHeight * viewHeight / m_totalHeight);
        int scrollbarY = m_totalHeight > viewHeight 
            ? m_scrollPosition * (viewHeight - scrollbarHeight) / (m_totalHeight - viewHeight)
            : 0;
        
        dc.SetBrush(wxBrush(wxColour(160, 160, 160, 180)));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRoundedRectangle(viewWidth - 10, scrollbarY, 8, scrollbarHeight, 4);
    }
    
    // Draw new message indicator
    if (m_showNewMessageIndicator && !IsAtBottom()) {
        dc.SetBrush(wxBrush(m_config.linkColor));
        dc.SetPen(*wxTRANSPARENT_PEN);
        
        int btnWidth = 120;
        int btnHeight = 28;
        int btnX = (viewWidth - btnWidth) / 2;
        int btnY = viewHeight - btnHeight - 10;
        
        dc.DrawRoundedRectangle(btnX, btnY, btnWidth, btnHeight, 14);
        
        dc.SetFont(m_config.messageFont);
        dc.SetTextForeground(*wxWHITE);
        wxString label = m_newMessageCount > 1 
            ? wxString::Format("%d new messages", m_newMessageCount)
            : "New message";
        wxSize labelSize = dc.GetTextExtent(label);
        dc.DrawText(label, btnX + (btnWidth - labelSize.GetWidth()) / 2, 
                   btnY + (btnHeight - labelSize.GetHeight()) / 2);
    }
}

void VirtualizedChatWidget::RenderMessage(wxDC& dc, const MessageInfo& msg, 
                                          MessageLayout& layout, int yOffset) {
    int x = m_config.horizontalPadding;
    int y = yOffset + m_config.verticalPadding;
    int viewWidth = GetClientSize().GetWidth();
    
    // Check for highlight (mention)
    bool isMentioned = false;
    if (!m_currentUsername.IsEmpty() && !msg.text.IsEmpty() && !msg.isOutgoing) {
        wxString lowerText = msg.text.Lower();
        wxString lowerUsername = m_currentUsername.Lower();
        if (lowerText.Contains("@" + lowerUsername) || lowerText.Contains(lowerUsername)) {
            isMentioned = true;
        }
    }
    
    // Draw highlight background
    if (isMentioned) {
        dc.SetBrush(wxBrush(m_config.highlightColor));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(0, yOffset, viewWidth, layout.height);
    }
    
    // Render timestamp
    wxString timestamp = FormatTimestamp(msg.date);
    RenderTimestamp(dc, timestamp, x, y);
    x += m_config.timestampWidth;
    
    // Render username
    RenderUsername(dc, msg.senderName, msg.isOutgoing, x, y, m_config.usernameWidth);
    x += m_config.usernameWidth + 10;
    
    int textAreaWidth = viewWidth - x - m_config.horizontalPadding - 30;
    if (textAreaWidth < 100) textAreaWidth = 100;
    
    // Render message text with formatting
    int textX = x;
    int textY = y;
    RenderMessageText(dc, layout, textX, textY, textAreaWidth, msg);
    
    // Track current Y position
    wxFontMetrics fm = dc.GetFontMetrics();
    int lineHeight = fm.height + m_config.lineSpacing;
    int currentY = y + layout.displayLines.size() * lineHeight;
    
    // Render media placeholder
    if (layout.hasMedia) {
        layout.mediaRect = wxRect(x, currentY, textAreaWidth, m_config.mediaPlaceholderHeight);
        RenderMediaPlaceholder(dc, layout.mediaInfo, x, currentY, textAreaWidth, layout);
        currentY += m_config.mediaPlaceholderHeight + m_config.lineSpacing;
    }
    
    // Render reactions
    if (!msg.reactions.empty()) {
        RenderReactions(dc, msg.reactions, x, currentY, textAreaWidth, layout);
        currentY += m_config.reactionHeight + m_config.lineSpacing;
    }
    
    // Render status ticks for outgoing messages
    if (msg.isOutgoing) {
        int tickX = viewWidth - m_config.horizontalPadding - 25;
        RenderStatusTicks(dc, layout.status, tickX, y, layout);
    }
    
    // Render edit marker
    if (msg.isEdited) {
        dc.SetFont(m_config.timestampFont);
        wxString editText = "(edited)";
        wxSize editSize = dc.GetTextExtent(editText);
        int editX = viewWidth - m_config.horizontalPadding - editSize.GetWidth() - 30;
        
        dc.SetTextForeground(m_config.editedColor);
        dc.DrawText(editText, editX, y);
        
        layout.editMarkerRect = wxRect(editX, y, editSize.GetWidth(), editSize.GetHeight());
    }
    
    // Calculate clickable areas for hit testing
    CalculateClickableAreas(layout, msg, textX, textY, textAreaWidth);
}

void VirtualizedChatWidget::RenderTimestamp(wxDC& dc, const wxString& timestamp, int x, int y) {
    dc.SetFont(m_config.timestampFont);
    dc.SetTextForeground(m_config.timestampColor);
    dc.DrawText("[" + timestamp + "]", x, y);
}

void VirtualizedChatWidget::RenderUsername(wxDC& dc, const wxString& username, bool isOwn, 
                                           int x, int y, int maxWidth) {
    dc.SetFont(m_config.usernameFont);
    dc.SetTextForeground(isOwn ? m_config.ownUsernameColor : m_config.otherUsernameColor);
    
    wxString displayName = username.IsEmpty() ? "Unknown" : username;
    
    // Truncate if too long (like MessageFormatter does)
    if ((int)displayName.length() > 12) {
        displayName = displayName.Left(11) + wxString::FromUTF8("\xE2\x80\xA6");  // ellipsis
    }
    
    wxString formatted = "<" + displayName + ">";
    wxSize textSize = dc.GetTextExtent(formatted);
    
    // Right-align within the column (HexChat/IRC style)
    int padding = maxWidth - textSize.GetWidth();
    if (padding < 0) padding = 0;
    
    dc.DrawText(formatted, x + padding, y);
}

void VirtualizedChatWidget::RenderMessageText(wxDC& dc, MessageLayout& layout, 
                                               int x, int y, int maxWidth, const MessageInfo& msg) {
    dc.SetFont(m_config.messageFont);
    wxFontMetrics fm = dc.GetFontMetrics();
    int lineHeight = fm.height + m_config.lineSpacing;
    
    int currentY = y;
    
    // Raw terminal style - render line by line with inline formatting
    // Keep it simple like HexChat/IRC
    if (!layout.textSegments.empty() && layout.textSegments.size() > 1) {
        int currentX = x;
        
        for (const auto& segment : layout.textSegments) {
            // Simple styling - just color changes, no fancy backgrounds
            switch (segment.type) {
                case TextSegment::Type::Bold:
                    dc.SetFont(m_config.boldFont);
                    dc.SetTextForeground(m_config.textColor);
                    break;
                case TextSegment::Type::Italic:
                    dc.SetFont(m_config.italicFont);
                    dc.SetTextForeground(m_config.textColor);
                    break;
                case TextSegment::Type::Code:
                case TextSegment::Type::Pre:
                    // Just use monospace font, no background
                    dc.SetFont(m_config.codeFont);
                    dc.SetTextForeground(m_config.textColor);
                    break;
                case TextSegment::Type::Link:
                case TextSegment::Type::Email:
                case TextSegment::Type::Phone:
                    dc.SetFont(m_config.messageFont);
                    dc.SetTextForeground(m_config.linkColor);
                    break;
                case TextSegment::Type::Mention:
                    dc.SetFont(m_config.messageFont);
                    dc.SetTextForeground(m_config.mentionColor);
                    break;
                default:
                    dc.SetFont(m_config.messageFont);
                    dc.SetTextForeground(m_config.textColor);
                    break;
            }
            
            dc.DrawText(segment.text, currentX, currentY);
            wxSize segSize = dc.GetTextExtent(segment.text);
            
            // Underline links (terminal style)
            if (segment.type == TextSegment::Type::Link || 
                segment.type == TextSegment::Type::Email ||
                segment.type == TextSegment::Type::Phone) {
                dc.SetPen(wxPen(m_config.linkColor, 1));
                dc.DrawLine(currentX, currentY + segSize.GetHeight() - 1, 
                           currentX + segSize.GetWidth(), currentY + segSize.GetHeight() - 1);
            }
            
            currentX += segSize.GetWidth();
        }
    } else {
        // Plain text - just render lines
        dc.SetTextForeground(m_config.textColor);
        
        for (const auto& line : layout.displayLines) {
            dc.DrawText(line, x, currentY);
            currentY += lineHeight;
        }
    }
}

void VirtualizedChatWidget::RenderTextSegment(wxDC& dc, const TextSegment& segment, int x, int y) {
    // This is called for inline segment rendering
    dc.DrawText(segment.text, x, y);
}

void VirtualizedChatWidget::RenderDateSeparator(wxDC& dc, const wxString& dateText, int y, int width) {
    dc.SetFont(m_config.timestampFont);
    dc.SetTextForeground(m_config.dateSeparatorColor);
    
    // IRC/HexChat style: ─────────── January 15, 2025 ───────────
    wxString dashes = wxString::FromUTF8("───────────");
    wxString fullText = dashes + " " + dateText + " " + dashes;
    
    wxSize textSize = dc.GetTextExtent(fullText);
    int textX = (width - textSize.GetWidth()) / 2;
    int textY = y + (DATE_SEPARATOR_HEIGHT - textSize.GetHeight()) / 2;
    
    dc.DrawText(fullText, textX, textY);
}

void VirtualizedChatWidget::RenderMediaPlaceholder(wxDC& dc, const MediaInfo& media, 
                                                    int x, int y, int width, MessageLayout& layout) {
    dc.SetFont(m_config.messageFont);
    
    wxString emoji = GetMediaEmoji(media.type);
    wxString label;
    
    switch (media.type) {
        case MediaType::Photo:
            label = "[Photo]";
            break;
        case MediaType::Video:
            if (media.duration > 0) {
                int mins = media.duration / 60;
                int secs = media.duration % 60;
                label = wxString::Format("[Video %d:%02d]", mins, secs);
            } else {
                label = "[Video]";
            }
            break;
        case MediaType::Sticker:
            label = "[Sticker]";
            if (!media.emoji.IsEmpty()) {
                label += " " + media.emoji;
            }
            break;
        case MediaType::GIF:
            label = "[GIF]";
            break;
        case MediaType::Voice:
            {
                int mins = media.duration / 60;
                int secs = media.duration % 60;
                wxString waveform = GenerateAsciiWaveform(media.waveform, 20);
                if (!waveform.IsEmpty()) {
                    label = wxString::Format("[Voice %d:%02d]  %s", mins, secs, waveform);
                } else {
                    label = wxString::Format("[Voice %d:%02d]", mins, secs);
                }
            }
            break;
        case MediaType::VideoNote:
            if (media.duration > 0) {
                int mins = media.duration / 60;
                int secs = media.duration % 60;
                label = wxString::Format("[Video Message %d:%02d]", mins, secs);
            } else {
                label = "[Video Message]";
            }
            break;
        case MediaType::File:
            label = "[File: " + media.fileName + "]";
            break;
        default:
            label = "[Media]";
            break;
    }
    
    dc.SetTextForeground(m_config.mediaColor);
    
    wxString fullText = emoji + " " + label;
    dc.DrawText(fullText, x, y);
    
    // Draw underline
    wxSize textSize = dc.GetTextExtent(fullText);
    dc.SetPen(wxPen(m_config.mediaColor, 1));
    dc.DrawLine(x, y + textSize.GetHeight() - 2, x + textSize.GetWidth(), y + textSize.GetHeight() - 2);
    
    // Update layout rect
    layout.mediaRect = wxRect(x, y, textSize.GetWidth(), textSize.GetHeight());
    
    // Add caption if present
    if (!media.caption.IsEmpty()) {
        dc.SetTextForeground(m_config.textColor);
        dc.DrawText(" " + media.caption, x + textSize.GetWidth(), y);
    }
}

void VirtualizedChatWidget::RenderReactions(wxDC& dc, const std::map<wxString, std::vector<wxString>>& reactions,
                                             int x, int y, int width, MessageLayout& layout) {
    if (reactions.empty()) return;
    
    dc.SetFont(m_config.messageFont);
    dc.SetTextForeground(m_config.reactionColor);
    
    wxString reactionText;
    for (const auto& r : reactions) {
        if (!reactionText.IsEmpty()) reactionText += "  ";
        reactionText += r.first + " ";
        
        // Add user names (first 3)
        size_t count = 0;
        for (const auto& user : r.second) {
            if (count > 0) reactionText += ", ";
            reactionText += user;
            count++;
            if (count >= 3) break;
        }
        if (r.second.size() > 3) {
            reactionText += wxString::Format(" +%zu", r.second.size() - 3);
        }
    }
    
    dc.DrawText(reactionText, x, y);
    
    wxSize size = dc.GetTextExtent(reactionText);
    layout.reactionsRect = wxRect(x, y, size.GetWidth(), size.GetHeight());
}

void VirtualizedChatWidget::RenderStatusTicks(wxDC& dc, MessageStatus status, int x, int y, MessageLayout& layout) {
    if (status == MessageStatus::None) return;
    
    dc.SetFont(m_config.timestampFont);
    
    wxString tick;
    wxColour tickColor;
    
    switch (status) {
        case MessageStatus::Sending:
            tick = "...";
            tickColor = m_config.sentTickColor;
            break;
        case MessageStatus::Sent:
            tick = wxString::FromUTF8("\xE2\x9C\x93");  // ✓
            tickColor = m_config.sentTickColor;
            break;
        case MessageStatus::Read:
            tick = wxString::FromUTF8("\xE2\x9C\x93\xE2\x9C\x93");  // ✓✓
            tickColor = m_config.readTickColor;
            break;
        default:
            return;
    }
    
    dc.SetTextForeground(tickColor);
    dc.DrawText(tick, x, y);
    
    wxSize size = dc.GetTextExtent(tick);
    layout.statusRect = wxRect(x, y, size.GetWidth(), size.GetHeight());
}

void VirtualizedChatWidget::RenderEditMarker(wxDC& dc, int x, int y, MessageLayout& layout) {
    dc.SetFont(m_config.timestampFont);
    dc.SetTextForeground(m_config.editedColor);
    
    wxString text = "(edited)";
    dc.DrawText(text, x, y);
    
    wxSize size = dc.GetTextExtent(text);
    layout.editMarkerRect = wxRect(x, y, size.GetWidth(), size.GetHeight());
}

void VirtualizedChatWidget::RenderLoadingIndicator(wxDC& dc, int y, int width) {
    dc.SetFont(m_config.messageFont);
    dc.SetTextForeground(m_config.timestampColor);
    
    wxString text = "Loading older messages...";
    wxSize textSize = dc.GetTextExtent(text);
    int textX = (width - textSize.GetWidth()) / 2;
    
    dc.DrawText(text, textX, y);
}

void VirtualizedChatWidget::RenderSelection(wxDC& dc, const MessageLayout& layout, int yOffset) {
    if (!m_hasSelection) return;
    
    int startMsg = std::min(m_selectionStartMsg, m_selectionEndMsg);
    int endMsg = std::max(m_selectionStartMsg, m_selectionEndMsg);
    
    // Check if this message is in selection range
    int msgIndex = -1;
    for (size_t i = 0; i < m_layouts.size(); i++) {
        if (m_layouts[i].messageId == layout.messageId) {
            msgIndex = i;
            break;
        }
    }
    
    if (msgIndex < startMsg || msgIndex > endMsg) return;
    
    // Draw selection highlight
    dc.SetBrush(wxBrush(m_config.selectionColor));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(0, yOffset, GetClientSize().GetWidth(), layout.height);
}

void VirtualizedChatWidget::RenderUnreadMarker(wxDC& dc, int y, int width) {
    dc.SetFont(m_config.timestampFont);
    dc.SetTextForeground(wxColour(200, 50, 50));
    
    // IRC/HexChat style: ─────────── New messages ───────────
    wxString dashes = wxString::FromUTF8("───────────");
    wxString text = dashes + " New messages " + dashes;
    
    wxSize textSize = dc.GetTextExtent(text);
    int textX = (width - textSize.GetWidth()) / 2;
    int textY = y + 5;
    
    dc.DrawText(text, textX, textY);
}

// === Visibility Calculations ===

int VirtualizedChatWidget::GetFirstVisibleMessageIndex() const {
    if (m_layouts.empty()) return -1;
    
    int low = 0;
    int high = m_layouts.size() - 1;
    
    while (low < high) {
        int mid = (low + high) / 2;
        if (m_layouts[mid].yPosition + m_layouts[mid].height < m_scrollPosition) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    
    return low;
}

int VirtualizedChatWidget::GetLastVisibleMessageIndex() const {
    if (m_layouts.empty()) return -1;
    
    int viewHeight = GetClientSize().GetHeight();
    int bottomY = m_scrollPosition + viewHeight;
    
    int low = 0;
    int high = m_layouts.size() - 1;
    
    while (low < high) {
        int mid = (low + high + 1) / 2;
        if (m_layouts[mid].yPosition <= bottomY) {
            low = mid;
        } else {
            high = mid - 1;
        }
    }
    
    return low;
}

int VirtualizedChatWidget::GetTotalVirtualHeight() const {
    return m_totalHeight;
}

// === Scroll Management ===

void VirtualizedChatWidget::UpdateScrollPosition(int newPos) {
    int viewHeight = GetClientSize().GetHeight();
    int maxScroll = std::max(0, m_totalHeight - viewHeight);
    
    m_scrollPosition = std::max(0, std::min(newPos, maxScroll));
    m_wasAtBottom = IsAtBottom();
}

void VirtualizedChatWidget::EnsureScrollInBounds() {
    UpdateScrollPosition(m_scrollPosition);
}

void VirtualizedChatWidget::CheckAndTriggerLoadMore() {
    if (m_isLoadingHistory || m_allHistoryLoaded) {
        return;
    }
    
    // Cooldown check
    wxLongLong now = wxGetLocalTimeMillis();
    if (now - m_lastLoadTime < LOAD_COOLDOWN_MS) {
        return;
    }
    
    if (m_scrollPosition < LOAD_MORE_THRESHOLD) {
        if (m_loadMoreCallback && !m_messages.empty()) {
            m_isLoadingHistory = true;
            m_lastLoadTime = now;
            
            std::lock_guard<std::mutex> lock(m_messagesMutex);
            int64_t oldestId = m_messages.front().id;
            
            m_loadMoreCallback(oldestId);
            Refresh();
        }
    }
}

void VirtualizedChatWidget::ApplySmoothScroll(int targetDelta) {
    m_scrollVelocity = targetDelta;
    if (!m_scrollTimer.IsRunning()) {
        m_scrollTimer.Start(SCROLL_TIMER_INTERVAL);
    }
}

// === Hit Testing ===

int VirtualizedChatWidget::HitTestMessage(int y) const {
    if (m_layouts.empty()) return -1;
    
    int low = 0;
    int high = m_layouts.size() - 1;
    
    while (low <= high) {
        int mid = (low + high) / 2;
        int msgTop = m_layouts[mid].yPosition;
        int msgBottom = msgTop + m_layouts[mid].height;
        
        if (y < msgTop) {
            high = mid - 1;
        } else if (y > msgBottom) {
            low = mid + 1;
        } else {
            return mid;
        }
    }
    
    return -1;
}

ClickableArea* VirtualizedChatWidget::HitTestClickable(int x, int y) {
    int msgIndex = HitTestMessage(y);
    if (msgIndex < 0 || msgIndex >= (int)m_layouts.size()) {
        return nullptr;
    }
    
    auto& layout = m_layouts[msgIndex];
    int localY = y - layout.yPosition;
    
    for (auto& area : layout.clickableAreas) {
        if (area.rect.Contains(x, localY)) {
            return &area;
        }
    }
    
    return nullptr;
}

const ClickableArea* VirtualizedChatWidget::HitTestClickable(int x, int y) const {
    return const_cast<VirtualizedChatWidget*>(this)->HitTestClickable(x, y);
}

int VirtualizedChatWidget::HitTestCharacter(int x, int y, int& charIndex) const {
    int msgIndex = HitTestMessage(y);
    charIndex = 0;
    
    if (msgIndex < 0 || msgIndex >= (int)m_messages.size()) {
        return -1;
    }
    
    // Approximate character position based on x coordinate
    // This is a simplified version - for accurate positioning we'd need
    // to track character positions during layout
    const auto& msg = m_messages[msgIndex];
    if (msg.text.IsEmpty()) {
        charIndex = 0;
        return msgIndex;
    }
    
    wxClientDC dc(const_cast<VirtualizedChatWidget*>(this));
    dc.SetFont(m_config.messageFont);
    
    int textStartX = m_config.horizontalPadding + m_config.timestampWidth + m_config.usernameWidth + 10;
    int relX = x - textStartX;
    
    if (relX <= 0) {
        charIndex = 0;
    } else {
        // Binary search for character position
        int low = 0, high = msg.text.length();
        while (low < high) {
            int mid = (low + high) / 2;
            wxSize size = dc.GetTextExtent(msg.text.Left(mid));
            if (size.GetWidth() < relX) {
                low = mid + 1;
            } else {
                high = mid;
            }
        }
        charIndex = low;
    }
    
    return msgIndex;
}

// === Selection ===

void VirtualizedChatWidget::UpdateSelection(const wxPoint& start, const wxPoint& end) {
    int startChar, endChar;
    m_selectionStartMsg = HitTestCharacter(start.x, start.y + m_scrollPosition, startChar);
    m_selectionEndMsg = HitTestCharacter(end.x, end.y + m_scrollPosition, endChar);
    m_selectionStartChar = startChar;
    m_selectionEndChar = endChar;
    
    m_hasSelection = (m_selectionStartMsg >= 0 && m_selectionEndMsg >= 0);
}

wxString VirtualizedChatWidget::GetTextInRange(int startMsg, int startChar, int endMsg, int endChar) const {
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    
    if (startMsg < 0 || endMsg < 0 || startMsg >= (int)m_messages.size()) {
        return wxEmptyString;
    }
    
    if (startMsg > endMsg) {
        std::swap(startMsg, endMsg);
        std::swap(startChar, endChar);
    }
    
    wxString result;
    
    for (int i = startMsg; i <= endMsg && i < (int)m_messages.size(); i++) {
        const auto& msg = m_messages[i];
        
        if (i == startMsg && i == endMsg) {
            // Single message selection
            if (startChar >= 0 && endChar >= 0 && startChar < (int)msg.text.length()) {
                result = msg.text.Mid(startChar, endChar - startChar);
            } else {
                result = msg.text;
            }
        } else if (i == startMsg) {
            if (startChar >= 0 && startChar < (int)msg.text.length()) {
                result = msg.text.Mid(startChar);
            } else {
                result = msg.text;
            }
        } else if (i == endMsg) {
            if (!result.IsEmpty()) result += "\n";
            if (endChar >= 0 && endChar <= (int)msg.text.length()) {
                result += msg.text.Left(endChar);
            } else {
                result += msg.text;
            }
        } else {
            if (!result.IsEmpty()) result += "\n";
            result += msg.text;
        }
    }
    
    return result;
}

// === Sorting ===

void VirtualizedChatWidget::SortMessages() {
    if (m_messages.size() <= 1) return;
    
    // Check if already sorted
    bool sorted = true;
    for (size_t i = 1; i < m_messages.size(); i++) {
        if (m_messages[i-1].id > m_messages[i].id) {
            sorted = false;
            break;
        }
    }
    if (sorted) return;
    
    // Create indexed list
    std::vector<std::pair<size_t, int64_t>> indexed;
    indexed.reserve(m_messages.size());
    for (size_t i = 0; i < m_messages.size(); i++) {
        indexed.push_back({i, m_messages[i].id});
    }
    
    std::sort(indexed.begin(), indexed.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });
    
    // Reorder
    std::vector<MessageInfo> sortedMessages;
    std::vector<MessageLayout> sortedLayouts;
    sortedMessages.reserve(m_messages.size());
    sortedLayouts.reserve(m_layouts.size());
    
    for (const auto& pair : indexed) {
        sortedMessages.push_back(std::move(m_messages[pair.first]));
        if (pair.first < m_layouts.size()) {
            sortedLayouts.push_back(std::move(m_layouts[pair.first]));
        } else {
            sortedLayouts.push_back(MessageLayout());
        }
    }
    
    m_messages = std::move(sortedMessages);
    m_layouts = std::move(sortedLayouts);
    
    RebuildIndex();
}

void VirtualizedChatWidget::RebuildIndex() {
    m_messageIdToIndex.clear();
    m_fileIdToIndex.clear();
    
    for (size_t i = 0; i < m_messages.size(); i++) {
        if (m_messages[i].id != 0) {
            m_messageIdToIndex[m_messages[i].id] = i;
        }
        if (m_messages[i].mediaFileId != 0) {
            m_fileIdToIndex[m_messages[i].mediaFileId] = i;
        }
    }
}

// === Helpers ===

wxString VirtualizedChatWidget::FormatTimestamp(int64_t unixTime) const {
    if (unixTime == 0) return "--:--:--";
    
    time_t t = static_cast<time_t>(unixTime);
    wxDateTime dt(t);
    
    return dt.Format("%H:%M:%S");
}

wxString VirtualizedChatWidget::FormatSmartTimestamp(int64_t unixTime) const {
    if (unixTime <= 0) return wxEmptyString;
    
    time_t t = static_cast<time_t>(unixTime);
    wxDateTime dt(t);
    wxDateTime now = wxDateTime::Now();
    wxDateTime today = now.GetDateOnly();
    wxDateTime yesterday = today - wxDateSpan::Day();
    wxDateTime msgDate = dt.GetDateOnly();
    
    wxString timeStr = dt.Format("%H:%M:%S");
    
    if (msgDate == today) {
        return timeStr;
    } else if (msgDate == yesterday) {
        return "Yesterday " + timeStr;
    } else if (msgDate > today - wxDateSpan::Week()) {
        return dt.Format("%a ") + timeStr;
    } else {
        return dt.Format("%b %d ") + timeStr;
    }
}

bool VirtualizedChatWidget::NeedsDateSeparator(size_t index) const {
    if (index == 0) return true;
    if (index >= m_messages.size()) return false;
    
    time_t t1 = static_cast<time_t>(m_messages[index - 1].date);
    time_t t2 = static_cast<time_t>(m_messages[index].date);
    
    wxDateTime dt1(t1);
    wxDateTime dt2(t2);
    
    return dt1.GetDateOnly() != dt2.GetDateOnly();
}

wxString VirtualizedChatWidget::GetDateSeparatorText(int64_t unixTime) const {
    if (unixTime == 0) return "Unknown Date";
    
    time_t t = static_cast<time_t>(unixTime);
    wxDateTime dt(t);
    wxDateTime today = wxDateTime::Now().GetDateOnly();
    wxDateTime yesterday = today - wxDateSpan::Day();
    wxDateTime msgDate = dt.GetDateOnly();
    
    if (msgDate == today) {
        return "Today";
    } else if (msgDate == yesterday) {
        return "Yesterday";
    } else {
        return dt.Format("%B %d, %Y");
    }
}

bool VirtualizedChatWidget::ShouldGroupWithPrevious(size_t index) const {
    if (index == 0 || index >= m_messages.size()) return false;
    
    const auto& prev = m_messages[index - 1];
    const auto& curr = m_messages[index];
    
    // Different sender
    if (prev.senderName != curr.senderName) return false;
    
    // Different day
    if (NeedsDateSeparator(index)) return false;
    
    // Time difference too large
    int64_t timeDiff = curr.date - prev.date;
    if (timeDiff > m_config.messageGroupTimeWindow) return false;
    
    return true;
}

wxString VirtualizedChatWidget::GetMediaEmoji(MediaType type) const {
    switch (type) {
        case MediaType::Photo: return wxString::FromUTF8("\xF0\x9F\x93\xB7");      // 📷
        case MediaType::Video: return wxString::FromUTF8("\xF0\x9F\x8E\xAC");      // 🎬
        case MediaType::Sticker: return wxString::FromUTF8("\xF0\x9F\x8F\xB7");    // 🏷️
        case MediaType::GIF: return wxString::FromUTF8("\xF0\x9F\x8E\x9E");        // 🎞️
        case MediaType::Voice: return wxString::FromUTF8("\xF0\x9F\x8E\xA4");      // 🎤
        case MediaType::VideoNote: return wxString::FromUTF8("\xF0\x9F\x8E\xA5");  // 🎥
        case MediaType::File: return wxString::FromUTF8("\xF0\x9F\x93\x8E");       // 📎
        default: return wxString::FromUTF8("\xF0\x9F\x93\x81");                    // 📁
    }
}

wxString VirtualizedChatWidget::GenerateAsciiWaveform(const std::vector<uint8_t>& waveformData, int targetLength) const {
    if (waveformData.empty()) return wxEmptyString;
    
    // TDLib waveform is 5-bit values packed into bytes
    std::vector<int> values;
    int bitPos = 0;
    
    while (bitPos + 5 <= (int)waveformData.size() * 8) {
        int byteIndex = bitPos / 8;
        int bitOffset = bitPos % 8;
        
        int value = 0;
        if (bitOffset <= 3) {
            value = (waveformData[byteIndex] >> bitOffset) & 0x1F;
        } else {
            value = ((waveformData[byteIndex] >> bitOffset) |
                    ((byteIndex + 1 < (int)waveformData.size() ? waveformData[byteIndex + 1] : 0) << (8 - bitOffset))) & 0x1F;
        }
        values.push_back(value);
        bitPos += 5;
    }
    
    if (values.empty()) return wxEmptyString;
    
    // Resample to target length
    std::vector<int> resampled(targetLength);
    for (int i = 0; i < targetLength; i++) {
        int srcIdx = i * values.size() / targetLength;
        if (srcIdx >= (int)values.size()) srcIdx = values.size() - 1;
        resampled[i] = values[srcIdx];
    }
    
    // Convert to block characters
    static const wchar_t* blocks[] = {
        L"\u2581", L"\u2582", L"\u2583", L"\u2584",
        L"\u2585", L"\u2586", L"\u2587", L"\u2588"
    };
    
    wxString result;
    for (int val : resampled) {
        int blockIdx = val * 7 / 31;  // Map 0-31 to 0-7
        if (blockIdx > 7) blockIdx = 7;
        result += wxString(blocks[blockIdx]);
    }
    
    return result;
}

MediaInfo VirtualizedChatWidget::BuildMediaInfo(const MessageInfo& msg) const {
    MediaInfo info;
    info.fileId = msg.mediaFileId;
    info.localPath = msg.mediaLocalPath;
    info.fileName = msg.mediaFileName;
    info.caption = msg.mediaCaption;
    info.thumbnailFileId = msg.mediaThumbnailFileId;
    info.thumbnailPath = msg.mediaThumbnailPath;
    info.duration = msg.mediaDuration;
    info.waveform = msg.mediaWaveform;
    
    if (msg.hasPhoto) info.type = MediaType::Photo;
    else if (msg.hasVideo) info.type = MediaType::Video;
    else if (msg.hasSticker) info.type = MediaType::Sticker;
    else if (msg.hasAnimation) info.type = MediaType::GIF;
    else if (msg.hasVoice) info.type = MediaType::Voice;
    else if (msg.hasVideoNote) info.type = MediaType::VideoNote;
    else if (msg.hasDocument) info.type = MediaType::File;
    
    if (msg.mediaFileSize > 0) {
        info.fileSize = wxString::Format("%lld bytes", msg.mediaFileSize);
    }
    
    return info;
}

// === Event Handlers ===

void VirtualizedChatWidget::OnSize(wxSizeEvent& event) {
    event.Skip();
    
    int newWidth = GetClientSize().GetWidth();
    
    if (newWidth != m_lastLayoutWidth && newWidth > 100) {
        bool wasBottom = IsAtBottom();
        RecalculateAllLayouts();
        
        if (wasBottom) {
            ScrollToBottom();
        } else {
            EnsureScrollInBounds();
        }
    }
    
    Refresh();
}

void VirtualizedChatWidget::OnMouseWheel(wxMouseEvent& event) {
    int rotation = event.GetWheelRotation();
    int delta = event.GetWheelDelta();
    
    if (delta == 0) delta = 120;
    
    int lines = -(rotation / delta) * m_config.scrollSpeed;
    int lineHeight = m_config.messageFont.GetPixelSize().GetHeight() + m_config.lineSpacing;
    if (lineHeight < 10) lineHeight = 18;
    
    int oldScrollPos = m_scrollPosition;
    ScrollByPixels(lines * lineHeight);
    
    if (m_scrollPosition < oldScrollPos) {
        CheckAndTriggerLoadMore();
    }
    
    m_wasAtBottom = IsAtBottom();
    
    if (IsAtBottom()) {
        HideNewMessageIndicator();
    }
}

void VirtualizedChatWidget::OnMouseDown(wxMouseEvent& event) {
    SetFocus();
    
    int x = event.GetX();
    int y = event.GetY() + m_scrollPosition;
    
    // Check for clickable area
    auto* clickable = HitTestClickable(x, y);
    if (clickable) {
        switch (clickable->type) {
            case ClickableArea::Type::Link:
            case ClickableArea::Type::Mention:
                if (m_linkClickCallback) {
                    m_linkClickCallback(clickable->data);
                } else {
                    wxLaunchDefaultBrowser(clickable->data);
                }
                return;
                
            case ClickableArea::Type::Media:
                if (m_mediaClickCallback) {
                    m_mediaClickCallback(clickable->mediaInfo);
                } else {
                    ShowMediaPopup(clickable->mediaInfo, ClientToScreen(event.GetPosition()), 
                                  GetScreenRect().GetBottom());
                }
                return;
                
            default:
                break;
        }
    }
    
    // Hide media popup if clicking elsewhere
    HideMediaPopup();
    
    // Start text selection
    m_isSelecting = true;
    m_selectionAnchor = event.GetPosition();
    m_selectionEnd = m_selectionAnchor;
    
    CaptureMouse();
    event.Skip();
}

void VirtualizedChatWidget::OnMouseUp(wxMouseEvent& event) {
    if (HasCapture()) {
        ReleaseMouse();
    }
    
    if (m_isSelecting) {
        // Finalize selection
        wxPoint dist = event.GetPosition() - m_selectionAnchor;
        if (abs(dist.x) > MIN_SELECTION_DISTANCE || abs(dist.y) > MIN_SELECTION_DISTANCE) {
            UpdateSelection(m_selectionAnchor, event.GetPosition());
        }
    }
    
    m_isSelecting = false;
    event.Skip();
}

void VirtualizedChatWidget::OnMouseMove(wxMouseEvent& event) {
    static wxLongLong s_lastProcessTime = 0;
    wxLongLong now = wxGetLocalTimeMillis();
    if (now - s_lastProcessTime < 30) {
        event.Skip();
        return;
    }
    s_lastProcessTime = now;
    
    int x = event.GetX();
    int y = event.GetY() + m_scrollPosition;
    
    // Update hover state
    int newHoverIndex = HitTestMessage(y);
    if (newHoverIndex != m_hoverMessageIndex) {
        m_hoverMessageIndex = newHoverIndex;
    }
    
    // Check for clickable hover
    auto* clickable = HitTestClickable(x, y);
    wxStockCursor newCursor = wxCURSOR_IBEAM;
    wxString tooltip;
    
    if (clickable) {
        newCursor = wxCURSOR_HAND;
        
        switch (clickable->type) {
            case ClickableArea::Type::Link:
                tooltip = clickable->data;
                break;
            case ClickableArea::Type::Media:
                tooltip = clickable->mediaInfo.fileName.IsEmpty() ? "Click to view" : clickable->mediaInfo.fileName;
                break;
            case ClickableArea::Type::Mention:
                tooltip = clickable->data;
                break;
            case ClickableArea::Type::Edit:
                tooltip = "Click to see original message";
                break;
            default:
                break;
        }
    }
    
    if (newCursor != m_currentCursor) {
        m_currentCursor = newCursor;
        SetCursor(wxCursor(newCursor));
    }
    
    if (m_hoverClickable != clickable) {
        m_hoverClickable = clickable;
        if (tooltip.IsEmpty()) {
            UnsetToolTip();
        } else {
            SetToolTip(tooltip);
        }
    }
    
    // Handle selection drag
    if (m_isSelecting) {
        m_selectionEnd = event.GetPosition();
        UpdateSelection(m_selectionAnchor, m_selectionEnd);
        Refresh();
    }
    
    event.Skip();
}

void VirtualizedChatWidget::OnMouseLeave(wxMouseEvent& event) {
    m_hoverMessageIndex = -1;
    m_hoverClickable = nullptr;
    m_currentCursor = wxCURSOR_ARROW;
    SetCursor(wxCursor(wxCURSOR_DEFAULT));
    SetToolTip(NULL);
    event.Skip();
}

void VirtualizedChatWidget::OnRightDown(wxMouseEvent& event) {
    m_contextMenuPos = event.GetPosition();
    
    int x = event.GetX();
    int y = event.GetY() + m_scrollPosition;
    
    // Store context for menu
    m_contextMenuLink.Clear();
    m_contextMenuMedia = MediaInfo();
    m_contextMenuMessageId = 0;
    
    auto* clickable = HitTestClickable(x, y);
    if (clickable) {
        switch (clickable->type) {
            case ClickableArea::Type::Link:
                m_contextMenuLink = clickable->data;
                break;
            case ClickableArea::Type::Media:
                m_contextMenuMedia = clickable->mediaInfo;
                break;
            default:
                break;
        }
        m_contextMenuMessageId = clickable->messageId;
    }
    
    int msgIndex = HitTestMessage(y);
    if (msgIndex >= 0 && msgIndex < (int)m_messages.size()) {
        m_contextMenuMessageId = m_messages[msgIndex].id;
    }
    
    ShowContextMenu(event.GetPosition());
}

void VirtualizedChatWidget::ShowContextMenu(const wxPoint& pos) {
    wxMenu menu;
    
    // Copy selection
    if (m_hasSelection) {
        menu.Append(wxID_COPY, "Copy\tCtrl+C");
    }
    
    // Copy link
    if (!m_contextMenuLink.IsEmpty()) {
        menu.Append(wxID_ANY, "Copy Link");
        menu.Append(wxID_ANY, "Open Link");
    }
    
    // Media options
    if (m_contextMenuMedia.fileId != 0 || !m_contextMenuMedia.localPath.IsEmpty()) {
        if (!m_contextMenuMedia.localPath.IsEmpty() && CachedFileExists(m_contextMenuMedia.localPath)) {
            menu.Append(wxID_ANY, "Open Media");
            menu.Append(wxID_ANY, "Save Media As...");
        } else {
            menu.Append(wxID_ANY, "Download Media");
        }
    }
    
    if (menu.GetMenuItemCount() == 0) {
        menu.Append(wxID_ANY, "Select All\tCtrl+A");
    }
    
    // Bind event handlers
    menu.Bind(wxEVT_MENU, [this](wxCommandEvent& evt) {
        int id = evt.GetId();
        if (id == wxID_COPY) {
            wxString text = GetSelectedText();
            if (!text.IsEmpty()) {
                CopyToClipboard(text);
            }
        }
    });
    
    PopupMenu(&menu, pos);
}

void VirtualizedChatWidget::OnKeyDown(wxKeyEvent& event) {
    int keyCode = event.GetKeyCode();
    bool ctrl = event.ControlDown();
    
    if (ctrl) {
        switch (keyCode) {
            case 'C':
            case 'c':
                if (m_hasSelection) {
                    wxString text = GetSelectedText();
                    if (!text.IsEmpty()) {
                        CopyToClipboard(text);
                    }
                }
                return;
                
            case 'A':
            case 'a':
                SelectAll();
                return;
                
            case WXK_HOME:
                UpdateScrollPosition(0);
                Refresh();
                CheckAndTriggerLoadMore();
                return;
                
            case WXK_END:
                ScrollToBottom();
                return;
        }
    }
    
    switch (keyCode) {
        case WXK_HOME:
            UpdateScrollPosition(0);
            Refresh();
            CheckAndTriggerLoadMore();
            break;
            
        case WXK_END:
            ScrollToBottom();
            break;
            
        case WXK_PAGEUP:
            ScrollByPixels(-GetClientSize().GetHeight() + 50);
            CheckAndTriggerLoadMore();
            break;
            
        case WXK_PAGEDOWN:
            ScrollByPixels(GetClientSize().GetHeight() - 50);
            break;
            
        case WXK_UP:
            ScrollByLines(-1);
            CheckAndTriggerLoadMore();
            break;
            
        case WXK_DOWN:
            ScrollByLines(1);
            break;
            
        case WXK_ESCAPE:
            ClearSelection();
            HideMediaPopup();
            break;
            
        default:
            event.Skip();
            break;
    }
}

void VirtualizedChatWidget::OnChar(wxKeyEvent& event) {
    event.Skip();
}

void VirtualizedChatWidget::OnScrollTimer(wxTimerEvent& event) {
    if (m_scrollVelocity == 0) {
        m_scrollTimer.Stop();
        return;
    }
    
    ScrollByPixels(m_scrollVelocity);
    
    // Apply friction
    m_scrollVelocity = static_cast<int>(m_scrollVelocity * m_scrollFriction);
    
    if (abs(m_scrollVelocity) < 2) {
        m_scrollVelocity = 0;
        m_scrollTimer.Stop();
    }
}