#include "VirtualizedChatWidget.h"
#include "MainFrame.h"
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <algorithm>
#include <ctime>

wxBEGIN_EVENT_TABLE(VirtualizedChatWidget, wxPanel)
    EVT_PAINT(VirtualizedChatWidget::OnPaint)
    EVT_SIZE(VirtualizedChatWidget::OnSize)
    EVT_MOUSEWHEEL(VirtualizedChatWidget::OnMouseWheel)
    EVT_LEFT_DOWN(VirtualizedChatWidget::OnMouseDown)
    EVT_LEFT_UP(VirtualizedChatWidget::OnMouseUp)
    EVT_MOTION(VirtualizedChatWidget::OnMouseMove)
    EVT_LEAVE_WINDOW(VirtualizedChatWidget::OnMouseLeave)
    EVT_KEY_DOWN(VirtualizedChatWidget::OnKeyDown)
    EVT_TIMER(wxID_ANY, VirtualizedChatWidget::OnScrollTimer)
wxEND_EVENT_TABLE()

VirtualizedChatWidget::VirtualizedChatWidget(wxWindow* parent, MainFrame* mainFrame)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, 
              wxFULL_REPAINT_ON_RESIZE | wxWANTS_CHARS)
    , m_mainFrame(mainFrame)
    , m_scrollTimer(this)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);  // Required for buffered painting
    
    // Initialize default configuration
    m_config.timestampFont = wxFont(9, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    m_config.usernameFont = wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
    m_config.messageFont = wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    m_config.emojiFont = wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    
    // Use system colors for native look
    m_config.backgroundColor = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
    m_config.textColor = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    m_config.timestampColor = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
    m_config.ownUsernameColor = wxColour(0, 128, 0);  // Green for own messages
    m_config.otherUsernameColor = wxSystemSettings::GetColour(wxSYS_COLOUR_HOTLIGHT);
    m_config.linkColor = wxSystemSettings::GetColour(wxSYS_COLOUR_HOTLIGHT);
    m_config.mentionColor = wxColour(255, 128, 0);  // Orange for mentions
    m_config.systemMessageColor = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
    m_config.selectionColor = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT);
    m_config.dateSeparatorColor = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
    
    SetBackgroundColour(m_config.backgroundColor);
}

VirtualizedChatWidget::~VirtualizedChatWidget() {
    if (m_scrollTimer.IsRunning()) {
        m_scrollTimer.Stop();
    }
}

void VirtualizedChatWidget::AddMessage(const MessageInfo& msg) {
    bool wasBottom = IsAtBottom();
    
    {
        std::lock_guard<std::mutex> lock(m_messagesMutex);
        
        // Check for duplicate
        if (m_messageIdToIndex.count(msg.id) > 0) {
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
    }
    
    // Sort to maintain order
    SortMessages();
    
    // Recalculate layouts
    RecalculateAllLayouts();
    
    // If was at bottom, scroll to bottom
    if (wasBottom) {
        ScrollToBottom();
    }
    
    Refresh();
}

void VirtualizedChatWidget::AddMessages(const std::vector<MessageInfo>& messages) {
    if (messages.empty()) return;
    
    bool wasBottom = IsAtBottom();
    
    {
        std::lock_guard<std::mutex> lock(m_messagesMutex);
        
        for (const auto& msg : messages) {
            if (msg.id != 0 && m_messageIdToIndex.count(msg.id) > 0) {
                continue;  // Skip duplicates
            }
            
            size_t index = m_messages.size();
            m_messages.push_back(msg);
            m_layouts.push_back(MessageLayout());
            m_layouts.back().messageId = msg.id;
            m_layouts.back().needsRecalc = true;
            
            if (msg.id != 0) {
                m_messageIdToIndex[msg.id] = index;
            }
        }
    }
    
    SortMessages();
    RecalculateAllLayouts();
    
    if (wasBottom) {
        ScrollToBottom();
    }
    
    Refresh();
}

void VirtualizedChatWidget::PrependMessages(const std::vector<MessageInfo>& messages) {
    if (messages.empty()) {
        m_isLoadingHistory = false;
        return;
    }
    
    // Find the first visible message to anchor to
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
                continue;  // Skip duplicates
            }
            
            size_t index = m_messages.size();
            m_messages.push_back(msg);
            m_layouts.push_back(MessageLayout());
            m_layouts.back().messageId = msg.id;
            m_layouts.back().needsRecalc = true;
            
            if (msg.id != 0) {
                m_messageIdToIndex[msg.id] = index;
            }
            addedCount++;
        }
    }
    
    if (addedCount == 0) {
        m_isLoadingHistory = false;
        return;
    }
    
    SortMessages();
    RecalculateAllLayouts();
    
    // Restore scroll position relative to anchor message
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
    if (it == m_messageIdToIndex.end()) {
        return;
    }
    
    size_t index = it->second;
    if (index >= m_messages.size()) {
        return;
    }
    
    m_messages[index] = msg;
    m_layouts[index].needsRecalc = true;
    
    // Recalculate from this point
    RecalculateLayoutsFrom(index);
    Refresh();
}

void VirtualizedChatWidget::RemoveMessage(int64_t messageId) {
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    
    auto it = m_messageIdToIndex.find(messageId);
    if (it == m_messageIdToIndex.end()) {
        return;
    }
    
    size_t index = it->second;
    if (index >= m_messages.size()) {
        return;
    }
    
    m_messages.erase(m_messages.begin() + index);
    m_layouts.erase(m_layouts.begin() + index);
    
    // Rebuild index
    m_messageIdToIndex.clear();
    for (size_t i = 0; i < m_messages.size(); i++) {
        if (m_messages[i].id != 0) {
            m_messageIdToIndex[m_messages[i].id] = i;
        }
    }
    
    RecalculateAllLayouts();
    Refresh();
}

void VirtualizedChatWidget::ClearMessages() {
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    
    m_messages.clear();
    m_layouts.clear();
    m_messageIdToIndex.clear();
    m_totalHeight = 0;
    m_scrollPosition = 0;
    m_wasAtBottom = true;
    m_isLoadingHistory = false;
    m_allHistoryLoaded = false;
    m_dateSeparatorDays.clear();
    
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

void VirtualizedChatWidget::ScrollToBottom() {
    int viewHeight = GetClientSize().GetHeight();
    int maxScroll = std::max(0, m_totalHeight - viewHeight);
    UpdateScrollPosition(maxScroll);
    m_wasAtBottom = true;
    Refresh();
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
    ScrollByPixels(lines * lineHeight);
}

void VirtualizedChatWidget::ScrollByPixels(int pixels) {
    UpdateScrollPosition(m_scrollPosition + pixels);
    Refresh();
}

bool VirtualizedChatWidget::IsAtBottom() const {
    int viewHeight = GetClientSize().GetHeight();
    int maxScroll = std::max(0, m_totalHeight - viewHeight);
    return m_scrollPosition >= maxScroll - 10;  // 10px tolerance
}

wxString VirtualizedChatWidget::GetSelectedText() const {
    // TODO: Implement text selection
    return wxEmptyString;
}

void VirtualizedChatWidget::ClearSelection() {
    m_isSelecting = false;
    m_selectionStartMsg = -1;
    m_selectionEndMsg = -1;
    Refresh();
}

void VirtualizedChatWidget::SetConfig(const ChatRenderConfig& config) {
    m_config = config;
    SetBackgroundColour(m_config.backgroundColor);
    
    // Invalidate all layouts
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

// === Event Handlers ===

void VirtualizedChatWidget::OnPaint(wxPaintEvent& event) {
    wxAutoBufferedPaintDC dc(this);
    
    // Clear background
    dc.SetBackground(wxBrush(m_config.backgroundColor));
    dc.Clear();
    
    wxSize clientSize = GetClientSize();
    int viewHeight = clientSize.GetHeight();
    int viewWidth = clientSize.GetWidth();
    
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    
    if (m_messages.empty()) {
        // Draw placeholder text
        dc.SetFont(m_config.messageFont);
        dc.SetTextForeground(m_config.timestampColor);
        wxString placeholder = "No messages yet";
        wxSize textSize = dc.GetTextExtent(placeholder);
        dc.DrawText(placeholder, (viewWidth - textSize.GetWidth()) / 2, viewHeight / 2);
        return;
    }
    
    // Find first and last visible messages
    int firstVisible = GetFirstVisibleMessageIndex();
    int lastVisible = GetLastVisibleMessageIndex();
    
    if (firstVisible < 0) firstVisible = 0;
    if (lastVisible >= (int)m_messages.size()) lastVisible = m_messages.size() - 1;
    
    // Render loading indicator at top if loading
    if (m_isLoadingHistory && firstVisible == 0) {
        RenderLoadingIndicator(dc, 10 - m_scrollPosition, viewWidth);
    }
    
    // Render visible messages
    for (int i = firstVisible; i <= lastVisible && i < (int)m_messages.size(); i++) {
        const auto& msg = m_messages[i];
        const auto& layout = m_layouts[i];
        
        int yOffset = layout.yPosition - m_scrollPosition;
        
        // Check if we need a date separator before this message
        if (NeedsDateSeparator(i)) {
            wxString dateText = GetDateSeparatorText(msg.date);
            RenderDateSeparator(dc, dateText, yOffset - DATE_SEPARATOR_HEIGHT, viewWidth);
        }
        
        RenderMessage(dc, msg, layout, yOffset);
    }
    
    // Draw scrollbar indicator (simple custom scrollbar)
    if (m_totalHeight > viewHeight) {
        int scrollbarHeight = std::max(20, viewHeight * viewHeight / m_totalHeight);
        int scrollbarY = m_totalHeight > viewHeight 
            ? m_scrollPosition * (viewHeight - scrollbarHeight) / (m_totalHeight - viewHeight)
            : 0;
        
        dc.SetBrush(wxBrush(wxColour(128, 128, 128, 128)));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRoundedRectangle(viewWidth - 8, scrollbarY, 6, scrollbarHeight, 3);
    }
}

void VirtualizedChatWidget::OnSize(wxSizeEvent& event) {
    event.Skip();
    
    int newWidth = GetClientSize().GetWidth();
    
    // Recalculate layouts if width changed
    if (newWidth != m_lastLayoutWidth && newWidth > 0) {
        m_lastLayoutWidth = newWidth;
        
        // Invalidate all layouts
        for (auto& layout : m_layouts) {
            layout.needsRecalc = true;
        }
        
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
    if (lineHeight < 10) lineHeight = 20;  // Fallback
    
    int oldScrollPos = m_scrollPosition;
    ScrollByPixels(lines * lineHeight);
    
    // Check if we need to load more messages
    if (m_scrollPosition < oldScrollPos) {  // Scrolling up
        CheckAndTriggerLoadMore();
    }
    
    m_wasAtBottom = IsAtBottom();
}

void VirtualizedChatWidget::OnMouseDown(wxMouseEvent& event) {
    SetFocus();
    
    int x = event.GetX();
    int y = event.GetY() + m_scrollPosition;
    
    // Check for clickable area
    auto* clickable = HitTestClickable(x, y);
    if (clickable) {
        // Handle click on link/media
        if (clickable->type == MessageLayout::ClickableArea::Link) {
            if (m_linkClickCallback) {
                m_linkClickCallback(clickable->data);
            }
        } else if (clickable->type == MessageLayout::ClickableArea::Media) {
            // TODO: Handle media click
        }
        return;
    }
    
    // Start text selection
    m_isSelecting = true;
    m_selectionStart = wxPoint(x, y);
    m_selectionEnd = m_selectionStart;
    m_selectionStartMsg = HitTestMessage(y);
    
    CaptureMouse();
    event.Skip();
}

void VirtualizedChatWidget::OnMouseUp(wxMouseEvent& event) {
    if (HasCapture()) {
        ReleaseMouse();
    }
    
    m_isSelecting = false;
    event.Skip();
}

void VirtualizedChatWidget::OnMouseMove(wxMouseEvent& event) {
    int x = event.GetX();
    int y = event.GetY() + m_scrollPosition;
    
    // Update hover state
    int newHoverIndex = HitTestMessage(y);
    if (newHoverIndex != m_hoverMessageIndex) {
        m_hoverMessageIndex = newHoverIndex;
        Refresh();
    }
    
    // Check for clickable hover
    auto* clickable = HitTestClickable(x, y);
    if (clickable != m_hoverClickable) {
        m_hoverClickable = clickable;
        SetCursor(clickable ? wxCursor(wxCURSOR_HAND) : wxCursor(wxCURSOR_IBEAM));
        Refresh();
    }
    
    // Handle selection drag
    if (m_isSelecting) {
        m_selectionEnd = wxPoint(x, y);
        m_selectionEndMsg = HitTestMessage(y);
        Refresh();
    }
    
    event.Skip();
}

void VirtualizedChatWidget::OnMouseLeave(wxMouseEvent& event) {
    m_hoverMessageIndex = -1;
    m_hoverClickable = nullptr;
    SetCursor(wxCursor(wxCURSOR_DEFAULT));
    Refresh();
    event.Skip();
}

void VirtualizedChatWidget::OnKeyDown(wxKeyEvent& event) {
    int keyCode = event.GetKeyCode();
    
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
            ScrollByPixels(-GetClientSize().GetHeight());
            CheckAndTriggerLoadMore();
            break;
            
        case WXK_PAGEDOWN:
            ScrollByPixels(GetClientSize().GetHeight());
            break;
            
        case WXK_UP:
            ScrollByLines(-1);
            CheckAndTriggerLoadMore();
            break;
            
        case WXK_DOWN:
            ScrollByLines(1);
            break;
            
        default:
            event.Skip();
            break;
    }
}

void VirtualizedChatWidget::OnScrollTimer(wxTimerEvent& event) {
    if (m_scrollVelocity == 0) {
        m_scrollTimer.Stop();
        return;
    }
    
    ScrollByPixels(m_scrollVelocity);
    
    // Apply friction
    if (m_scrollVelocity > 0) {
        m_scrollVelocity = std::max(0, m_scrollVelocity - 2);
    } else {
        m_scrollVelocity = std::min(0, m_scrollVelocity + 2);
    }
    
    if (m_scrollVelocity == 0) {
        m_scrollTimer.Stop();
    }
}

// === Layout Calculation ===

void VirtualizedChatWidget::RecalculateAllLayouts() {
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    
    int availableWidth = GetClientSize().GetWidth() - 2 * m_config.horizontalPadding;
    if (availableWidth < 100) availableWidth = 400;  // Fallback
    
    int currentY = m_config.verticalPadding;
    m_dateSeparatorDays.clear();
    
    wxClientDC dc(this);
    dc.SetFont(m_config.messageFont);
    
    for (size_t i = 0; i < m_messages.size(); i++) {
        // Check for date separator
        if (NeedsDateSeparator(i)) {
            currentY += DATE_SEPARATOR_HEIGHT;
            
            // Track which days have separators
            time_t t = static_cast<time_t>(m_messages[i].date);
            int64_t day = t / 86400;
            m_dateSeparatorDays.insert(day);
        }
        
        m_layouts[i].yPosition = currentY;
        
        if (m_layouts[i].needsRecalc) {
            m_layouts[i].height = CalculateMessageHeight(m_messages[i], availableWidth);
            m_layouts[i].needsRecalc = false;
        }
        
        currentY += m_layouts[i].height + m_config.messagePadding;
    }
    
    m_totalHeight = currentY + m_config.verticalPadding;
    m_lastLayoutWidth = GetClientSize().GetWidth();
}

void VirtualizedChatWidget::RecalculateLayout(size_t index) {
    if (index >= m_layouts.size()) return;
    
    m_layouts[index].needsRecalc = true;
    RecalculateLayoutsFrom(index);
}

void VirtualizedChatWidget::RecalculateLayoutsFrom(size_t startIndex) {
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    
    if (startIndex >= m_messages.size()) return;
    
    int availableWidth = GetClientSize().GetWidth() - 2 * m_config.horizontalPadding;
    if (availableWidth < 100) availableWidth = 400;
    
    wxClientDC dc(this);
    dc.SetFont(m_config.messageFont);
    
    int currentY = (startIndex > 0) 
        ? m_layouts[startIndex - 1].yPosition + m_layouts[startIndex - 1].height + m_config.messagePadding
        : m_config.verticalPadding;
    
    for (size_t i = startIndex; i < m_messages.size(); i++) {
        if (NeedsDateSeparator(i)) {
            currentY += DATE_SEPARATOR_HEIGHT;
        }
        
        m_layouts[i].yPosition = currentY;
        
        if (m_layouts[i].needsRecalc) {
            m_layouts[i].height = CalculateMessageHeight(m_messages[i], availableWidth);
            m_layouts[i].needsRecalc = false;
        }
        
        currentY += m_layouts[i].height + m_config.messagePadding;
    }
    
    m_totalHeight = currentY + m_config.verticalPadding;
}

int VirtualizedChatWidget::CalculateMessageHeight(const MessageInfo& msg, int availableWidth) {
    wxClientDC dc(this);
    
    // Timestamp width + username width + padding
    int textAreaWidth = availableWidth - m_config.timestampWidth - m_config.usernameWidth - 20;
    if (textAreaWidth < 100) textAreaWidth = 100;
    
    dc.SetFont(m_config.messageFont);
    
    // Calculate text height
    std::vector<wxString> lines = WrapText(msg.text, dc, textAreaWidth);
    int lineHeight = dc.GetFontMetrics().height + m_config.lineSpacing;
    int textHeight = std::max(1, (int)lines.size()) * lineHeight;
    
    // Add height for media if present
    int mediaHeight = 0;
    if (msg.hasPhoto || msg.hasVideo || msg.hasSticker || msg.hasAnimation) {
        mediaHeight = 60;  // Placeholder height for media
    } else if (msg.hasVoice || msg.hasVideoNote) {
        mediaHeight = 40;
    } else if (msg.hasDocument) {
        mediaHeight = 30;
    }
    
    // Add height for reactions if present
    int reactionsHeight = 0;
    if (!msg.reactions.empty()) {
        reactionsHeight = 25;
    }
    
    return textHeight + mediaHeight + reactionsHeight + 2 * m_config.verticalPadding;
}

std::vector<wxString> VirtualizedChatWidget::WrapText(const wxString& text, wxDC& dc, int maxWidth) {
    std::vector<wxString> result;
    
    if (text.IsEmpty()) {
        return result;
    }
    
    // Split by newlines first
    wxArrayString paragraphs = wxSplit(text, '\n');
    
    for (const auto& para : paragraphs) {
        if (para.IsEmpty()) {
            result.push_back(wxEmptyString);
            continue;
        }
        
        wxString remaining = para;
        
        while (!remaining.IsEmpty()) {
            wxSize extent = dc.GetTextExtent(remaining);
            
            if (extent.GetWidth() <= maxWidth) {
                result.push_back(remaining);
                break;
            }
            
            // Binary search for break point
            size_t low = 1;
            size_t high = remaining.length();
            size_t breakPoint = high;
            
            while (low < high) {
                size_t mid = (low + high + 1) / 2;
                wxSize testExtent = dc.GetTextExtent(remaining.Left(mid));
                
                if (testExtent.GetWidth() <= maxWidth) {
                    low = mid;
                } else {
                    high = mid - 1;
                }
            }
            
            breakPoint = low;
            
            // Try to break at word boundary
            size_t wordBreak = remaining.rfind(' ', breakPoint);
            if (wordBreak != wxString::npos && wordBreak > 0) {
                breakPoint = wordBreak;
            }
            
            result.push_back(remaining.Left(breakPoint));
            remaining = remaining.Mid(breakPoint);
            
            // Skip leading space on next line
            if (!remaining.IsEmpty() && remaining[0] == ' ') {
                remaining = remaining.Mid(1);
            }
        }
    }
    
    return result;
}

// === Rendering ===

void VirtualizedChatWidget::RenderMessage(wxDC& dc, const MessageInfo& msg, 
                                          const MessageLayout& layout, int yOffset) {
    int x = m_config.horizontalPadding;
    int y = yOffset + m_config.verticalPadding;
    
    // Render timestamp
    wxString timestamp = FormatTimestamp(msg.date);
    RenderTimestamp(dc, timestamp, x, y);
    x += m_config.timestampWidth;
    
    // Render username
    RenderUsername(dc, msg.senderName, msg.isOutgoing, x, y);
    x += m_config.usernameWidth + 10;
    
    // Render message text
    int textAreaWidth = GetClientSize().GetWidth() - x - m_config.horizontalPadding;
    if (textAreaWidth < 100) textAreaWidth = 100;
    
    dc.SetFont(m_config.messageFont);
    std::vector<wxString> lines = WrapText(msg.text, dc, textAreaWidth);
    RenderMessageText(dc, lines, x, y);
    
    // Render media placeholder if present
    int currentY = y + lines.size() * (dc.GetFontMetrics().height + m_config.lineSpacing);
    
    if (msg.hasPhoto || msg.hasVideo || msg.hasSticker || msg.hasAnimation ||
        msg.hasVoice || msg.hasVideoNote || msg.hasDocument) {
        MediaInfo media;
        media.fileId = msg.mediaFileId;
        media.localPath = msg.mediaLocalPath;
        if (msg.hasPhoto) media.type = MediaType::Photo;
        else if (msg.hasVideo) media.type = MediaType::Video;
        else if (msg.hasSticker) media.type = MediaType::Sticker;
        else if (msg.hasAnimation) media.type = MediaType::GIF;
        else if (msg.hasVoice) media.type = MediaType::Voice;
        else if (msg.hasVideoNote) media.type = MediaType::VideoNote;
        else if (msg.hasDocument) media.type = MediaType::File;
        
        RenderMediaPlaceholder(dc, media, x, currentY, textAreaWidth);
    }
}

void VirtualizedChatWidget::RenderTimestamp(wxDC& dc, const wxString& timestamp, int x, int y) {
    dc.SetFont(m_config.timestampFont);
    dc.SetTextForeground(m_config.timestampColor);
    dc.DrawText(timestamp, x, y);
}

void VirtualizedChatWidget::RenderUsername(wxDC& dc, const wxString& username, bool isOwn, int x, int y) {
    dc.SetFont(m_config.usernameFont);
    dc.SetTextForeground(isOwn ? m_config.ownUsernameColor : m_config.otherUsernameColor);
    
    wxString displayName = username.IsEmpty() ? "Unknown" : username;
    
    // Truncate if too long
    wxSize extent = dc.GetTextExtent(displayName);
    if (extent.GetWidth() > m_config.usernameWidth - 10) {
        while (displayName.length() > 3 && dc.GetTextExtent(displayName + "...").GetWidth() > m_config.usernameWidth - 10) {
            displayName = displayName.Left(displayName.length() - 1);
        }
        displayName += "...";
    }
    
    dc.DrawText(displayName, x, y);
}

void VirtualizedChatWidget::RenderMessageText(wxDC& dc, const std::vector<wxString>& lines, int x, int y) {
    dc.SetFont(m_config.messageFont);
    dc.SetTextForeground(m_config.textColor);
    
    int lineHeight = dc.GetFontMetrics().height + m_config.lineSpacing;
    
    for (size_t i = 0; i < lines.size(); i++) {
        dc.DrawText(lines[i], x, y + i * lineHeight);
    }
}

void VirtualizedChatWidget::RenderDateSeparator(wxDC& dc, const wxString& dateText, int y, int width) {
    dc.SetFont(m_config.timestampFont);
    dc.SetTextForeground(m_config.dateSeparatorColor);
    
    wxSize textSize = dc.GetTextExtent(dateText);
    int textX = (width - textSize.GetWidth()) / 2;
    int textY = y + (DATE_SEPARATOR_HEIGHT - textSize.GetHeight()) / 2;
    
    // Draw lines on either side
    int lineY = y + DATE_SEPARATOR_HEIGHT / 2;
    dc.SetPen(wxPen(m_config.dateSeparatorColor, 1, wxPENSTYLE_SOLID));
    
    dc.DrawLine(m_config.horizontalPadding, lineY, textX - 10, lineY);
    dc.DrawLine(textX + textSize.GetWidth() + 10, lineY, width - m_config.horizontalPadding, lineY);
    
    dc.DrawText(dateText, textX, textY);
}

void VirtualizedChatWidget::RenderMediaPlaceholder(wxDC& dc, const MediaInfo& media, int x, int y, int width) {
    wxString emoji;
    wxString label;
    
    switch (media.type) {
        case MediaType::Photo: emoji = wxString::FromUTF8("\xF0\x9F\x93\xB7"); label = "Photo"; break;
        case MediaType::Video: emoji = wxString::FromUTF8("\xF0\x9F\x8E\xAC"); label = "Video"; break;
        case MediaType::Sticker: emoji = wxString::FromUTF8("\xF0\x9F\x8F\xB7"); label = "Sticker"; break;
        case MediaType::GIF: emoji = wxString::FromUTF8("\xF0\x9F\x8E\x9E"); label = "GIF"; break;
        case MediaType::Voice: emoji = wxString::FromUTF8("\xF0\x9F\x8E\xA4"); label = "Voice"; break;
        case MediaType::VideoNote: emoji = wxString::FromUTF8("\xF0\x9F\x8E\xA5"); label = "Video Note"; break;
        case MediaType::File: emoji = wxString::FromUTF8("\xF0\x9F\x93\x8E"); label = "File"; break;
        default: emoji = wxString::FromUTF8("\xF0\x9F\x93\x81"); label = "Media"; break;
    }
    
    dc.SetFont(m_config.messageFont);
    dc.SetTextForeground(m_config.linkColor);
    dc.DrawText(emoji + " [" + label + "]", x, y);
}

void VirtualizedChatWidget::RenderLoadingIndicator(wxDC& dc, int y, int width) {
    dc.SetFont(m_config.messageFont);
    dc.SetTextForeground(m_config.timestampColor);
    
    wxString text = "Loading older messages...";
    wxSize textSize = dc.GetTextExtent(text);
    int textX = (width - textSize.GetWidth()) / 2;
    
    dc.DrawText(text, textX, y);
}

// === Visibility Calculations ===

int VirtualizedChatWidget::GetFirstVisibleMessageIndex() const {
    if (m_layouts.empty()) return -1;
    
    // Binary search for first visible message
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
    
    // Binary search for last visible message
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
    
    // Check if near the top
    if (m_scrollPosition < LOAD_MORE_THRESHOLD) {
        if (m_loadMoreCallback && !m_messages.empty()) {
            m_isLoadingHistory = true;
            
            std::lock_guard<std::mutex> lock(m_messagesMutex);
            int64_t oldestId = m_messages.front().id;
            
            m_loadMoreCallback(oldestId);
            Refresh();
        }
    }
}

// === Hit Testing ===

int VirtualizedChatWidget::HitTestMessage(int y) const {
    if (m_layouts.empty()) return -1;
    
    // Binary search
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

MessageLayout::ClickableArea* VirtualizedChatWidget::HitTestClickable(int x, int y) {
    int msgIndex = HitTestMessage(y);
    if (msgIndex < 0 || msgIndex >= (int)m_layouts.size()) {
        return nullptr;
    }
    
    auto& layout = m_layouts[msgIndex];
    for (auto& area : layout.clickableAreas) {
        if (area.rect.Contains(x, y - layout.yPosition)) {
            return &area;
        }
    }
    
    return nullptr;
}

// === Sorting ===

void VirtualizedChatWidget::SortMessages() {
    // Sort by message ID (chronological order)
    std::vector<std::pair<size_t, int64_t>> indexed;
    indexed.reserve(m_messages.size());
    for (size_t i = 0; i < m_messages.size(); i++) {
        indexed.push_back({i, m_messages[i].id});
    }
    
    std::sort(indexed.begin(), indexed.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });
    
    // Check if already sorted
    bool needsSort = false;
    for (size_t i = 0; i < indexed.size(); i++) {
        if (indexed[i].first != i) {
            needsSort = true;
            break;
        }
    }
    
    if (!needsSort) return;
    
    // Reorder messages and layouts
    std::vector<MessageInfo> sortedMessages;
    std::vector<MessageLayout> sortedLayouts;
    sortedMessages.reserve(m_messages.size());
    sortedLayouts.reserve(m_layouts.size());
    
    for (const auto& pair : indexed) {
        sortedMessages.push_back(std::move(m_messages[pair.first]));
        sortedLayouts.push_back(std::move(m_layouts[pair.first]));
    }
    
    m_messages = std::move(sortedMessages);
    m_layouts = std::move(sortedLayouts);
    
    // Rebuild index
    m_messageIdToIndex.clear();
    for (size_t i = 0; i < m_messages.size(); i++) {
        if (m_messages[i].id != 0) {
            m_messageIdToIndex[m_messages[i].id] = i;
        }
    }
}

// === Helpers ===

wxString VirtualizedChatWidget::FormatTimestamp(int64_t unixTime) const {
    if (unixTime == 0) return "[--:--]";
    
    time_t t = static_cast<time_t>(unixTime);
    wxDateTime dt(t);
    
    return dt.Format("[%H:%M]");
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