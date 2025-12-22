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

// Helper function to format last seen time
static wxString FormatLastSeen(int64_t lastSeenTime)
{
    if (lastSeenTime <= 0) {
        return "last seen recently";
    }

    wxDateTime lastSeen(static_cast<time_t>(lastSeenTime));
    if (!lastSeen.IsValid()) {
        return "last seen recently";
    }

    wxDateTime now = wxDateTime::Now();
    wxTimeSpan diff = now - lastSeen;

    // Guard against negative time differences (clock skew)
    if (diff.IsNegative()) {
        return "last seen just now";
    }

    if (diff.GetMinutes() < 1) {
        return "last seen just now";
    } else if (diff.GetMinutes() < 60) {
        int mins = static_cast<int>(diff.GetMinutes());
        return wxString::Format("last seen %d min ago", mins);
    } else if (diff.GetHours() < 24) {
        int hours = static_cast<int>(diff.GetHours());
        return wxString::Format("last seen %d hour%s ago", hours, hours == 1 ? "" : "s");
    } else if (diff.GetDays() == 1) {
        return "last seen yesterday at " + lastSeen.Format("%H:%M:%S");
    } else if (diff.GetDays() < 7) {
        return "last seen " + lastSeen.Format("%A at %H:%M:%S");
    } else {
        return "last seen " + lastSeen.Format("%b %d");
    }
}
// Debug logging - enabled for troubleshooting
#include <iostream>
// #define DBGLOG(msg) std::cerr << "[MainFrame] " << msg << std::endl
// Disable debug logging for release:
#define DBGLOG(msg) do {} while(0)

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

    // macOS renders fonts smaller, so use larger sizes on Mac
#ifdef __WXOSX__
    int chatFontSize = 12;
    int treeFontSize = 12;
    int memberFontSize = 12;
    int inputFontSize = 12;  // Same as ChatArea font
#else
    int chatFontSize = 9;
    int treeFontSize = 9;
    int memberFontSize = 9;
    int inputFontSize = 8;  // Smaller than ChatArea font
#endif

    m_chatFont = wxFont(chatFontSize, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, fontName);
    m_treeFont = wxFont(treeFontSize, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    m_memberListFont = wxFont(memberFontSize, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    // Input font must match ChatArea exactly - same size, same family, no explicit font name
    m_inputFont = wxFont(inputFontSize, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
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

    // Welcome chat panel (shown when "Teleliter" is selected in tree)
    m_welcomeChat = new WelcomeChat(parent, this);
    sizer->Add(m_welcomeChat, 1, wxEXPAND);

    // Chat view widget (hidden initially, shown when a chat is selected)
    // Uses ChatArea internally which handles colors and font consistently with WelcomeChat
    m_chatViewWidget = new ChatViewWidget(parent, this);
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
#ifdef __WXOSX__
    m_memberCountLabel->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
#else
    m_memberCountLabel->SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
#endif
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
    // Topic bar is set via ChatViewWidget::SetTopicText in OnChatTreeSelectionChanged

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
                m_chatViewWidget->AddMediaSpan(startPos, endPos, photoInfo, 0);
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
                m_chatViewWidget->AddMediaSpan(startPos, endPos, videoInfo, 0);
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
                m_chatViewWidget->AddMediaSpan(startPos, endPos, fileInfo, 0);
            }

            // Voice message
            MediaInfo voiceInfo;
            voiceInfo.type = MediaType::Voice;
            voiceInfo.fileId = 0;  // Test data - no real file ID
            {
                long startPos = display->GetLastPosition();
                formatter->AppendMediaMessage("12:05", "David", voiceInfo, "");
                long endPos = display->GetLastPosition();
                m_chatViewWidget->AddMediaSpan(startPos, endPos, voiceInfo, 0);
            }

            // Video note (round video)
            MediaInfo videoNoteInfo;
            videoNoteInfo.type = MediaType::VideoNote;
            videoNoteInfo.fileId = 0;  // Test data - no real file ID
            {
                long startPos = display->GetLastPosition();
                formatter->AppendMediaMessage("12:06", "Eve", videoNoteInfo, "");
                long endPos = display->GetLastPosition();
                m_chatViewWidget->AddMediaSpan(startPos, endPos, videoNoteInfo, 0);
            }

            // Sticker
            MediaInfo stickerInfo;
            stickerInfo.type = MediaType::Sticker;
            stickerInfo.fileId = 0;  // Test data - no real file ID
            stickerInfo.emoji = ":)";
            {
                long startPos = display->GetLastPosition();
                formatter->AppendMediaMessage("12:07", "Frank", stickerInfo, "");
                long endPos = display->GetLastPosition();
                m_chatViewWidget->AddMediaSpan(startPos, endPos, stickerInfo, 0);
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
                m_chatViewWidget->AddMediaSpan(startPos, endPos, gifInfo, 0);
            }

            // Action message (/me)
            formatter->AppendActionMessage("12:09", "Henry", "is laughing at the GIF");

            // Reply message
            formatter->AppendReplyMessage("12:10", "Alice", "Bob: Check out this funny cat video!",
                               "Haha that's so cute! :D");

            // Forward message
            formatter->AppendForwardMessage("12:11", "Bob", "Tech News Channel",
                             "Breaking: New wxWidgets 3.3 released with improved dark mode support!");

            // Edited message
            formatter->AppendEditedMessage("12:12", "Charlie", "I made a typo but fixed it now (edited)");

            // Notice message
            formatter->AppendNoticeMessage("12:13", "Teleliter", "This is a system notice");

            // User joined/left messages
            formatter->AppendUserJoinedMessage("12:14", "NewMember");
            formatter->AppendUserLeftMessage("12:14", "OldMember");

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
    // Must be logged in and have a chat selected to upload files
    if (!m_isLoggedIn || !m_telegramClient) {
        if (m_statusBar) {
            ShowStatusError("Please log in first to send files");
        }
        return;
    }

    if (m_currentChatId == 0) {
        ShowStatusError("Please select a chat first to send files");
        return;
    }

    for (const auto& file : files) {
        // Start upload with transfer manager for progress tracking
        wxFile wxf(file);
        int64_t fileSize = wxf.IsOpened() ? wxf.Length() : 0;
        wxf.Close();
        int transferId = m_transferManager.StartUpload(file, fileSize);

        // Actually send the file via TDLib
        // TelegramClient::SendFile automatically detects media type based on extension
        // and sends as photo, video, audio, or document accordingly
        m_telegramClient->SendFile(m_currentChatId, file, "");

        // Mark transfer as complete (actual progress tracking would require
        // hooking into TDLib's updateFile events for uploads)
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
                 "  /leave           - Leave current chat\n"
                 "  /help            - Show all commands\n\n"
                 "Keyboard:\n"
                 "  Tab              - User name completion\n"
                 "  Up/Down          - Input history\n"
                 "  Page Up/Down     - Scroll chat\n"
                 "  Ctrl+V           - Paste image\n\n"
                 "Drag & drop files to upload\n"
                 "Click [Photo], [Video] to preview",
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
            m_chatViewWidget->GetMessageFormatter()->AppendServiceMessage(wxDateTime::Now().Format("%H:%M:%S"),
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
            m_chatViewWidget->GetMessageFormatter()->AppendServiceMessage(wxDateTime::Now().Format("%H:%M:%S"),
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
            m_chatViewWidget->GetMessageFormatter()->AppendServiceMessage(wxDateTime::Now().Format("%H:%M:%S"),
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
            m_chatViewWidget->GetMessageFormatter()->AppendServiceMessage(wxDateTime::Now().Format("%H:%M:%S"),
                                "Searching for: " + query);
        }
    }
}

void MainFrame::OnSavedMessages(wxCommandEvent& event)
{
    m_currentChatTitle = "Saved Messages";
    m_currentChatType = TelegramChatType::SavedMessages;
    if (m_chatViewWidget) {
        m_chatViewWidget->SetTopicText("Saved Messages", "Your cloud storage");
    }

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
    // Topic bar is managed by ChatViewWidget - toggle not currently supported
    // Could add a method to ChatViewWidget to show/hide topic bar if needed
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
    if (!m_welcomeChat || !m_chatViewWidget || !m_chatPanel || !m_chatListWidget) {
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

            // Clear member panel when on welcome screen
            if (m_memberList) {
                m_memberList->DeleteAllItems();
            }
            if (m_memberCountLabel) {
                m_memberCountLabel->SetLabel("");
            }

            // Update status bar - no chat selected
            if (m_statusBar) {
                m_statusBar->SetCurrentChatId(0);
                m_statusBar->SetCurrentChatTitle("");
                m_statusBar->SetCurrentChatMemberCount(0);
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
                m_chatViewWidget->SetTopicText("Test Chat", "Demo mode - Testing features");
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
                            topicInfo += wxString::Format(" - %d subscribers", chatInfo.memberCount);
                        }
                    } else if (chatInfo.isSupergroup || chatInfo.isGroup) {
                        topicInfo = chatInfo.isSupergroup ? "Supergroup" : "Group";
                        if (chatInfo.memberCount > 0) {
                            topicInfo += wxString::Format(" - %d members", chatInfo.memberCount);
                        }
                    } else if (chatInfo.isBot) {
                        topicInfo = "Bot";
                    } else if (chatInfo.isPrivate && chatInfo.userId != 0) {
                        // For private chats, show online status or last seen
                        bool userFound = false;
                        UserInfo userInfo = m_telegramClient->GetUser(chatInfo.userId, &userFound);
                        if (userFound) {
                            if (userInfo.isOnline) {
                                topicInfo = "online";
                            } else {
                                topicInfo = FormatLastSeen(userInfo.lastSeenTime);
                            }
                        } else {
                            topicInfo = "Private chat";
                        }
                    } else if (chatInfo.isPrivate) {
                        topicInfo = "Private chat";
                    }
                    m_chatViewWidget->SetTopicText(chatInfo.title, topicInfo);

                    // Update status bar with member count
                    if (m_statusBar && chatInfo.memberCount > 0) {
                        m_statusBar->SetCurrentChatMemberCount(chatInfo.memberCount);
                    }
                }

                m_telegramClient->OpenChatAndLoadMessages(chatId);
                // Note: MarkChatAsRead is called in OnMessagesLoaded after messages are displayed
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
            if (m_chatViewWidget) {
                m_chatViewWidget->SetTopicText(chatName, "");
            }
            wxSizer* sizer = m_chatPanel->GetSizer();
            if (sizer) {
                sizer->Show(m_welcomeChat, false);
                sizer->Show(m_chatViewWidget, true);
            }
            m_chatPanel->Layout();

            // Update status bar
            if (m_statusBar) {
                m_statusBar->SetCurrentChatId(0);
                m_statusBar->SetCurrentChatTitle(m_currentChatTitle);
                m_statusBar->SetCurrentChatMemberCount(0);
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
        m_chatViewWidget->GetMessageFormatter()->AppendServiceMessage(wxDateTime::Now().Format("%H:%M:%S"),
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
        return wxDateTime::Now().Format("%H:%M:%S");
    }

    time_t t = static_cast<time_t>(unixTime);
    wxDateTime dt(t);
    return dt.Format("%H:%M:%S");
}

void MainFrame::OnConnected()
{

}

void MainFrame::OnLoginSuccess(const wxString& userName)
{
    m_isLoggedIn = true;
    m_currentUser = userName;

    // Update status bar
    if (m_statusBar) {
        m_statusBar->SetLoggedIn(true);
        m_statusBar->SetOnline(true);
        m_statusBar->SetCurrentUser(userName);
        m_statusBar->ResetSessionTimer();
    }

    // Update InputBoxWidget with current user
    if (m_inputBoxWidget) {
        m_inputBoxWidget->SetCurrentUser(userName);
    }

    // Update ChatViewWidget with current user for mention/highlight detection
    if (m_chatViewWidget) {
        m_chatViewWidget->SetCurrentUsername(userName);
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
        m_statusBar->SetCurrentChatTitle("");
        m_statusBar->SetCurrentChatId(0);
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

    // Sort chats: unread first (within each category), then by order (descending)
    // This ensures unread chats appear at the top of their respective category
    std::vector<ChatInfo> sortedChats;
    for (const auto& [id, chat] : chats) {
        sortedChats.push_back(chat);
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

    // Update status bar with chat counts
    if (m_statusBar) {
        int totalChats = static_cast<int>(sortedChats.size());
        int unreadChats = 0;
        for (const auto& chat : sortedChats) {
            if (chat.unreadCount > 0) {
                unreadChats++;
            }
        }
        m_statusBar->SetTotalChats(totalChats);
        m_statusBar->SetUnreadChats(unreadChats);
    }

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

    // Clear reloading state now that we have fresh messages
    m_chatViewWidget->SetReloading(false);

    // Clear existing messages and display all new ones in bulk
    // ChatViewWidget::DisplayMessages handles sorting internally
    m_chatViewWidget->ClearMessages();
    m_chatViewWidget->DisplayMessages(messages);

    // Scroll to bottom after loading
    m_chatViewWidget->ScrollToBottom();

    // Mark the chat as read now that messages are loaded and displayed
    if (m_telegramClient && !messages.empty()) {
        // Find the last message ID (newest) - need to find max since messages may not be sorted
        int64_t lastMsgId = 0;
        for (const auto& msg : messages) {
            if (msg.id > lastMsgId) {
                lastMsgId = msg.id;
            }
        }
        if (lastMsgId > 0) {
            // Update our local tracking
            MarkMessageAsRead(chatId, lastMsgId);
            // Tell TDLib we've viewed up to this message
            m_telegramClient->MarkChatAsRead(chatId);
        }
    }

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

    // Skip messages while reloading to prevent display corruption
    if (m_chatViewWidget && m_chatViewWidget->IsReloading()) {
        DBGLOG("OnNewMessage: skipping message id=" << message.id << " while reloading");
        return;
    }

    // Remove the "read up to here" marker if present, since user is actively viewing
    if (m_chatViewWidget && m_chatViewWidget->GetMessageFormatter()) {
        MessageFormatter* formatter = m_chatViewWidget->GetMessageFormatter();
        if (formatter->HasUnreadMarker()) {
            formatter->RemoveUnreadMarker();
        }
    }

    // Display the new message - ChatViewWidget now handles ordering automatically
    // Messages are stored in a sorted vector and re-rendered in correct order
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

void MainFrame::OnMessageUpdated(int64_t chatId, const MessageInfo& message)
{
    // Only update if this is the current chat
    if (chatId != m_currentChatId) return;
    
    if (!m_chatViewWidget) return;
    
    DBGLOG("OnMessageUpdated: chatId=" << chatId << " msgId=" << message.id 
           << " fileId=" << message.mediaFileId << " thumbId=" << message.mediaThumbnailFileId);
    
    // Update the message in ChatViewWidget's storage and refresh display
    m_chatViewWidget->UpdateMessage(message);
}

void MainFrame::OnMessageEdited(int64_t chatId, int64_t messageId, const wxString& newText, const wxString& senderName)
{
    if (chatId != m_currentChatId) {
        return;
    }

    // Don't show edit notifications for media messages (photos, videos, etc.)
    // These show as "[Photo]", "[Video]", etc. and edits are typically just caption changes
    // which aren't important enough to announce
    if (newText.StartsWith("[Photo]") || newText.StartsWith("[Video]") ||
        newText.StartsWith("[File]") || newText.StartsWith("[Voice") ||
        newText.StartsWith("[Sticker]") || newText.StartsWith("[GIF]") ||
        newText.StartsWith("[Audio]") || newText.StartsWith("[Contact]") ||
        newText.StartsWith("[Location]") || newText.StartsWith("[Poll]") ||
        newText.StartsWith("[Game]") || newText.StartsWith("[Invoice]")) {
        // Silently ignore media message edits (usually just caption changes)
        return;
    }

    // Show edit notification with sender name and full message content
    if (m_chatViewWidget && m_chatViewWidget->GetMessageFormatter() && !newText.IsEmpty()) {
        wxString sender = senderName.IsEmpty() ? "Someone" : senderName;
        // Show the full message (or truncate if extremely long)
        wxString displayText = newText;
        if (displayText.Length() > 500) {
            displayText = displayText.Left(500) + "...";
        }
        m_chatViewWidget->GetMessageFormatter()->AppendServiceMessage(wxDateTime::Now().Format("%H:%M:%S"),
                            wxString::Format("* %s edited: %s", sender, displayText));
        m_chatViewWidget->ScrollToBottomIfAtBottom();
    }
}

void MainFrame::OnFileDownloaded(int32_t fileId, const wxString& localPath)
{
    DBGLOG("OnFileDownloaded: fileId=" << fileId << " path=" << localPath.ToStdString());

    // Check if this is a pending download for media preview in ChatViewWidget
    if (m_chatViewWidget) {
        // Update m_messages (single source of truth) with the downloaded file path
        m_chatViewWidget->UpdateMediaPath(fileId, localPath);

        // Update media popup if it's showing this file
        m_chatViewWidget->UpdateMediaPopup(fileId, localPath);

        // Clean up pending download tracking
        if (m_chatViewWidget->HasPendingDownload(fileId)) {
            m_chatViewWidget->RemovePendingDownload(fileId);
        }
    }

    // Complete the transfer in TransferManager (status bar will auto-update)
    auto it = m_fileToTransferId.find(fileId);
    if (it != m_fileToTransferId.end()) {
        m_transferManager.CompleteTransfer(it->second, localPath);
        m_fileToTransferId.erase(it);
    }
}

void MainFrame::OnFileProgress(int32_t fileId, int64_t downloadedSize, int64_t totalSize)
{
    // Update progress in TransferManager (status bar will auto-update via callback)
    auto it = m_fileToTransferId.find(fileId);
    if (it != m_fileToTransferId.end()) {
        m_transferManager.UpdateProgress(it->second, downloadedSize, totalSize);
    }
}

void MainFrame::OnDownloadStarted(int32_t fileId, const wxString& fileName, int64_t totalSize)
{
    // Start a new download in TransferManager
    int transferId = m_transferManager.StartDownload(fileName, totalSize);
    m_fileToTransferId[fileId] = transferId;

    DBGLOG("Download started: fileId=" << fileId << " transferId=" << transferId << " file=" << fileName.ToStdString());
}

void MainFrame::OnDownloadFailed(int32_t fileId, const wxString& error)
{
    // Fail the transfer in TransferManager
    auto it = m_fileToTransferId.find(fileId);
    if (it != m_fileToTransferId.end()) {
        m_transferManager.FailTransfer(it->second, error);
        m_fileToTransferId.erase(it);
    }
    
    // Clean up pending download tracking so user can retry
    if (m_chatViewWidget) {
        if (m_chatViewWidget->HasPendingDownload(fileId)) {
            m_chatViewWidget->RemovePendingDownload(fileId);
        }
    }
}

void MainFrame::OnDownloadRetrying(int32_t fileId, int retryCount)
{
    // Update transfer status to show retry
    auto it = m_fileToTransferId.find(fileId);
    if (it != m_fileToTransferId.end()) {
        // Reset transfer to in-progress state for retry
        TransferInfo* info = m_transferManager.GetTransfer(it->second);
        if (info) {
            info->status = TransferStatus::InProgress;
            info->error = wxString::Format("Retry %d/%d", retryCount, 3);
        }
    }
}

void MainFrame::OnUserStatusChanged(int64_t userId, bool isOnline, int64_t lastSeenTime)
{
    // Guard against invalid state
    if (userId == 0) {
        return;
    }

    // Check if this user is the one in the current private chat
    if (m_currentChatId == 0 || !m_telegramClient || !m_chatViewWidget) {
        return;
    }

    bool chatFound = false;
    ChatInfo chatInfo = m_telegramClient->GetChat(m_currentChatId, &chatFound);

    if (!chatFound || !chatInfo.isPrivate || chatInfo.userId != userId) {
        return;  // Not the current chat's user
    }

    // Update the topic bar with new status
    wxString topicInfo;
    if (isOnline) {
        topicInfo = "online";
    } else {
        topicInfo = FormatLastSeen(lastSeenTime);
    }

    m_chatViewWidget->SetTopicText(chatInfo.title, topicInfo);
}

void MainFrame::OnMembersLoaded(int64_t chatId, const std::vector<UserInfo>& members)
{
    // Only update if this is still the current chat
    if (chatId != m_currentChatId) {
        return;
    }

    if (!m_memberList || !m_memberCountLabel) {
        return;
    }

    // Clear existing members
    m_memberList->DeleteAllItems();

    long idx = 0;
    for (const auto& member : members) {
        wxString displayName = member.GetDisplayName();
        if (displayName.IsEmpty()) {
            displayName = wxString::Format("User %lld", member.id);
        }

        // Mark current user
        if (member.isSelf || member.id == (m_telegramClient ? m_telegramClient->GetCurrentUser().id : 0)) {
            displayName += " (you)";
        }

        // Mark bots
        if (member.isBot) {
            displayName += " [bot]";
        }

        m_memberList->InsertItem(idx++, displayName);
    }

    // Update member count label
    if (idx > 0) {
        m_memberCountLabel->SetLabel(wxString::Format("%ld member%s", idx, idx == 1 ? "" : "s"));
    } else {
        m_memberCountLabel->SetLabel("No members");
    }



    DBGLOG("OnMembersLoaded: loaded " << idx << " members for chat " << chatId);
}

void MainFrame::ShowStatusError(const wxString& error)
{

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

        DBGLOG("UpdateMemberList: bot chat - added 2 members");
        return;
    }

    // For groups and channels, load members from TDLib
    DBGLOG("UpdateMemberList: group/channel chat - loading members from TDLib");

    // Show placeholder while loading
    if (!m_currentUser.IsEmpty()) {
        m_memberList->InsertItem(0, m_currentUser + " (you)");
    }

    // Show member count from chat info while we load details
    if (chat.memberCount > 0) {
        m_memberCountLabel->SetLabel(wxString::Format("%d members (loading...)", chat.memberCount));
    } else {
        m_memberCountLabel->SetLabel("Loading members...");
    }

    // Request member list from TDLib - OnMembersLoaded will be called when ready
    m_telegramClient->LoadChatMembers(chatId);
    DBGLOG("UpdateMemberList: requested member list from TDLib");
}

void MainFrame::OnRefreshTimer(wxTimerEvent& event)
{
    // Periodic refresh - reload chats to get updated unread counts
    if (m_telegramClient && m_telegramClient->IsLoggedIn()) {
        m_telegramClient->LoadChats();
    }
}

void MainFrame::ReactiveRefresh()
{
    // REACTIVE MVC: Poll dirty flags and update UI accordingly
    // This is called when TelegramClient signals updates are available
    if (!m_telegramClient) return;
    
    DirtyFlag flags = m_telegramClient->GetAndClearDirtyFlags();
    if (flags == DirtyFlag::None) return;
    
    // Handle chat list updates
    if ((flags & DirtyFlag::ChatList) != DirtyFlag::None) {
        RefreshChatList();
    }
    
    // Handle message updates for current chat
    if ((flags & DirtyFlag::Messages) != DirtyFlag::None && m_currentChatId != 0) {
        // Get new messages
        auto newMessages = m_telegramClient->GetNewMessages(m_currentChatId);
        for (const auto& msg : newMessages) {
            OnNewMessage(msg);
        }
        
        // Get updated messages (edits)
        auto updatedMessages = m_telegramClient->GetUpdatedMessages(m_currentChatId);
        for (const auto& msg : updatedMessages) {
            if (msg.isEdited) {
                OnMessageEdited(msg.chatId, msg.id, msg.text, msg.senderName);
            } else {
                OnMessageUpdated(msg.chatId, msg);
            }
        }
    }
    
    // Handle download updates
    if ((flags & DirtyFlag::Downloads) != DirtyFlag::None) {
        // Get started downloads first - register them with TransferManager
        auto startedDownloads = m_telegramClient->GetStartedDownloads();
        for (const auto& started : startedDownloads) {
            OnDownloadStarted(started.fileId, started.fileName, started.totalSize);
        }
        
        // Get completed downloads
        auto completedDownloads = m_telegramClient->GetCompletedDownloads();
        for (const auto& result : completedDownloads) {
            if (result.success) {
                OnFileDownloaded(result.fileId, result.localPath);
            } else {
                OnDownloadFailed(result.fileId, result.error);
            }
        }
        
        // Get progress updates with actual byte counts
        auto progressUpdates = m_telegramClient->GetDownloadProgressUpdates();
        for (const auto& progress : progressUpdates) {
            // Update transfer manager with actual bytes
            auto it = m_fileToTransferId.find(progress.fileId);
            if (it != m_fileToTransferId.end()) {
                m_transferManager.UpdateProgress(it->second, progress.downloadedSize, progress.totalSize);
            }
        }
    }
    
    // Handle user status updates
    if ((flags & DirtyFlag::UserStatus) != DirtyFlag::None) {
        // For private chats, update the topic bar with current user status
        if (m_currentChatId != 0 && m_chatViewWidget) {
            bool found = false;
            ChatInfo chat = m_telegramClient->GetChat(m_currentChatId, &found);
            if (found && chat.isPrivate && chat.userId > 0) {
                UserInfo user = m_telegramClient->GetUser(chat.userId, &found);
                if (found) {
                    OnUserStatusChanged(chat.userId, user.isOnline, user.lastSeenTime);
                }
            }
        }
    }
}

void MainFrame::OnStatusTimer(wxTimerEvent& event)
{
    // Update status bar periodically (connection status, session time, etc.)
    if (m_statusBar) {
        m_statusBar->SetOnline(m_telegramClient && m_telegramClient->IsLoggedIn());
        m_statusBar->SetLoggedIn(m_isLoggedIn);
        m_statusBar->SetCurrentUser(m_currentUser);
        m_statusBar->SetCurrentChatTitle(m_currentChatTitle);
        m_statusBar->SetCurrentChatId(m_currentChatId);
        m_statusBar->UpdateStatusBar();
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
    // First check our local tracking
    auto it = m_lastReadMessages.find(chatId);
    if (it != m_lastReadMessages.end() && it->second != 0) {
        return it->second;
    }

    // Fallback to TDLib's tracked value from ChatInfo
    if (m_telegramClient) {
        bool found = false;
        ChatInfo chat = m_telegramClient->GetChat(chatId, &found);
        if (found && chat.lastReadInboxMessageId != 0) {
            return chat.lastReadInboxMessageId;
        }
    }

    return 0;
}
