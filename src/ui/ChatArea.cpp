#include "ChatArea.h"
#include <wx/datetime.h>

ChatArea::ChatArea(wxWindow* parent, wxWindowID id)
    : wxPanel(parent, id),
      m_chatDisplay(nullptr),
      m_wasAtBottom(true),
      m_batchDepth(0)
{
    SetupColors();
    CreateUI();
}

void ChatArea::SetupColors()
{
    // HexChat dark theme - exactly like WelcomeChat
    m_bgColor = wxColour(0x2B, 0x2B, 0x2B);
    m_fgColor = wxColour(0xD3, 0xD7, 0xCF);
    m_timestampColor = wxColour(0x88, 0x88, 0x88);
    m_infoColor = wxColour(0x72, 0x9F, 0xCF);       // Blue
    m_errorColor = wxColour(0xEF, 0x29, 0x29);      // Red
    m_successColor = wxColour(0x8A, 0xE2, 0x34);    // Green
    m_promptColor = wxColour(0xFC, 0xAF, 0x3E);     // Orange
    m_serviceColor = wxColour(0x88, 0x88, 0x88);    // Gray
    m_actionColor = wxColour(0xCE, 0x5C, 0x00);     // Orange
    m_linkColor = wxColour(0x72, 0x9F, 0xCF);       // Blue

    // Monospace font - exactly like WelcomeChat
    // macOS renders fonts smaller, so use larger size on Mac
#ifdef __WXOSX__
    int fontSize = 12;
#else
    int fontSize = 8;
#endif
    m_chatFont = wxFont(fontSize, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);

    // User colors - no grays (gray is reserved for current user)
    m_userColors[0]  = wxColour(0x35, 0x36, 0xB2);  // Blue
    m_userColors[1]  = wxColour(0x2A, 0x8C, 0x2A);  // Green
    m_userColors[2]  = wxColour(0xC3, 0x38, 0x38);  // Red
    m_userColors[3]  = wxColour(0xC7, 0x38, 0x38);  // Dark red
    m_userColors[4]  = wxColour(0x80, 0x00, 0x80);  // Purple
    m_userColors[5]  = wxColour(0xFF, 0x80, 0x00);  // Orange
    m_userColors[6]  = wxColour(0x80, 0x80, 0x00);  // Olive
    m_userColors[7]  = wxColour(0x33, 0xCC, 0x33);  // Lime
    m_userColors[8]  = wxColour(0x00, 0x80, 0x80);  // Teal
    m_userColors[9]  = wxColour(0x33, 0xCC, 0xCC);  // Cyan
    m_userColors[10] = wxColour(0x66, 0x66, 0xFF);  // Light blue
    m_userColors[11] = wxColour(0xFF, 0x00, 0xFF);  // Magenta
    m_userColors[12] = wxColour(0x72, 0x9F, 0xCF);  // Sky blue
    m_userColors[13] = wxColour(0xAD, 0x7F, 0xA8);  // Pink/mauve
    m_userColors[14] = wxColour(0xFC, 0xAF, 0x3E);  // Gold
    m_userColors[15] = wxColour(0x8A, 0xE2, 0x34);  // Yellow-green

    // Gray color for current user (not in the hash palette)
    m_selfColor = wxColour(0xAA, 0xAA, 0xAA);
}

void ChatArea::CreateUI()
{
    SetBackgroundColour(m_bgColor);

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    // Chat display - exactly like WelcomeChat
    m_chatDisplay = new wxRichTextCtrl(this, wxID_ANY,
        wxEmptyString, wxDefaultPosition, wxDefaultSize,
        wxRE_MULTILINE | wxRE_READONLY | wxBORDER_NONE | wxVSCROLL);
    m_chatDisplay->SetBackgroundColour(m_bgColor);
    m_chatDisplay->SetFont(m_chatFont);
    m_chatDisplay->SetCursor(wxCursor(wxCURSOR_ARROW));  // Arrow cursor by default, not I-beam
    
    // Bind SET_CURSOR to prevent wxRichTextCtrl from forcing I-beam cursor
    m_chatDisplay->Bind(wxEVT_SET_CURSOR, &ChatArea::OnSetCursor, this);

    wxRichTextAttr defaultStyle;
    defaultStyle.SetTextColour(m_fgColor);
    defaultStyle.SetBackgroundColour(m_bgColor);
    defaultStyle.SetFont(m_chatFont);
    m_chatDisplay->SetDefaultStyle(defaultStyle);
    m_chatDisplay->SetBasicStyle(defaultStyle);

    sizer->Add(m_chatDisplay, 1, wxEXPAND);
    SetSizer(sizer);
    Layout();
    m_chatDisplay->Show();
}

void ChatArea::OnSetCursor(wxSetCursorEvent& event)
{
    // Override wxRichTextCtrl's default I-beam cursor with our tracked cursor
    event.SetCursor(wxCursor(m_currentCursor));
}

void ChatArea::Clear()
{
    m_chatDisplay->Clear();
    ResetStyles();
}

void ChatArea::ResetStyles()
{
    if (!m_chatDisplay) return;

    // Force end any potentially open style blocks
    // This prevents style leaking (especially underline) from corrupting future text
    m_chatDisplay->EndAllStyles();

    // Reset all text styles to prevent style corruption from leaking
    // This ensures underline, bold, italic, etc. don't persist
    wxRichTextAttr defaultStyle;
    defaultStyle.SetTextColour(m_fgColor);
    defaultStyle.SetBackgroundColour(m_bgColor);
    defaultStyle.SetFont(m_chatFont);
    defaultStyle.SetFontUnderlined(false);
    defaultStyle.SetFontWeight(wxFONTWEIGHT_NORMAL);
    defaultStyle.SetFontStyle(wxFONTSTYLE_NORMAL);
    m_chatDisplay->SetDefaultStyle(defaultStyle);
    m_chatDisplay->SetBasicStyle(defaultStyle);
}

void ChatArea::ScrollToBottom()
{
    m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
}

void ChatArea::ScrollToBottomIfAtBottom()
{
    if (IsAtBottom()) {
        ScrollToBottom();
    }
}

bool ChatArea::IsAtBottom() const
{
    if (!m_chatDisplay) return true;

    int scrollPos = m_chatDisplay->GetScrollPos(wxVERTICAL);
    int scrollRange = m_chatDisplay->GetScrollRange(wxVERTICAL);
    int scrollThumb = m_chatDisplay->GetScrollThumb(wxVERTICAL);

    // Consider "at bottom" if within 50 pixels of the end
    return (scrollPos + scrollThumb >= scrollRange - 50);
}

void ChatArea::BeginBatchUpdate()
{
    if (m_batchDepth == 0) {
        m_wasAtBottom = IsAtBottom();
        m_chatDisplay->Freeze();
    }
    m_batchDepth++;
}

void ChatArea::EndBatchUpdate()
{
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

wxString ChatArea::GetCurrentTimestamp()
{
    return wxDateTime::Now().Format("%H:%M:%S");
}

void ChatArea::WriteTimestamp()
{
    WriteTimestamp(GetCurrentTimestamp());
}

void ChatArea::WriteTimestamp(const wxString& timestamp)
{
    if (!m_chatDisplay) return;
    m_chatDisplay->BeginTextColour(m_timestampColor);
    m_chatDisplay->WriteText("[" + timestamp + "] ");
    m_chatDisplay->EndTextColour();
}

void ChatArea::AppendInfo(const wxString& message)
{
    if (!m_chatDisplay) return;
    WriteTimestamp();

    m_chatDisplay->BeginTextColour(m_infoColor);
    m_chatDisplay->WriteText("* " + message + "\n");
    m_chatDisplay->EndTextColour();

    m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
}

void ChatArea::AppendError(const wxString& message)
{
    if (!m_chatDisplay) return;
    WriteTimestamp();

    m_chatDisplay->BeginTextColour(m_errorColor);
    m_chatDisplay->WriteText("* Error: " + message + "\n");
    m_chatDisplay->EndTextColour();

    m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
}

void ChatArea::AppendSuccess(const wxString& message)
{
    if (!m_chatDisplay) return;
    WriteTimestamp();

    m_chatDisplay->BeginTextColour(m_successColor);
    m_chatDisplay->WriteText("* " + message + "\n");
    m_chatDisplay->EndTextColour();

    m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
}

void ChatArea::AppendPrompt(const wxString& prompt)
{
    if (!m_chatDisplay) return;
    WriteTimestamp();

    m_chatDisplay->BeginTextColour(m_promptColor);
    m_chatDisplay->WriteText(">> " + prompt + "\n");
    m_chatDisplay->EndTextColour();

    m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
}

void ChatArea::AppendUserInput(const wxString& input)
{
    if (!m_chatDisplay) return;
    WriteTimestamp();

    m_chatDisplay->BeginTextColour(m_fgColor);
    m_chatDisplay->WriteText("> " + input + "\n");
    m_chatDisplay->EndTextColour();

    m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
}

void ChatArea::AppendService(const wxString& message)
{
    if (!m_chatDisplay) return;
    WriteTimestamp();

    m_chatDisplay->BeginTextColour(m_serviceColor);
    m_chatDisplay->WriteText("* " + message + "\n");
    m_chatDisplay->EndTextColour();

    m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
}

void ChatArea::AppendMessage(const wxString& sender, const wxString& message)
{
    AppendMessage(GetCurrentTimestamp(), sender, message);
}

void ChatArea::AppendMessage(const wxString& timestamp, const wxString& sender, const wxString& message)
{
    if (!m_chatDisplay) return;
    WriteTimestamp(timestamp);

    wxColour userColor = GetUserColor(sender);
    m_chatDisplay->BeginTextColour(userColor);
    m_chatDisplay->WriteText("<");
    m_chatDisplay->BeginBold();
    m_chatDisplay->WriteText(sender);
    m_chatDisplay->EndBold();
    m_chatDisplay->WriteText("> ");
    m_chatDisplay->EndTextColour();

    m_chatDisplay->BeginTextColour(m_fgColor);
    m_chatDisplay->WriteText(message + "\n");
    m_chatDisplay->EndTextColour();

    m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
}

void ChatArea::AppendAction(const wxString& sender, const wxString& action)
{
    AppendAction(GetCurrentTimestamp(), sender, action);
}

void ChatArea::AppendAction(const wxString& timestamp, const wxString& sender, const wxString& action)
{
    if (!m_chatDisplay) return;
    WriteTimestamp(timestamp);

    m_chatDisplay->BeginTextColour(m_actionColor);
    m_chatDisplay->WriteText("* ");
    m_chatDisplay->BeginBold();
    m_chatDisplay->WriteText(sender);
    m_chatDisplay->EndBold();
    m_chatDisplay->WriteText(" " + action + "\n");
    m_chatDisplay->EndTextColour();

    m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
}

void ChatArea::AppendJoin(const wxString& user)
{
    AppendJoin(GetCurrentTimestamp(), user);
}

void ChatArea::AppendJoin(const wxString& timestamp, const wxString& user)
{
    if (!m_chatDisplay) return;
    WriteTimestamp(timestamp);

    m_chatDisplay->BeginTextColour(m_serviceColor);
    m_chatDisplay->WriteText("--> " + user + " has joined\n");
    m_chatDisplay->EndTextColour();

    m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
}

void ChatArea::AppendLeave(const wxString& user)
{
    AppendLeave(GetCurrentTimestamp(), user);
}

void ChatArea::AppendLeave(const wxString& timestamp, const wxString& user)
{
    if (!m_chatDisplay) return;
    WriteTimestamp(timestamp);

    m_chatDisplay->BeginTextColour(m_serviceColor);
    m_chatDisplay->WriteText("<-- " + user + " has left\n");
    m_chatDisplay->EndTextColour();

    m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
}

void ChatArea::SetUserColors(const wxColour colors[16])
{
    for (int i = 0; i < 16; i++) {
        m_userColors[i] = colors[i];
    }
}

wxColour ChatArea::GetUserColor(const wxString& username) const
{
    // Handle empty username - return a default color
    if (username.IsEmpty()) {
        return m_userColors[0];
    }

    // Current user always gets gray
    if (!m_currentUsername.IsEmpty() && username == m_currentUsername) {
        return m_selfColor;
    }

    // Other users get a color from the palette (no grays)
    unsigned long hash = 0;
    for (size_t i = 0; i < username.length(); i++) {
        hash = static_cast<unsigned long>(username[i].GetValue()) + (hash << 6) + (hash << 16) - hash;
    }
    return m_userColors[hash % 16];
}
