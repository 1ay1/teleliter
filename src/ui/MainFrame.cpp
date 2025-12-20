#include "MainFrame.h"
#include "WelcomeChat.h"
#include "ChatListWidget.h"
#include "ChatViewWidget.h"
#include "InputBoxWidget.h"
#include "MessageFormatter.h"
#include "MediaPopup.h"
#include "FileDropTarget.h"
#include "../telegram/TelegramClient.h"
#include "../telegram/Types.h"
#include <wx/artprov.h>
#include <wx/settings.h>
#include <wx/filename.h>
#include <ctime>
// Debug logging - enabled for troubleshooting
#include <iostream>
#define DBGLOG(msg) std::cerr << "[MainFrame] " << msg << std::endl
// Disable debug logging for release:
// #define DBGLOG(msg) do {} while(0)

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
    EVT_MENU(ID_UNREAD_FIRST, MainFrame::OnToggleUnreadFirst)
    // Tree events are bound dynamically in CreateMainLayout() since tree is in ChatListWidget
    EVT_LIST_ITEM_ACTIVATED(ID_MEMBER_LIST, MainFrame::OnMemberListItemActivated)
    EVT_LIST_ITEM_RIGHT_CLICK(ID_MEMBER_LIST, MainFrame::OnMemberListRightClick)
    EVT_TIMER(ID_REFRESH_TIMER, MainFrame::OnRefreshTimer)
    EVT_TIMER(ID_STATUS_TIMER, MainFrame::OnStatusTimer)
wxEND_EVENT_TABLE()

MainFrame::MainFrame(const wxString& title, const wxPoint& pos, const wxSize& size)
    : wxFrame(NULL, wxID_ANY, title, pos, size),
      m_telegramClient(nullptr),
      m_refreshTimer(nullptr),
      m_statusTimer(nullptr),
      m_mainSplitter(nullptr),
      m_rightSplitter(nullptr),
      m_leftPanel(nullptr),
      m_chatListWidget(nullptr),
      m_chatPanel(nullptr),
      m_welcomeChat(nullptr),
      m_chatInfoBar(nullptr),
      m_chatViewWidget(nullptr),
      m_inputBoxWidget(nullptr),
      m_rightPanel(nullptr),
      m_memberList(nullptr),
      m_memberCountLabel(nullptr),
      m_statusBar(nullptr),
      m_showChatList(true),
      m_showMembers(true),
      m_showChatInfo(true),
      m_showUnreadFirst(true),
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
    
    // Setup status bar manager
    m_statusBar = new StatusBarManager(this);
    m_statusBar->Setup();
    
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
    
    // Connect status bar to telegram client
    m_statusBar->SetTelegramClient(m_telegramClient);
    
    // Setup transfer manager callbacks
    m_transferManager.SetProgressCallback([this](const TransferInfo& info) {
        m_statusBar->SetActiveTransferCount(m_transferManager.GetActiveCount());
        m_statusBar->UpdateTransferProgress(info);
    });
    m_transferManager.SetCompleteCallback([this](const TransferInfo& info) {
        m_statusBar->OnTransferComplete(info);
        if (!m_transferManager.HasActiveTransfers()) {
            CallAfter([this]() {
                wxMilliSleep(2000);
                if (!m_transferManager.HasActiveTransfers()) {
                    m_statusBar->HideTransferProgress();
                }
            });
        }
    });
    m_transferManager.SetErrorCallback([this](const TransferInfo& info) {
        m_statusBar->OnTransferError(info);
        if (!m_transferManager.HasActiveTransfers()) {
            CallAfter([this]() {
                wxMilliSleep(3000);
                if (!m_transferManager.HasActiveTransfers()) {
                    m_statusBar->HideTransferProgress();
                }
            });
        }
    });
    
    // Start refresh timer (every 30 seconds)
    m_refreshTimer = new wxTimer(this, ID_REFRESH_TIMER);
    m_refreshTimer->Start(30000);
    
    // Start status bar update timer (every 1 second)
    m_statusTimer = new wxTimer(this, ID_STATUS_TIMER);
    m_statusTimer->Start(1000);
    
    // Ensure welcome chat is visible on startup
    if (m_welcomeChat && m_chatViewWidget && m_chatPanel) {
        wxSizer* sizer = m_chatPanel->GetSizer();
        if (sizer) {
            sizer->Show(m_welcomeChat, true);
            sizer->Show(m_chatViewWidget, false);
            m_chatPanel->Layout();
        }
    }
    
    SetMinSize(wxSize(800, 600));
    SetBackgroundColour(m_bgColor);
}

MainFrame::~MainFrame()
{
    if (m_statusTimer) {
        m_statusTimer->Stop();
        delete m_statusTimer;
        m_statusTimer = nullptr;
    }
    if (m_refreshTimer) {
        m_refreshTimer->Stop();
        delete m_refreshTimer;
        m_refreshTimer = nullptr;
    }
    
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
    
    // Message colors (HexChat-style)
    m_timestampColor = wxColour(0x88, 0x88, 0x88);  // Gray
    m_textColor = wxColour(0xD3, 0xD7, 0xCF);       // Light gray
    m_serviceColor = wxColour(0x88, 0x88, 0x88);    // Gray for server messages
    m_highlightColor = wxColour(0xFC, 0xAF, 0x3E);  // Yellow for highlights
    m_actionColor = wxColour(0xCE, 0x5C, 0x00);     // Orange for /me actions
    m_linkColor = wxColour(0x72, 0x9F, 0xCF);       // Blue for links
    m_mediaColor = wxColour(0x72, 0x9F, 0xCF);      // Blue for media tags
    m_editedColor = wxColour(0x88, 0x88, 0x88);     // Gray for (edited)
    m_forwardColor = wxColour(0xAD, 0x7F, 0xA8);    // Purple for forwards
    m_replyColor = wxColour(0x72, 0x9F, 0xCF);      // Blue for replies
    m_noticeColor = wxColour(0xAD, 0x7F, 0xA8);     // Purple for notices
    
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
    menuView->AppendCheckItem(ID_UNREAD_FIRST, "Unread Chats First");
    menuView->Check(ID_UNREAD_FIRST, true);
    menuView->AppendSeparator();
    menuView->Append(ID_FULLSCREEN, "Fullscreen\tF11");
    menuBar->Append(menuView, "&View");
    
    // Window menu
    wxMenu* menuWindow = new wxMenu;
    menuWindow->Append(wxID_ANY, "Previous Chat\tCtrl+PgUp");
    menuWindow->Append(wxID_ANY, "Next Chat\tCtrl+PgDn");
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
    
    // Left panel - Chat list widget
    m_leftPanel = new wxPanel(m_mainSplitter);
    m_leftPanel->SetBackgroundColour(m_treeBgColor);
    
    // Create chat list widget
    m_chatListWidget = new ChatListWidget(m_leftPanel, this);
    m_chatListWidget->SetTreeColors(m_treeBgColor, m_treeFgColor, m_treeSelBgColor);
    m_chatListWidget->SetTreeFont(m_treeFont);
    
    // Bind tree events directly to the tree control (since it's in a child widget)
    wxTreeCtrl* chatTree = m_chatListWidget->GetTreeCtrl();
    if (chatTree) {
        chatTree->Bind(wxEVT_TREE_SEL_CHANGED, &MainFrame::OnChatTreeSelectionChanged, this);
        chatTree->Bind(wxEVT_TREE_ITEM_ACTIVATED, &MainFrame::OnChatTreeItemActivated, this);
    }
    
    wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);
    leftSizer->Add(m_chatListWidget, 1, wxEXPAND);
    m_leftPanel->SetSizer(leftSizer);
    
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
    
    // Chat view widget (hidden initially, shown when a chat is selected)
    m_chatViewWidget = new ChatViewWidget(parent, this);
    m_chatViewWidget->SetColors(m_bgColor, m_fgColor, m_timestampColor, m_textColor,
        m_serviceColor, m_actionColor, m_mediaColor, m_editedColor,
        m_forwardColor, m_replyColor, m_highlightColor, m_noticeColor);
    m_chatViewWidget->SetUserColors(m_userColors);
    m_chatViewWidget->SetChatFont(m_chatFont);
    sizer->Add(m_chatViewWidget, 1, wxEXPAND);
    
    // Hide chat view widget from sizer (not just visually) - welcome chat is shown initially
    sizer->Show(m_chatViewWidget, false);
    sizer->Show(m_welcomeChat, true);
    
    // Bottom separator
    wxPanel* separator2 = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    separator2->SetBackgroundColour(wxColour(0x1A, 0x1A, 0x1A));
    sizer->Add(separator2, 0, wxEXPAND);
    
    // Input box widget
    m_inputBoxWidget = new InputBoxWidget(parent, this);
    m_inputBoxWidget->SetColors(m_inputBgColor, m_inputFgColor);
    m_inputBoxWidget->SetInputFont(m_inputFont);
    m_inputBoxWidget->SetChatView(m_chatViewWidget);
    m_inputBoxWidget->SetWelcomeChat(m_welcomeChat);
    // Note: member list and message formatter will be connected after CreateMemberList
    sizer->Add(m_inputBoxWidget, 0, wxEXPAND | wxALL, 2);
    
    parent->SetSizer(sizer);
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
    
    // Now connect the member list to the input box widget for tab completion
    if (m_inputBoxWidget) {
        m_inputBoxWidget->SetMemberList(m_memberList);
        if (m_chatViewWidget) {
            m_inputBoxWidget->SetMessageFormatter(m_chatViewWidget->GetMessageFormatter());
        }
    }
}

void MainFrame::OnStatusTimer(wxTimerEvent& event)
{
    if (m_statusBar) {
        m_statusBar->UpdateStatusBar();
    }
}



void MainFrame::PopulateDummyData()
{
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
    m_currentChatTitle = "Test Chat - Media Demo";
    m_currentChatType = TelegramChatType::Supergroup;
    m_chatInfoBar->SetValue("Test Chat - Media Demo | Testing all message types");
    
    // Add sample messages via ChatViewWidget
    if (m_chatViewWidget) {
        MessageFormatter* formatter = m_chatViewWidget->GetMessageFormatter();
        if (formatter) {
            wxRichTextCtrl* display = m_chatViewWidget->GetDisplayCtrl();
            if (display) {
                display->BeginSuppressUndo();
            }
            
            // Service messages
            formatter->AppendServiceMessage("12:00", "Welcome to the Test Chat!");
            formatter->AppendServiceMessage("12:00", "This chat demonstrates all message types");
            
            // Regular messages
            formatter->AppendMessage("12:01", "Alice", "Hey everyone! Let's test some messages");
            formatter->AppendMessage("12:01", "Bob", "Sure! I'll send some media");
            
            // Photo message
            MediaInfo photoInfo;
            photoInfo.type = MediaType::Photo;
            photoInfo.fileId = 0;  // Test data - no real file ID
            photoInfo.caption = "Beautiful sunset";
            {
                long startPos = display->GetLastPosition();
                formatter->AppendMediaMessage("12:02", "Alice", photoInfo, "Beautiful sunset I captured yesterday");
                long endPos = display->GetLastPosition();
                m_chatViewWidget->AddMediaSpan(startPos, endPos, photoInfo);
            }
            
            // Video message
            MediaInfo videoInfo;
            videoInfo.type = MediaType::Video;
            videoInfo.fileId = 0;  // Test data - no real file ID
            videoInfo.fileName = "funny_cat.mp4";
            videoInfo.fileSize = "12.5 MB";
            {
                long startPos = display->GetLastPosition();
                formatter->AppendMediaMessage("12:03", "Bob", videoInfo, "Check out this funny cat video!");
                long endPos = display->GetLastPosition();
                m_chatViewWidget->AddMediaSpan(startPos, endPos, videoInfo);
            }
            
            // Document/File message
            MediaInfo fileInfo;
            fileInfo.type = MediaType::File;
            fileInfo.fileId = 0;  // Test data - no real file ID
            fileInfo.fileName = "linux_guide.pdf";
            fileInfo.fileSize = "2.3 MB";
            {
                long startPos = display->GetLastPosition();
                formatter->AppendMediaMessage("12:04", "Charlie", fileInfo, "Here's that PDF you asked for");
                long endPos = display->GetLastPosition();
                m_chatViewWidget->AddMediaSpan(startPos, endPos, fileInfo);
            }
            
            // Voice message
            MediaInfo voiceInfo;
            voiceInfo.type = MediaType::Voice;
            voiceInfo.fileId = 0;  // Test data - no real file ID
            {
                long startPos = display->GetLastPosition();
                formatter->AppendMediaMessage("12:05", "David", voiceInfo, "");
                long endPos = display->GetLastPosition();
                m_chatViewWidget->AddMediaSpan(startPos, endPos, voiceInfo);
            }
            
            // Video note (round video)
            MediaInfo videoNoteInfo;
            videoNoteInfo.type = MediaType::VideoNote;
            videoNoteInfo.fileId = 0;  // Test data - no real file ID
            {
                long startPos = display->GetLastPosition();
                formatter->AppendMediaMessage("12:06", "Eve", videoNoteInfo, "");
                long endPos = display->GetLastPosition();
                m_chatViewWidget->AddMediaSpan(startPos, endPos, videoNoteInfo);
            }
            
            // Sticker
            MediaInfo stickerInfo;
            stickerInfo.type = MediaType::Sticker;
            stickerInfo.fileId = 0;  // Test data - no real file ID
            stickerInfo.emoji = "😄";
            {
                long startPos = display->GetLastPosition();
                formatter->AppendMediaMessage("12:07", "Frank", stickerInfo, "");
                long endPos = display->GetLastPosition();
                m_chatViewWidget->AddMediaSpan(startPos, endPos, stickerInfo);
            }
            
            // GIF/Animation
            MediaInfo gifInfo;
            gifInfo.type = MediaType::GIF;
            gifInfo.fileId = 0;  // Test data - no real file ID
            gifInfo.fileName = "dancing.gif";
            {
                long startPos = display->GetLastPosition();
                formatter->AppendMediaMessage("12:08", "Grace", gifInfo, "This is hilarious!");
                long endPos = display->GetLastPosition();
                m_chatViewWidget->AddMediaSpan(startPos, endPos, gifInfo);
            }
            
            // Action message (/me)
            formatter->AppendActionMessage("12:09", "Henry", "is laughing at the GIF");
            
            // Reply message
            formatter->AppendReplyMessage("12:10", "Alice", "Bob: Check out this funny cat video!", 
                               "Haha that's so cute! 😂");
            
            // Forward message
            formatter->AppendForwardMessage("12:11", "Bob", "Tech News Channel", 
                             "Breaking: New wxWidgets 3.3 released with improved dark mode support!");
            
            // Edited message
            formatter->AppendEditedMessage("12:12", "Charlie", "I made a typo but fixed it now (edited)");
            
            // Notice message
            formatter->AppendNoticeMessage("12:13", "Teleliter", "This is a system notice");
            
            // Join/Leave messages
            formatter->AppendJoinMessage("12:14", "NewMember");
            formatter->AppendLeaveMessage("12:14", "OldMember");
            
            // More regular messages
            formatter->AppendMessage("12:15", "Admin", "Welcome NewMember! Feel free to test the upload button");
            formatter->AppendMessage("12:15", "NewMember", "Thanks! Testing the chat now");
            
            // Link in message
            formatter->AppendMessage("12:16", "David", "Check out https://github.com for more projects");
            
            // Long message
            formatter->AppendMessage("12:17", "Eve", 
                "This is a longer message to test how the chat handles multi-line content. "
                "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod tempor "
                "incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud "
                "exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat.");
            
            // Final service message
            formatter->AppendServiceMessage("12:18", "End of test messages - try the Upload button!");
            
            if (display) {
                display->EndSuppressUndo();
                display->ShowPosition(display->GetLastPosition());
            }
        }
    }
    
    // Enable upload button for testing
    if (m_inputBoxWidget) {
        m_inputBoxWidget->EnableUploadButtons(true);
    }
}



// File drop handler

void MainFrame::OnFilesDropped(const wxArrayString& files)
{
    wxDateTime now = wxDateTime::Now();
    wxString timestamp = now.Format("%H:%M");
    
    MessageFormatter* formatter = m_chatViewWidget ? m_chatViewWidget->GetMessageFormatter() : nullptr;
    
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
        if (formatter) {
            formatter->AppendMediaMessage(timestamp, m_currentUser.IsEmpty() ? "You" : m_currentUser, 
                              media, "");
            if (m_chatViewWidget) {
                m_chatViewWidget->AddMediaSpan(formatter->GetLastMediaSpanStart(), 
                    formatter->GetLastMediaSpanEnd(), media);
            }
        }
        
        // Simulate completion (will be replaced by TDLib callbacks)
        m_transferManager.CompleteTransfer(transferId, file);
    }
    
    if (m_chatViewWidget) {
        m_chatViewWidget->ScrollToBottom();
    }
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

// Menu event handlers

void MainFrame::OnExit(wxCommandEvent& event)
{
    Close(true);
}

void MainFrame::OnAbout(wxCommandEvent& event)
{
    wxMessageBox("Teleliter 0.1.0\n\n"
                 "A Telegram client with HexChat-style interface\n\n"
                 "Built with TDLib and wxWidgets\n\n"
                 "Commands:\n"
                 "  /me <action>     - Send an action\n"
                 "  /clear           - Clear chat window\n"
                 "  /query <user>    - Open private chat\n"
                 "  /whois <user>    - View user info\n"
                 "  /away [message]  - Set away status\n"
                 "  /help            - Show all commands\n\n"
                 "Keyboard:\n"
                 "  Tab              - Nick completion\n"
                 "  Up/Down          - Input history\n"
                 "  Page Up/Down     - Scroll chat\n"
                 "  Ctrl+V           - Paste image\n\n"
                 "Drag & drop files to upload\n"
                 "Hover over [Photo], [Video] to preview",
                 "About Teleliter",
                 wxOK | wxICON_INFORMATION, this);
}

void MainFrame::OnLogin(wxCommandEvent& event)
{
    // Switch to Teleliter welcome chat and start login
    if (m_chatListWidget) {
        m_chatListWidget->SelectTeleliter();
    }
    
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
        if (m_chatViewWidget && m_chatViewWidget->GetMessageFormatter()) {
            m_chatViewWidget->GetMessageFormatter()->AppendServiceMessage(wxDateTime::Now().Format("%H:%M"), 
                                "Starting chat with " + contact);
        }
    }
}

void MainFrame::OnNewGroup(wxCommandEvent& event)
{
    wxTextEntryDialog dlg(this, "Enter group name:", "New Group");
    if (dlg.ShowModal() == wxID_OK) {
        wxString groupName = dlg.GetValue();
        if (m_chatViewWidget && m_chatViewWidget->GetMessageFormatter()) {
            m_chatViewWidget->GetMessageFormatter()->AppendServiceMessage(wxDateTime::Now().Format("%H:%M"), 
                                "Creating group: " + groupName);
        }
    }
}

void MainFrame::OnNewChannel(wxCommandEvent& event)
{
    wxTextEntryDialog dlg(this, "Enter channel name:", "New Channel");
    if (dlg.ShowModal() == wxID_OK) {
        wxString channelName = dlg.GetValue();
        if (m_chatViewWidget && m_chatViewWidget->GetMessageFormatter()) {
            m_chatViewWidget->GetMessageFormatter()->AppendServiceMessage(wxDateTime::Now().Format("%H:%M"), 
                                "Creating channel: " + channelName);
        }
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
        if (m_chatViewWidget && m_chatViewWidget->GetMessageFormatter()) {
            m_chatViewWidget->GetMessageFormatter()->AppendServiceMessage(wxDateTime::Now().Format("%H:%M"), 
                                "Searching for: " + query);
        }
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
    if (m_chatViewWidget) {
        m_chatViewWidget->ClearMessages();
    }
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

void MainFrame::OnToggleUnreadFirst(wxCommandEvent& event)
{
    m_showUnreadFirst = !m_showUnreadFirst;
    RefreshChatList();
}

void MainFrame::OnFullscreen(wxCommandEvent& event)
{
    ShowFullScreen(!IsFullScreen(), wxFULLSCREEN_NOTOOLBAR | wxFULLSCREEN_NOSTATUSBAR | wxFULLSCREEN_NOBORDER | wxFULLSCREEN_NOCAPTION);
}

void MainFrame::OnChatTreeSelectionChanged(wxTreeEvent& event)
{
    DBGLOG("OnChatTreeSelectionChanged called");
    
    // Guard against events during initialization when UI elements aren't created yet
    if (!m_chatInfoBar || !m_welcomeChat || !m_chatViewWidget || !m_chatPanel || !m_chatListWidget) {
        DBGLOG("Guard check failed - UI elements not ready");
        return;
    }
    
    wxTreeItemId item = event.GetItem();
    if (item.IsOk()) {
        wxTreeCtrl* chatTree = m_chatListWidget->GetTreeCtrl();
        if (!chatTree) return;
        
        wxString chatName = chatTree->GetItemText(item);
        
        // Check if Teleliter (welcome) is selected
        if (m_chatListWidget->IsTeleliterSelected()) {
            m_currentChatId = 0;
            m_chatInfoBar->SetValue("Teleliter");
            // Clear topic bar when going to welcome screen
            if (m_chatViewWidget) {
                m_chatViewWidget->ClearTopicText();
            }
            // Use sizer Show/Hide to properly manage layout
            wxSizer* sizer = m_chatPanel->GetSizer();
            if (sizer) {
                sizer->Show(m_welcomeChat, true);
                sizer->Show(m_chatViewWidget, false);
            }
            m_chatPanel->Layout();
            
            // Update status bar - no chat selected
            if (m_statusBar) {
                m_statusBar->SetCurrentChatId(0);
                m_statusBar->SetCurrentChatTitle("");
            }
            
            // Disable upload buttons when no chat selected
            if (m_inputBoxWidget) {
                m_inputBoxWidget->EnableUploadButtons(false);
            }
            return;
        }
        
        // Check if this is a category item
        if (item == m_chatListWidget->GetPinnedChats() || 
            item == m_chatListWidget->GetPrivateChats() || 
            item == m_chatListWidget->GetGroups() ||
            item == m_chatListWidget->GetChannels() || 
            item == m_chatListWidget->GetBots()) {
            return;  // Don't select categories
        }
        
        // Look up chat ID from tree item
        int64_t chatId = m_chatListWidget->GetChatIdFromTreeItem(item);
        DBGLOG("Chat ID from tree item: " << chatId);
        if (chatId != 0) {
            // Update current chat
            m_currentChatId = chatId;
            m_currentChatTitle = chatName;
            
            // Remove unread indicator from title
            int parenPos = chatName.Find('(');
            if (parenPos != wxNOT_FOUND) {
                m_currentChatTitle = chatName.Left(parenPos).Trim();
            }
            
            m_chatInfoBar->SetValue(m_currentChatTitle);
            
            // Update status bar with current chat info
            if (m_statusBar) {
                m_statusBar->SetCurrentChatId(chatId);
                m_statusBar->SetCurrentChatTitle(m_currentChatTitle);
            }
            // Use sizer Show/Hide to properly manage layout
            wxSizer* sizer = m_chatPanel->GetSizer();
            if (sizer) {
                sizer->Show(m_welcomeChat, false);
                sizer->Show(m_chatViewWidget, true);
            }
            m_chatPanel->Layout();
            
            // Remove bold and unread indicator (HexChat style)
            chatTree->SetItemBold(item, false);
            chatTree->SetItemTextColour(item, m_treeFgColor);
            
            // Update title to remove unread count
            if (chatId != -1 && m_telegramClient) {
                bool found = false;
                ChatInfo chat = m_telegramClient->GetChat(chatId, &found);
                if (found) {
                    chatTree->SetItemText(item, chat.title);
                }
            }
            
            // Mark chat as read
            m_chatsWithUnread.erase(chatId);
            
            // Update member list for this chat
            UpdateMemberList(chatId);
            
            // Check if this is the Test Chat (ID -1)
            if (chatId == -1) {
                DBGLOG("Test chat selected, loading dummy data");
                // Load dummy data for testing
                m_chatViewWidget->ClearMessages();
                m_chatViewWidget->SetTopicText("Test Chat", "Demo mode • Testing features");
                PopulateDummyData();
            } else if (m_telegramClient) {
                DBGLOG("Loading messages from TDLib for chatId=" << chatId);
                // Clear the view and load messages (OpenChatAndLoadMessages handles opening first)
                m_chatViewWidget->ClearMessages();
                
                // Set topic bar with chat info (HexChat-style)
                bool chatFound = false;
                ChatInfo chatInfo = m_telegramClient->GetChat(chatId, &chatFound);
                if (chatFound) {
                    wxString topicInfo;
                    if (chatInfo.isChannel) {
                        topicInfo = "Channel";
                        if (chatInfo.memberCount > 0) {
                            topicInfo += wxString::Format(" • %d subscribers", chatInfo.memberCount);
                        }
                    } else if (chatInfo.isSupergroup || chatInfo.isGroup) {
                        topicInfo = chatInfo.isSupergroup ? "Supergroup" : "Group";
                        if (chatInfo.memberCount > 0) {
                            topicInfo += wxString::Format(" • %d members", chatInfo.memberCount);
                        }
                    } else if (chatInfo.isBot) {
                        topicInfo = "Bot";
                    } else if (chatInfo.isPrivate) {
                        topicInfo = "Private chat";
                    }
                    m_chatViewWidget->SetTopicText(chatInfo.title, topicInfo);
                }
                
                m_telegramClient->OpenChatAndLoadMessages(chatId);
                m_telegramClient->MarkChatAsRead(chatId);
            } else {
                DBGLOG("ERROR: m_telegramClient is null!");
            }
            
            // Set focus to input box and enable upload buttons (always enable for test chat)
            if (m_inputBoxWidget) {
                m_inputBoxWidget->SetFocus();
                m_inputBoxWidget->EnableUploadButtons(m_isLoggedIn || chatId == -1);
            }
        } else {
            // Fallback for items without chat ID (shouldn't happen with TDLib)
            m_currentChatTitle = chatName;
            m_chatInfoBar->SetValue(chatName);
            wxSizer* sizer = m_chatPanel->GetSizer();
            if (sizer) {
                sizer->Show(m_welcomeChat, false);
                sizer->Show(m_chatViewWidget, true);
            }
            m_chatPanel->Layout();
            
            // Update status bar
            if (m_statusBar) {
                m_statusBar->SetCurrentChatId(0);
                m_statusBar->SetCurrentChatTitle(chatName);
            }
            
            // Set focus to input box and enable upload buttons
            if (m_inputBoxWidget) {
                m_inputBoxWidget->SetFocus();
                m_inputBoxWidget->EnableUploadButtons(m_isLoggedIn);
            }
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
    
    if (m_chatViewWidget && m_chatViewWidget->GetMessageFormatter()) {
        m_chatViewWidget->GetMessageFormatter()->AppendServiceMessage(wxDateTime::Now().Format("%H:%M"), 
                            "Opening profile: " + username);
    }
}

void MainFrame::OnMemberListRightClick(wxListEvent& event)
{
    (void)event; // Unused for now
    
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
        return wxDateTime::Now().Format("%H:%M");
    }
    
    time_t t = static_cast<time_t>(unixTime);
    wxDateTime dt(t);
    return dt.Format("%H:%M");
}

void MainFrame::OnConnected()
{
    if (m_statusBar) {
        m_statusBar->SetOnline(true);
        m_statusBar->UpdateStatusBar();
    }
}

void MainFrame::OnLoginSuccess(const wxString& userName)
{
    m_isLoggedIn = true;
    m_currentUser = userName;
    
    // Update InputBoxWidget with current user
    if (m_inputBoxWidget) {
        m_inputBoxWidget->SetCurrentUser(userName);
    }
    
    // Update ChatViewWidget with current user for mention/highlight detection
    if (m_chatViewWidget) {
        m_chatViewWidget->SetCurrentUsername(userName);
    }
    
    // Update status bar
    if (m_statusBar) {
        m_statusBar->SetLoggedIn(true);
        m_statusBar->SetOnline(true);
        m_statusBar->SetCurrentUser(userName);
        m_statusBar->ResetSessionTimer();
        m_statusBar->UpdateStatusBar();
    }
    
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
    
    // Update status bar
    if (m_statusBar) {
        m_statusBar->SetLoggedIn(false);
        m_statusBar->SetOnline(false);
        m_statusBar->SetCurrentUser("");
        m_statusBar->SetTotalChats(0);
        m_statusBar->SetUnreadChats(0);
        m_statusBar->SetCurrentChatMemberCount(0);
    }
    
    // Clear current user from InputBoxWidget and disable upload buttons
    if (m_inputBoxWidget) {
        m_inputBoxWidget->SetCurrentUser("");
        m_inputBoxWidget->EnableUploadButtons(false);
    }
    
    // Clear chat list
    if (m_chatListWidget) {
        m_chatListWidget->ClearAllChats();
    }
    
    // Show welcome chat
    wxSizer* sizer = m_chatPanel->GetSizer();
    if (sizer) {
        sizer->Show(m_welcomeChat, true);
        sizer->Show(m_chatViewWidget, false);
    }
    m_chatPanel->Layout();
    if (m_chatListWidget) {
        m_chatListWidget->SelectTeleliter();
    }
    
    if (m_statusBar) {
        m_statusBar->UpdateStatusBar();
    }
    
    // Update menu state
    wxMenuBar* menuBar = GetMenuBar();
    if (menuBar) {
        menuBar->Enable(ID_LOGIN, true);
        menuBar->Enable(ID_LOGOUT, false);
    }
}

void MainFrame::RefreshChatList()
{
    if (!m_telegramClient || !m_chatListWidget) return;
    
    // Get chats from TelegramClient
    const auto& chats = m_telegramClient->GetChats();
    
    // Track chat counts for status bar
    int totalChats = chats.size();
    int unreadChats = 0;
    
    // Sort chats: unread first (within each category), then by order (descending)
    // This ensures unread chats appear at the top of their respective category
    std::vector<ChatInfo> sortedChats;
    for (const auto& [id, chat] : chats) {
        sortedChats.push_back(chat);
        if (chat.unreadCount > 0) {
            unreadChats++;
        }
    }
    
    // Update status bar with chat counts
    if (m_statusBar) {
        m_statusBar->SetTotalChats(totalChats);
        m_statusBar->SetUnreadChats(unreadChats);
    }
    bool unreadFirst = m_showUnreadFirst;
    std::sort(sortedChats.begin(), sortedChats.end(), 
              [unreadFirst](const ChatInfo& a, const ChatInfo& b) {
                  // Helper to get category priority (for grouping)
                  auto getCategoryPriority = [](const ChatInfo& c) -> int {
                      if (c.isPinned) return 0;
                      if (c.isBot) return 4;
                      if (c.isChannel) return 3;
                      if (c.isGroup || c.isSupergroup) return 2;
                      return 1; // Private chats
                  };
                  
                  int catA = getCategoryPriority(a);
                  int catB = getCategoryPriority(b);
                  
                  // First sort by category
                  if (catA != catB) {
                      return catA < catB;
                  }
                  
                  // Within same category: unread chats first (if setting enabled)
                  if (unreadFirst) {
                      bool aHasUnread = a.unreadCount > 0;
                      bool bHasUnread = b.unreadCount > 0;
                      if (aHasUnread != bHasUnread) {
                          return aHasUnread; // Unread comes first
                      }
                  }
                  
                  // Then by order (descending - higher order first)
                  return a.order > b.order;
              });
    
    // Refresh chat list widget with sorted chats
    m_chatListWidget->RefreshChatList(sortedChats);
    
    // Expand categories that have items
    wxTreeCtrl* chatTree = m_chatListWidget->GetTreeCtrl();
    if (chatTree) {
        if (chatTree->GetChildrenCount(m_chatListWidget->GetPinnedChats()) > 0) {
            chatTree->Expand(m_chatListWidget->GetPinnedChats());
        }
        if (chatTree->GetChildrenCount(m_chatListWidget->GetPrivateChats()) > 0) {
            chatTree->Expand(m_chatListWidget->GetPrivateChats());
        }
        if (chatTree->GetChildrenCount(m_chatListWidget->GetGroups()) > 0) {
            chatTree->Expand(m_chatListWidget->GetGroups());
        }
        if (chatTree->GetChildrenCount(m_chatListWidget->GetChannels()) > 0) {
            chatTree->Expand(m_chatListWidget->GetChannels());
        }
        if (chatTree->GetChildrenCount(m_chatListWidget->GetBots()) > 0) {
            chatTree->Expand(m_chatListWidget->GetBots());
        }
    }
}

void MainFrame::OnMessagesLoaded(int64_t chatId, const std::vector<MessageInfo>& messages)
{
    DBGLOG("OnMessagesLoaded called: chatId=" << chatId << " m_currentChatId=" << m_currentChatId << " messages.size()=" << messages.size());
    
    if (chatId != m_currentChatId) {
        DBGLOG("Ignoring messages - chatId mismatch");
        return;  // Not the current chat, ignore
    }
    
    if (!m_chatViewWidget) {
        DBGLOG("ERROR: m_chatViewWidget is null!");
        return;
    }
    
    wxRichTextCtrl* display = m_chatViewWidget->GetDisplayCtrl();
    if (display) {
        display->BeginSuppressUndo();
    }
    m_chatViewWidget->ClearMessages();
    
    // Sort messages by message ID (primary) then date (secondary) for correct order
    // Telegram message IDs are monotonically increasing within a chat, making them
    // more reliable for ordering than timestamps (which have second granularity)
    std::vector<MessageInfo> sortedMessages = messages;
    std::sort(sortedMessages.begin(), sortedMessages.end(),
              [](const MessageInfo& a, const MessageInfo& b) {
                  if (a.id != b.id) {
                      return a.id < b.id;
                  }
                  return a.date < b.date;
              });
    
    // Get last read message ID for this chat
    int64_t lastReadId = GetLastReadMessageId(chatId);
    bool unreadMarkerInserted = false;
    
    // Get unread count from chat info
    int32_t unreadCount = 0;
    if (m_telegramClient) {
        bool found = false;
        ChatInfo chat = m_telegramClient->GetChat(chatId, &found);
        if (found) {
            unreadCount = chat.unreadCount;
        }
    }
    
    DBGLOG("Displaying " << sortedMessages.size() << " messages (unreadCount=" << unreadCount << ")");
    
    // Display messages in chronological order (oldest first)
    for (auto it = sortedMessages.begin(); it != sortedMessages.end(); ++it) {
        // Insert unread marker before first unread message (HexChat style)
        if (!unreadMarkerInserted && unreadCount > 0) {
            // Check if this message is after the last read one (it's unread)
            if (lastReadId > 0 && it->id > lastReadId) {
                // This is the first unread message, insert marker before it
                MessageFormatter* formatter = m_chatViewWidget->GetMessageFormatter();
                if (formatter) {
                    formatter->AppendUnreadMarker();
                }
                unreadMarkerInserted = true;
            }
        }
        
        // If no lastReadId but has unread, insert marker based on unread count
        if (!unreadMarkerInserted && unreadCount > 0 && lastReadId == 0) {
            // Calculate position: marker goes before the last N unread messages
            size_t currentIndex = std::distance(sortedMessages.begin(), it);
            size_t totalMessages = sortedMessages.size();
            size_t unreadStartIndex = totalMessages - unreadCount;
            if (currentIndex == unreadStartIndex) {
                MessageFormatter* formatter = m_chatViewWidget->GetMessageFormatter();
                if (formatter) {
                    formatter->AppendUnreadMarker();
                }
                unreadMarkerInserted = true;
            }
        }
        
        DisplayMessage(*it);
    }
    
    if (display) {
        display->EndSuppressUndo();
        display->LayoutContent();
        display->Refresh();
        display->Update();
    }
    m_chatViewWidget->ScrollToBottom();
    
    DBGLOG("Finished displaying messages, scrolled to bottom");
}

void MainFrame::OnNewMessage(const MessageInfo& message)
{
    if (message.chatId != m_currentChatId) {
        // Update unread count in tree - HexChat style
        m_chatsWithUnread.insert(message.chatId);
        
        if (m_chatListWidget) {
            wxTreeItemId item = m_chatListWidget->GetTreeItemFromChatId(message.chatId);
            if (item.IsOk()) {
                wxTreeCtrl* chatTree = m_chatListWidget->GetTreeCtrl();
                if (chatTree) {
                    // Make bold and change color for unread
                    chatTree->SetItemBold(item, true);
                    chatTree->SetItemTextColour(item, wxColour(0xFF, 0x80, 0x00)); // Orange
                    
                    // Update title with incremented unread count
                    wxString title = chatTree->GetItemText(item);
                    int parenPos = title.Find(" (");
                    int currentCount = 1;
                    if (parenPos != wxNOT_FOUND) {
                        wxString countStr = title.Mid(parenPos + 2);
                        countStr = countStr.BeforeFirst(')');
                        long count = 0;
                        if (countStr.ToLong(&count)) {
                            currentCount = count + 1;
                        }
                        title = title.Left(parenPos);
                    }
                    chatTree->SetItemText(item, title + wxString::Format(" (%d)", currentCount));
                }
            }
        }
        
        // Flash the window title to notify user (HexChat style)
        if (!HasFocus()) {
            RequestUserAttention(wxUSER_ATTENTION_INFO);
        }
        return;
    }
    
    // Display the new message
    DisplayMessage(message);
    if (m_chatViewWidget) {
        // Use smart scrolling - only scroll if user was already at bottom
        m_chatViewWidget->ScrollToBottomIfAtBottom();
    }
    
    // Mark as read since we're viewing this chat
    if (m_telegramClient) {
        m_telegramClient->MarkChatAsRead(message.chatId);
    }
    MarkMessageAsRead(message.chatId, message.id);
}

void MainFrame::DisplayMessage(const MessageInfo& msg)
{
    if (!m_chatViewWidget) return;
    
    // Delegate to ChatViewWidget which handles all message formatting
    m_chatViewWidget->DisplayMessage(msg);
}

void MainFrame::OnMessageEdited(int64_t chatId, int64_t messageId, const wxString& newText)
{
    if (chatId != m_currentChatId) {
        return;
    }
    
    // For now, just add a service message noting the edit
    // A more sophisticated implementation would find and update the original message
    if (m_chatViewWidget && m_chatViewWidget->GetMessageFormatter()) {
        m_chatViewWidget->GetMessageFormatter()->AppendServiceMessage(wxDateTime::Now().Format("%H:%M"),
                            wxString::Format("Message %lld was edited: %s", messageId, newText));
        m_chatViewWidget->ScrollToBottomIfAtBottom();
    }
}

void MainFrame::OnFileDownloaded(int32_t fileId, const wxString& localPath)
{
    // Check if this is a pending download for media preview in ChatViewWidget
    if (m_chatViewWidget) {
        // Update media popup if it's showing this file
        m_chatViewWidget->UpdateMediaPopup(fileId, localPath);
        
        if (m_chatViewWidget->HasPendingDownload(fileId)) {
            MediaInfo info = m_chatViewWidget->GetPendingDownload(fileId);
            info.localPath = localPath;
            m_chatViewWidget->RemovePendingDownload(fileId);
        }
    }
    
    // Don't reload all messages just for a download - it's disruptive
    // The media popup will update automatically
    
    // Update transfer manager
    // Find transfer by checking pending transfers (simplified - in real impl would track by file ID)
    SetStatusText("Download complete: " + localPath.AfterLast('/'), 1);
}

void MainFrame::OnFileProgress(int32_t fileId, int64_t downloadedSize, int64_t totalSize)
{
    // Progress is now handled by StatusBarManager via TransferManager callbacks
    // This function is called directly from TelegramClient for file downloads
    // We could forward this to the TransferManager if needed
    (void)fileId;
    (void)downloadedSize;
    (void)totalSize;
}

void MainFrame::ShowStatusError(const wxString& error)
{
    SetStatusText("Error: " + error, 1);
}

void MainFrame::UpdateMemberList(int64_t chatId)
{
    DBGLOG("UpdateMemberList called: chatId=" << chatId);
    
    if (!m_memberList || !m_memberCountLabel) {
        DBGLOG("UpdateMemberList: m_memberList or m_memberCountLabel is null");
        return;
    }
    
    // Clear existing members
    m_memberList->DeleteAllItems();
    DBGLOG("UpdateMemberList: cleared existing items");
    
    // Handle test chat
    if (chatId == -1) {
        // Populate with dummy members for test chat
        long idx = 0;
        m_memberList->InsertItem(idx++, "Admin (owner)");
        m_memberList->InsertItem(idx++, "Alice");
        m_memberList->InsertItem(idx++, "Bob");
        m_memberList->InsertItem(idx++, "Charlie");
        m_memberList->InsertItem(idx++, "David");
        m_memberList->InsertItem(idx++, "Eve");
        m_memberList->InsertItem(idx++, "Frank");
        m_memberList->InsertItem(idx++, "Grace");
        m_memberList->InsertItem(idx++, "Henry");
        m_memberCountLabel->SetLabel(wxString::Format("%ld members", idx));
        if (m_statusBar) {
            m_statusBar->SetCurrentChatMemberCount(idx);
        }
        return;
    }
    
    if (!m_telegramClient) {
        DBGLOG("UpdateMemberList: m_telegramClient is null");
        return;
    }
    
    // Get chat info
    bool found = false;
    ChatInfo chat = m_telegramClient->GetChat(chatId, &found);
    DBGLOG("UpdateMemberList: GetChat found=" << found << " title=" << chat.title.ToStdString());
    if (!found) {
        DBGLOG("UpdateMemberList: chat not found, returning");
        return;
    }
    
    // For private chats (1-1), show just the two participants
    DBGLOG("UpdateMemberList: isPrivate=" << chat.isPrivate << " isBot=" << chat.isBot << " isGroup=" << chat.isGroup);
    if (chat.isPrivate) {
        long idx = 0;
        
        // Add current user (you)
        if (!m_currentUser.IsEmpty()) {
            m_memberList->InsertItem(idx++, m_currentUser + " (you)");
        } else {
            m_memberList->InsertItem(idx++, "You");
        }
        
        // Add the other user (the chat title is usually their name)
        m_memberList->InsertItem(idx++, chat.title);
        
        m_memberCountLabel->SetLabel("2 members");
        if (m_statusBar) {
            m_statusBar->SetCurrentChatMemberCount(2);
        }
        DBGLOG("UpdateMemberList: private chat - added 2 members");
        return;
    }
    
    // For bot chats, show just you and the bot
    if (chat.isBot) {
        long idx = 0;
        
        if (!m_currentUser.IsEmpty()) {
            m_memberList->InsertItem(idx++, m_currentUser + " (you)");
        } else {
            m_memberList->InsertItem(idx++, "You");
        }
        
        m_memberList->InsertItem(idx++, chat.title + " [bot]");
        
        m_memberCountLabel->SetLabel("2 members");
        if (m_statusBar) {
            m_statusBar->SetCurrentChatMemberCount(2);
        }
        DBGLOG("UpdateMemberList: bot chat - added 2 members");
        return;
    }
    
    // For groups and channels, we would need to fetch member list from TDLib
    // For now, show placeholder with current user
    // TODO: Implement TDLib getChatMembers / getSupergroupMembers
    DBGLOG("UpdateMemberList: group/channel chat - showing placeholder");
    long idx = 0;
    
    if (!m_currentUser.IsEmpty()) {
        m_memberList->InsertItem(idx++, m_currentUser + " (you)");
    }
    
    // For groups/supergroups, show member count from chat info if available
    if (chat.memberCount > 0) {
        m_memberCountLabel->SetLabel(wxString::Format("%d members", chat.memberCount));
        if (m_statusBar) {
            m_statusBar->SetCurrentChatMemberCount(chat.memberCount);
        }
    } else {
        // Show placeholder message
        m_memberCountLabel->SetLabel("Loading members...");
        if (m_statusBar) {
            m_statusBar->SetCurrentChatMemberCount(0);
        }
    }
    DBGLOG("UpdateMemberList: added " << idx << " members, count label set");
    
    // TODO: Add actual member loading via TDLib
    // For groups: use getBasicGroupFullInfo
    // For supergroups/channels: use getSupergroupMembers
}

void MainFrame::OnRefreshTimer(wxTimerEvent& event)
{
    // Periodic refresh - reload chats to get updated unread counts
    if (m_telegramClient && m_telegramClient->IsLoggedIn()) {
        m_telegramClient->LoadChats();
    }
}

void MainFrame::MarkMessageAsRead(int64_t chatId, int64_t messageId)
{
    m_lastReadMessages[chatId] = messageId;
    m_chatsWithUnread.erase(chatId);
    
    // Update chat list to remove unread indicator
    if (m_chatListWidget) {
        wxTreeItemId item = m_chatListWidget->GetTreeItemFromChatId(chatId);
        if (item.IsOk()) {
            wxTreeCtrl* chatTree = m_chatListWidget->GetTreeCtrl();
            if (chatTree) {
                chatTree->SetItemBold(item, false);
            }
        }
    }
}

void MainFrame::UpdateUnreadIndicator(int64_t chatId, int32_t unreadCount)
{
    if (!m_chatListWidget) return;
    
    wxTreeItemId item = m_chatListWidget->GetTreeItemFromChatId(chatId);
    if (!item.IsOk()) return;
    
    wxTreeCtrl* chatTree = m_chatListWidget->GetTreeCtrl();
    if (!chatTree) return;
    
    // Get chat info for title
    wxString title;
    if (m_telegramClient) {
        bool found = false;
        ChatInfo chat = m_telegramClient->GetChat(chatId, &found);
        title = found ? chat.title : chatTree->GetItemText(item);
    } else {
        title = chatTree->GetItemText(item);
    }
    
    // Remove old unread count from title if present
    int parenPos = title.Find(" (");
    if (parenPos != wxNOT_FOUND) {
        title = title.Left(parenPos);
    }
    
    // Add new unread count
    if (unreadCount > 0) {
        title += wxString::Format(" (%d)", unreadCount);
        chatTree->SetItemBold(item, true);
        chatTree->SetItemTextColour(item, wxColour(0xFF, 0x80, 0x00)); // Orange for unread
        m_chatsWithUnread.insert(chatId);
    } else {
        chatTree->SetItemBold(item, false);
        chatTree->SetItemTextColour(item, m_treeFgColor);
        m_chatsWithUnread.erase(chatId);
    }
    
    chatTree->SetItemText(item, title);
}

int64_t MainFrame::GetLastReadMessageId(int64_t chatId) const
{
    auto it = m_lastReadMessages.find(chatId);
    if (it != m_lastReadMessages.end()) {
        return it->second;
    }
    return 0;
}