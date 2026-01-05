#include "ChatArea.h"
#include <cmath>
#include <iostream>
#include <wx/datetime.h>
#include <wx/dcbuffer.h>
#include <wx/settings.h>

// #define CALOG(msg) std::cerr << "[ChatArea] " << msg << std::endl
#define CALOG(msg)                                                             \
  do {                                                                         \
  } while (0)
// #define SCROLL_LOG(msg) std::cerr << "[ChatArea:Scroll] " << msg << std::endl
#define SCROLL_LOG(msg)                                                        \
  do {                                                                         \
  } while (0)

ChatArea::ChatArea(wxWindow *parent, wxWindowID id)
    : wxPanel(parent, id), m_chatDisplay(nullptr), m_wasAtBottom(true),
      m_batchDepth(0), m_refreshPending(false), m_scrollTimer(this),
      m_scrollTargetPos(0), m_scrollCurrentPos(0), m_scrollStartPos(0),
      m_scrollSteps(SCROLL_ANIMATION_STEPS), m_scrollStepCount(0),
      m_smoothScrollEnabled(true), m_needsIdleRefresh(false) {
  SetupColors();
  CreateUI();

  // Bind timer for smooth scroll animation
  Bind(wxEVT_TIMER, &ChatArea::OnScrollTimer, this, m_scrollTimer.GetId());

  // Bind idle event for coalesced layout updates
  Bind(wxEVT_IDLE, &ChatArea::OnIdleRefresh, this);

  // Bind size event for reactive resizing
  Bind(wxEVT_SIZE, [this](wxSizeEvent &event) {
    event.Skip();
    // Schedule a layout update on idle to avoid excessive reflows during resize
    m_needsIdleRefresh = true;
  });
}

void ChatArea::SetupColors() {
  // Default to monospace font (Teletype) - actual font will be set from
  // settings via SetChatFont()
  m_chatFont = wxFont(12, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL,
                      wxFONTWEIGHT_NORMAL);

  // Only set colors that are actually needed for semantic meaning
  // All other colors will use native defaults
  m_errorColor = wxColour(0xCC, 0x00, 0x00);   // Red for errors
  m_successColor = wxColour(0x00, 0x80, 0x00); // Green for success
  m_readColor = wxColour(0x00, 0xAA, 0x00);    // Green for read status (✓✓)
  m_readHighlightColor =
      wxColour(0x00, 0xFF, 0x44); // Bright green for recently read

  // User colors for sender names - need distinct colors for different users
  m_userColors[0] = wxColour(0x00, 0x00, 0xAA);  // Dark blue
  m_userColors[1] = wxColour(0x00, 0x73, 0x00);  // Dark green
  m_userColors[2] = wxColour(0xAA, 0x00, 0x00);  // Dark red
  m_userColors[3] = wxColour(0xAA, 0x55, 0x00);  // Brown/orange
  m_userColors[4] = wxColour(0x55, 0x00, 0x55);  // Purple
  m_userColors[5] = wxColour(0x00, 0x73, 0x73);  // Teal
  m_userColors[6] = wxColour(0x73, 0x00, 0x73);  // Magenta
  m_userColors[7] = wxColour(0x00, 0x55, 0xAA);  // Steel blue
  m_userColors[8] = wxColour(0x55, 0x55, 0x00);  // Olive
  m_userColors[9] = wxColour(0x73, 0x3D, 0x00);  // Sienna
  m_userColors[10] = wxColour(0x00, 0x55, 0x55); // Dark cyan
  m_userColors[11] = wxColour(0x55, 0x00, 0xAA); // Indigo
  m_userColors[12] = wxColour(0xAA, 0x00, 0x55); // Deep pink
  m_userColors[13] = wxColour(0x3D, 0x73, 0x00); // Dark lime
  m_userColors[14] = wxColour(0x00, 0x3D, 0x73); // Navy
  m_userColors[15] = wxColour(0x73, 0x00, 0x3D); // Maroon
}

void ChatArea::CreateUI() {
  wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);

  // Chat display - let it use native styling
  // Use wxBUFFER_VIRTUAL_AREA for double buffering to reduce flicker
  m_chatDisplay = new wxRichTextCtrl(
      this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
      wxRE_MULTILINE | wxRE_READONLY | wxBORDER_NONE | wxVSCROLL);
  m_chatDisplay->SetFont(m_chatFont);
  m_chatDisplay->SetCursor(wxCursor(wxCURSOR_ARROW));

  // Enable double buffering to prevent flicker during rapid updates
  m_chatDisplay->SetDoubleBuffered(true);

  // Use buffered painting for smoother rendering
  m_chatDisplay->SetBackgroundStyle(wxBG_STYLE_SYSTEM);

  // Disable automatic scrolling on content change - we handle it manually
  m_chatDisplay->SetInsertionPointEnd();

  // Bind SET_CURSOR to prevent wxRichTextCtrl from forcing I-beam cursor
  m_chatDisplay->Bind(wxEVT_SET_CURSOR, &ChatArea::OnSetCursor, this);

  // Build cached default style and apply it
  RebuildCachedStyle();
  m_chatDisplay->SetDefaultStyle(m_cachedDefaultStyle);
  m_chatDisplay->SetBasicStyle(m_cachedDefaultStyle);

  sizer->Add(m_chatDisplay, 1, wxEXPAND);
  SetSizer(sizer);
  Layout();
  m_chatDisplay->Show();
}

void ChatArea::OnSetCursor(wxSetCursorEvent &event) {
  // Override wxRichTextCtrl's default I-beam cursor with our tracked cursor
  event.SetCursor(wxCursor(m_currentCursor));
}

void ChatArea::Clear() {
  m_chatDisplay->Clear();
  ResetStyles();
}

void ChatArea::ResetStyles() {
  if (!m_chatDisplay)
    return;

  // Force end any potentially open style blocks
  m_chatDisplay->EndAllStyles();

  // Use cached default style to avoid repeated allocations
  // The style is rebuilt only when font changes (in SetChatFont)
  m_chatDisplay->SetDefaultStyle(m_cachedDefaultStyle);
}

void ChatArea::RebuildCachedStyle() {
  // Rebuild the cached default style - called when font changes
  m_cachedDefaultStyle = wxRichTextAttr();
  m_cachedDefaultStyle.SetFont(m_chatFont);
  m_cachedDefaultStyle.SetFontUnderlined(false);
  m_cachedDefaultStyle.SetFontWeight(wxFONTWEIGHT_NORMAL);
  m_cachedDefaultStyle.SetFontStyle(wxFONTSTYLE_NORMAL);
  m_cachedDefaultStyle.SetLineSpacing(10);
  m_cachedDefaultStyle.SetParagraphSpacingBefore(0);
  m_cachedDefaultStyle.SetParagraphSpacingAfter(0);
}

void ChatArea::SetChatFont(const wxFont &font) {
  if (!font.IsOk())
    return;

  m_chatFont = font;
  RebuildCachedStyle();

  if (m_chatDisplay) {
    // Freeze to prevent rendering issues during font change
    m_chatDisplay->Freeze();

    m_chatDisplay->SetFont(m_chatFont);

    // Update default and basic styles for new text
    wxRichTextAttr defaultStyle;
    defaultStyle.SetFont(m_chatFont);
    defaultStyle.SetLineSpacing(10);
    defaultStyle.SetParagraphSpacingBefore(0);
    defaultStyle.SetParagraphSpacingAfter(0);
    m_chatDisplay->SetDefaultStyle(defaultStyle);
    m_chatDisplay->SetBasicStyle(defaultStyle);

    // Apply font to ALL existing text WITHOUT using selection
    // This avoids visual artifacts caused by selection-based style changes
    long textLength = m_chatDisplay->GetLastPosition();
    if (textLength > 0) {
      wxRichTextAttr fontAttr;
      fontAttr.SetFont(m_chatFont);
      // Use SetStyleEx without selection - apply directly to range
      m_chatDisplay->SetStyleEx(wxRichTextRange(0, textLength), fontAttr,
                                wxRICHTEXT_SETSTYLE_OPTIMIZE);
    }

    // Force layout recalculation
    m_chatDisplay->LayoutContent();

    m_chatDisplay->Thaw();
    m_chatDisplay->Refresh();
    m_chatDisplay->Update();
  }
}

void ChatArea::ScrollToBottom() {
  if (!m_chatDisplay)
    return;

  SCROLL_LOG("ScrollToBottom called, batchDepth="
             << m_batchDepth << " smoothEnabled=" << m_smoothScrollEnabled);

  // If we're in a batch update, just mark that we need to scroll
  if (m_batchDepth > 0) {
    SCROLL_LOG("  -> in batch, marking wasAtBottom=true");
    m_wasAtBottom = true;
    return;
  }

  // Use smooth scroll if enabled and not already at bottom
  if (m_smoothScrollEnabled && !IsAtBottom()) {
    SCROLL_LOG("  -> starting smooth scroll");
    ScrollToBottomSmooth();
  } else {
    SCROLL_LOG("  -> instant scroll to last position");
    m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
    // Only call Refresh, not Update - let the event loop coalesce repaints
    ScheduleRefresh();
  }
}

void ChatArea::ScrollToBottomSmooth() {
  if (!m_chatDisplay)
    return;

  // Stop any existing animation
  if (m_scrollTimer.IsRunning()) {
    m_scrollTimer.Stop();
  }

  // Get current and target scroll positions
  int scrollRange = m_chatDisplay->GetScrollRange(wxVERTICAL);
  int scrollThumb = m_chatDisplay->GetScrollThumb(wxVERTICAL);
  m_scrollTargetPos = scrollRange - scrollThumb;
  m_scrollStartPos = m_chatDisplay->GetScrollPos(wxVERTICAL);
  m_scrollCurrentPos = m_scrollStartPos;

  // Calculate scroll distance
  int distance = m_scrollTargetPos - m_scrollCurrentPos;

  // If already at bottom or distance is too small, just jump there (no
  // animation)
  if (distance <= MIN_SCROLL_DISTANCE_FOR_ANIMATION) {
    m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
    return;
  }

  // Adjust animation steps based on distance - smoother for bigger scrolls
  // Use more steps for longer distances, but cap it for responsiveness
  m_scrollSteps = std::min(SCROLL_ANIMATION_STEPS, std::max(6, distance / 30));
  m_scrollStepCount = 0;

  // Start animation
  m_scrollTimer.Start(SCROLL_TIMER_INTERVAL_MS);
}

void ChatArea::OnScrollTimer(wxTimerEvent &event) {
  if (!m_chatDisplay) {
    m_scrollTimer.Stop();
    return;
  }

  m_scrollStepCount++;

  if (m_scrollStepCount >= m_scrollSteps) {
    // Animation complete - snap to final position
    m_scrollTimer.Stop();
    m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
    m_scrollCurrentPos = m_scrollTargetPos;
    // Final refresh only, no Update() to avoid blocking
    m_chatDisplay->Refresh();
    return;
  }

  // Ease-out quintic for extra smooth deceleration: 1 - (1 - t)^5
  // This provides a more natural feel than cubic
  double t = static_cast<double>(m_scrollStepCount) / m_scrollSteps;
  double easedT = 1.0 - std::pow(1.0 - t, 5.0);

  int distance = m_scrollTargetPos - m_scrollStartPos;
  m_scrollCurrentPos = m_scrollStartPos + static_cast<int>(distance * easedT);

  // Apply the scroll position without forcing immediate repaint
  m_chatDisplay->SetScrollPos(wxVERTICAL, m_scrollCurrentPos, true);

  // Only refresh every other frame to reduce CPU usage during animation
  if (m_scrollStepCount % 2 == 0) {
    m_chatDisplay->Refresh();
  }
}

void ChatArea::OnIdleRefresh(wxIdleEvent &event) {
  event.Skip();

  if (m_needsIdleRefresh && m_chatDisplay && m_batchDepth == 0) {
    m_needsIdleRefresh = false;

    // Check CURRENT scroll position, not stale m_wasAtBottom
    bool currentlyAtBottom = IsAtBottom();
    SCROLL_LOG(
        "OnIdleRefresh: processing, currentlyAtBottom=" << currentlyAtBottom);

    // Only scroll to bottom if we're CURRENTLY at bottom
    // Don't do any layout here - it causes jitter. Layout happens in batch
    // updates.
    if (currentlyAtBottom) {
      SCROLL_LOG("OnIdleRefresh: scrolling to bottom");
      m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
    }
  }
}

void ChatArea::ScrollToBottomIfAtBottom() {
  if (!m_chatDisplay)
    return;

  // Update our tracking of whether we're at bottom
  bool atBottom = IsAtBottom();

  SCROLL_LOG("ScrollToBottomIfAtBottom: atBottom=" << atBottom << " batchDepth="
                                                   << m_batchDepth);

  if (atBottom) {
    // If in batch mode, just mark the flag - EndBatchUpdate will handle it
    if (m_batchDepth > 0) {
      SCROLL_LOG("  -> in batch, marking wasAtBottom=true");
      m_wasAtBottom = true;
    } else {
      SCROLL_LOG("  -> instant scroll to last position");
      // Use instant scroll when following new messages to avoid lag
      m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
      ScheduleRefresh();
    }
  }
}

bool ChatArea::IsAtBottom() const {
  if (!m_chatDisplay)
    return true;

  int scrollPos = m_chatDisplay->GetScrollPos(wxVERTICAL);
  int scrollRange = m_chatDisplay->GetScrollRange(wxVERTICAL);
  int scrollThumb = m_chatDisplay->GetScrollThumb(wxVERTICAL);

  // Maximum scroll position is range - thumb
  int maxScrollPos = scrollRange - scrollThumb;

  // If there's no scrollbar (content fits), we're at bottom
  if (maxScrollPos <= 0) {
    SCROLL_LOG("IsAtBottom: no scrollbar (maxScrollPos=" << maxScrollPos
                                                         << ") -> true");
    return true;
  }

  // Consider "at bottom" if within 10 pixels of max scroll position
  bool result = (scrollPos >= maxScrollPos - 10);
  SCROLL_LOG("IsAtBottom: pos=" << scrollPos << " maxScrollPos=" << maxScrollPos
                                << " -> " << result);
  return result;
}

void ChatArea::BeginBatchUpdate() {
  SCROLL_LOG("BeginBatchUpdate: depth=" << m_batchDepth << " -> "
                                        << (m_batchDepth + 1));
  if (m_batchDepth == 0) {
    m_wasAtBottom = IsAtBottom();
    SCROLL_LOG("  -> captured wasAtBottom=" << m_wasAtBottom);
    // Don't freeze here - we'll freeze only during the actual content
    // modification This reduces the total freeze time and improves
    // responsiveness
  }
  m_batchDepth++;
}

void ChatArea::EndBatchUpdate() {
  SCROLL_LOG("EndBatchUpdate: depth=" << m_batchDepth << " -> "
                                      << (m_batchDepth - 1)
                                      << " wasAtBottom=" << m_wasAtBottom);
  if (m_batchDepth > 0) {
    m_batchDepth--;
    if (m_batchDepth == 0 && m_chatDisplay) {
      // Handle scroll after content is ready
      if (m_wasAtBottom) {
        SCROLL_LOG("  -> scrolling to bottom");
        m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
      }

      // Single refresh at end of batch - let the system handle repainting
      // efficiently
      m_chatDisplay->Refresh();
    }
  }
}

void ChatArea::ScheduleRefresh() {
  // Coalesce multiple refresh requests - only refresh once per idle cycle
  // If we're in a batch update, don't schedule individual refreshes
  if (m_batchDepth > 0) {
    return;
  }

  // If a refresh is already pending, don't schedule another
  if (m_refreshPending) {
    return;
  }

  m_refreshPending = true;

  // Use CallAfter to coalesce multiple rapid updates into a single refresh
  // This runs after the current event processing is complete
  CallAfter([this]() {
    if (m_refreshPending && m_chatDisplay) {
      m_refreshPending = false;
      DoRefresh();
    }
  });
}

void ChatArea::DoRefresh() {
  if (!m_chatDisplay) {
    return;
  }

  // Skip refresh if we're in a batch update - EndBatchUpdate will handle it
  if (m_batchDepth > 0) {
    return;
  }

  // Freeze during layout to prevent visual glitches
  m_chatDisplay->Freeze();
  m_chatDisplay->LayoutContent();
  m_chatDisplay->Thaw();

  // Queue repaint - don't call Update() to let event loop coalesce
  m_chatDisplay->Refresh();
}

wxString ChatArea::GetCurrentTimestamp() {
  return wxDateTime::Now().Format("%H:%M:%S");
}

void ChatArea::WriteTimestamp() { WriteTimestamp(GetCurrentTimestamp()); }

void ChatArea::WriteTimestamp(const wxString &timestamp) {
  WriteTimestamp(timestamp, MessageStatus::None, false);
}

void ChatArea::WriteTimestamp(const wxString &timestamp, MessageStatus status,
                              bool highlight) {
  if (!m_chatDisplay)
    return;
  m_chatDisplay->BeginTextColour(GetTimestampColor());
  m_chatDisplay->WriteText("[" + timestamp + "] ");
  m_chatDisplay->EndTextColour();

  // Note: status parameters kept for API compatibility but not used here
  // Status ticks are now appended at end of message by MessageFormatter
}

void ChatArea::WriteStatusMarker(MessageStatus status, bool highlight) {
  if (!m_chatDisplay)
    return;

  switch (status) {
  case MessageStatus::Sending:
    m_chatDisplay->BeginTextColour(GetTimestampColor());
    m_chatDisplay->WriteText(".."); // 2 chars
    m_chatDisplay->EndTextColour();
    break;
  case MessageStatus::Sent:
    m_chatDisplay->BeginTextColour(GetSentColor());
    m_chatDisplay->WriteText(
        " " + wxString::FromUTF8("\xE2\x9C\x93")); // space + check
    m_chatDisplay->EndTextColour();
    break;
  case MessageStatus::Read:
    if (highlight) {
      m_chatDisplay->BeginTextColour(m_readHighlightColor);
    } else {
      m_chatDisplay->BeginTextColour(m_readColor);
    }
    m_chatDisplay->WriteText(
        wxString::FromUTF8("\xE2\x9C\x93\xE2\x9C\x93")); // double check
    m_chatDisplay->EndTextColour();
    break;
  case MessageStatus::None:
  default:
    break;
  }
}

void ChatArea::AppendInfo(const wxString &message) {
  if (!m_chatDisplay)
    return;
  WriteTimestamp();

  m_chatDisplay->BeginTextColour(GetInfoColor());
  m_chatDisplay->WriteText("* " + message + "\n");
  m_chatDisplay->EndTextColour();

  ScheduleRefresh();
}

void ChatArea::AppendError(const wxString &message) {
  if (!m_chatDisplay)
    return;
  WriteTimestamp();

  m_chatDisplay->BeginTextColour(m_errorColor);
  m_chatDisplay->WriteText("* Error: " + message + "\n");
  m_chatDisplay->EndTextColour();

  ScheduleRefresh();
}

void ChatArea::AppendSuccess(const wxString &message) {
  if (!m_chatDisplay)
    return;
  WriteTimestamp();

  m_chatDisplay->BeginTextColour(m_successColor);
  m_chatDisplay->WriteText("* " + message + "\n");
  m_chatDisplay->EndTextColour();

  ScheduleRefresh();
}

void ChatArea::AppendPrompt(const wxString &prompt) {
  if (!m_chatDisplay)
    return;
  WriteTimestamp();

  m_chatDisplay->BeginTextColour(GetPromptColor());
  m_chatDisplay->WriteText(">> " + prompt + "\n");
  m_chatDisplay->EndTextColour();

  ScheduleRefresh();
}

void ChatArea::AppendUserInput(const wxString &input) {
  if (!m_chatDisplay)
    return;
  WriteTimestamp();

  m_chatDisplay->BeginTextColour(GetFgColor());
  m_chatDisplay->WriteText("> " + input + "\n");
  m_chatDisplay->EndTextColour();

  ScheduleRefresh();
}

void ChatArea::AppendService(const wxString &message) {
  if (!m_chatDisplay)
    return;
  WriteTimestamp();

  m_chatDisplay->BeginTextColour(GetServiceColor());
  m_chatDisplay->WriteText("* " + message + "\n");
  m_chatDisplay->EndTextColour();

  ScheduleRefresh();
}

void ChatArea::AppendMessage(const wxString &sender, const wxString &message) {
  AppendMessage(GetCurrentTimestamp(), sender, message);
}

void ChatArea::AppendMessage(const wxString &timestamp, const wxString &sender,
                             const wxString &message) {
  if (!m_chatDisplay)
    return;
  WriteTimestamp(timestamp);

  wxColour userColor = GetUserColor(sender);
  m_chatDisplay->BeginTextColour(userColor);
  m_chatDisplay->WriteText("<");
  m_chatDisplay->BeginBold();
  m_chatDisplay->WriteText(sender);
  m_chatDisplay->EndBold();
  m_chatDisplay->WriteText("> ");
  m_chatDisplay->EndTextColour();

  m_chatDisplay->BeginTextColour(GetFgColor());
  m_chatDisplay->WriteText(message + "\n");
  m_chatDisplay->EndTextColour();

  ScheduleRefresh();
}

void ChatArea::AppendAction(const wxString &sender, const wxString &action) {
  AppendAction(GetCurrentTimestamp(), sender, action);
}

void ChatArea::AppendAction(const wxString &timestamp, const wxString &sender,
                            const wxString &action) {
  if (!m_chatDisplay)
    return;
  WriteTimestamp(timestamp);

  m_chatDisplay->BeginTextColour(GetActionColor());
  m_chatDisplay->WriteText("* ");
  m_chatDisplay->BeginBold();
  m_chatDisplay->WriteText(sender);
  m_chatDisplay->EndBold();
  m_chatDisplay->WriteText(" " + action + "\n");
  m_chatDisplay->EndTextColour();

  ScheduleRefresh();
}

void ChatArea::AppendJoin(const wxString &user) {
  AppendJoin(GetCurrentTimestamp(), user);
}

void ChatArea::AppendJoin(const wxString &timestamp, const wxString &user) {
  if (!m_chatDisplay)
    return;
  WriteTimestamp(timestamp);

  m_chatDisplay->BeginTextColour(GetServiceColor());
  m_chatDisplay->WriteText("--> " + user + " has joined\n");
  m_chatDisplay->EndTextColour();

  ScheduleRefresh();
}

void ChatArea::AppendLeave(const wxString &user) {
  AppendLeave(GetCurrentTimestamp(), user);
}

void ChatArea::AppendLeave(const wxString &timestamp, const wxString &user) {
  if (!m_chatDisplay)
    return;
  WriteTimestamp(timestamp);

  m_chatDisplay->BeginTextColour(GetServiceColor());
  m_chatDisplay->WriteText("<-- " + user + " has left\n");
  m_chatDisplay->EndTextColour();

  ScheduleRefresh();
}

void ChatArea::SetUserColors(const wxColour colors[16]) {
  for (int i = 0; i < 16; i++) {
    m_userColors[i] = colors[i];
  }
}

wxColour ChatArea::GetUserColor(const wxString &username) const {
  // Handle empty username - return a default color
  if (username.IsEmpty()) {
    return m_userColors[0];
  }

  // Current user always gets gray
  if (!m_currentUsername.IsEmpty() && username == m_currentUsername) {
    return GetSelfColor();
  }

  // Other users get a color from the palette (no grays)
  unsigned long hash = 0;
  for (size_t i = 0; i < username.length(); i++) {
    hash = static_cast<unsigned long>(username[i].GetValue()) + (hash << 6) +
           (hash << 16) - hash;
  }
  return m_userColors[hash % 16];
}
