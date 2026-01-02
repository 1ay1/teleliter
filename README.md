<div align="center">

# 📡 Teleliter

**A HexChat-style Telegram client for power users**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C++-17-blue.svg)](https://isocpp.org/)
[![TDLib](https://img.shields.io/badge/TDLib-1.8+-00ACED.svg)](https://github.com/tdlib/td)
[![wxWidgets](https://img.shields.io/badge/wxWidgets-3.2+-orange.svg)](https://www.wxwidgets.org/)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS-lightgrey.svg)](#installation)

<br>

*Text-first. Keyboard-driven. No bloat.*

<br>

[Features](#features) •
[Installation](#installation) •
[Usage](#usage) •
[Commands](#commands) •
[Philosophy](#philosophy) •
[Building](#building)

<br>

<!-- 
Screenshot placeholder - replace with actual screenshot
![Teleliter Screenshot](docs/screenshot.png)
-->

```
┌─────────────────────────────────────────────────────────────────────────┐
│ Teleliter                                                     [—][□][×]│
├──────────────┬──────────────────────────────────────────────────────────┤
│ ▼ Pinned     │ 📡 Welcome to Teleliter                                  │
│ ▼ Private    │──────────────────────────────────────────────────────────│
│   🟢 Alice   │ [14:32:01] <Alice> Hey, have you tried Teleliter?        │
│   Bob        │ [14:32:15] <You> Just installed it. Feels like IRC! 🎉   │
│   Charlie    │ [14:32:20] <Alice> Right? /me loves the keyboard nav     │
│ ▼ Groups     │ [14:32:21]        * Alice loves the keyboard nav         │
│   Dev Team   │ [14:32:45] <You> 📷 [Photo] Check this out               │
│   Anime Club │ [14:33:01] <Alice> Nice! ✓✓                              │
│ ▼ Channels   │                                                          │
│   Tech News  │──────────────────────────────────────────────────────────│
│              │ Type a message or /help for commands...              [📎]│
├──────────────┴──────────────────────────────────────────────────────────┤
│ 🟢 Connected │ Alice is typing...                           │ 00:15:32  │
└─────────────────────────────────────────────────────────────────────────┘
```

</div>

---

## ✨ Features

### 🎯 Text-First Design
- **IRC/HexChat aesthetic** — Messages displayed as clean text, not bubbles
- **Monospace font** — Perfect alignment, easy to scan
- **Inline media indicators** — `📷 [Photo]`, `🎬 [Video]`, `🎤 [Voice 0:15]`
- **Click to preview** — Minimal popup, no clutter

### ⌨️ Keyboard-Driven
- **Slash commands** — `/me`, `/clear`, `/query`, `/whois`, `/leave`
- **Quick navigation** — `Ctrl+PgUp/PgDn` to switch chats
- **Tab completion** — Complete usernames with `Tab`
- **Command history** — `↑/↓` to recall previous messages

### 🚀 Blazing Fast
- **Virtualized rendering** — Only visible messages are drawn
- **Lazy loading** — Chats and history load on-demand
- **Reactive architecture** — No UI freezes, ever
- **Native performance** — C++ with wxWidgets

### 📨 Full Telegram Support
- **All chat types** — Private, groups, supergroups, channels, bots
- **Media playback** — Photos, videos, GIFs, stickers, voice notes
- **Read receipts** — `✓` sent, `✓✓` read
- **Typing indicators** — See who's typing in real-time
- **Online status** — 🟢 Green dot for online users
- **Reactions display** — See reactions from others (read-only by design)

---

## 📦 Installation

### Arch Linux (AUR)
```bash
yay -S teleliter-git
```

### Ubuntu / Debian
```bash
# Install dependencies
sudo apt install build-essential cmake libwxgtk3.2-dev libssl-dev \
  zlib1g-dev libwebp-dev libavformat-dev libavcodec-dev libswscale-dev \
  libswresample-dev libavutil-dev libsdl2-dev librlottie-dev

# Build TDLib (see Building section)
# Then build Teleliter
git clone https://github.com/1ay1/teleliter.git
cd teleliter && mkdir build && cd build
cmake .. && make -j$(nproc)
./teleliter
```

### macOS (Homebrew)
```bash
brew install wxwidgets tdlib ffmpeg sdl2 webp rlottie
git clone https://github.com/1ay1/teleliter.git
cd teleliter && mkdir build && cd build
cmake .. && make -j$(sysctl -n hw.ncpu)
./teleliter
```

---

## 🎮 Usage

### First Launch
1. Run `./teleliter`
2. Enter your phone number (with country code: `+1234567890`)
3. Enter the verification code from Telegram
4. Start chatting!

### Navigation
| Key | Action |
|-----|--------|
| `Ctrl+PgUp` | Previous chat |
| `Ctrl+PgDn` | Next chat |
| `Ctrl+W` | Close chat |
| `F7` | Toggle member list |
| `F9` | Toggle chat list |
| `F11` | Fullscreen |
| `Ctrl+L` | Login |
| `Ctrl+E` | Preferences |

---

## 💬 Commands

```
/me <action>     Send an action message (* You does something)
/clear           Clear the chat window
/query <user>    Open a private chat with user
/whois <user>    View user information
/leave           Leave the current chat
/help            Show all available commands
```

**Example:**
```
/me is excited about Teleliter
→ * YourName is excited about Teleliter
```

---

## 🧠 Philosophy

Teleliter follows the **Unix philosophy** applied to messaging:

> *Do one thing well: Display and send messages.*

### What We Do
✅ Display all Telegram content beautifully in text  
✅ Send messages quickly with keyboard  
✅ Show reactions, edits, and read receipts  
✅ Preview media with minimal UI  

### What We Don't Do
❌ Reaction picker (use official app)  
❌ Message editing UI (use official app)  
❌ Sticker browser (use official app)  
❌ Voice recording (use official app)  

This isn't a limitation — it's a **feature**. Teleliter is for *reading* and *quick replies*, not for composing elaborate multimedia messages.

---

## 🔧 Building

### Dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| [TDLib](https://github.com/tdlib/td) | 1.8+ | Telegram API |
| [wxWidgets](https://wxwidgets.org/) | 3.2+ | GUI framework |
| [FFmpeg](https://ffmpeg.org/) | 5.0+ | Media playback |
| [SDL2](https://libsdl.org/) | 2.0+ | Audio output |
| [libwebp](https://chromium.googlesource.com/webm/libwebp) | 1.0+ | WebP stickers |
| [rlottie](https://github.com/aspect-ui/rlottie) | 0.2+ | Animated stickers |

### Building TDLib

TDLib must be built from source and installed to `~/.local`:

```bash
git clone https://github.com/tdlib/td.git
cd td && mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=~/.local ..
cmake --build . -j$(nproc)
cmake --install .
```

### Building Teleliter

```bash
git clone https://github.com/1ay1/teleliter.git
cd teleliter
mkdir build && cd build
cmake ..
make -j$(nproc)
```

---

## 📄 License

MIT License — See [LICENSE](LICENSE) for details.

---

## 🙏 Acknowledgments

- [TDLib](https://github.com/tdlib/td) — The backbone of this client
- [HexChat](https://hexchat.github.io/) — Inspiration for the UI
- [wxWidgets](https://wxwidgets.org/) — Cross-platform GUI

---

<div align="center">

**If you love keyboard-driven interfaces, give Teleliter a ⭐!**

*Made with ❤️ for power users who miss the days of IRC*

</div>