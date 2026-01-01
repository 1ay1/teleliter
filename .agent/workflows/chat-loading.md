---
description: Chat loading architecture - lazy and efficient
---

# Chat Loading Architecture

## Core Principles

1. **Lazy Loading**: Load only what's needed, when needed
2. **Thumbnails First**: Download thumbnails, not full media
3. **On-Demand Full Media**: Only download full media when user interacts
4. **50-message Window**: Initial load is small, scroll-to-load-more

## State Machine

```
IDLE → LOADING → READY
         ↓
       ERROR
```

## Flow

### 1. User Clicks Chat
```
MainFrame::OnChatSelected(chatId)
  ├── m_currentChatId = chatId
  ├── m_chatViewWidget->Clear()
  ├── m_chatViewWidget->ShowLoadingIndicator()
  └── m_telegramClient->SwitchToChat(chatId, 50)  // Only 50 messages
```

### 2. TelegramClient Loads Messages
```
TelegramClient::SwitchToChat(chatId, limit)
  ├── Close previous chat (if any)
  ├── m_currentChatId = chatId
  ├── Clear typing users
  ├── openChat(chatId)
  ├── getChatHistory(chatId, 0, 0, limit)
  │   └── On success:
  │       ├── Cache messages in m_messages[chatId]
  │       ├── SetDirty(Messages)
  │       └── NotifyUIRefresh()
  └── NO auto-download of full media!
```

### 3. UI Renders Messages
```
MainFrame::OnRefreshTimer() or OnUIRefresh()
  ├── Check dirty flags
  ├── If Messages && m_currentChatId matches:
  │   ├── messages = GetCachedMessages(m_currentChatId)
  │   ├── m_chatViewWidget->RenderMessages(messages)
  │   └── HideLoadingIndicator()
```

### 4. Lazy Media Loading (NEW)
```
Thumbnails: Download when message is rendered (small, fast)
Full Media: Download ONLY when:
  - User hovers over media (for popup preview)
  - User clicks to open externally
```

### 5. Scroll-to-Load-More
```
User scrolls to top
  ├── Detect scroll position near top
  ├── If more history available:
  │   ├── ShowTopLoadingIndicator()
  │   ├── LoadMoreMessages(oldestMessageId, 30)
  │   └── Prepend new messages
```

## Key Removals

1. ❌ Remove `AutoDownloadChatMedia()` call in OpenChatAndLoadMessages
2. ❌ Remove bulk media download in OnMessagesLoaded
3. ❌ Remove double ClearMessages() calls

## Key Additions

1. ✅ Thumbnail-only auto-download (small, ~10KB each)
2. ✅ On-hover full media download
3. ✅ Loading states for UI feedback
4. ✅ Chat switch guard to prevent stale renders

## Implementation Order

1. Remove aggressive auto-download
2. Add thumbnail-only download for visible messages
3. Consolidate ClearMessages logic
4. Add scroll-to-load-more detection
5. Add loading indicators
