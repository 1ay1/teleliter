#include "MainFrame.h"
#include "WelcomeChat.h"
#include "TelegramClient.h"
#include <wx/artprov.h>
#include <wx/settings.h>
#include <wx/filename.h>
#include <ctime>

wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_MENU(wxID_EXIT, MainFrame::OnExit)
    EVT_MENU(wxID_ABOUT, MainFrame::OnAbout)
    EVT_MENU(ID_LOGIN, MainFrame::OnLogin)
    EVT_MENU(ID_LOGOUT, MainFrame::OnLogout)
    EVT_MENU(ID_NEW_CHAT, MainFrame::OnNewChat)
    EVT_MENU(ID_NEW_GROUP, MainFrame::OnNewGroup)
    EVT_MENU(ID_NEW_CHANNEL, MainFrame::OnNewChannel)
    EVT_MENU(ID_CONTACTS, MainFrame::OnContacts)
    EVT_MENU(ID_SEARCH, MainFrame::OnSearch)
    EVT_MENU(ID_SAVED_MESSAGES, MainFrame::OnSavedMessages)
    EVT_MENU(ID_UPLOAD_FILE, MainFrame::OnUploadFile)
    EVT_MENU(ID_PREFERENCES, MainFrame::OnPreferences)
    EVT_MENU(ID_CLEAR_WINDOW, MainFrame::OnClearWindow)
    EVT_MENU(ID_SHOW_CHAT_LIST, MainFrame::OnToggleChatList)
    EVT_MENU(ID_SHOW_MEMBERS, MainFrame::OnToggleMembers)
    EVT_MENU(ID_SHOW_CHAT_INFO, MainFrame::OnToggleChatInfo)
    EVT_MENU(ID_FULLSCREEN, MainFrame::OnFullscreen)
    EVT_TREE_SEL_CHANGED(ID_CHAT_TREE, MainFrame::OnChatTreeSelectionChanged)
    EVT_TREE_ITEM_ACTIVATED(ID_CHAT_TREE, MainFrame::OnChatTreeItemActivated)
    EVT_LIST_ITEM_ACTIVATED(ID_MEMBER_LIST, MainFrame::OnMemberListItemActivated)
    EVT_LIST_ITEM_RIGHT_CLICK(ID_MEMBER_LIST, MainFrame::OnMemberListRightClick)
    EVT_TEXT_ENTER(ID_INPUT_BOX, MainFrame::OnInputEnter)
wxEND_EVENT_TABLE()

MainFrame::MainFrame(const wxString& title, const wxPoint& pos, const wxSize& size)
    : wxFrame(NULL, wxID_ANY, title, pos, size),
      m_telegramClient(nullptr),
      m_mainSplitter(nullptr),
      m_rightSplitter(nullptr),
      m_leftPanel(nullptr),
      m_chatTree(nullptr),
      m_chatPanel(nullptr),
      m_welcomeChat(nullptr),
      m_chatInfoBar(nullptr),
      m_chatDisplay(nullptr),
      m_inputBox(nullptr),
      m_rightPanel(nullptr),
      m_memberList(nullptr),
      m_memberCountLabel(nullptr),
      m_mediaPopup(nullptr),
      m_progressGauge(nullptr),
      m_progressLabel(nullptr),
      m_showChatList(true),
      m_showMembers(true),
      m_showChatInfo(true),
      m_isLoggedIn(false),
      m_currentUser(""),
      m_currentChatId(0),
      m_currentChatTitle(""),
      m_currentChatType(TelegramChatType::Private)
{
    SetupColors();
    SetupFonts();
    CreateMenuBar();
    CreateMainLayout();
    SetupStatusBar();
    
    // Create media popup (hidden initially)
    m_mediaPopup = new MediaPopup(this);
    
    // Create TelegramClient and start it immediately for faster login
    m_telegramClient = new TelegramClient();
    m_telegramClient->SetMainFrame(this);
    m_telegramClient->SetWelcomeChat(m_welcomeChat);
    
    // Connect WelcomeChat to TelegramClient
    if (m_welcomeChat) {
        m_welcomeChat->SetTelegramClient(m_telegramClient);
    }
    
    // Start TDLib immediately in background so it's ready when user wants to login
    m_telegramClient->Start();
    
    // Update status bar to show connecting
    SetStatusText("Connecting...", 3);
    
    // Ensure welcome chat is visible on startup
    if (m_welcomeChat && m_chatDisplay && m_chatPanel) {
        wxSizer* sizer = m_chatPanel->GetSizer();
        if (sizer) {
            sizer->Show(m_welcomeChat, true);
            sizer->Show(m_chatDisplay, false);
            m_chatPanel->Layout();
        }
    }
    
    SetMinSize(wxSize(800, 600));
    SetBackgroundColour(m_bgColor);
}

MainFrame::~MainFrame()
{
    if (m_telegramClient) {
        m_telegramClient->Stop();
        delete m_telegramClient;
        m_telegramClient = nullptr;
    }
}

void MainFrame::SetupColors()
{
    // HexChat-style dark theme
    m_bgColor = wxColour(0x2B, 0x2B, 0x2B);
    m_fgColor = wxColour(0xD3, 0xD7, 0xCF);
    m_inputBgColor = wxColour(0x2B, 0x2B, 0x2B);
    m_inputFgColor = wxColour(0xD3, 0xD7, 0xCF);
    m_treeBgColor = wxColour(0x2B, 0x2B, 0x2B);
    m_treeFgColor = wxColour(0xD3, 0xD7, 0xCF);
    m_treeSelBgColor = wxColour(0x3C, 0x3C, 0x3C);
    m_memberListBgColor = wxColour(0x2B, 0x2B, 0x2B);
    m_memberListFgColor = wxColour(0xD3, 0xD7, 0xCF);
    m_chatInfoBgColor = wxColour(0x23, 0x23, 0x23);
    m_chatInfoFgColor = wxColour(0xD3, 0xD7, 0xCF);
    
    // Message colors
    m_timestampColor = wxColour(0x88, 0x88, 0x88);
    m_textColor = wxColour(0xD3, 0xD7, 0xCF);
    m_serviceColor = wxColour(0x88, 0x88, 0x88);
    m_highlightColor = wxColour(0xFC, 0xAF, 0x3E);
    m_linkColor = wxColour(0x72, 0x9F, 0xCF);
    m_mediaColor = wxColour(0x8A, 0xE2, 0x34);
    m_editedColor = wxColour(0x88, 0x88, 0x88);
    m_forwardColor = wxColour(0x72, 0x9F, 0xCF);
    m_replyColor = wxColour(0x88, 0x88, 0x88);
    
    // User colors (for sender names)
    m_userColors[0]  = wxColour(0xCC, 0xCC, 0xCC);
    m_userColors[1]  = wxColour(0x35, 0x36, 0xB2);
    m_userColors[2]  = wxColour(0x2A, 0x8C, 0x2A);
    m_userColors[3]  = wxColour(0xC3, 0x38, 0x38);
    m_userColors[4]  = wxColour(0xC7, 0x38, 0x38);
    m_userColors[5]  = wxColour(0x80, 0x00, 0x80);
    m_userColors[6]  = wxColour(0xFF, 0x80, 0x00);
    m_userColors[7]  = wxColour(0x80, 0x80, 0x00);
    m_userColors[8]  = wxColour(0x33, 0xCC, 0x33);
    m_userColors[9]  = wxColour(0x00, 0x80, 0x80);
    m_userColors[10] = wxColour(0x33, 0xCC, 0xCC);
    m_userColors[11] = wxColour(0x66, 0x66, 0xFF);
    m_userColors[12] = wxColour(0xFF, 0x00, 0xFF);
    m_userColors[13] = wxColour(0x80, 0x80, 0x80);
    m_userColors[14] = wxColour(0xCC, 0xCC, 0xCC);
    m_userColors[15] = wxColour(0x72, 0x9F, 0xCF);
}

void MainFrame::SetupFonts()
{
    wxArrayString monoFonts;
    monoFonts.Add("Monospace");
    monoFonts.Add("DejaVu Sans Mono");
    monoFonts.Add("Consolas");
    monoFonts.Add("Monaco");
    monoFonts.Add("Menlo");
    monoFonts.Add("Courier New");
    
    wxString fontName = "Monospace";
    for (const auto& font : monoFonts) {
        if (wxFontEnumerator::IsValidFacename(font)) {
            fontName = font;
            break;
        }
    }
    
    m_chatFont = wxFont(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, fontName);
    m_treeFont = wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    m_memberListFont = wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    m_inputFont = wxFont(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, fontName);
}

void MainFrame::CreateMenuBar()
{
    wxMenuBar* menuBar = new wxMenuBar;
    
    // Teleliter menu
    wxMenu* menuApp = new wxMenu;
    menuApp->Append(ID_LOGIN, "Login...\tCtrl+L");
    menuApp->Append(ID_LOGOUT, "Logout");
    menuApp->AppendSeparator();
    menuApp->Append(ID_RAW_LOG, "TDLib Log...");
    menuApp->AppendSeparator();
    menuApp->Append(wxID_EXIT, "Quit\tCtrl+Q");
    menuBar->Append(menuApp, "&Teleliter");
    
    // Telegram menu
    wxMenu* menuTelegram = new wxMenu;
    menuTelegram->Append(ID_NEW_CHAT, "New Private Chat...\tCtrl+N");
    menuTelegram->Append(ID_NEW_GROUP, "New Group...\tCtrl+G");
    menuTelegram->Append(ID_NEW_CHANNEL, "New Channel...");
    menuTelegram->AppendSeparator();
    menuTelegram->Append(ID_CONTACTS, "Contacts...\tCtrl+Shift+C");
    menuTelegram->Append(ID_SEARCH, "Search...\tCtrl+F");
    menuTelegram->AppendSeparator();
    menuTelegram->Append(ID_SAVED_MESSAGES, "Saved Messages");
    menuTelegram->AppendSeparator();
    menuTelegram->Append(ID_UPLOAD_FILE, "Upload File...\tCtrl+U");
    menuBar->Append(menuTelegram, "&Telegram");
    
    // Edit menu
    wxMenu* menuEdit = new wxMenu;
    menuEdit->Append(wxID_CUT, "Cut\tCtrl+X");
    menuEdit->Append(wxID_COPY, "Copy\tCtrl+C");
    menuEdit->Append(wxID_PASTE, "Paste\tCtrl+V");
    menuEdit->AppendSeparator();
    menuEdit->Append(ID_CLEAR_WINDOW, "Clear Chat Window\tCtrl+Shift+L");
    menuEdit->AppendSeparator();
    menuEdit->Append(ID_PREFERENCES, "Preferences\tCtrl+E");
    menuBar->Append(menuEdit, "&Edit");
    
    // View menu
    wxMenu* menuView = new wxMenu;
    menuView->AppendCheckItem(ID_SHOW_CHAT_LIST, "Chat List\tF9");
    menuView->Check(ID_SHOW_CHAT_LIST, true);
    menuView->AppendCheckItem(ID_SHOW_MEMBERS, "Members List\tF7");
    menuView->Check(ID_SHOW_MEMBERS, true);
    menuView->AppendCheckItem(ID_SHOW_CHAT_INFO, "Chat Info Bar");
    menuView->Check(ID_SHOW_CHAT_INFO, true);
    menuView->AppendSeparator();
    menuView->Append(ID_FULLSCREEN, "Fullscreen\tF11");
    menuBar->Append(menuView, "&View");
    
    // Window menu
    wxMenu* menuWindow = new wxMenu;
    menuWindow->Append(wxID_ANY, "Previous Chat\tCtrl+Page_Up");
    menuWindow->Append(wxID_ANY, "Next Chat\tCtrl+Page_Down");
    menuWindow->AppendSeparator();
    menuWindow->Append(wxID_ANY, "Close Chat\tCtrl+W");
    menuBar->Append(menuWindow, "&Window");
    
    // Help menu
    wxMenu* menuHelp = new wxMenu;
    menuHelp->Append(wxID_ANY, "Documentation\tF1");
    menuHelp->AppendSeparator();
    menuHelp->Append(wxID_ABOUT, "About");
    menuBar->Append(menuHelp, "&Help");
    
    SetMenuBar(menuBar);
}

void MainFrame::CreateMainLayout()
{
    wxPanel* mainPanel = new wxPanel(this);
    mainPanel->SetBackgroundColour(m_bgColor);
    
    // Main horizontal splitter (chat list | rest)
    m_mainSplitter = new wxSplitterWindow(mainPanel, wxID_ANY, 
        wxDefaultPosition, wxDefaultSize, 
        wxSP_LIVE_UPDATE | wxSP_3DSASH | wxSP_NO_XP_THEME);
    m_mainSplitter->SetBackgroundColour(wxColour(0x1A, 0x1A, 0x1A));
    m_mainSplitter->SetSashGravity(0.0);
    m_mainSplitter->SetMinimumPaneSize(120);
    
    // Left panel - Chat list
    m_leftPanel = new wxPanel(m_mainSplitter);
    m_leftPanel->SetBackgroundColour(m_treeBgColor);
    CreateChatList(m_leftPanel);
    
    // Right splitter (chat area | member list)
    m_rightSplitter = new wxSplitterWindow(m_mainSplitter, wxID_ANY,
        wxDefaultPosition, wxDefaultSize,
        wxSP_LIVE_UPDATE | wxSP_3DSASH | wxSP_NO_XP_THEME);
    m_rightSplitter->SetBackgroundColour(wxColour(0x1A, 0x1A, 0x1A));
    m_rightSplitter->SetSashGravity(1.0);
    m_rightSplitter->SetMinimumPaneSize(100);
    
    // Center panel - Chat
    m_chatPanel = new wxPanel(m_rightSplitter);
    m_chatPanel->SetBackgroundColour(m_bgColor);
    CreateChatPanel(m_chatPanel);
    
    // Right panel - Member list
    m_rightPanel = new wxPanel(m_rightSplitter);
    m_rightPanel->SetBackgroundColour(m_memberListBgColor);
    CreateMemberList(m_rightPanel);
    
    // Split the right splitter (chat | members)
    m_rightSplitter->SplitVertically(m_chatPanel, m_rightPanel, -130);
    
    // Split the main splitter (chat list | rest)
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

void MainFrame::CreateChatList(wxWindow* parent)
{
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    
    // Tree control for chat list
    m_chatTree = new wxTreeCtrl(parent, ID_CHAT_TREE,
        wxDefaultPosition, wxDefaultSize,
        wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxTR_NO_LINES | 
        wxTR_FULL_ROW_HIGHLIGHT | wxBORDER_NONE | wxTR_SINGLE);
    
    m_chatTree->SetBackgroundColour(m_treeBgColor);
    m_chatTree->SetForegroundColour(m_treeFgColor);
    m_chatTree->SetFont(m_treeFont);
    
    // Create root
    m_treeRoot = m_chatTree->AddRoot("Chats");
    
    // Add Teleliter at the top (like HexChat's network/status)
    m_teleliterItem = m_chatTree->AppendItem(m_treeRoot, "Teleliter");
    m_chatTree->SetItemBold(m_teleliterItem, true);
    
    // Create categories
    m_pinnedChats = m_chatTree->AppendItem(m_treeRoot, "Pinned");
    m_privateChats = m_chatTree->AppendItem(m_treeRoot, "Private Chats");
    m_groups = m_chatTree->AppendItem(m_treeRoot, "Groups");
    m_channels = m_chatTree->AppendItem(m_treeRoot, "Channels");
    m_bots = m_chatTree->AppendItem(m_treeRoot, "Bots");
    
    // Make categories bold
    m_chatTree->SetItemBold(m_pinnedChats, true);
    m_chatTree->SetItemBold(m_privateChats, true);
    m_chatTree->SetItemBold(m_groups, true);
    m_chatTree->SetItemBold(m_channels, true);
    m_chatTree->SetItemBold(m_bots, true);
    
    // Select Teleliter by default
    m_chatTree->SelectItem(m_teleliterItem);
    
    sizer->Add(m_chatTree, 1, wxEXPAND);
    parent->SetSizer(sizer);
}

void MainFrame::CreateChatPanel(wxWindow* parent)
{
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    
    // Chat info bar (shows chat name and description)
    m_chatInfoBar = new wxTextCtrl(parent, ID_CHAT_INFO_BAR, "Teleliter",
        wxDefaultPosition, wxSize(-1, 22),
        wxTE_READONLY | wxBORDER_NONE);
    m_chatInfoBar->SetBackgroundColour(m_chatInfoBgColor);
    m_chatInfoBar->SetForegroundColour(m_chatInfoFgColor);
    m_chatInfoBar->SetFont(m_chatFont);
    sizer->Add(m_chatInfoBar, 0, wxEXPAND);
    
    // Separator line
    wxPanel* separator = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    separator->SetBackgroundColour(wxColour(0x1A, 0x1A, 0x1A));
    sizer->Add(separator, 0, wxEXPAND);
    
    // Welcome chat panel (shown when "Teleliter" is selected in tree)
    m_welcomeChat = new WelcomeChat(parent, this);
    sizer->Add(m_welcomeChat, 1, wxEXPAND);
    
    // Regular chat display (hidden initially, shown when a chat is selected)
    m_chatDisplay = new wxRichTextCtrl(parent, ID_CHAT_DISPLAY,
        wxEmptyString, wxDefaultPosition, wxDefaultSize,
        wxRE_MULTILINE | wxRE_READONLY | wxBORDER_NONE | wxVSCROLL);
    m_chatDisplay->SetBackgroundColour(m_bgColor);
    m_chatDisplay->SetFont(m_chatFont);
    
    wxRichTextAttr defaultStyle;
    defaultStyle.SetTextColour(m_textColor);
    defaultStyle.SetBackgroundColour(m_bgColor);
    defaultStyle.SetFont(m_chatFont);
    m_chatDisplay->SetDefaultStyle(defaultStyle);
    m_chatDisplay->SetBasicStyle(defaultStyle);
    
    // Bind mouse events for hover detection
    m_chatDisplay->Bind(wxEVT_MOTION, &MainFrame::OnChatDisplayMouseMove, this);
    m_chatDisplay->Bind(wxEVT_LEAVE_WINDOW, &MainFrame::OnChatDisplayMouseLeave, this);
    m_chatDisplay->Bind(wxEVT_LEFT_DOWN, &MainFrame::OnChatDisplayLeftDown, this);
    
    sizer->Add(m_chatDisplay, 1, wxEXPAND);
    
    // Hide chat display from sizer (not just visually) - welcome chat is shown initially
    sizer->Show(m_chatDisplay, false);
    sizer->Show(m_welcomeChat, true);
    
    // Bottom separator
    wxPanel* separator2 = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    separator2->SetBackgroundColour(wxColour(0x1A, 0x1A, 0x1A));
    sizer->Add(separator2, 0, wxEXPAND);
    
    // Input area
    wxPanel* inputPanel = new wxPanel(parent);
    inputPanel->SetBackgroundColour(m_bgColor);
    
    wxBoxSizer* inputSizer = new wxBoxSizer(wxHORIZONTAL);
    
    // Input text box
    m_inputBox = new wxTextCtrl(inputPanel, ID_INPUT_BOX, "",
        wxDefaultPosition, wxSize(-1, 24),
        wxTE_PROCESS_ENTER | wxBORDER_NONE);
    m_inputBox->SetBackgroundColour(m_inputBgColor);
    m_inputBox->SetForegroundColour(m_inputFgColor);
    m_inputBox->SetFont(m_inputFont);
    m_inputBox->SetHint("Type a command or message...");
    
    // Bind key events for clipboard paste
    m_inputBox->Bind(wxEVT_KEY_DOWN, &MainFrame::OnInputKeyDown, this);
    
    inputSizer->Add(m_inputBox, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);
    
    inputPanel->SetSizer(inputSizer);
    sizer->Add(inputPanel, 0, wxEXPAND | wxALL, 2);
    
    parent->SetSizer(sizer);
    
    // Set up drag and drop for file uploads
    FileDropTarget* dropTarget = new FileDropTarget(this, 
        [this](const wxArrayString& files) {
            OnFilesDropped(files);
        });
    m_chatDisplay->SetDropTarget(dropTarget);
}

void MainFrame::CreateMemberList(wxWindow* parent)
{
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    
    // Member list
    m_memberList = new wxListCtrl(parent, ID_MEMBER_LIST,
        wxDefaultPosition, wxDefaultSize,
        wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER | wxBORDER_NONE);
    m_memberList->SetBackgroundColour(m_memberListBgColor);
    m_memberList->SetForegroundColour(m_memberListFgColor);
    m_memberList->SetFont(m_memberListFont);
    
    // Single column for usernames
    m_memberList->InsertColumn(0, "Members", wxLIST_FORMAT_LEFT, 120);
    
    sizer->Add(m_memberList, 1, wxEXPAND);
    
    // Member count at bottom
    m_memberCountLabel = new wxStaticText(parent, wxID_ANY, "0 members");
    m_memberCountLabel->SetForegroundColour(m_treeFgColor);
    m_memberCountLabel->SetBackgroundColour(m_memberListBgColor);
    m_memberCountLabel->SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    sizer->Add(m_memberCountLabel, 0, wxALL, 3);
    
    parent->SetSizer(sizer);
}

void MainFrame::SetupStatusBar()
{
    wxStatusBar* statusBar = wxFrame::CreateStatusBar(4);
    statusBar->SetBackgroundColour(m_bgColor);
    
    // Field widths: chat info | progress label | progress gauge | connection status
    int widths[] = {-3, 150, 100, 80};
    statusBar->SetStatusWidths(4, widths);
    
    // Create progress label (in field 1)
    m_progressLabel = new wxStaticText(statusBar, wxID_ANY, "");
    m_progressLabel->SetForegroundColour(m_fgColor);
    m_progressLabel->SetBackgroundColour(m_bgColor);
    m_progressLabel->Hide();
    
    // Create progress gauge (in field 2)
    m_progressGauge = new wxGauge(statusBar, wxID_ANY, 100,
        wxDefaultPosition, wxSize(90, 16), wxGA_HORIZONTAL | wxGA_SMOOTH);
    m_progressGauge->SetBackgroundColour(m_bgColor);
    m_progressGauge->Hide();
    
    // Position the widgets in the status bar
    wxRect labelRect, gaugeRect;
    statusBar->GetFieldRect(1, labelRect);
    statusBar->GetFieldRect(2, gaugeRect);
    
    m_progressLabel->SetPosition(wxPoint(labelRect.x + 2, labelRect.y + 2));
    m_progressLabel->SetSize(labelRect.width - 4, labelRect.height - 4);
    
    m_progressGauge->SetPosition(wxPoint(gaugeRect.x + 2, gaugeRect.y + 2));
    m_progressGauge->SetSize(gaugeRect.width - 4, gaugeRect.height - 4);
    
    SetStatusText("Not logged in", 0);
    SetStatusText("Connecting...", 3);
    
    // Setup transfer manager callbacks
    m_transferManager.SetProgressCallback([this](const TransferInfo& info) {
        UpdateTransferProgress(info);
    });
    m_transferManager.SetCompleteCallback([this](const TransferInfo& info) {
        OnTransferComplete(info);
    });
    m_transferManager.SetErrorCallback([this](const TransferInfo& info) {
        OnTransferError(info);
    });
}

void MainFrame::UpdateTransferProgress(const TransferInfo& info)
{
    // Show progress widgets
    m_progressLabel->Show();
    m_progressGauge->Show();
    
    // Update label: "⬆ photo.jpg 45%"
    wxString label = info.GetDirectionSymbol() + " " + info.fileName;
    m_progressLabel->SetLabel(label);
    
    // Update gauge
    m_progressGauge->SetValue(info.GetProgressPercent());
    
    // If multiple transfers, show count
    int activeCount = m_transferManager.GetActiveCount();
    if (activeCount > 1) {
        m_progressLabel->SetLabel(wxString::Format("%s (%d)", label, activeCount));
    }
}

void MainFrame::OnTransferComplete(const TransferInfo& info)
{
    // Show completion briefly
    m_progressLabel->SetLabel(info.GetDirectionSymbol() + " " + info.fileName + " Done");
    m_progressGauge->SetValue(100);
    
    // Hide after a short delay if no more active transfers
    if (!m_transferManager.HasActiveTransfers()) {
        // Use CallAfter to hide after UI updates
        CallAfter([this]() {
            wxMilliSleep(1500);
            if (!m_transferManager.HasActiveTransfers()) {
                m_progressLabel->Hide();
                m_progressGauge->Hide();
            }
        });
    }
}

void MainFrame::OnTransferError(const TransferInfo& info)
{
    // Show error in status bar
    SetStatusText("Error: " + info.fileName + " - " + info.error, 0);
    
    // Hide progress if no more active transfers
    if (!m_transferManager.HasActiveTransfers()) {
        m_progressLabel->Hide();
        m_progressGauge->Hide();
    }
}

wxColour MainFrame::GetUserColor(const wxString& username)
{
    // Hash the username to get consistent color
    unsigned long hash = 0;
    for (size_t i = 0; i < username.length(); i++) {
        hash = static_cast<unsigned long>(username[i].GetValue()) + (hash << 6) + (hash << 16) - hash;
    }
    return m_userColors[hash % 16];
}

void MainFrame::AddMediaSpan(long startPos, long endPos, const MediaInfo& info)
{
    MediaSpan span;
    span.startPos = startPos;
    span.endPos = endPos;
    span.info = info;
    m_mediaSpans.push_back(span);
}

MediaSpan* MainFrame::GetMediaSpanAtPosition(long pos)
{
    for (auto& span : m_mediaSpans) {
        if (span.Contains(pos)) {
            return &span;
        }
    }
    return nullptr;
}

void MainFrame::ClearMediaSpans()
{
    m_mediaSpans.clear();
}

void MainFrame::OpenMedia(const MediaInfo& info)
{
    // Open the media file or URL
    wxString pathToOpen;
    
    if (!info.localPath.IsEmpty()) {
        pathToOpen = info.localPath;
    } else if (!info.remoteUrl.IsEmpty()) {
        pathToOpen = info.remoteUrl;
    } else {
        // No path available - show error in status bar
        SetStatusText("Error: Media not yet downloaded", 0);
        return;
    }
    
    // Open with default application
    wxLaunchDefaultApplication(pathToOpen);
}

void MainFrame::PopulateDummyData()
{
    // Add sample chats to tree
    m_chatTree->AppendItem(m_pinnedChats, "Saved Messages");
    m_chatTree->AppendItem(m_pinnedChats, "Family Group");
    
    m_chatTree->AppendItem(m_privateChats, "Alice");
    m_chatTree->AppendItem(m_privateChats, "Bob");
    m_chatTree->AppendItem(m_privateChats, "Charlie");
    m_chatTree->AppendItem(m_privateChats, "David");
    
    m_chatTree->AppendItem(m_groups, "Family Group");
    m_chatTree->AppendItem(m_groups, "Work Team");
    m_chatTree->AppendItem(m_groups, "Linux Enthusiasts");
    m_chatTree->AppendItem(m_groups, "Project Alpha");
    
    m_chatTree->AppendItem(m_channels, "Tech News");
    m_chatTree->AppendItem(m_channels, "Telegram Tips");
    m_chatTree->AppendItem(m_channels, "Daily Memes");
    
    m_chatTree->AppendItem(m_bots, "BotFather");
    m_chatTree->AppendItem(m_bots, "GitHub Bot");
    m_chatTree->AppendItem(m_bots, "IFTTT");
    
    m_chatTree->ExpandAll();
    
    // Add sample members (for a group chat)
    long idx = 0;
    m_memberList->InsertItem(idx++, "Admin (owner)");
    m_memberList->InsertItem(idx++, "Moderator (admin)");
    m_memberList->InsertItem(idx++, "Alice");
    m_memberList->InsertItem(idx++, "Bob");
    m_memberList->InsertItem(idx++, "Charlie");
    m_memberList->InsertItem(idx++, "David");
    m_memberList->InsertItem(idx++, "Eve");
    m_memberList->InsertItem(idx++, "Frank");
    m_memberList->InsertItem(idx++, "Grace");
    m_memberList->InsertItem(idx++, "Henry");
    
    m_memberCountLabel->SetLabel(wxString::Format("%ld members", idx));
    
    // Set chat info
    m_currentChatTitle = "Linux Enthusiasts";
    m_currentChatType = TelegramChatType::Supergroup;
    m_chatInfoBar->SetValue("Linux Enthusiasts | 1,234 members | Welcome to the Linux discussion group!");
    
    // Add sample messages
    m_chatDisplay->BeginSuppressUndo();
    
    AppendServiceMessage("12:00:00", "You joined the group");
    AppendServiceMessage("12:00:00", "Group created by Admin");
    
    AppendMessage("12:00:15", "Alice", "Hey everyone! Just installed Arch btw :P");
    AppendMessage("12:00:22", "Bob", "Hi Alice, how long did that take?");
    AppendMessage("12:00:35", "Alice", "About 3 hours, but I learned a lot!");
    AppendMessage("12:00:42", "Charlie", "Nice! I'm still on Ubuntu");
    
    AppendReplyMessage("12:00:58", "David", "Alice: Just installed Arch btw", 
                       "Congrats! The Arch wiki is your best friend now");
    
    // Sample media message with MediaInfo
    MediaInfo photoInfo;
    photoInfo.type = MediaType::Photo;
    photoInfo.caption = "Look at my desktop!";
    photoInfo.id = "photo123";
    AppendMediaMessage("12:01:10", "Eve", photoInfo, "Look at my desktop!");
    
    AppendMessage("12:01:25", "Frank", "That looks clean! What DE are you using?");
    AppendMessage("12:01:32", "Eve", "It's KDE Plasma with some custom themes");
    
    AppendForwardMessage("12:01:45", "Grace", "Tech News", 
                         "Linux kernel 6.8 released with major performance improvements");
    
    AppendJoinMessage("12:01:55", "NewUser");
    
    AppendMessage("12:02:05", "Admin", "Welcome NewUser! Please read the pinned messages");
    AppendMessage("12:02:15", "NewUser", "Thanks! Happy to be here");
    
    AppendEditedMessage("12:02:30", "Henry", "Has anyone tried Wayland yet? It works great now!");
    
    AppendMessage("12:02:45", "Alice", "Yes! Been using it for months, very stable");
    AppendMessage("12:02:58", "Bob", "Still having some issues with screen sharing in Discord");
    
    // Sample file message
    MediaInfo fileInfo;
    fileInfo.type = MediaType::File;
    fileInfo.fileName = "linux-guide.pdf";
    fileInfo.fileSize = "2.4 MB";
    AppendMediaMessage("12:03:10", "Charlie", fileInfo, "");
    
    AppendLeaveMessage("12:03:20", "OldUser");
    
    AppendMessage("12:03:30", "David", "Check out this link: https://kernel.org");
    
    // Sample sticker
    MediaInfo stickerInfo;
    stickerInfo.type = MediaType::Sticker;
    stickerInfo.emoji = "😎";
    AppendMediaMessage("12:03:45", "Eve", stickerInfo, "");
    
    m_chatDisplay->EndSuppressUndo();
    m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
}

void MainFrame::AppendMessage(const wxString& timestamp, const wxString& sender,
                              const wxString& message)
{
    // Format: [HH:MM:SS] <sender> message
    m_chatDisplay->BeginTextColour(m_timestampColor);
    m_chatDisplay->WriteText("[" + timestamp + "] ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(GetUserColor(sender));
    m_chatDisplay->WriteText("<" + sender + "> ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_textColor);
    m_chatDisplay->WriteText(message + "\n");
    m_chatDisplay->EndTextColour();
}

void MainFrame::AppendServiceMessage(const wxString& timestamp, const wxString& message)
{
    // Format: [HH:MM:SS] * message
    m_chatDisplay->BeginTextColour(m_timestampColor);
    m_chatDisplay->WriteText("[" + timestamp + "] ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_serviceColor);
    m_chatDisplay->WriteText("* " + message + "\n");
    m_chatDisplay->EndTextColour();
}

void MainFrame::AppendJoinMessage(const wxString& timestamp, const wxString& user)
{
    // Format: [HH:MM:SS] --> user joined the group
    m_chatDisplay->BeginTextColour(m_timestampColor);
    m_chatDisplay->WriteText("[" + timestamp + "] ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_serviceColor);
    m_chatDisplay->WriteText("--> " + user + " joined the group\n");
    m_chatDisplay->EndTextColour();
}

void MainFrame::AppendLeaveMessage(const wxString& timestamp, const wxString& user)
{
    // Format: [HH:MM:SS] <-- user left the group
    m_chatDisplay->BeginTextColour(m_timestampColor);
    m_chatDisplay->WriteText("[" + timestamp + "] ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_serviceColor);
    m_chatDisplay->WriteText("<-- " + user + " left the group\n");
    m_chatDisplay->EndTextColour();
}

void MainFrame::AppendMediaMessage(const wxString& timestamp, const wxString& sender,
                                   const MediaInfo& media, const wxString& caption)
{
    // Format: [HH:MM:SS] <sender> [Photo] caption
    m_chatDisplay->BeginTextColour(m_timestampColor);
    m_chatDisplay->WriteText("[" + timestamp + "] ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(GetUserColor(sender));
    m_chatDisplay->WriteText("<" + sender + "> ");
    m_chatDisplay->EndTextColour();
    
    // Get position before media tag for hover tracking
    long startPos = m_chatDisplay->GetLastPosition();
    
    // Media tag
    m_chatDisplay->BeginTextColour(m_mediaColor);
    m_chatDisplay->BeginUnderline();
    
    wxString mediaLabel;
    switch (media.type) {
        case MediaType::Photo:
            mediaLabel = "[Photo]";
            break;
        case MediaType::Video:
            mediaLabel = "[Video]";
            break;
        case MediaType::Sticker:
            mediaLabel = "[Sticker " + media.emoji + "]";
            break;
        case MediaType::GIF:
            mediaLabel = "[GIF]";
            break;
        case MediaType::Voice:
            mediaLabel = "[Voice]";
            break;
        case MediaType::VideoNote:
            mediaLabel = "[Video Message]";
            break;
        case MediaType::File:
            mediaLabel = "[File: " + media.fileName + "]";
            break;
        default:
            mediaLabel = "[Media]";
            break;
    }
    
    m_chatDisplay->WriteText(mediaLabel);
    m_chatDisplay->EndUnderline();
    m_chatDisplay->EndTextColour();
    
    // Get position after media tag
    long endPos = m_chatDisplay->GetLastPosition();
    
    // Store media span for hover detection
    AddMediaSpan(startPos, endPos, media);
    
    // Caption
    if (!caption.IsEmpty()) {
        m_chatDisplay->BeginTextColour(m_textColor);
        m_chatDisplay->WriteText(" " + caption);
        m_chatDisplay->EndTextColour();
    }
    
    m_chatDisplay->WriteText("\n");
}

void MainFrame::AppendReplyMessage(const wxString& timestamp, const wxString& sender,
                                   const wxString& replyTo, const wxString& message)
{
    // Format: [HH:MM:SS] <sender> [> replyTo] message
    m_chatDisplay->BeginTextColour(m_timestampColor);
    m_chatDisplay->WriteText("[" + timestamp + "] ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(GetUserColor(sender));
    m_chatDisplay->WriteText("<" + sender + "> ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_replyColor);
    m_chatDisplay->WriteText("[> " + replyTo + "] ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_textColor);
    m_chatDisplay->WriteText(message + "\n");
    m_chatDisplay->EndTextColour();
}

void MainFrame::AppendForwardMessage(const wxString& timestamp, const wxString& sender,
                                     const wxString& forwardFrom, const wxString& message)
{
    // Format: [HH:MM:SS] <sender> [Fwd: forwardFrom] message
    m_chatDisplay->BeginTextColour(m_timestampColor);
    m_chatDisplay->WriteText("[" + timestamp + "] ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(GetUserColor(sender));
    m_chatDisplay->WriteText("<" + sender + "> ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_forwardColor);
    m_chatDisplay->WriteText("[Fwd: " + forwardFrom + "] ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_textColor);
    m_chatDisplay->WriteText(message + "\n");
    m_chatDisplay->EndTextColour();
}

void MainFrame::AppendEditedMessage(const wxString& timestamp, const wxString& sender,
                                    const wxString& message)
{
    // Format: [HH:MM:SS] <sender> message (edited)
    m_chatDisplay->BeginTextColour(m_timestampColor);
    m_chatDisplay->WriteText("[" + timestamp + "] ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(GetUserColor(sender));
    m_chatDisplay->WriteText("<" + sender + "> ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_textColor);
    m_chatDisplay->WriteText(message + " ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_editedColor);
    m_chatDisplay->WriteText("(edited)\n");
    m_chatDisplay->EndTextColour();
}

// Mouse event handlers for hover detection

void MainFrame::OnChatDisplayMouseMove(wxMouseEvent& event)
{
    event.Skip();
    
    wxPoint pos = event.GetPosition();
    long textPos;
    
    // Hit test to find text position
    wxTextCtrlHitTestResult result = m_chatDisplay->HitTest(pos, &textPos);
    
    if (result == wxTE_HT_ON_TEXT) {
        MediaSpan* span = GetMediaSpanAtPosition(textPos);
        
        if (span) {
            // We're over a media span - show popup instantly
            wxPoint screenPos = m_chatDisplay->ClientToScreen(pos);
            screenPos.x += 10;  // Offset slightly
            screenPos.y += 10;
            m_mediaPopup->ShowMedia(span->info, screenPos);
            
            // Change cursor to hand
            m_chatDisplay->SetCursor(wxCursor(wxCURSOR_HAND));
        } else {
            // Not over a media span - hide popup
            if (m_mediaPopup->IsShown()) {
                m_mediaPopup->Hide();
            }
            m_chatDisplay->SetCursor(wxCursor(wxCURSOR_IBEAM));
        }
    } else {
        if (m_mediaPopup->IsShown()) {
            m_mediaPopup->Hide();
        }
        m_chatDisplay->SetCursor(wxCursor(wxCURSOR_IBEAM));
    }
}

void MainFrame::OnChatDisplayMouseLeave(wxMouseEvent& event)
{
    event.Skip();
    
    // Hide popup when mouse leaves chat display
    if (m_mediaPopup && m_mediaPopup->IsShown()) {
        m_mediaPopup->Hide();
    }
}

void MainFrame::OnChatDisplayLeftDown(wxMouseEvent& event)
{
    event.Skip();
    
    wxPoint pos = event.GetPosition();
    long textPos;
    
    wxTextCtrlHitTestResult result = m_chatDisplay->HitTest(pos, &textPos);
    
    if (result == wxTE_HT_ON_TEXT) {
        MediaSpan* span = GetMediaSpanAtPosition(textPos);
        
        if (span) {
            // Clicked on media - open it
            OpenMedia(span->info);
        }
    }
}

// File drop handler

void MainFrame::OnFilesDropped(const wxArrayString& files)
{
    wxDateTime now = wxDateTime::Now();
    wxString timestamp = now.Format("%H:%M:%S");
    
    for (const auto& file : files) {
        wxFileName fn(file);
        wxString filename = fn.GetFullName();
        
        FileMediaType type = GetMediaTypeFromExtension(file);
        
        MediaInfo media;
        media.localPath = file;
        media.fileName = filename;
        
        switch (type) {
            case FileMediaType::Image:
                media.type = MediaType::Photo;
                break;
            case FileMediaType::Video:
                media.type = MediaType::Video;
                break;
            case FileMediaType::Audio:
                media.type = MediaType::Voice;
                break;
            default:
                media.type = MediaType::File;
                break;
        }
        
        // Start upload with transfer manager
        wxFile wxf(file);
        int64_t fileSize = wxf.IsOpened() ? wxf.Length() : 0;
        wxf.Close();
        int transferId = m_transferManager.StartUpload(file, fileSize);
        
        // TODO: Actually upload via TDLib
        // For now, simulate progress and complete
        AppendMediaMessage(timestamp, m_currentUser.IsEmpty() ? "You" : m_currentUser, 
                          media, "");
        
        // Simulate completion (will be replaced by TDLib callbacks)
        m_transferManager.CompleteTransfer(transferId, file);
    }
    
    m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
}

void MainFrame::OnUploadFile(wxCommandEvent& event)
{
    wxFileDialog dialog(this, "Select file to upload", "", "",
        "All files (*.*)|*.*|"
        "Images (*.jpg;*.png;*.gif)|*.jpg;*.jpeg;*.png;*.gif;*.webp|"
        "Videos (*.mp4;*.mkv;*.avi)|*.mp4;*.mkv;*.avi;*.mov;*.webm|"
        "Documents (*.pdf;*.doc;*.txt)|*.pdf;*.doc;*.docx;*.txt",
        wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);
    
    if (dialog.ShowModal() == wxID_OK) {
        wxArrayString files;
        dialog.GetPaths(files);
        OnFilesDropped(files);
    }
}

void MainFrame::OnInputKeyDown(wxKeyEvent& event)
{
    // Check for Ctrl+V (paste)
    if (event.ControlDown() && event.GetKeyCode() == 'V') {
        HandleClipboardPaste();
    }
    event.Skip();
}

void MainFrame::HandleClipboardPaste()
{
    if (wxTheClipboard->Open()) {
        // Check if clipboard has an image
        if (wxTheClipboard->IsSupported(wxDF_BITMAP)) {
            wxBitmapDataObject bitmapData;
            if (wxTheClipboard->GetData(bitmapData)) {
                wxBitmap bitmap = bitmapData.GetBitmap();
                if (bitmap.IsOk()) {
                    // Save to temp file and "upload"
                    wxString tempPath = wxFileName::GetTempDir() + "/teleliter_paste.png";
                    bitmap.SaveFile(tempPath, wxBITMAP_TYPE_PNG);
                    
                    wxArrayString files;
                    files.Add(tempPath);
                    OnFilesDropped(files);
                }
            }
        }
        wxTheClipboard->Close();
    }
}

// Menu event handlers

void MainFrame::OnExit(wxCommandEvent& event)
{
    Close(true);
}

void MainFrame::OnAbout(wxCommandEvent& event)
{
    wxMessageBox("Teleliter\n\n"
                 "Telegram client with HexChat-style interface\n"
                 "Built with TDLib\n\n"
                 "Version 0.1.0\n\n"
                 "Features:\n"
                 "- Hover over [Photo], [Video], [Sticker] to preview\n"
                 "- Click to open the file\n"
                 "- Drag & drop files to upload\n"
                 "- Ctrl+V to paste images",
                 "About Teleliter",
                 wxOK | wxICON_INFORMATION, this);
}

void MainFrame::OnLogin(wxCommandEvent& event)
{
    // Switch to Teleliter welcome chat and start login
    m_chatTree->SelectItem(m_teleliterItem);
    
    if (m_welcomeChat) {
        m_welcomeChat->StartLogin();
    }
}

void MainFrame::OnLogout(wxCommandEvent& event)
{
    if (m_telegramClient && m_telegramClient->IsLoggedIn()) {
        m_telegramClient->LogOut();
    }
}

void MainFrame::OnNewChat(wxCommandEvent& event)
{
    wxTextEntryDialog dlg(this, "Enter username or phone number:", "New Private Chat");
    if (dlg.ShowModal() == wxID_OK) {
        wxString contact = dlg.GetValue();
        AppendServiceMessage(wxDateTime::Now().Format("%H:%M:%S"), 
                            "Starting chat with " + contact);
    }
}

void MainFrame::OnNewGroup(wxCommandEvent& event)
{
    wxTextEntryDialog dlg(this, "Enter group name:", "New Group");
    if (dlg.ShowModal() == wxID_OK) {
        wxString groupName = dlg.GetValue();
        AppendServiceMessage(wxDateTime::Now().Format("%H:%M:%S"), 
                            "Creating group: " + groupName);
    }
}

void MainFrame::OnNewChannel(wxCommandEvent& event)
{
    wxTextEntryDialog dlg(this, "Enter channel name:", "New Channel");
    if (dlg.ShowModal() == wxID_OK) {
        wxString channelName = dlg.GetValue();
        AppendServiceMessage(wxDateTime::Now().Format("%H:%M:%S"), 
                            "Creating channel: " + channelName);
    }
}

void MainFrame::OnContacts(wxCommandEvent& event)
{
    wxMessageBox("Contacts dialog will be implemented.", "Contacts", wxOK, this);
}

void MainFrame::OnSearch(wxCommandEvent& event)
{
    wxTextEntryDialog dlg(this, "Search messages, chats, and users:", "Search");
    if (dlg.ShowModal() == wxID_OK) {
        wxString query = dlg.GetValue();
        AppendServiceMessage(wxDateTime::Now().Format("%H:%M:%S"), 
                            "Searching for: " + query);
    }
}

void MainFrame::OnSavedMessages(wxCommandEvent& event)
{
    m_currentChatTitle = "Saved Messages";
    m_currentChatType = TelegramChatType::SavedMessages;
    m_chatInfoBar->SetValue("Saved Messages");
    SetStatusText("Saved Messages", 0);
}

void MainFrame::OnPreferences(wxCommandEvent& event)
{
    wxMessageBox("Preferences dialog will be implemented.", "Preferences", wxOK, this);
}

void MainFrame::OnClearWindow(wxCommandEvent& event)
{
    m_chatDisplay->Clear();
    ClearMediaSpans();
}

void MainFrame::OnToggleChatList(wxCommandEvent& event)
{
    m_showChatList = !m_showChatList;
    if (m_showChatList) {
        m_mainSplitter->SplitVertically(m_leftPanel, m_rightSplitter, 180);
    } else {
        m_mainSplitter->Unsplit(m_leftPanel);
    }
}

void MainFrame::OnToggleMembers(wxCommandEvent& event)
{
    m_showMembers = !m_showMembers;
    if (m_showMembers) {
        m_rightSplitter->SplitVertically(m_chatPanel, m_rightPanel, -130);
    } else {
        m_rightSplitter->Unsplit(m_rightPanel);
    }
}

void MainFrame::OnToggleChatInfo(wxCommandEvent& event)
{
    m_showChatInfo = !m_showChatInfo;
    m_chatInfoBar->Show(m_showChatInfo);
    m_chatPanel->Layout();
}

void MainFrame::OnFullscreen(wxCommandEvent& event)
{
    ShowFullScreen(!IsFullScreen());
}

void MainFrame::OnChatTreeSelectionChanged(wxTreeEvent& event)
{
    // Guard against events during initialization when UI elements aren't created yet
    if (!m_chatInfoBar || !m_welcomeChat || !m_chatDisplay || !m_chatPanel) {
        return;
    }
    
    wxTreeItemId item = event.GetItem();
    if (item.IsOk()) {
        wxString chatName = m_chatTree->GetItemText(item);
        
        // Check if Teleliter (welcome) is selected
        if (item == m_teleliterItem) {
            m_currentChatId = 0;
            m_chatInfoBar->SetValue("Teleliter");
            // Use sizer Show/Hide to properly manage layout
            wxSizer* sizer = m_chatPanel->GetSizer();
            if (sizer) {
                sizer->Show(m_welcomeChat, true);
                sizer->Show(m_chatDisplay, false);
            }
            m_chatPanel->Layout();
            SetStatusText("Teleliter", 0);
            return;
        }
        
        // Check if this is a category item
        if (item == m_pinnedChats || item == m_privateChats || item == m_groups ||
            item == m_channels || item == m_bots) {
            return;  // Don't select categories
        }
        
        // Look up chat ID from tree item
        auto it = m_treeItemToChatId.find(item);
        if (it != m_treeItemToChatId.end()) {
            int64_t chatId = it->second;
            
            // Update current chat
            m_currentChatId = chatId;
            m_currentChatTitle = chatName;
            
            // Remove unread indicator from title
            int parenPos = chatName.Find('(');
            if (parenPos != wxNOT_FOUND) {
                m_currentChatTitle = chatName.Left(parenPos).Trim();
            }
            
            m_chatInfoBar->SetValue(m_currentChatTitle);
            // Use sizer Show/Hide to properly manage layout
            wxSizer* sizer = m_chatPanel->GetSizer();
            if (sizer) {
                sizer->Show(m_welcomeChat, false);
                sizer->Show(m_chatDisplay, true);
            }
            m_chatPanel->Layout();
            SetStatusText(m_currentChatTitle, 0);
            
            // Remove bold (unread indicator)
            m_chatTree->SetItemBold(item, false);
            
            // Load messages for this chat
            if (m_telegramClient) {
                m_chatDisplay->Clear();
                ClearMediaSpans();
                m_telegramClient->LoadMessages(chatId);
                m_telegramClient->MarkChatAsRead(chatId);
            }
        } else {
            // Fallback for items without chat ID (shouldn't happen with TDLib)
            m_currentChatTitle = chatName;
            m_chatInfoBar->SetValue(chatName);
            wxSizer* sizer = m_chatPanel->GetSizer();
            if (sizer) {
                sizer->Show(m_welcomeChat, false);
                sizer->Show(m_chatDisplay, true);
            }
            m_chatPanel->Layout();
            SetStatusText(chatName, 0);
        }
    }
}

void MainFrame::OnChatTreeItemActivated(wxTreeEvent& event)
{
    OnChatTreeSelectionChanged(event);
}

void MainFrame::OnMemberListItemActivated(wxListEvent& event)
{
    long index = event.GetIndex();
    wxString username = m_memberList->GetItemText(index);
    
    // Remove role suffix if present
    int parenPos = username.Find(" (");
    if (parenPos != wxNOT_FOUND) {
        username = username.Left(parenPos);
    }
    
    AppendServiceMessage(wxDateTime::Now().Format("%H:%M:%S"), 
                        "Opening profile: " + username);
}

void MainFrame::OnMemberListRightClick(wxListEvent& event)
{
    long index = event.GetIndex();
    wxString username = m_memberList->GetItemText(index);
    
    wxMenu menu;
    menu.Append(wxID_ANY, "View Profile");
    menu.Append(wxID_ANY, "Send Message");
    menu.AppendSeparator();
    menu.Append(wxID_ANY, "Mention");
    menu.AppendSeparator();
    menu.Append(wxID_ANY, "Promote to Admin");
    menu.Append(wxID_ANY, "Restrict");
    menu.Append(wxID_ANY, "Remove from Group");
    
    PopupMenu(&menu);
}

void MainFrame::OnInputEnter(wxCommandEvent& event)
{
    wxString message = m_inputBox->GetValue();
    if (message.IsEmpty()) {
        return;
    }
    
    // Check if welcome chat is active - forward input there
    if (IsWelcomeChatActive()) {
        ForwardInputToWelcomeChat(message);
        m_inputBox->Clear();
        return;
    }
    
    // Send via TDLib if logged in and have a chat selected
    if (m_telegramClient && m_telegramClient->IsLoggedIn() && m_currentChatId != 0) {
        m_telegramClient->SendMessage(m_currentChatId, message);
        m_inputBox->Clear();
        // Message will appear via OnNewMessage callback
        return;
    }
    
    // Fallback: display locally
    wxDateTime now = wxDateTime::Now();
    wxString timestamp = now.Format("%H:%M:%S");
    AppendMessage(timestamp, m_currentUser.IsEmpty() ? "You" : m_currentUser, message);
    
    m_inputBox->Clear();
    m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
}

bool MainFrame::IsWelcomeChatActive() const
{
    return m_welcomeChat && m_welcomeChat->IsShown();
}

void MainFrame::ForwardInputToWelcomeChat(const wxString& input)
{
    if (m_welcomeChat) {
        m_welcomeChat->ProcessInput(input);
    }
}

// ============================================================================
// TelegramClient callbacks
// ============================================================================

wxString MainFrame::FormatTimestamp(int64_t unixTime)
{
    if (unixTime <= 0) {
        return wxDateTime::Now().Format("%H:%M:%S");
    }
    
    time_t t = static_cast<time_t>(unixTime);
    wxDateTime dt(t);
    return dt.Format("%H:%M:%S");
}

void MainFrame::OnConnected()
{
    SetStatusText("Connected", 3);
}

void MainFrame::OnLoginSuccess(const wxString& userName)
{
    m_isLoggedIn = true;
    m_currentUser = userName;
    
    SetStatusText("Logged in as " + userName, 0);
    SetStatusText("Online", 3);
    
    // Update menu state
    wxMenuBar* menuBar = GetMenuBar();
    if (menuBar) {
        menuBar->Enable(ID_LOGIN, false);
        menuBar->Enable(ID_LOGOUT, true);
    }
}

void MainFrame::OnLoggedOut()
{
    m_isLoggedIn = false;
    m_currentUser.Clear();
    m_currentChatId = 0;
    
    // Clear chat list mappings
    m_treeItemToChatId.clear();
    m_chatIdToTreeItem.clear();
    
    // Clear tree (keep categories)
    m_chatTree->DeleteChildren(m_pinnedChats);
    m_chatTree->DeleteChildren(m_privateChats);
    m_chatTree->DeleteChildren(m_groups);
    m_chatTree->DeleteChildren(m_channels);
    m_chatTree->DeleteChildren(m_bots);
    
    // Show welcome chat
    wxSizer* sizer = m_chatPanel->GetSizer();
    if (sizer) {
        sizer->Show(m_welcomeChat, true);
        sizer->Show(m_chatDisplay, false);
    }
    m_chatPanel->Layout();
    m_chatTree->SelectItem(m_teleliterItem);
    
    SetStatusText("Logged out", 0);
    SetStatusText("Offline", 3);
    
    // Update menu state
    wxMenuBar* menuBar = GetMenuBar();
    if (menuBar) {
        menuBar->Enable(ID_LOGIN, true);
        menuBar->Enable(ID_LOGOUT, false);
    }
}

void MainFrame::RefreshChatList()
{
    if (!m_telegramClient) return;
    
    // Clear existing items (keep categories)
    m_chatTree->DeleteChildren(m_pinnedChats);
    m_chatTree->DeleteChildren(m_privateChats);
    m_chatTree->DeleteChildren(m_groups);
    m_chatTree->DeleteChildren(m_channels);
    m_chatTree->DeleteChildren(m_bots);
    m_treeItemToChatId.clear();
    m_chatIdToTreeItem.clear();
    
    // Get chats from TelegramClient
    const auto& chats = m_telegramClient->GetChats();
    
    // Sort chats by order (descending)
    std::vector<const ChatInfo*> sortedChats;
    for (const auto& [id, chat] : chats) {
        sortedChats.push_back(&chat);
    }
    std::sort(sortedChats.begin(), sortedChats.end(), 
              [](const ChatInfo* a, const ChatInfo* b) {
                  return a->order > b->order;
              });
    
    // Add chats to appropriate categories
    for (const ChatInfo* chat : sortedChats) {
        wxTreeItemId parent;
        wxString displayTitle = chat->title;
        
        // Add unread count to title if present
        if (chat->unreadCount > 0) {
            displayTitle += wxString::Format(" (%d)", chat->unreadCount);
        }
        
        // Determine category
        if (chat->isPinned) {
            parent = m_pinnedChats;
        } else if (chat->isChannel) {
            parent = m_channels;
        } else if (chat->isGroup || chat->isSupergroup) {
            parent = m_groups;
        } else if (chat->isBot) {
            parent = m_bots;
        } else {
            parent = m_privateChats;
        }
        
        wxTreeItemId item = m_chatTree->AppendItem(parent, displayTitle);
        m_treeItemToChatId[item] = chat->id;
        m_chatIdToTreeItem[chat->id] = item;
        
        // Make bold if unread
        if (chat->unreadCount > 0) {
            m_chatTree->SetItemBold(item, true);
        }
    }
    
    // Expand categories that have items
    if (m_chatTree->GetChildrenCount(m_pinnedChats) > 0) {
        m_chatTree->Expand(m_pinnedChats);
    }
    if (m_chatTree->GetChildrenCount(m_privateChats) > 0) {
        m_chatTree->Expand(m_privateChats);
    }
    if (m_chatTree->GetChildrenCount(m_groups) > 0) {
        m_chatTree->Expand(m_groups);
    }
    if (m_chatTree->GetChildrenCount(m_channels) > 0) {
        m_chatTree->Expand(m_channels);
    }
    if (m_chatTree->GetChildrenCount(m_bots) > 0) {
        m_chatTree->Expand(m_bots);
    }
}

void MainFrame::OnMessagesLoaded(int64_t chatId, const std::vector<MessageInfo>& messages)
{
    if (chatId != m_currentChatId) {
        return;  // Not the current chat, ignore
    }
    
    m_chatDisplay->BeginSuppressUndo();
    m_chatDisplay->Clear();
    ClearMediaSpans();
    
    // Messages are in reverse order (newest first), so display from end
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        DisplayMessage(*it);
    }
    
    m_chatDisplay->EndSuppressUndo();
    m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
}

void MainFrame::OnNewMessage(const MessageInfo& message)
{
    if (message.chatId != m_currentChatId) {
        // Update unread count in tree
        auto treeIt = m_chatIdToTreeItem.find(message.chatId);
        if (treeIt != m_chatIdToTreeItem.end()) {
            m_chatTree->SetItemBold(treeIt->second, true);
        }
        return;
    }
    
    // Display the new message
    DisplayMessage(message);
    m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
}

void MainFrame::DisplayMessage(const MessageInfo& msg)
{
    wxString timestamp = FormatTimestamp(msg.date);
    wxString sender = msg.senderName.IsEmpty() ? 
        wxString::Format("User %lld", msg.senderId) : msg.senderName;
    
    // Handle forwarded messages
    if (msg.isForwarded && !msg.forwardedFrom.IsEmpty()) {
        AppendForwardMessage(timestamp, sender, msg.forwardedFrom, msg.text);
        return;
    }
    
    // Handle edited messages
    if (msg.isEdited) {
        AppendEditedMessage(timestamp, sender, msg.text);
        return;
    }
    
    // Handle media messages
    if (msg.hasPhoto || msg.hasVideo || msg.hasDocument || msg.hasVoice || 
        msg.hasVideoNote || msg.hasSticker || msg.hasAnimation) {
        
        MediaInfo mediaInfo;
        mediaInfo.id = wxString::Format("%d", msg.mediaFileId);
        mediaInfo.caption = msg.mediaCaption;
        mediaInfo.fileName = msg.mediaFileName;
        mediaInfo.localPath = msg.mediaLocalPath;
        
        if (msg.hasPhoto) {
            mediaInfo.type = MediaType::Photo;
        } else if (msg.hasVideo) {
            mediaInfo.type = MediaType::Video;
        } else if (msg.hasDocument) {
            mediaInfo.type = MediaType::File;
            mediaInfo.fileSize = wxString::Format("%lld bytes", msg.mediaFileSize);
        } else if (msg.hasVoice) {
            mediaInfo.type = MediaType::Voice;
        } else if (msg.hasVideoNote) {
            mediaInfo.type = MediaType::VideoNote;
        } else if (msg.hasSticker) {
            mediaInfo.type = MediaType::Sticker;
            mediaInfo.emoji = msg.mediaCaption;
        } else if (msg.hasAnimation) {
            mediaInfo.type = MediaType::GIF;
        }
        
        AppendMediaMessage(timestamp, sender, mediaInfo, msg.mediaCaption);
        
        // If there's also text, display it
        if (!msg.text.IsEmpty() && !msg.text.StartsWith("[")) {
            AppendMessage(timestamp, sender, msg.text);
        }
        return;
    }
    
    // Regular text message
    AppendMessage(timestamp, sender, msg.text);
}

void MainFrame::OnMessageEdited(int64_t chatId, int64_t messageId, const wxString& newText)
{
    if (chatId != m_currentChatId) {
        return;
    }
    
    // For now, just add a service message noting the edit
    // A more sophisticated implementation would find and update the original message
    AppendServiceMessage(wxDateTime::Now().Format("%H:%M:%S"),
                        wxString::Format("Message %lld was edited: %s", messageId, newText));
    m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
}

void MainFrame::OnFileDownloaded(int32_t fileId, const wxString& localPath)
{
    // Check if this is a pending download for media preview
    auto it = m_pendingDownloads.find(fileId);
    if (it != m_pendingDownloads.end()) {
        MediaInfo& info = it->second;
        info.localPath = localPath;
        
        // Update media popup if showing this file
        if (m_mediaPopup && m_mediaPopup->IsShown()) {
            const MediaInfo& popupInfo = m_mediaPopup->GetMediaInfo();
            if (popupInfo.id == info.id) {
                m_mediaPopup->SetImage(localPath);
            }
        }
        
        m_pendingDownloads.erase(it);
    }
    
    // Update transfer manager
    // Find transfer by checking pending transfers (simplified - in real impl would track by file ID)
    SetStatusText("Download complete: " + localPath.AfterLast('/'), 1);
}

void MainFrame::OnFileProgress(int32_t fileId, int64_t downloadedSize, int64_t totalSize)
{
    if (totalSize > 0) {
        int percent = static_cast<int>((downloadedSize * 100) / totalSize);
        if (m_progressGauge) {
            m_progressGauge->SetValue(percent);
        }
        if (m_progressLabel) {
            m_progressLabel->SetLabel(wxString::Format("⬇ %d%%", percent));
        }
    }
}

void MainFrame::ShowStatusError(const wxString& error)
{
    SetStatusText("Error: " + error, 1);
}