#include "InputBoxWidget.h"
#include "MainFrame.h"
#include "ChatViewWidget.h"
#include "MessageFormatter.h"
#include "WelcomeChat.h"
#include "../telegram/TelegramClient.h"
#include <wx/listctrl.h>
#include <wx/clipbrd.h>
#include <wx/filename.h>
#include <wx/stc/stc.h>
#include <wx/filedlg.h>

InputBoxWidget::InputBoxWidget(wxWindow* parent, MainFrame* mainFrame)
    : wxPanel(parent, wxID_ANY),
      m_mainFrame(mainFrame),
      m_chatView(nullptr),
      m_memberList(nullptr),
      m_messageFormatter(nullptr),
      m_welcomeChat(nullptr),
      m_inputBox(nullptr),
      m_uploadBtn(nullptr),
      m_historyIndex(0),
      m_tabCompletionIndex(0),
      m_tabCompletionActive(false),
      m_bgColor(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW)),
      m_fgColor(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT)),
      m_placeholder("Type a command or message..."),
      m_showingPlaceholder(true),
      m_placeholderColor(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT))
{
    CreateLayout();
    CreateButtons();
}

InputBoxWidget::~InputBoxWidget()
{
}

void InputBoxWidget::CreateLayout()
{
    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);

    // Input text box using wxStyledTextCtrl for block cursor support
    m_inputBox = new wxStyledTextCtrl(this, wxID_ANY,
        wxDefaultPosition, wxDefaultSize,
        wxBORDER_NONE);

    // Configure for single-line input behavior
    m_inputBox->SetUseHorizontalScrollBar(false);
    m_inputBox->SetUseVerticalScrollBar(false);
    m_inputBox->SetWrapMode(wxSTC_WRAP_NONE);
    m_inputBox->SetMarginWidth(0, 0);  // Remove line number margin
    m_inputBox->SetMarginWidth(1, 0);  // Remove symbol margin
    m_inputBox->SetMarginWidth(2, 0);  // Remove fold margin

    // Use system colors
    wxColour bgColor = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
    wxColour fgColor = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    wxColour selBgColor = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT);
    wxColour selFgColor = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT);
    m_inputBox->StyleSetBackground(wxSTC_STYLE_DEFAULT, bgColor);
    m_inputBox->StyleSetForeground(wxSTC_STYLE_DEFAULT, fgColor);
    m_inputBox->StyleClearAll();
    m_inputBox->SetCaretForeground(fgColor);
    m_inputBox->SetSelBackground(true, selBgColor);
    m_inputBox->SetSelForeground(true, selFgColor);

    // Block cursor!
    m_inputBox->SetCaretStyle(wxSTC_CARETSTYLE_BLOCK);

    // Bind events
    m_inputBox->Bind(wxEVT_KEY_DOWN, &InputBoxWidget::OnKeyDown, this);
    m_inputBox->Bind(wxEVT_STC_MODIFIED, &InputBoxWidget::OnTextChanged, this);
    m_inputBox->Bind(wxEVT_SET_FOCUS, &InputBoxWidget::OnFocusGained, this);
    m_inputBox->Bind(wxEVT_KILL_FOCUS, &InputBoxWidget::OnFocusLost, this);

    // Show placeholder initially
    m_inputBox->SetText(m_placeholder);
    m_inputBox->StyleSetForeground(wxSTC_STYLE_DEFAULT, wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    m_inputBox->StyleClearAll();

    // Use ALIGN_CENTER_VERTICAL to center text vertically
    sizer->Add(m_inputBox, 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 2);
    SetSizer(sizer);
}

void InputBoxWidget::CreateButtons()
{
    wxBoxSizer* sizer = dynamic_cast<wxBoxSizer*>(GetSizer());
    if (!sizer) return;

    // Create upload button (text) - use native styling
    m_uploadBtn = new wxButton(this, ID_UPLOAD_BTN, "Upload",
        wxDefaultPosition, wxDefaultSize);
    m_uploadBtn->SetToolTip("Upload file (Ctrl+U)");

    // Bind button event
    m_uploadBtn->Bind(wxEVT_BUTTON, &InputBoxWidget::OnUploadClick, this);

    // Add button to sizer (after the input box, on the right)
    sizer->Add(m_uploadBtn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 4);

    // Initially disabled until logged in and chat selected
    m_uploadBtn->Enable(false);

    Layout();
}

void InputBoxWidget::EnableUploadButtons(bool enable)
{
    if (m_uploadBtn) {
        m_uploadBtn->Enable(enable);
    }
}

void InputBoxWidget::OnUploadClick(wxCommandEvent& event)
{
    if (!m_mainFrame) return;

    int64_t chatId = m_mainFrame->GetCurrentChatId();
    if (chatId == 0) {
        if (m_messageFormatter) {
            m_messageFormatter->AppendServiceMessage(GetCurrentTimestamp(),
                "Select a chat first to upload");
        }
        return;
    }

    // Create popup menu
    wxMenu menu;
    menu.Append(ID_UPLOAD_PHOTO, "Photo");
    menu.Append(ID_UPLOAD_VIDEO, "Video");
    menu.AppendSeparator();
    menu.Append(ID_UPLOAD_FILE, "File");

    // Bind menu events
    menu.Bind(wxEVT_MENU, &InputBoxWidget::OnUploadPhoto, this, ID_UPLOAD_PHOTO);
    menu.Bind(wxEVT_MENU, &InputBoxWidget::OnUploadVideo, this, ID_UPLOAD_VIDEO);
    menu.Bind(wxEVT_MENU, &InputBoxWidget::OnUploadFile, this, ID_UPLOAD_FILE);

    // Show menu below the button
    wxPoint pos = m_uploadBtn->GetPosition();
    pos.y += m_uploadBtn->GetSize().GetHeight();
    PopupMenu(&menu, pos);
}

void InputBoxWidget::OnUploadPhoto(wxCommandEvent& event)
{
    if (!m_mainFrame) return;

    int64_t chatId = m_mainFrame->GetCurrentChatId();
    if (chatId == 0) return;

    wxFileDialog dlg(this, "Select photo to upload", "", "",
        "Images (*.jpg;*.jpeg;*.png;*.gif;*.webp;*.bmp)|*.jpg;*.jpeg;*.png;*.gif;*.webp;*.bmp|"
        "All Files (*.*)|*.*",
        wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);

    if (dlg.ShowModal() == wxID_OK) {
        wxArrayString paths;
        dlg.GetPaths(paths);

        TelegramClient* client = m_mainFrame->GetTelegramClient();
        if (client && client->IsLoggedIn()) {
            for (const wxString& path : paths) {
                client->SendFile(chatId, path);

                if (m_messageFormatter) {
                    wxFileName fn(path);
                    m_messageFormatter->AppendServiceMessage(GetCurrentTimestamp(),
                        "Uploading photo: " + fn.GetFullName());
                }
            }
        }
    }
}

void InputBoxWidget::OnUploadVideo(wxCommandEvent& event)
{
    if (!m_mainFrame) return;

    int64_t chatId = m_mainFrame->GetCurrentChatId();
    if (chatId == 0) return;

    wxFileDialog dlg(this, "Select video to upload", "", "",
        "Videos (*.mp4;*.mkv;*.avi;*.mov;*.webm;*.wmv)|*.mp4;*.mkv;*.avi;*.mov;*.webm;*.wmv|"
        "All Files (*.*)|*.*",
        wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);

    if (dlg.ShowModal() == wxID_OK) {
        wxArrayString paths;
        dlg.GetPaths(paths);

        TelegramClient* client = m_mainFrame->GetTelegramClient();
        if (client && client->IsLoggedIn()) {
            for (const wxString& path : paths) {
                client->SendFile(chatId, path);

                if (m_messageFormatter) {
                    wxFileName fn(path);
                    m_messageFormatter->AppendServiceMessage(GetCurrentTimestamp(),
                        "Uploading video: " + fn.GetFullName());
                }
            }
        }
    }
}

void InputBoxWidget::OnUploadFile(wxCommandEvent& event)
{
    if (!m_mainFrame) return;

    int64_t chatId = m_mainFrame->GetCurrentChatId();
    if (chatId == 0) {
        if (m_messageFormatter) {
            m_messageFormatter->AppendServiceMessage(GetCurrentTimestamp(),
                "Select a chat first to upload files");
        }
        return;
    }

    wxFileDialog dlg(this, "Select file to upload", "", "",
        "All Files (*.*)|*.*|"
        "Documents (*.pdf;*.doc;*.docx;*.xls;*.xlsx;*.txt)|*.pdf;*.doc;*.docx;*.xls;*.xlsx;*.txt|"
        "Archives (*.zip;*.rar;*.7z;*.tar;*.gz)|*.zip;*.rar;*.7z;*.tar;*.gz",
        wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);

    if (dlg.ShowModal() == wxID_OK) {
        wxArrayString paths;
        dlg.GetPaths(paths);

        TelegramClient* client = m_mainFrame->GetTelegramClient();
        if (client && client->IsLoggedIn()) {
            for (const wxString& path : paths) {
                client->SendFile(chatId, path);

                if (m_messageFormatter) {
                    wxFileName fn(path);
                    m_messageFormatter->AppendServiceMessage(GetCurrentTimestamp(),
                        "Uploading: " + fn.GetFullName());
                }
            }
        }
    }
}

void InputBoxWidget::Clear()
{
    if (m_inputBox) {
        m_inputBox->ClearAll();
    }
}

void InputBoxWidget::SetValue(const wxString& value)
{
    if (m_inputBox) {
        m_inputBox->SetText(value);
    }
}

wxString InputBoxWidget::GetValue() const
{
    if (m_showingPlaceholder) {
        return wxString();
    }
    return m_inputBox ? m_inputBox->GetText() : wxString();
}

void InputBoxWidget::SetFocus()
{
    if (m_inputBox) {
        // Clear placeholder when gaining focus
        if (m_showingPlaceholder) {
            m_showingPlaceholder = false;
            m_inputBox->ClearAll();
            m_inputBox->StyleSetForeground(wxSTC_STYLE_DEFAULT, wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
            m_inputBox->StyleClearAll();
        }
        m_inputBox->SetFocus();
    }
}

void InputBoxWidget::SetInsertionPointEnd()
{
    if (m_inputBox) {
        m_inputBox->GotoPos(m_inputBox->GetTextLength());
    }
}

void InputBoxWidget::SetColors(const wxColour& bg, const wxColour& fg)
{
    // Use system colors instead of custom ones
    m_bgColor = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
    m_fgColor = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);

    if (m_inputBox) {
        m_inputBox->StyleSetBackground(wxSTC_STYLE_DEFAULT, m_bgColor);
        m_inputBox->StyleSetForeground(wxSTC_STYLE_DEFAULT, m_fgColor);
        m_inputBox->StyleClearAll();
        m_inputBox->SetCaretForeground(m_fgColor);
        m_inputBox->Refresh();
    }

    // Let upload button use native styling
    if (m_uploadBtn) {
        m_uploadBtn->Refresh();
    }
}

void InputBoxWidget::SetInputFont(const wxFont& font)
{
    m_font = font;

    if (m_inputBox) {
        m_inputBox->StyleSetFont(wxSTC_STYLE_DEFAULT, m_font);
        m_inputBox->StyleClearAll();

        // Calculate height based on font
        int fontHeight = m_font.GetPixelSize().GetHeight();
        if (fontHeight <= 0) {
            fontHeight = m_font.GetPointSize() * 4 / 3;  // Approximate
        }
        SetMinSize(wxSize(-1, fontHeight));

        m_inputBox->Refresh();
        Layout();
    }
}

void InputBoxWidget::SetHint(const wxString& hint)
{
    m_placeholder = hint;
    if (m_showingPlaceholder && m_inputBox) {
        m_inputBox->SetText(m_placeholder);
    }
}

void InputBoxWidget::UpdatePlaceholder()
{
    if (!m_inputBox) return;

    wxString text = m_inputBox->GetText();

    if (text.IsEmpty() && !m_showingPlaceholder) {
        // Show placeholder
        m_showingPlaceholder = true;
        m_inputBox->SetText(m_placeholder);
        m_inputBox->StyleSetForeground(wxSTC_STYLE_DEFAULT, wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        m_inputBox->StyleClearAll();
        m_inputBox->GotoPos(0);
    }
}

void InputBoxWidget::OnTextChanged(wxStyledTextEvent& event)
{
    event.Skip();
}

void InputBoxWidget::OnFocusGained(wxFocusEvent& event)
{
    if (m_showingPlaceholder && m_inputBox) {
        m_showingPlaceholder = false;
        m_inputBox->ClearAll();
        m_inputBox->StyleSetForeground(wxSTC_STYLE_DEFAULT, wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
        m_inputBox->StyleClearAll();
    }
    event.Skip();
}

void InputBoxWidget::OnFocusLost(wxFocusEvent& event)
{
    if (m_inputBox && m_inputBox->GetText().IsEmpty()) {
        UpdatePlaceholder();
    }
    event.Skip();
}

void InputBoxWidget::AddToHistory(const wxString& text)
{
    if (text.IsEmpty()) return;

    // Don't add duplicates of the last entry
    if (m_inputHistory.empty() || m_inputHistory.back() != text) {
        m_inputHistory.push_back(text);
        if (m_inputHistory.size() > MAX_HISTORY_SIZE) {
            m_inputHistory.pop_front();
        }
    }
    m_historyIndex = m_inputHistory.size();
}

void InputBoxWidget::ClearHistory()
{
    m_inputHistory.clear();
    m_historyIndex = 0;
}

void InputBoxWidget::ResetTabCompletion()
{
    m_tabCompletionActive = false;
    m_tabCompletionPrefix.Clear();
    m_tabCompletionIndex = 0;
}

wxString InputBoxWidget::GetCurrentTimestamp() const
{
    wxDateTime now = wxDateTime::Now();
    return now.Format("%H:%M:%S");
}

void InputBoxWidget::OnTextEnter(wxCommandEvent& event)
{
    if (m_showingPlaceholder) {
        return;
    }

    wxString message = m_inputBox->GetText();
    if (message.IsEmpty()) {
        return;
    }

    // Add to input history
    AddToHistory(message);

    // Check if WelcomeChat is active - forward input there for login flow
    // If WelcomeChat doesn't handle it (returns false), continue processing here
    if (m_welcomeChat && m_welcomeChat->IsShown()) {
        if (m_welcomeChat->ProcessInput(message)) {
            m_inputBox->ClearAll();
            return;
        }
        // WelcomeChat didn't handle it - fall through to regular command handling
    }

    // Check if this is a command
    if (message.StartsWith("/")) {
        if (ProcessCommand(message)) {
            m_inputBox->ClearAll();
            return;
        }
    }

    // Not a command - send as regular message
    if (m_mainFrame) {
        TelegramClient* client = m_mainFrame->GetTelegramClient();
        int64_t chatId = m_mainFrame->GetCurrentChatId();

        if (client && client->IsLoggedIn() && chatId != 0) {
            client->SendMessage(chatId, message);
            m_inputBox->ClearAll();
            // Always scroll to bottom when user sends a message
            if (m_chatView) {
                m_chatView->ForceScrollToBottom();
            }
            // Message will appear via OnNewMessage callback
            return;
        }
    }

    // Fallback: display locally if we have a message formatter
    if (m_messageFormatter) {
        wxString sender = m_currentUser.IsEmpty() ? "You" : m_currentUser;
        m_messageFormatter->AppendMessage(GetCurrentTimestamp(), sender, message);
    }

    m_inputBox->ClearAll();
    UpdatePlaceholder();

    if (m_chatView) {
        m_chatView->ScrollToBottom();
    }
}

void InputBoxWidget::OnKeyDown(wxKeyEvent& event)
{
    int keyCode = event.GetKeyCode();

    // Handle Enter key for sending messages
    if (keyCode == WXK_RETURN || keyCode == WXK_NUMPAD_ENTER) {
        if (!event.ShiftDown()) {
            wxCommandEvent evt;
            OnTextEnter(evt);
            return;
        }
    }

    // Check for Ctrl+U (upload menu)
    if (event.ControlDown() && !event.ShiftDown() && keyCode == 'U') {
        if (m_uploadBtn && m_uploadBtn->IsEnabled()) {
            wxCommandEvent evt(wxEVT_BUTTON, ID_UPLOAD_BTN);
            OnUploadClick(evt);
        }
        return;
    }

    // Check for Ctrl+V (paste)
    if (event.ControlDown() && keyCode == 'V') {
        HandleClipboardPaste();
        event.Skip();
        return;
    }

    // Up arrow - previous history
    if (keyCode == WXK_UP) {
        NavigateHistoryUp();
        return; // Don't skip - consume the event
    }

    // Down arrow - next history
    if (keyCode == WXK_DOWN) {
        NavigateHistoryDown();
        return; // Don't skip - consume the event
    }

    // Tab - user name completion
    if (keyCode == WXK_TAB) {
        DoTabCompletion();
        return; // Don't skip - consume the event
    }

    // Any other key resets tab completion
    if (keyCode != WXK_SHIFT && keyCode != WXK_CONTROL && keyCode != WXK_ALT) {
        m_tabCompletionActive = false;
    }

    // Page Up/Down in input box scrolls chat (HexChat style)
    if (keyCode == WXK_PAGEUP) {
        if (m_chatView && m_chatView->GetDisplayCtrl()) {
            m_chatView->GetDisplayCtrl()->PageUp();
        }
        return;
    }
    if (keyCode == WXK_PAGEDOWN) {
        if (m_chatView && m_chatView->GetDisplayCtrl()) {
            m_chatView->GetDisplayCtrl()->PageDown();
        }
        return;
    }

    event.Skip();
}

bool InputBoxWidget::ProcessCommand(const wxString& command)
{
    wxString cmd = command.AfterFirst('/').BeforeFirst(' ').Lower();
    wxString args = command.AfterFirst(' ');

    if (cmd == "me" && !args.IsEmpty()) {
        ProcessMeCommand(args);
        return true;
    }
    else if (cmd == "clear") {
        ProcessClearCommand();
        return true;
    }
    else if (cmd == "query" || cmd == "msg") {
        ProcessQueryCommand(args);
        return true;
    }
    else if (cmd == "leave" || cmd == "close") {
        ProcessLeaveCommand();
        return true;
    }
    else if (cmd == "topic") {
        ProcessTopicCommand(args);
        return true;
    }
    else if (cmd == "whois") {
        ProcessWhoisCommand(args);
        return true;
    }
    else if (cmd == "away") {
        ProcessAwayCommand(args);
        return true;
    }
    else if (cmd == "back") {
        ProcessBackCommand();
        return true;
    }
    else if (cmd == "help") {
        ProcessHelpCommand();
        return true;
    }
    else {
        if (m_messageFormatter) {
            m_messageFormatter->AppendServiceMessage(GetCurrentTimestamp(),
                "Unknown command: /" + cmd + ". Type /help for available commands.");
        }
        return true;
    }

    return false;
}

void InputBoxWidget::ProcessMeCommand(const wxString& args)
{
    wxString sender = m_currentUser.IsEmpty() ? "You" : m_currentUser;

    if (m_messageFormatter) {
        m_messageFormatter->AppendActionMessage(GetCurrentTimestamp(), sender, args);
    }

    // Send via TDLib if connected
    if (m_mainFrame) {
        TelegramClient* client = m_mainFrame->GetTelegramClient();
        int64_t chatId = m_mainFrame->GetCurrentChatId();

        if (client && client->IsLoggedIn() && chatId != 0) {
            client->SendMessage(chatId, "/me " + args);
        }
    }

    if (m_chatView) {
        m_chatView->ScrollToBottom();
    }
}

void InputBoxWidget::ProcessClearCommand()
{
    if (m_chatView) {
        m_chatView->ClearMessages();
    }

    if (m_messageFormatter) {
        m_messageFormatter->AppendServiceMessage(GetCurrentTimestamp(), "Window cleared");
    }
}

void InputBoxWidget::ProcessQueryCommand(const wxString& args)
{
    if (!args.IsEmpty()) {
        wxString target = args.BeforeFirst(' ');
        if (m_messageFormatter) {
            m_messageFormatter->AppendServiceMessage(GetCurrentTimestamp(),
                "Opening query with " + target);
        }
        // TODO: Actually open/create private chat via TDLib
    } else {
        if (m_messageFormatter) {
            m_messageFormatter->AppendServiceMessage(GetCurrentTimestamp(),
                "Usage: /query <username> [message]");
        }
    }
}

void InputBoxWidget::ProcessLeaveCommand()
{
    if (m_messageFormatter) {
        m_messageFormatter->AppendServiceMessage(GetCurrentTimestamp(),
            "Leaving chat...");
    }
    // TODO: Leave/close chat via TDLib
}

void InputBoxWidget::ProcessTopicCommand(const wxString& args)
{
    if (!args.IsEmpty()) {
        if (m_messageFormatter) {
            m_messageFormatter->AppendServiceMessage(GetCurrentTimestamp(),
                "Setting topic: " + args);
        }
        // TODO: Set chat description via TDLib
    } else {
        if (m_messageFormatter) {
            m_messageFormatter->AppendServiceMessage(GetCurrentTimestamp(),
                "Topic: (use /topic <text> to set)");
        }
    }
}

void InputBoxWidget::ProcessWhoisCommand(const wxString& args)
{
    if (!args.IsEmpty()) {
        wxString target = args.BeforeFirst(' ');
        if (m_messageFormatter) {
            m_messageFormatter->AppendNoticeMessage(GetCurrentTimestamp(),
                "Teleliter", "Looking up " + target + "...");
            // TODO: Get user info via TDLib
            m_messageFormatter->AppendNoticeMessage(GetCurrentTimestamp(),
                "Teleliter", target + " is a Telegram user");
        }
    } else {
        if (m_messageFormatter) {
            m_messageFormatter->AppendServiceMessage(GetCurrentTimestamp(),
                "Usage: /whois <username>");
        }
    }
}

void InputBoxWidget::ProcessAwayCommand(const wxString& args)
{
    if (!args.IsEmpty()) {
        if (m_messageFormatter) {
            m_messageFormatter->AppendServiceMessage(GetCurrentTimestamp(),
                "You are now away: " + args);
        }
    } else {
        if (m_messageFormatter) {
            m_messageFormatter->AppendServiceMessage(GetCurrentTimestamp(),
                "You are no longer away");
        }
    }
    // TODO: Set Telegram presence/status
}

void InputBoxWidget::ProcessBackCommand()
{
    if (m_messageFormatter) {
        m_messageFormatter->AppendServiceMessage(GetCurrentTimestamp(),
            "You are no longer away");
    }
}

void InputBoxWidget::ProcessHelpCommand()
{
    if (!m_messageFormatter) return;

    wxString ts = GetCurrentTimestamp();
    m_messageFormatter->AppendServiceMessage(ts, "Available commands:");
    m_messageFormatter->AppendServiceMessage(ts, "  /me <action>     - Send an action message");
    m_messageFormatter->AppendServiceMessage(ts, "  /clear           - Clear chat window");
    m_messageFormatter->AppendServiceMessage(ts, "  /query <user>    - Open private chat");
    m_messageFormatter->AppendServiceMessage(ts, "  /msg <user> <text> - Send private message");
    m_messageFormatter->AppendServiceMessage(ts, "  /whois <user>    - View user info");
    m_messageFormatter->AppendServiceMessage(ts, "  /leave           - Leave current chat");
    m_messageFormatter->AppendServiceMessage(ts, "  /help            - Show this help");

    if (m_chatView) {
        m_chatView->ScrollToBottom();
    }
}

void InputBoxWidget::NavigateHistoryUp()
{
    if (!m_inputHistory.empty() && m_historyIndex > 0) {
        m_historyIndex--;
        m_inputBox->SetText(m_inputHistory[m_historyIndex]);
        m_inputBox->GotoPos(m_inputBox->GetTextLength());
    }
    m_tabCompletionActive = false;
}

void InputBoxWidget::NavigateHistoryDown()
{
    if (!m_inputHistory.empty()) {
        if (m_historyIndex < m_inputHistory.size() - 1) {
            m_historyIndex++;
            m_inputBox->SetText(m_inputHistory[m_historyIndex]);
            m_inputBox->GotoPos(m_inputBox->GetTextLength());
        } else if (m_historyIndex == m_inputHistory.size() - 1) {
            m_historyIndex = m_inputHistory.size();
            m_inputBox->ClearAll();
        }
    }
    m_tabCompletionActive = false;
}

void InputBoxWidget::DoTabCompletion()
{
    if (!m_inputBox || !m_memberList) return;

    wxString text = m_inputBox->GetText();
    long insertionPoint = m_inputBox->GetCurrentPos();

    // Find the word being completed
    long wordStart = insertionPoint;
    while (wordStart > 0 && text[wordStart - 1] != ' ') {
        wordStart--;
    }
    wxString prefix = text.Mid(wordStart, insertionPoint - wordStart);

    if (prefix.IsEmpty()) {
        return;
    }

    // Build list of matching members
    wxArrayString matches = GetMatchingMembers(prefix);

    if (matches.IsEmpty()) {
        return;
    }

    // Cycle through matches
    if (!m_tabCompletionActive || m_tabCompletionPrefix != prefix) {
        m_tabCompletionPrefix = prefix;
        m_tabCompletionIndex = 0;
        m_tabCompletionActive = true;
    } else {
        m_tabCompletionIndex = (m_tabCompletionIndex + 1) % matches.size();
    }

    // Replace the prefix with the match
    wxString completion = matches[m_tabCompletionIndex];
    // Add ": " if at start of line (HexChat style)
    if (wordStart == 0) {
        completion += ": ";
    }

    wxString newText = text.Left(wordStart) + completion + text.Mid(insertionPoint);
    m_inputBox->SetText(newText);
    m_inputBox->GotoPos(wordStart + completion.length());
}

wxArrayString InputBoxWidget::GetMatchingMembers(const wxString& prefix)
{
    wxArrayString matches;

    if (!m_memberList) return matches;

    int memberCount = m_memberList->GetItemCount();
    for (int i = 0; i < memberCount; i++) {
        wxString memberName = m_memberList->GetItemText(i);
        // Remove role suffix if present (e.g., "User (Admin)")
        int parenPos = memberName.Find(" (");
        if (parenPos != wxNOT_FOUND) {
            memberName = memberName.Left(parenPos);
        }
        if (memberName.Lower().StartsWith(prefix.Lower())) {
            matches.Add(memberName);
        }
    }

    return matches;
}

void InputBoxWidget::HandleClipboardPaste()
{
    if (!wxTheClipboard->Open()) return;

    // Check if clipboard has an image
    if (wxTheClipboard->IsSupported(wxDF_BITMAP)) {
        wxBitmapDataObject bitmapData;
        if (wxTheClipboard->GetData(bitmapData)) {
            wxBitmap bitmap = bitmapData.GetBitmap();
            if (bitmap.IsOk() && m_mainFrame) {
                // Save to temp file and "upload"
                wxString tempPath = wxFileName::GetTempDir() + "/teleliter_paste.png";
                bitmap.SaveFile(tempPath, wxBITMAP_TYPE_PNG);

                wxArrayString files;
                files.Add(tempPath);
                m_mainFrame->OnFilesDropped(files);
            }
        }
    }

    wxTheClipboard->Close();
}
