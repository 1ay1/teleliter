#pragma once

#include <cstdint>
#include <variant>

namespace tl::msg {

// ─── App control ──────────────────────────────────────────────────────────────
struct Quit {};
struct Tick {};
struct Resize  { int w; int h; };
struct CycleFocus {};

// ─── Chat list navigation ─────────────────────────────────────────────────────
struct SelectChatUp {};
struct SelectChatDown {};
struct OpenSelectedChat {};
struct SearchInput   { char32_t cp; };
struct SearchBack {};
struct SearchClear {};

// ─── Composer text editing ───────────────────────────────────────────────────
struct CharIn        { char32_t cp; };
struct Backspace {};
struct DeleteWord {};
struct DeleteToStart {};
struct DeleteToEnd {};
struct CursorLeft {};
struct CursorRight {};
struct CursorHome {};
struct CursorEnd {};
struct SendComposer {};

// ─── Message list scrolling ──────────────────────────────────────────────────
struct ScrollUp {};
struct ScrollDown {};
struct ScrollPageUp {};
struct ScrollPageDown {};
struct ScrollLatest {};
struct ScrollOldest {};
struct ClearChannel {};

// ─── Overlays + side panel ───────────────────────────────────────────────────
struct ToggleRightPanel {};
struct ToggleHelp {};
struct ToggleJumper {};
struct HelpScroll    { int dy; };

struct JumperChar    { char32_t cp; };
struct JumperBack {};
struct JumperUp {};
struct JumperDown {};
struct JumperPick {};

// ─── Mouse — first-class input ───────────────────────────────────────────────
// Coordinates are absolute terminal cells (1-indexed origin from maya).
// update() routes by coordinate using app::mouse::compute_layout.
struct MouseClick { int x; int y; };
// Triggers a re-render without changing model state. Used to drive a
// repaint after maya's auto_dispatch has silently mutated a ScrollState
// (e.g., during a scrollbar drag — Move events update the scroll
// invisibly and we need a Program-loop turn to actually paint).
struct Refresh {};

using Msg = std::variant<
    Quit, Tick, Resize, CycleFocus,
    SelectChatUp, SelectChatDown, OpenSelectedChat,
    SearchInput, SearchBack, SearchClear,
    CharIn, Backspace, DeleteWord, DeleteToStart, DeleteToEnd,
    CursorLeft, CursorRight, CursorHome, CursorEnd, SendComposer,
    ScrollUp, ScrollDown, ScrollPageUp, ScrollPageDown, ScrollLatest, ScrollOldest, ClearChannel,
    ToggleRightPanel, ToggleHelp, ToggleJumper, HelpScroll,
    JumperChar, JumperBack, JumperUp, JumperDown, JumperPick,
    MouseClick, Refresh
>;

}  // namespace tl::msg
