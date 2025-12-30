#include "ChatArea.h"
#include <wx/datetime.h>
#include <wx/settings.h>

ChatArea::ChatArea(wxWindow *parent, wxWindowID id)
    : wxPanel(parent, id), m_chatDisplay(nullptr), m_wasAtBottom(true),
      m_batchDepth(0) {
  SetupColors();
  CreateUI();
}

void ChatArea::SetupColors() {
  // Use native system font
#ifdef __WXGTK__
  // On Linux/GTK, wxSYS_ANSI_FIXED_FONT might not return a monospace font
  // Force a monospace font using the TELETYPE family
  m_chatFont = wxFont(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE));
#else
  m_chatFont = wxSystemSettings::GetFont(wxSYS_ANSI_FIXED_FONT);
#endif

  // Only set colors that are actually needed for semantic meaning
  // All other colors will use native defaults
  m_errorColor = wxColour(0xCC, 0x00, 0x00);   // Red for errors
  m_successColor = wxColour(0x00, 0x80, 0x00); // Green for success

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
  m_chatDisplay = new wxRichTextCtrl(
      this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
      wxRE_MULTILINE | wxRE_READONLY | wxBORDER_NONE | wxVSCROLL);
  m_chatDisplay->SetFont(m_chatFont);
  m_chatDisplay->SetCursor(wxCursor(wxCURSOR_ARROW));

  // Bind SET_CURSOR to prevent wxRichTextCtrl from forcing I-beam cursor
  m_chatDisplay->Bind(wxEVT_SET_CURSOR, &ChatArea::OnSetCursor, this);

  // Only set font in default style, let colors be native
  wxRichTextAttr defaultStyle;
  defaultStyle.SetFont(m_chatFont);
  m_chatDisplay->SetDefaultStyle(defaultStyle);
  m_chatDisplay->SetBasicStyle(defaultStyle);

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

  // Reset text styles - only set font, let colors be native
  wxRichTextAttr defaultStyle;
  defaultStyle.SetFont(m_chatFont);
  defaultStyle.SetFontUnderlined(false);
  defaultStyle.SetFontWeight(wxFONTWEIGHT_NORMAL);
  defaultStyle.SetFontStyle(wxFONTSTYLE_NORMAL);
  m_chatDisplay->SetDefaultStyle(defaultStyle);
  m_chatDisplay->SetBasicStyle(defaultStyle);
}

void ChatArea::ScrollToBottom() {
  m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
}

void ChatArea::ScrollToBottomIfAtBottom() {
  if (IsAtBottom()) {
    ScrollToBottom();
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
      if (m_wasAtBottom) {
        ScrollToBottom();
      }
    }
  }
}

wxString ChatArea::GetCurrentTimestamp() {
  return wxDateTime::Now().Format("%H:%M:%S");
}

void ChatArea::WriteTimestamp() { WriteTimestamp(GetCurrentTimestamp()); }

void ChatArea::WriteTimestamp(const wxString &timestamp) {
  if (!m_chatDisplay)
    return;
  m_chatDisplay->BeginTextColour(GetTimestampColor());
  m_chatDisplay->WriteText("[" + timestamp + "] ");
  m_chatDisplay->EndTextColour();
}

void ChatArea::AppendInfo(const wxString &message) {
  if (!m_chatDisplay)
    return;
  WriteTimestamp();

  m_chatDisplay->BeginTextColour(GetInfoColor());
  m_chatDisplay->WriteText("* " + message + "\n");
  m_chatDisplay->EndTextColour();
}

void ChatArea::AppendError(const wxString &message) {
  if (!m_chatDisplay)
    return;
  WriteTimestamp();

  m_chatDisplay->BeginTextColour(m_errorColor);
  m_chatDisplay->WriteText("* Error: " + message + "\n");
  m_chatDisplay->EndTextColour();
}

void ChatArea::AppendSuccess(const wxString &message) {
  if (!m_chatDisplay)
    return;
  WriteTimestamp();

  m_chatDisplay->BeginTextColour(m_successColor);
  m_chatDisplay->WriteText("* " + message + "\n");
  m_chatDisplay->EndTextColour();
}

void ChatArea::AppendPrompt(const wxString &prompt) {
  if (!m_chatDisplay)
    return;
  WriteTimestamp();

  m_chatDisplay->BeginTextColour(GetPromptColor());
  m_chatDisplay->WriteText(">> " + prompt + "\n");
  m_chatDisplay->EndTextColour();
}

void ChatArea::AppendUserInput(const wxString &input) {
  if (!m_chatDisplay)
    return;
  WriteTimestamp();

  m_chatDisplay->BeginTextColour(GetFgColor());
  m_chatDisplay->WriteText("> " + input + "\n");
  m_chatDisplay->EndTextColour();
}

void ChatArea::AppendService(const wxString &message) {
  if (!m_chatDisplay)
    return;
  WriteTimestamp();

  m_chatDisplay->BeginTextColour(GetServiceColor());
  m_chatDisplay->WriteText("* " + message + "\n");
  m_chatDisplay->EndTextColour();
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
