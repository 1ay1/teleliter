#include "MainFrame.h"
#include <wx/artprov.h>

wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_MENU(wxID_EXIT, MainFrame::OnExit)
    EVT_MENU(wxID_ABOUT, MainFrame::OnAbout)
    EVT_MENU(ID_CONNECT, MainFrame::OnConnect)
    EVT_MENU(ID_DISCONNECT, MainFrame::OnDisconnect)
    EVT_MENU(ID_PREFERENCES, MainFrame::OnPreferences)
    EVT_MENU(ID_CLEAR_WINDOW, MainFrame::OnClearWindow)
    EVT_MENU(ID_SHOW_USERLIST, MainFrame::OnToggleUserList)
    EVT_MENU(ID_SHOW_CHANNEL_TREE, MainFrame::OnToggleChannelTree)
    EVT_MENU(ID_FULLSCREEN, MainFrame::OnFullscreen)
    EVT_TREE_SEL_CHANGED(ID_CHAT_TREE, MainFrame::OnChatTreeSelectionChanged)
    EVT_LIST_ITEM_ACTIVATED(ID_USER_LIST, MainFrame::OnUserListItemActivated)
    EVT_TEXT_ENTER(ID_INPUT_BOX, MainFrame::OnInputEnter)
wxEND_EVENT_TABLE()

MainFrame::MainFrame(const wxString& title, const wxPoint& pos, const wxSize& size)
    : wxFrame(NULL, wxID_ANY, title, pos, size),
      m_showUserList(true),
      m_showChannelTree(true),
      m_currentNick("You")
{
    SetupColors();
    CreateMenuBar();
    CreateMainLayout();
    CreateStatusBar();
    PopulateDummyData();
    
    // Set minimum size
    SetMinSize(wxSize(800, 600));
    
    // Set the background color for the frame
    SetBackgroundColour(m_bgColor);
}

void MainFrame::SetupColors()
{
    // HexChat dark theme colors
    m_bgColor = wxColour(46, 52, 54);           // Dark gray background
    m_fgColor = wxColour(211, 215, 207);        // Light gray text
    m_inputBgColor = wxColour(46, 52, 54);      // Dark gray input
    m_inputFgColor = wxColour(211, 215, 207);   // Light gray input text
    m_treeItemBgColor = wxColour(46, 52, 54);   // Tree background
    m_treeItemFgColor = wxColour(211, 215, 207);// Tree text
    m_userListBgColor = wxColour(46, 52, 54);   // User list background
    m_userListFgColor = wxColour(211, 215, 207);// User list text
    m_topicBgColor = wxColour(85, 87, 83);      // Topic bar background
    m_topicFgColor = wxColour(211, 215, 207);   // Topic bar text
    m_timestampColor = wxColour(85, 87, 83);    // Gray timestamps
    m_nicknameColor = wxColour(114, 159, 207);  // Blue nicknames
    m_actionColor = wxColour(173, 127, 168);    // Purple actions
    m_noticeColor = wxColour(252, 175, 62);     // Orange notices
    m_highlightColor = wxColour(239, 41, 41);   // Red highlights
}

void MainFrame::CreateMenuBar()
{
    wxMenuBar* menuBar = new wxMenuBar;
    
    // Teleliter menu (like HexChat menu)
    wxMenu* menuTeleliter = new wxMenu;
    menuTeleliter->Append(ID_NETWORK_LIST, "Network List...\tCtrl+S");
    menuTeleliter->AppendSeparator();
    menuTeleliter->Append(ID_NEW_CHAT, "New Chat...\tCtrl+N");
    menuTeleliter->AppendSeparator();
    menuTeleliter->Append(ID_RAW_LOG, "Raw Log...");
    menuTeleliter->AppendSeparator();
    menuTeleliter->Append(wxID_EXIT, "Quit\tCtrl+Q");
    menuBar->Append(menuTeleliter, "&Teleliter");
    
    // Telegram menu (like Server menu)
    wxMenu* menuTelegram = new wxMenu;
    menuTelegram->Append(ID_CONNECT, "Connect\tCtrl+Shift+C");
    menuTelegram->Append(ID_DISCONNECT, "Disconnect\tCtrl+Shift+D");
    menuTelegram->AppendSeparator();
    menuTelegram->Append(ID_JOIN_GROUP, "Join Group...\tCtrl+J");
    menuTelegram->Append(ID_JOIN_CHANNEL, "Join Channel...\tCtrl+Shift+J");
    menuBar->Append(menuTelegram, "&Telegram");
    
    // Edit menu
    wxMenu* menuEdit = new wxMenu;
    menuEdit->Append(wxID_CUT, "Cut\tCtrl+X");
    menuEdit->Append(wxID_COPY, "Copy\tCtrl+C");
    menuEdit->Append(wxID_PASTE, "Paste\tCtrl+V");
    menuEdit->AppendSeparator();
    menuEdit->Append(ID_CLEAR_WINDOW, "Clear Window\tCtrl+L");
    menuEdit->AppendSeparator();
    menuEdit->Append(ID_PREFERENCES, "Preferences...\tCtrl+P");
    menuBar->Append(menuEdit, "&Edit");
    
    // View menu
    wxMenu* menuView = new wxMenu;
    menuView->AppendCheckItem(ID_SHOW_CHANNEL_TREE, "Channel Tree\tF7");
    menuView->Check(ID_SHOW_CHANNEL_TREE, true);
    menuView->AppendCheckItem(ID_SHOW_USERLIST, "User List\tF8");
    menuView->Check(ID_SHOW_USERLIST, true);
    menuView->AppendSeparator();
    menuView->AppendCheckItem(ID_HIDE_JOIN_PART, "Hide Join/Part Messages");
    menuView->AppendSeparator();
    menuView->Append(ID_FULLSCREEN, "Fullscreen\tF11");
    menuBar->Append(menuView, "&View");
    
    // Window menu
    wxMenu* menuWindow = new wxMenu;
    menuWindow->Append(wxID_ANY, "Previous Tab\tCtrl+Page_Up");
    menuWindow->Append(wxID_ANY, "Next Tab\tCtrl+Page_Down");
    menuWindow->AppendSeparator();
    menuWindow->Append(wxID_ANY, "Close Tab\tCtrl+W");
    menuBar->Append(menuWindow, "&Window");
    
    // Help menu
    wxMenu* menuHelp = new wxMenu;
    menuHelp->Append(wxID_ANY, "Documentation\tF1");
    menuHelp->AppendSeparator();
    menuHelp->Append(wxID_ABOUT, "About Teleliter");
    menuBar->Append(menuHelp, "&Help");
    
    SetMenuBar(menuBar);
}

void MainFrame::CreateMainLayout()
{
    // Main panel to hold everything
    wxPanel* mainPanel = new wxPanel(this);
    mainPanel->SetBackgroundColour(m_bgColor);
    
    // Main horizontal splitter (left tree | rest)
    m_mainSplitter = new wxSplitterWindow(mainPanel, wxID_ANY, 
        wxDefaultPosition, wxDefaultSize, 
        wxSP_LIVE_UPDATE | wxSP_3DSASH);
    m_mainSplitter->SetBackgroundColour(m_bgColor);
    m_mainSplitter->SetMinimumPaneSize(150);
    
    // Left panel for chat tree
    m_leftPanel = new wxPanel(m_mainSplitter);
    m_leftPanel->SetBackgroundColour(m_bgColor);
    CreateChatTree(m_leftPanel);
    
    // Right splitter (chat area | user list)
    m_rightSplitter = new wxSplitterWindow(m_mainSplitter, wxID_ANY,
        wxDefaultPosition, wxDefaultSize,
        wxSP_LIVE_UPDATE | wxSP_3DSASH);
    m_rightSplitter->SetBackgroundColour(m_bgColor);
    m_rightSplitter->SetMinimumPaneSize(100);
    
    // Center panel for chat
    m_chatPanel = new wxPanel(m_rightSplitter);
    m_chatPanel->SetBackgroundColour(m_bgColor);
    CreateChatPanel(m_chatPanel);
    
    // Right panel for user list
    m_rightPanel = new wxPanel(m_rightSplitter);
    m_rightPanel->SetBackgroundColour(m_bgColor);
    CreateUserList(m_rightPanel);
    
    // Split the right splitter (chat | users)
    m_rightSplitter->SplitVertically(m_chatPanel, m_rightPanel, -150);
    
    // Split the main splitter (tree | rest)
    m_mainSplitter->SplitVertically(m_leftPanel, m_rightSplitter, 180);
    
    // Main sizer
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    mainSizer->Add(m_mainSplitter, 1, wxEXPAND);
    mainPanel->SetSizer(mainSizer);
    
    // Frame sizer
    wxBoxSizer* frameSizer = new wxBoxSizer(wxVERTICAL);
    frameSizer->Add(mainPanel, 1, wxEXPAND);
    SetSizer(frameSizer);
}

void MainFrame::CreateChatTree(wxWindow* parent)
{
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    
    // Tree control
    m_chatTree = new wxTreeCtrl(parent, ID_CHAT_TREE,
        wxDefaultPosition, wxDefaultSize,
        wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxTR_NO_LINES | 
        wxTR_FULL_ROW_HIGHLIGHT | wxBORDER_NONE);
    
    m_chatTree->SetBackgroundColour(m_treeItemBgColor);
    m_chatTree->SetForegroundColour(m_treeItemFgColor);
    
    // Create root and category items
    m_treeRoot = m_chatTree->AddRoot("Telegram");
    m_savedMessages = m_chatTree->AppendItem(m_treeRoot, "📌 Saved Messages");
    m_privateChats = m_chatTree->AppendItem(m_treeRoot, "👤 Private Chats");
    m_groups = m_chatTree->AppendItem(m_treeRoot, "👥 Groups");
    m_channels = m_chatTree->AppendItem(m_treeRoot, "📢 Channels");
    m_bots = m_chatTree->AppendItem(m_treeRoot, "🤖 Bots");
    
    // Expand all categories
    m_chatTree->Expand(m_privateChats);
    m_chatTree->Expand(m_groups);
    m_chatTree->Expand(m_channels);
    m_chatTree->Expand(m_bots);
    
    sizer->Add(m_chatTree, 1, wxEXPAND);
    parent->SetSizer(sizer);
}

void MainFrame::CreateChatPanel(wxWindow* parent)
{
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    
    // Topic bar at top
    m_topicBar = new wxTextCtrl(parent, ID_TOPIC_BAR, "",
        wxDefaultPosition, wxSize(-1, 24),
        wxTE_READONLY | wxBORDER_NONE);
    m_topicBar->SetBackgroundColour(m_topicBgColor);
    m_topicBar->SetForegroundColour(m_topicFgColor);
    m_topicBar->SetValue("Welcome to Teleliter - A Telegram client with HexChat interface");
    sizer->Add(m_topicBar, 0, wxEXPAND | wxBOTTOM, 1);
    
    // Chat display area
    m_chatDisplay = new wxRichTextCtrl(parent, ID_CHAT_DISPLAY,
        wxEmptyString, wxDefaultPosition, wxDefaultSize,
        wxRE_MULTILINE | wxRE_READONLY | wxBORDER_NONE | wxVSCROLL);
    m_chatDisplay->SetBackgroundColour(m_bgColor);
    m_chatDisplay->SetBasicStyle(wxRichTextAttr());
    sizer->Add(m_chatDisplay, 1, wxEXPAND);
    
    // Input area at bottom
    CreateInputArea(parent);
    
    // Input panel
    wxPanel* inputPanel = new wxPanel(parent);
    inputPanel->SetBackgroundColour(m_bgColor);
    
    wxBoxSizer* inputSizer = new wxBoxSizer(wxHORIZONTAL);
    
    // Nick label (like HexChat's nick box)
    m_nickLabel = new wxStaticText(inputPanel, wxID_ANY, "[You]");
    m_nickLabel->SetForegroundColour(m_nicknameColor);
    m_nickLabel->SetBackgroundColour(m_bgColor);
    wxFont nickFont = m_nickLabel->GetFont();
    nickFont.SetWeight(wxFONTWEIGHT_BOLD);
    m_nickLabel->SetFont(nickFont);
    inputSizer->Add(m_nickLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    
    // Input text box
    m_inputBox = new wxTextCtrl(inputPanel, ID_INPUT_BOX, "",
        wxDefaultPosition, wxSize(-1, 26),
        wxTE_PROCESS_ENTER | wxBORDER_SIMPLE);
    m_inputBox->SetBackgroundColour(m_inputBgColor);
    m_inputBox->SetForegroundColour(m_inputFgColor);
    m_inputBox->SetHint("Type a message...");
    inputSizer->Add(m_inputBox, 1, wxEXPAND);
    
    inputPanel->SetSizer(inputSizer);
    sizer->Add(inputPanel, 0, wxEXPAND | wxTOP, 2);
    
    parent->SetSizer(sizer);
}

void MainFrame::CreateInputArea(wxWindow* parent)
{
    // This is handled in CreateChatPanel for cleaner organization
}

void MainFrame::CreateUserList(wxWindow* parent)
{
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    
    // User count label at top
    m_userCountLabel = new wxStaticText(parent, wxID_ANY, "0 users");
    m_userCountLabel->SetForegroundColour(m_fgColor);
    m_userCountLabel->SetBackgroundColour(m_bgColor);
    sizer->Add(m_userCountLabel, 0, wxEXPAND | wxALL, 3);
    
    // User list
    m_userList = new wxListCtrl(parent, ID_USER_LIST,
        wxDefaultPosition, wxDefaultSize,
        wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER | wxBORDER_NONE);
    m_userList->SetBackgroundColour(m_userListBgColor);
    m_userList->SetForegroundColour(m_userListFgColor);
    
    // Add a single column
    m_userList->InsertColumn(0, "Users", wxLIST_FORMAT_LEFT, 140);
    
    sizer->Add(m_userList, 1, wxEXPAND);
    parent->SetSizer(sizer);
}

void MainFrame::CreateStatusBar()
{
    wxStatusBar* statusBar = wxFrame::CreateStatusBar(3);
    
    // Set field widths
    int widths[] = {-3, -1, -1};
    statusBar->SetStatusWidths(3, widths);
    
    statusBar->SetBackgroundColour(m_bgColor);
    
    // Set initial status
    SetStatusText("Not connected", 0);
    SetStatusText("Lag: N/A", 1);
    SetStatusText("", 2);
}

void MainFrame::PopulateDummyData()
{
    // Add some dummy chats to the tree
    m_chatTree->AppendItem(m_privateChats, "Alice");
    m_chatTree->AppendItem(m_privateChats, "Bob");
    m_chatTree->AppendItem(m_privateChats, "Charlie");
    
    m_chatTree->AppendItem(m_groups, "Family Group");
    m_chatTree->AppendItem(m_groups, "Work Team");
    m_chatTree->AppendItem(m_groups, "Linux Enthusiasts");
    
    m_chatTree->AppendItem(m_channels, "Tech News");
    m_chatTree->AppendItem(m_channels, "Daily Memes");
    
    m_chatTree->AppendItem(m_bots, "BotFather");
    m_chatTree->AppendItem(m_bots, "GitHubBot");
    
    // Expand categories
    m_chatTree->Expand(m_privateChats);
    m_chatTree->Expand(m_groups);
    m_chatTree->Expand(m_channels);
    m_chatTree->Expand(m_bots);
    
    // Add some dummy users to the user list
    wxListItem item;
    item.SetId(0);
    
    long idx = 0;
    m_userList->InsertItem(idx++, "👑 Admin");
    m_userList->InsertItem(idx++, "🔧 Moderator");
    m_userList->InsertItem(idx++, "Alice");
    m_userList->InsertItem(idx++, "Bob");
    m_userList->InsertItem(idx++, "Charlie");
    m_userList->InsertItem(idx++, "David");
    m_userList->InsertItem(idx++, "Eve");
    
    m_userCountLabel->SetLabel(wxString::Format("%ld users", idx));
    
    // Add some dummy messages to the chat display
    m_chatDisplay->BeginSuppressUndo();
    
    // Welcome message
    m_chatDisplay->BeginTextColour(m_noticeColor);
    m_chatDisplay->WriteText("* Now talking in #Linux Enthusiasts\n");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_noticeColor);
    m_chatDisplay->WriteText("* Topic is: Welcome to the Linux Enthusiasts group! | Rules: Be nice, no spam\n");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_noticeColor);
    m_chatDisplay->WriteText("* Set by Admin on 2024-01-15\n\n");
    m_chatDisplay->EndTextColour();
    
    // Sample messages
    AppendMessage("12:30:01", "Alice", "Hey everyone! Just installed Arch btw 🐧", false);
    AppendMessage("12:30:15", "Bob", "Nice! How long did it take?", false);
    AppendMessage("12:30:45", "Alice", "About 3 hours, but I learned a lot", false);
    AppendMessage("12:31:02", "Charlie", "I use NixOS btw", false);
    AppendMessage("12:31:30", "Admin", "Remember to check the wiki before asking questions!", false);
    AppendMessage("12:32:00", "David", "Has anyone tried the new kernel 6.7?", false);
    AppendMessage("12:32:15", "Eve", "Yes! The performance improvements are great", false);
    
    m_chatDisplay->EndSuppressUndo();
    
    // Scroll to bottom
    m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
}

void MainFrame::AppendMessage(const wxString& timestamp, const wxString& nick, 
                              const wxString& message, bool isAction)
{
    // Timestamp
    m_chatDisplay->BeginTextColour(m_timestampColor);
    m_chatDisplay->WriteText("[" + timestamp + "] ");
    m_chatDisplay->EndTextColour();
    
    if (isAction) {
        // Action message (* nick does something)
        m_chatDisplay->BeginTextColour(m_actionColor);
        m_chatDisplay->WriteText("* " + nick + " " + message + "\n");
        m_chatDisplay->EndTextColour();
    } else {
        // Regular message
        m_chatDisplay->BeginTextColour(m_nicknameColor);
        m_chatDisplay->BeginBold();
        m_chatDisplay->WriteText("<" + nick + "> ");
        m_chatDisplay->EndBold();
        m_chatDisplay->EndTextColour();
        
        m_chatDisplay->BeginTextColour(m_fgColor);
        m_chatDisplay->WriteText(message + "\n");
        m_chatDisplay->EndTextColour();
    }
}

// Event Handlers

void MainFrame::OnExit(wxCommandEvent& event)
{
    Close(true);
}

void MainFrame::OnAbout(wxCommandEvent& event)
{
    wxMessageBox("Teleliter - A Telegram client with HexChat interface\n\n"
                 "Version 0.1.0\n\n"
                 "A modern Telegram client inspired by the classic HexChat IRC client.",
                 "About Teleliter",
                 wxOK | wxICON_INFORMATION, this);
}

void MainFrame::OnConnect(wxCommandEvent& event)
{
    SetStatusText("Connecting to Telegram...", 0);
    // TODO: Implement actual Telegram connection
}

void MainFrame::OnDisconnect(wxCommandEvent& event)
{
    SetStatusText("Disconnected", 0);
    // TODO: Implement actual disconnection
}

void MainFrame::OnPreferences(wxCommandEvent& event)
{
    wxMessageBox("Preferences dialog will be implemented soon.",
                 "Preferences", wxOK | wxICON_INFORMATION, this);
}

void MainFrame::OnClearWindow(wxCommandEvent& event)
{
    m_chatDisplay->Clear();
}

void MainFrame::OnToggleUserList(wxCommandEvent& event)
{
    m_showUserList = !m_showUserList;
    if (m_showUserList) {
        m_rightSplitter->SplitVertically(m_chatPanel, m_rightPanel, -150);
    } else {
        m_rightSplitter->Unsplit(m_rightPanel);
    }
}

void MainFrame::OnToggleChannelTree(wxCommandEvent& event)
{
    m_showChannelTree = !m_showChannelTree;
    if (m_showChannelTree) {
        m_mainSplitter->SplitVertically(m_leftPanel, m_rightSplitter, 180);
    } else {
        m_mainSplitter->Unsplit(m_leftPanel);
    }
}

void MainFrame::OnFullscreen(wxCommandEvent& event)
{
    ShowFullScreen(!IsFullScreen());
}

void MainFrame::OnChatTreeSelectionChanged(wxTreeEvent& event)
{
    wxTreeItemId item = event.GetItem();
    if (item.IsOk()) {
        wxString chatName = m_chatTree->GetItemText(item);
        
        // Don't do anything for category items
        if (item == m_savedMessages || item == m_privateChats || 
            item == m_groups || item == m_channels || item == m_bots) {
            return;
        }
        
        // Update topic bar
        m_topicBar->SetValue("Chat: " + chatName);
        
        // Update status bar
        SetStatusText("Viewing: " + chatName, 0);
    }
}

void MainFrame::OnUserListItemActivated(wxListEvent& event)
{
    long index = event.GetIndex();
    wxString username = m_userList->GetItemText(index);
    
    // Remove emoji prefixes for display
    if (username.StartsWith("👑 ") || username.StartsWith("🔧 ")) {
        username = username.Mid(2);
    }
    
    wxMessageBox("User info for: " + username + "\n\n"
                 "Double-click to start private chat.",
                 "User Info", wxOK | wxICON_INFORMATION, this);
}

void MainFrame::OnInputEnter(wxCommandEvent& event)
{
    wxString message = m_inputBox->GetValue();
    if (message.IsEmpty()) {
        return;
    }
    
    // Get current time
    wxDateTime now = wxDateTime::Now();
    wxString timestamp = now.Format("%H:%M:%S");
    
    // Check for action command (/me)
    if (message.StartsWith("/me ")) {
        wxString action = message.Mid(4);
        AppendMessage(timestamp, m_currentNick, action, true);
    } else {
        AppendMessage(timestamp, m_currentNick, message, false);
    }
    
    // Clear input and scroll to bottom
    m_inputBox->Clear();
    m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
}

void MainFrame::OnInputKeyDown(wxKeyEvent& event)
{
    // Handle special keys like tab completion, history, etc.
    event.Skip();
}