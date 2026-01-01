# Teleliter Design Document

## Philosophy

Teleliter is a Telegram client that follows the **HexChat/IRC aesthetic** — text-first, minimal, and keyboard-friendly. The guiding principles are:

1. **Textual over graphical**: Prefer text representations over complex UI widgets
2. **Read-only for advanced features**: Display reactions, edits, and other TDLib features textually, but don't add UI complexity for sending them
3. **No unnecessary UI chrome**: Keep the interface clean and focused on conversation
4. **Keyboard-first**: Support power users with commands and shortcuts

## Core Design Decisions

### What We Display (Read-Only)

These TDLib features are **displayed textually** when received from others:

- **Reactions**: Shown as a compact line below messages (e.g., `👍 Alice, Bob  ❤️ Charlie`)
- **Edited messages**: Marked with `(edited)` suffix, with a service notification
- **Message deletions**: Service message noting deletion
- **Typing indicators**: Shown in status bar
- **Online status**: Green indicator 🟢 in chat list for online users
- **Read receipts**: Tick marks (✓ sent, ✓✓ read) on outgoing messages
- **Replies**: Displayed with quoted preview above the message
- **Forwards**: Attributed with "Forwarded from" header

### What We Don't Implement (Sending)

To keep the UI simple, we deliberately **do not** provide UI for:

- Sending reactions (no emoji picker, no reaction buttons)
- Editing sent messages (no edit mode)
- Deleting messages for everyone
- Replying to specific messages (no reply UI)
- Forwarding messages

Users who need these features can use the official Telegram app. Teleliter focuses on being an excellent **reader and simple sender**.

### Commands Over UI

Following IRC/HexChat tradition, functionality is exposed through slash commands rather than buttons:

```
/me <action>       - Send an action message
/clear             - Clear chat window
/query <user>      - Open private chat
/msg <user> <text> - Send private message
/whois <user>      - View user info
/leave             - Leave current chat
/help              - Show available commands
```

Commands that were considered but **intentionally not implemented** (to keep read-only philosophy):

- `/react` - Would send reactions (we only display them)
- `/edit` - Would edit messages (we only show edits from others)
- `/delete` - Would delete messages (we only show deletion notices)
- `/reply` - Would need message selection UI
- `/forward` - Would need chat selection UI

The corresponding TelegramClient API methods (`SendReaction`, `EditMessage`, `DeleteMessages`, `ForwardMessages`) were also removed to enforce this philosophy at the code level.

## Architecture

### Component Overview

```
┌─────────────────────────────────────────────────────────────┐
│                        MainFrame                             │
│  ┌──────────────┐  ┌─────────────────────────────────────┐  │
│  │ ChatList     │  │ ChatViewWidget                      │  │
│  │ Widget       │  │  ┌─────────────────────────────┐   │  │
│  │              │  │  │ ChatArea (wxRichTextCtrl)   │   │  │
│  │ - Categories │  │  │ - Message display           │   │  │
│  │ - Online     │  │  │ - Media spans               │   │  │
│  │   indicators │  │  │ - Link detection            │   │  │
│  │              │  │  └─────────────────────────────┘   │  │
│  │              │  │  ┌─────────────────────────────┐   │  │
│  │              │  │  │ InputBoxWidget              │   │  │
│  │              │  │  │ - Text input                │   │  │
│  │              │  │  │ - Command processing        │   │  │
│  │              │  │  │ - Upload button             │   │  │
│  │              │  │  └─────────────────────────────┘   │  │
│  └──────────────┘  └─────────────────────────────────────┘  │
│  ┌─────────────────────────────────────────────────────────┐│
│  │ StatusBarManager - Connection, transfers, chat info     ││
│  └─────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────┘
```

### Reactive MVC Pattern

The app uses a **poll-based reactive pattern** to avoid threading complexity:

1. **TelegramClient** (background thread):
   - Receives TDLib updates
   - Sets dirty flags (`DirtyFlag::Messages`, `DirtyFlag::ChatList`, etc.)
   - Queues data for UI consumption

2. **MainFrame::ReactiveRefresh()** (main thread, timer-driven):
   - Polls dirty flags
   - Fetches queued data
   - Updates UI components

This avoids cross-thread UI calls while maintaining responsiveness.

### Message Flow

```
TDLib Update → TelegramClient::ProcessUpdate()
                      ↓
              ConvertMessage() → MessageInfo
                      ↓
              Queue + SetDirty(DirtyFlag::Messages)
                      ↓
              [Timer fires]
                      ↓
              MainFrame::ReactiveRefresh()
                      ↓
              GetNewMessages() → Display in ChatViewWidget
```

### Media Handling

Media is displayed textually with clickable spans:

- **Photos**: `📷 [Photo]` or `📷 caption text`
- **Videos**: `🎬 [Video]` with duration
- **Files**: `📎 filename.pdf`
- **Voice**: `🎤 [Voice 0:15]` with ASCII waveform
- **Stickers**: `🏷️ [Sticker] emoji`

Clicking opens a **MediaPopup** for preview/playback. The popup is minimal — no forward/share buttons, just view and save.

### Service Messages

System events use a consistent arrow format:

```
[15:30:45]         ——▶ Available commands:
[15:30:45]              /me <action>     - Send an action message
[15:30:45]              /clear           - Clear chat window

[15:30:50]         ——▶ Alice joined the chat
[15:30:55]         ◀—— Bob left the chat
[15:31:00]         ——▶ Someone edited: "new text..."
```

## File Structure

```
src/
├── telegram/
│   ├── TelegramClient.cpp/h  - TDLib wrapper, message conversion
│   ├── Types.h               - Data structures (MessageInfo, ChatInfo, etc.)
│   └── TransferManager.cpp/h - Upload/download progress tracking
├── ui/
│   ├── MainFrame.cpp/h       - Main window, reactive refresh loop
│   ├── ChatArea.cpp/h        - Reusable rich text display
│   ├── ChatViewWidget.cpp/h  - Message rendering, media spans
│   ├── ChatListWidget.cpp/h  - Chat tree with categories
│   ├── InputBoxWidget.cpp/h  - Text input, command processing
│   ├── MessageFormatter.cpp/h - HexChat-style formatting
│   ├── StatusBarManager.cpp/h - Status bar updates
│   ├── MediaPopup.cpp/h      - Media preview popup
│   └── WelcomeChat.cpp/h     - Login flow UI
└── main.cpp                  - Entry point
```

## Performance Considerations

1. **Batch updates**: Multiple message updates are wrapped in `BeginBatchUpdate()`/`EndBatchUpdate()` to prevent flicker

2. **Double buffering**: `wxRichTextCtrl` uses double buffering for smooth rendering

3. **Coalesced refreshes**: `ScheduleRefresh()` debounces rapid update requests

4. **Lazy media loading**: Media is downloaded on-demand or with low priority for background chats

5. **Smart lazy loading**: Industry-standard pagination for chats and messages

## Lazy Loading Architecture

The app implements smart lazy loading for both chat lists and message history to maintain responsiveness and reduce memory usage.

### Chat List Lazy Loading

- **Initial load**: 30 chats loaded on startup
- **Scroll detection**: Monitors scroll position in `ChatListWidget`
- **Prefetch threshold**: Loads more when within 20% of bottom (or fewer than 20 visible chats)
- **State tracking**: `m_allChatsLoaded` and `m_isLoadingChats` flags prevent duplicate requests
- **TDLib integration**: Uses `loadChats()` with pagination, detects completion via 404 response

```
User scrolls → ChatListWidget::OnTreeScrolled()
                      ↓
              IsNearBottom() check (80% scroll position)
                      ↓
              m_loadMoreCallback() → TelegramClient::LoadMoreChats()
                      ↓
              loadChats(30) → getChats() → OnChatUpdate() for each
                      ↓
              SetDirty(ChatList) → UI refresh
```

### Message History Lazy Loading

- **Initial load**: 30 messages when opening a chat
- **Scroll detection**: Both `wxEVT_SCROLLWIN` and mouse wheel events
- **Prefetch threshold**: 20% from top (not at the very edge - smoother UX)
- **Anchor scrolling**: Maintains scroll position when prepending old messages
- **State tracking**: Per-chat `m_chatsWithAllMessagesLoaded` set, `m_isLoadingMessages` flag

```
User scrolls up → ChatViewWidget::OnScroll() / OnMouseWheel()
                      ↓
              scrollPercent < 0.20 (within top 20%)
                      ↓
              MainFrame::LoadMoreMessages(oldestId)
                      ↓
              TelegramClient::LoadMoreMessages(chatId, fromId, 30)
                      ↓
              getChatHistory() → ConvertMessage() → Queue
                      ↓
              SetDirty(Messages) → RefreshDisplay() with anchor scroll
```

### Key Design Decisions

1. **Batch sizes**: 30 items per batch balances responsiveness vs network efficiency
2. **Prefetch at 20%**: Starts loading before user hits the edge for seamless experience  
3. **Concurrent request prevention**: Atomic flags prevent duplicate loads
4. **State synchronization**: `ChatViewWidget` and `ChatListWidget` sync state with `TelegramClient`
5. **Empty response detection**: TDLib returns empty array when no more history exists

## Future Considerations

Features that could be added while maintaining the philosophy:

- **Search**: Find in current chat (textual, keyboard-driven)
- **Message history navigation**: Page up/down through history
- **Keyboard shortcuts**: More vim-style navigation
- **Themes**: Dark/light mode via system colors

Features that are **out of scope** (would complicate UI):

- Reaction picker
- Message editing UI
- Inline reply composer
- Sticker/GIF browser
- Voice message recording