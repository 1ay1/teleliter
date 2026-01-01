# High-Performance Chat Rendering

## Overview

Teleliter uses two approaches for high-performance chat rendering:

1. **ChatViewWidget (Default)** - Uses wxRichTextCtrl with a **sliding window optimization** that limits rendered messages to 150, keeping the native HexChat look while achieving O(1) refresh performance.

2. **VirtualizedChatWidget (Experimental)** - Custom widget with O(visible) rendering complexity for maximum performance.

> **Recommendation**: Use ChatViewWidget for the authentic HexChat/IRC aesthetic. The sliding window optimization makes it fast enough for most use cases.

## ChatViewWidget Sliding Window Optimization

The `ChatViewWidget` now uses a **sliding window** approach to achieve fast performance while keeping the wxRichTextCtrl that provides the authentic HexChat look:

### How It Works

```
Total messages in memory: [msg1, msg2, msg3, ... msg500, msg501, ... msg1000]
                                              ^                      ^
                                              |______________________|
                                              Display window (150 msgs)
```

- **All messages stored in memory** (`m_messages` vector) for fast lookup
- **Only 150 messages rendered** in wxRichTextCtrl at any time
- **Window shifts** when user scrolls to edges
- **O(150) = O(1)** refresh complexity regardless of total message count

### Key Constants

```cpp
static const size_t MAX_DISPLAYED_MESSAGES = 150;
size_t m_displayWindowStart = 0;  // Start index in m_messages
size_t m_displayWindowEnd = 0;    // End index in m_messages
```

### Window Behavior

| Scenario | Window Position |
|----------|-----------------|
| Fresh chat open | Last 150 messages (most recent) |
| Scroll to bottom | Shift window to show newest |
| Scroll to top | Shift window to show older (from memory) |
| Load history | Add to memory, shift window to include |

## VirtualizedChatWidget (Experimental)

For extreme performance needs, `VirtualizedChatWidget` renders messages with true O(visible) complexity using custom wxDC drawing. However, it lacks the native rich text feel.

## ChatViewWidget Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     ChatViewWidget                           │
│                                                              │
│  ┌──────────────────┐   ┌─────────────────────────────────┐ │
│  │ Message Storage  │   │ Display Window                  │ │
│  │ m_messages[]     │   │ m_displayWindowStart            │ │
│  │ (ALL messages)   │   │ m_displayWindowEnd              │ │
│  │                  │   │ MAX = 150 messages              │ │
│  │ Sorted by ID     │   │                                 │ │
│  └──────────────────┘   └─────────────────────────────────┘ │
│           │                         │                        │
│           ▼                         ▼                        │
│  ┌─────────────────────────────────────────────────────────┐│
│  │                 wxRichTextCtrl                           ││
│  │  Only contains messages[windowStart..windowEnd]          ││
│  │  Native text selection, copy, rich formatting            ││
│  │  HexChat/IRC aesthetic with MessageFormatter             ││
│  └─────────────────────────────────────────────────────────┘│
│                                                              │
│  ┌──────────────────┐   ┌─────────────────────────────────┐ │
│  │ AdjustWindow()   │   │ RefreshDisplay()                │ │
│  │ Shifts window    │   │ Clears & re-renders window      │ │
│  │ on scroll        │   │ O(150) = O(1) complexity        │ │
│  └──────────────────┘   └─────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

## VirtualizedChatWidget Architecture (Alternative)

```
┌─────────────────────────────────────────────────────────────┐
│                  VirtualizedChatWidget                       │
│                                                              │
│  ┌──────────────────┐   ┌─────────────────────────────────┐ │
│  │ Message Buffer   │   │ Layout Cache                    │ │
│  │ std::vector<     │   │ std::vector<MessageLayout>      │ │
│  │   MessageInfo>   │   │ - yPosition, height             │ │
│  │ All messages     │   │ - clickableAreas                │ │
│  └──────────────────┘   └─────────────────────────────────┘ │
│                                                              │
│  ┌─────────────────────────────────────────────────────────┐│
│  │                    OnPaint()                             ││
│  │  Binary search for visible range: O(log n)               ││
│  │  Render only visible: O(visible) ≈ O(20-30)              ││
│  └─────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────┘
```

## Key Data Structures

### MessageLayout

Caches layout information for each message to avoid recalculating on every paint:

```cpp
struct MessageLayout {
    int64_t messageId = 0;
    int yPosition = 0;          // Y position in virtual space
    int height = 0;             // Total height of this message
    int textHeight = 0;         // Height of text portion
    int mediaHeight = 0;        // Height of media section
    int reactionsHeight = 0;    // Height of reactions line
    bool needsRecalc = true;    // Flag to recalculate on next paint
    bool hasDateSeparator = false;  // Show date separator above
    
    // Parsed text with formatting
    std::vector<TextSegment> textSegments;  // Bold, italic, links, etc.
    std::vector<wxString> displayLines;     // Wrapped lines for rendering
    
    // Clickable areas (relative to message top)
    std::vector<ClickableArea> clickableAreas;
    
    // Media, reactions, status rects for hit testing
    bool hasMedia = false;
    MediaInfo mediaInfo;
    wxRect mediaRect;
    std::vector<std::pair<wxString, std::vector<wxString>>> reactions;
    wxRect reactionsRect;
    bool isEdited = false;
    wxRect editMarkerRect;
    MessageStatus status = MessageStatus::None;
    wxRect statusRect;
};
```

### ChatRenderConfig

Configures fonts, colors, and spacing:

```cpp
struct ChatRenderConfig {
    // Fonts
    wxFont timestampFont, usernameFont, messageFont, emojiFont;
    
    // Colors
    wxColour backgroundColor, textColor, timestampColor;
    wxColour ownUsernameColor, otherUsernameColor, linkColor;
    wxColour mentionColor, systemMessageColor, selectionColor;
    
    // Spacing
    int horizontalPadding = 10;
    int verticalPadding = 2;
    int messagePadding = 4;
    int timestampWidth = 70;
    int usernameWidth = 120;
    int lineSpacing = 2;
};
```

## Core Algorithms

### Visibility Calculation (Binary Search)

Finding visible messages uses binary search for O(log n) complexity:

```cpp
int GetFirstVisibleMessageIndex() const {
    // Binary search for first message where 
    // yPosition + height >= scrollPosition
    int low = 0, high = m_layouts.size() - 1;
    
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
```

### Paint Loop

Only visible messages are rendered:

```cpp
void OnPaint(wxPaintEvent& event) {
    wxAutoBufferedPaintDC dc(this);
    
    int firstVisible = GetFirstVisibleMessageIndex();  // O(log n)
    int lastVisible = GetLastVisibleMessageIndex();    // O(log n)
    
    // O(visible) - typically 10-30 messages on screen
    for (int i = firstVisible; i <= lastVisible; i++) {
        RenderMessage(dc, m_messages[i], m_layouts[i], yOffset);
    }
}
```

### Scroll Anchoring (History Loading)

When prepending older messages, the scroll position is anchored to maintain the user's view:

```cpp
void PrependMessages(const std::vector<MessageInfo>& messages) {
    // 1. Save anchor: find currently visible message
    int64_t anchorMessageId = m_messages[firstVisibleIndex].id;
    int anchorOffset = m_scrollPosition - m_layouts[firstVisibleIndex].yPosition;
    
    // 2. Add new messages and re-sort
    for (const auto& msg : messages) {
        m_messages.push_back(msg);
    }
    SortMessages();
    RecalculateAllLayouts();
    
    // 3. Restore scroll position relative to anchor
    auto it = m_messageIdToIndex.find(anchorMessageId);
    int newScrollPos = m_layouts[it->second].yPosition + anchorOffset;
    UpdateScrollPosition(newScrollPos);
}
```

## Lazy Loading System

### Overview

The lazy loading system loads messages on-demand as the user scrolls, rather than loading the entire history upfront.

```
User scrolls to top
        ↓
scrollPosition < LOAD_MORE_THRESHOLD (200px)
        ↓
CheckAndTriggerLoadMore()
        ↓
m_loadMoreCallback(oldestMessageId)
        ↓
MainFrame → TelegramClient::LoadMoreMessages()
        ↓
TDLib getChatHistory() API
        ↓
Messages returned → PrependMessages()
        ↓
Scroll anchor restores position (no jump)
```

### Trigger Conditions

```cpp
void CheckAndTriggerLoadMore() {
    // Don't load if already loading or all history loaded
    if (m_isLoadingHistory || m_allHistoryLoaded) return;
    
    // Trigger when within 200px of the top
    if (m_scrollPosition < LOAD_MORE_THRESHOLD) {
        m_isLoadingHistory = true;
        m_loadMoreCallback(m_messages.front().id);
    }
}
```

### State Flags

| Flag | Purpose |
|------|---------|
| `m_isLoadingHistory` | Prevents concurrent load requests |
| `m_allHistoryLoaded` | Stops loading when no more history exists |
| `m_wasAtBottom` | Tracks if auto-scroll should occur on new messages |

### Integration with TelegramClient

```cpp
// In MainFrame
m_chatWidget->SetLoadMoreCallback([this](int64_t oldestId) {
    if (m_currentChatId != 0) {
        m_telegramClient->LoadMoreMessages(m_currentChatId, oldestId, 30);
    }
});

// When TDLib returns messages
void MainFrame::HandleHistoryMessages(const std::vector<MessageInfo>& messages) {
    if (messages.empty()) {
        m_chatWidget->SetAllHistoryLoaded(true);
    } else {
        m_chatWidget->PrependMessages(messages);
    }
    m_chatWidget->SetLoadingHistory(false);
}
```

## Performance Characteristics

### ChatViewWidget (Sliding Window)

| Operation | Complexity | Notes |
|-----------|------------|-------|
| Add message | O(n log n) + O(150) | Sort + window render |
| Prepend messages | O(n log n) + O(150) | Sort + window render |
| RefreshDisplay | O(150) = O(1) | Fixed window size |
| AdjustDisplayWindow | O(150) = O(1) | Shift + re-render |
| Scroll | O(1) | Native wxRichTextCtrl |
| Text selection | O(1) | Native wxRichTextCtrl |

### VirtualizedChatWidget

| Operation | Complexity | Notes |
|-----------|------------|-------|
| Add message | O(n log n) | Sort + layout recalc |
| Prepend messages | O(n log n) | Sort + layout recalc |
| Paint | O(visible) | Typically 10-30 messages |
| Find visible range | O(log n) | Binary search |
| Scroll | O(1) | Just updates scroll position |

### Comparison

| Metric | ChatViewWidget (Window) | VirtualizedChatWidget |
|--------|------------------------|----------------------|
| Look & Feel | Native HexChat/IRC | Custom rendering |
| Text Selection | Native (excellent) | Basic (message-level) |
| RefreshDisplay | O(150) fixed | O(visible) variable |
| Memory | O(all) + O(150) buffer | O(all) + O(all) layout |
| Prepend | O(150) re-render | O(n log n) + anchor |

## Thread Safety

The widget uses a mutex to protect message/layout data:

```cpp
std::mutex m_messagesMutex;

void AddMessage(const MessageInfo& msg) {
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    // ... modify m_messages and m_layouts
}

void OnPaint(wxPaintEvent& event) {
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    // ... read m_messages and m_layouts
}
```

However, the preferred pattern is to only modify messages from the main thread (via `ReactiveRefresh()`), keeping the mutex as a safety net.

## API Reference

### Message Management

```cpp
void AddMessage(const MessageInfo& msg);
void AddMessages(const std::vector<MessageInfo>& messages);
void PrependMessages(const std::vector<MessageInfo>& messages);  // For history loading
void UpdateMessage(const MessageInfo& msg);
void RemoveMessage(int64_t messageId);
void ClearMessages();

// Lookup
const MessageInfo* GetMessageById(int64_t messageId) const;
MessageInfo* GetMessageByFileId(int32_t fileId);  // For media updates
size_t GetMessageCount() const;
```

### Scroll Control

```cpp
void ScrollToBottom();
void ForceScrollToBottom();
void ScrollToMessage(int64_t messageId);
void ScrollByLines(int lines);
void ScrollByPixels(int pixels);
bool IsAtBottom() const;
```

### Selection

```cpp
wxString GetSelectedText() const;
void ClearSelection();
void SelectAll();
```

### Callbacks

```cpp
// Called when user scrolls near top
void SetLoadMoreCallback(std::function<void(int64_t oldestMessageId)> callback);

// Called when user clicks media
void SetMediaClickCallback(std::function<void(const MediaInfo& media)> callback);

// Called when user wants to download media
void SetMediaDownloadCallback(std::function<void(int32_t fileId, int priority)> callback);

// Called when user clicks a link
void SetLinkClickCallback(std::function<void(const wxString& url)> callback);
```

### Loading State

```cpp
void SetLoadingHistory(bool loading);
bool IsLoadingHistory() const;
void SetAllHistoryLoaded(bool loaded);
bool IsAllHistoryLoaded() const;
```

### Read Status

```cpp
void SetLastReadOutboxId(int64_t messageId);
void SetReadStatus(int64_t messageId, int64_t readTime);
```

### Media Handling

```cpp
void ShowMediaPopup(const MediaInfo& info, const wxPoint& pos, int bottomBoundary);
void HideMediaPopup();
void UpdateMediaPath(int32_t fileId, const wxString& localPath);
void OnMediaDownloadComplete(int32_t fileId, const wxString& localPath);

// Pending download tracking
void AddPendingDownload(int32_t fileId);
bool HasPendingDownload(int32_t fileId) const;
void RemovePendingDownload(int32_t fileId);
void AddPendingOpen(int32_t fileId);
bool HasPendingOpen(int32_t fileId) const;
void RemovePendingOpen(int32_t fileId);
```

### UI Indicators

```cpp
void ShowUnreadMarker();
void HideUnreadMarker();
bool HasUnreadMarker() const;

void ShowNewMessageIndicator();
void HideNewMessageIndicator();
```

### Batch Updates

```cpp
void BeginBatchUpdate();  // Defer layout recalculation
void EndBatchUpdate();    // Apply pending recalculations
```

## Constants

```cpp
static const int SCROLL_TIMER_INTERVAL = 16;   // ~60fps smooth scrolling
static const int LOAD_MORE_THRESHOLD = 300;    // Pixels from top to trigger load
static const int DATE_SEPARATOR_HEIGHT = 30;   // Height of date separators
static const int MIN_SELECTION_DISTANCE = 3;   // Minimum pixels to start selection
static const int LOAD_COOLDOWN_MS = 800;       // Cooldown between load requests
```

## Usage Example

```cpp
// Create widget
auto* chatWidget = new VirtualizedChatWidget(parent, mainFrame);

// Configure callbacks
chatWidget->SetLoadMoreCallback([this](int64_t oldestId) {
    m_client->LoadMoreMessages(m_chatId, oldestId, 30);
});

chatWidget->SetMediaClickCallback([this](const MediaInfo& media) {
    ShowMediaPopup(media);
});

chatWidget->SetLinkClickCallback([](const wxString& url) {
    wxLaunchDefaultBrowser(url);
});

// Add initial messages
chatWidget->AddMessages(initialMessages);
chatWidget->ScrollToBottom();

// When history arrives from TDLib
chatWidget->PrependMessages(historyMessages);
```

## ChatViewWidget Features (Recommended)

All features work with the sliding window optimization:

- [x] **HexChat/IRC aesthetic** - authentic terminal-style formatting via MessageFormatter
- [x] **Right-aligned usernames** - `    <alice>` aligns with `      <bob>`
- [x] **Native text selection** - wxRichTextCtrl handles selection perfectly
- [x] **URL detection** - clickable links with underlines
- [x] **Rich text styling** - bold, italic, code via TDLib entities
- [x] **Media placeholders** - 📷 [Photo], 🎬 [Video], 🎤 [Voice] etc.
- [x] **ASCII waveform** - voice message visualization
- [x] **Reactions display** - emoji with user names
- [x] **Edit/status markers** - (edited), ✓, ✓✓
- [x] **Date separators** - `─────────── Today ───────────`
- [x] **Unread marker** - `─────────── New messages ───────────`
- [x] **Context menu** - Copy, Copy Link, Open Link, Save Media
- [x] **Sliding window** - O(150) refresh for 10k+ message chats

## VirtualizedChatWidget Features (Experimental)

- [x] **O(visible) rendering** - only paints visible messages
- [x] **Binary search visibility** - fast first/last visible lookup
- [x] **Scroll anchoring** - stable position during history load
- [x] **Basic text styling** - links, bold, italic
- [x] **Media placeholders** - clickable with popup
- [ ] **Native text selection** - message-level only (not character-level)
- [ ] **Rich formatting** - simpler than ChatViewWidget

## Future Improvements

### ChatViewWidget
- [ ] Smoother window transitions (fade effect when shifting)
- [ ] Predictive window positioning (shift before hitting edge)
- [ ] Inline image thumbnails in wxRichTextCtrl

### VirtualizedChatWidget
- [ ] GPU-accelerated rendering (wxGraphicsContext or custom OpenGL)
- [ ] Character-level text selection
- [ ] Inline image/thumbnail rendering
- [ ] Animated sticker/GIF playback inline

## Debugging

### Enable Verbose Logging

Add to `VirtualizedChatWidget.cpp`:

```cpp
#define VCHAT_DEBUG 1

#if VCHAT_DEBUG
#define VCHAT_LOG(fmt, ...) wxLogDebug("[VChat] " fmt, ##__VA_ARGS__)
#else
#define VCHAT_LOG(fmt, ...)
#endif
```

### Metrics to Watch

1. **Paint time**: Should be <16ms for 60fps
2. **Layout recalc time**: Acceptable up to 100ms for 1000+ messages
3. **Memory usage**: Monitor with large chat histories (10k+ messages)
4. **Scroll smoothness**: Check for frame drops during rapid scroll

### Common Issues

| Issue | Cause | Solution |
|-------|-------|----------|
| Scroll jumps after history load | Anchor not restored correctly | Check `anchorMessageId` lookup |
| Duplicate messages | Missing ID check | Ensure `m_messageIdToIndex` check |
| Layout corruption | Race condition | Verify mutex is held during access |
| Slow paint | Too many visible messages | Check visible range calculation |