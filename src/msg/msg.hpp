#pragma once

#include <cstdint>
#include <string>
#include <variant>

#include "td/event.hpp"

namespace tl::msg {

// ─── App control ──────────────────────────────────────────────────────────────
struct Quit {};
struct Tick {};
struct Resize  { int w; int h; };
struct CycleFocus {};

// ─── Auth (login overlay) ────────────────────────────────────────────────────
// Per-keystroke input into the auth overlay's active field, plus a
// Submit that flushes the field down to the TDLib runtime as a Submit*
// command. Backspace edits whatever field is currently active.
struct AuthCharIn   { char32_t cp; };
struct AuthBackspace {};
struct AuthSubmit {};

// ─── TDLib events (single carrier for the entire inbound stream) ─────────────
// Wrapped so the giant variant in this file doesn't have to enumerate
// every TDLib event alternative — those live in td::event::Event and
// dispatch happens via a nested std::visit in program.hpp.
struct TdEvent { td::event::Event payload; };

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
struct InsertNewline {};            // Alt-Enter / Shift-Enter → new line
struct SendComposer {};

// ─── Composer attachments + voice recording ───────────────────────
struct AttachPickFile {};           // demo: cycle through canned attachments
struct AttachClipboardPaste {};     // Ctrl-V → synthesize paste payload
struct AttachRemove  { std::size_t index; };
struct AttachClear {};              // drop every pending attachment
struct VoiceStart {};               // click 🎤 in idle mode
struct VoiceStop {};                // click 🎤 / ⏹ while recording → commit clip
struct VoiceCancel {};              // Esc while recording → discard
// Quote the most recent peer (non-self) message in the open chat into
// the composer's reply slot. CancelReply clears that slot.
struct ReplyLatest {};
struct CancelReply {};
// Toggle audio / video-note playback by source message id. The widget
// keeps a per-note state machine; Tick advances progress while playing.
struct ToggleAudioPlay { std::int64_t message_id; };
// Keyboard shortcut variant — toggle play on the most recent audio /
// video note in the open chat. Walks the messages backwards from the
// tail to find the first one with a playable note attached.
struct ToggleLatestNote {};

// Voice / video note polish controls. All operate on the most-recent
// audio_note / video_note in the open chat (or, for the *ById variants,
// the specifically-targetted message). UI-only — there's no real audio
// engine yet, but the model state drives the rendered affordance.
struct CyclePlaybackSpeed       { std::int64_t message_id; };  // 1.0 → 1.5 → 2.0 → 1.0
struct CycleLatestPlaybackSpeed {};
struct ToggleVideoNoteMute      { std::int64_t message_id; };
struct ToggleLatestVideoNoteMute {};
struct ToggleTranscript         { std::int64_t message_id; };  // voice-note transcript expand
struct ToggleLatestTranscript {};

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

// ─── Info pane (right panel) ─────────────────────────────────────────────────
struct InfoTabSelect       { int index; };  // 0=Media 1=Files 2=Links 3=Voice
struct InfoTabCycle {};                     // next tab, wraps
struct ToggleNotifications {};

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
    AuthCharIn, AuthBackspace, AuthSubmit, TdEvent,
    SelectChatUp, SelectChatDown, OpenSelectedChat,
    SearchInput, SearchBack, SearchClear,
    CharIn, Backspace, DeleteWord, DeleteToStart, DeleteToEnd,
    CursorLeft, CursorRight, CursorHome, CursorEnd,
    InsertNewline, SendComposer,
    AttachPickFile, AttachClipboardPaste, AttachRemove, AttachClear,
    VoiceStart, VoiceStop, VoiceCancel,
    ReplyLatest, CancelReply, ToggleAudioPlay, ToggleLatestNote,
    CyclePlaybackSpeed, CycleLatestPlaybackSpeed,
    ToggleVideoNoteMute, ToggleLatestVideoNoteMute,
    ToggleTranscript, ToggleLatestTranscript,
    ScrollUp, ScrollDown, ScrollPageUp, ScrollPageDown, ScrollLatest, ScrollOldest, ClearChannel,
    ToggleRightPanel, ToggleHelp, ToggleJumper, HelpScroll,
    InfoTabSelect, InfoTabCycle, ToggleNotifications,
    JumperChar, JumperBack, JumperUp, JumperDown, JumperPick,
    MouseClick, Refresh
>;

}  // namespace tl::msg
