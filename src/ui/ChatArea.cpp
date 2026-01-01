#include "ChatArea.h"
#include <wx/datetime.h>
#include <wx/settings.h>

ChatArea::ChatArea(wxWindow *parent, wxWindowID id)
    : wxPanel(parent, id), m_chatDisplay(nullptr), m_wasAtBottom(true),
      m_batchDepth(0), m_refreshPending(false) {
  SetupColors();
  CreateUI();
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
    
  // If we're in a batch update, just mark that we need to scroll
  if (m_batchDepth > 0) {
    m_wasAtBottom = true;
    return;
  }
  
  m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
  // Only call Refresh, not Update - let the event loop coalesce repaints
  ScheduleRefresh();
}

void ChatArea::ScrollToBottomIfAtBottom() {
  if (!m_chatDisplay)
    return;
    
  if (IsAtBottom()) {
    // If in batch mode, just mark the flag - EndBatchUpdate will handle it
    if (m_batchDepth > 0) {
      m_wasAtBottom = true;
    } else {
      ScrollToBottom();
    }
  }
}

bool ChatArea::IsAtBottom() const {
  if (!m_chatDisplay)
    return true;

  int scrollPos = m_chatDisplay->GetScrollPos(wxVERTICAL);
  int scrollRange = m_chatDisplay->GetScrollRange(wxVERTICAL);
  int scrollThumb = m_chatDisplay->GetScrollThumb(wxVERTICAL);

  // Consider "at bottom" if within 50 pixels of the end
  return (scrollPos + scrollThumb >= scrollRange - 50);
}

void ChatArea::BeginBatchUpdate() {
  if (m_batchDepth == 0) {
    m_wasAtBottom = IsAtBottom();
    m_chatDisplay->Freeze();
  }
  m_batchDepth++;
}

void ChatArea::EndBatchUpdate() {
  if (m_batchDepth > 0) {
    m_batchDepth--;
    if (m_batchDepth == 0) {
      m_chatDisplay->Thaw();
      // Layout content once after batch
      m_chatDisplay->LayoutContent();
      if (m_wasAtBottom) {
        m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
      }
      // Single refresh at end of batch - no Update() to let event loop coalesce
      m_chatDisplay->Refresh();
    }
  }
}

void ChatArea::ScheduleRefresh() {
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
    if (m_refreshPending) {
      DoRefresh();
      m_refreshPending = false;
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

  // Force layout recalculation and queue repaint
  // Don't call Update() - let event loop coalesce multiple refresh requests
  m_chatDisplay->LayoutContent();
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
    m_chatDisplay->WriteText(" \u2713"); // space + ✓ = 2 chars
    m_chatDisplay->EndTextColour();
    break;
  case MessageStatus::Read:
    if (highlight) {
      m_chatDisplay->BeginTextColour(m_readHighlightColor);
    } else {
      m_chatDisplay->BeginTextColour(m_readColor);
    }
    m_chatDisplay->WriteText("\u2713\u2713"); // ✓✓ = 2 chars
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
