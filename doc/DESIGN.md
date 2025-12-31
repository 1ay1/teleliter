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