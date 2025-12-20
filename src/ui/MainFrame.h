#ifndef MAINFRAME_H
#define MAINFRAME_H

#include <wx/wx.h>
#include <wx/splitter.h>
#include <wx/treectrl.h>
#include <wx/listctrl.h>
#include <wx/richtext/richtextctrl.h>
#include <wx/fontenum.h>
#include <wx/clipbrd.h>
#include <wx/gauge.h>
#include <wx/timer.h>
#include <wx/stopwatch.h>
#include <vector>
#include <map>
#include <set>

#include "MenuIds.h"
#include "MediaTypes.h"
#include "StatusBarManager.h"
#include "../telegram/TransferManager.h"

// Forward declarations
class WelcomeChat;
class TelegramClient;
class ChatListWidget;
class ChatViewWidget;
class InputBoxWidget;
class MediaPopup;
class MessageFormatter;
struct MessageInfo;
struct ChatInfo;

class MainFrame : public wxFrame
{
public:
    MainFrame(const wxString& title, const wxPoint& pos, const wxSize& size);
    virtual ~MainFrame();
    
    void OnFilesDropped(const wxArrayString& files);
    
    // TelegramClient callbacks
    void OnConnected();
    void OnLoginSuccess(const wxString& userName);
    void OnLoggedOut();
    void RefreshChatList();
    void OnMessagesLoaded(int64_t chatId, const std::vector<MessageInfo>& messages);
    void OnNewMessage(const MessageInfo& message);
    void OnMessageEdited(int64_t chatId, int64_t messageId, const wxString& newText);
    void OnFileDownloaded(int32_t fileId, const wxString& localPath);
    void OnFileProgress(int32_t fileId, int64_t downloadedSize, int64_t totalSize);
    void ShowStatusError(const wxString& error);
    void UpdateMemberList(int64_t chatId);
    
    // Unread message tracking
    void MarkMessageAsRead(int64_t chatId, int64_t messageId);
    void UpdateUnreadIndicator(int64_t chatId, int32_t unreadCount);
    int64_t GetLastReadMessageId(int64_t chatId) const;
    
    TelegramClient* GetTelegramClient() { return m_telegramClient; }
    int64_t GetCurrentChatId() const { return m_currentChatId; }
    wxString GetCurrentUser() const { return m_currentUser; }
    wxString GetCurrentChatTitle() const { return m_currentChatTitle; }
    
    // Widget access
    ChatListWidget* GetChatListWidget() { return m_chatListWidget; }
    ChatViewWidget* GetChatViewWidget() { return m_chatViewWidget; }
    InputBoxWidget* GetInputBoxWidget() { return m_inputBoxWidget; }
    wxListCtrl* GetMemberList() { return m_memberList; }
    
private:
    // UI Setup
    void CreateMenuBar();
    void CreateMainLayout();
    void CreateChatPanel(wxWindow* parent);
    void CreateMemberList(wxWindow* parent);
    void SetupColors();
    void SetupFonts();
    void PopulateDummyData();
    
    // Transfer handling
    void UpdateTransferProgress(const TransferInfo& info);
    void OnTransferComplete(const TransferInfo& info);
    void OnTransferError(const TransferInfo& info);
    
    // Message display
    void DisplayMessage(const MessageInfo& msg);
    wxString FormatTimestamp(int64_t unixTime);
    
    // Menu event handlers
    void OnExit(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);
    void OnLogin(wxCommandEvent& event);
    void OnLogout(wxCommandEvent& event);
    void OnNewChat(wxCommandEvent& event);
    void OnNewGroup(wxCommandEvent& event);
    void OnNewChannel(wxCommandEvent& event);
    void OnContacts(wxCommandEvent& event);
    void OnSearch(wxCommandEvent& event);
    void OnSavedMessages(wxCommandEvent& event);
    void OnUploadFile(wxCommandEvent& event);
    void OnPreferences(wxCommandEvent& event);
    void OnClearWindow(wxCommandEvent& event);
    void OnToggleChatList(wxCommandEvent& event);
    void OnToggleMembers(wxCommandEvent& event);
    void OnToggleChatInfo(wxCommandEvent& event);
    void OnFullscreen(wxCommandEvent& event);
    void OnToggleUnreadFirst(wxCommandEvent& event);
    
    // Timer event handlers
    void OnRefreshTimer(wxTimerEvent& event);
    void OnStatusTimer(wxTimerEvent& event);
    
    // UI event handlers
    void OnChatTreeSelectionChanged(wxTreeEvent& event);
    void OnChatTreeItemActivated(wxTreeEvent& event);
    void OnMemberListItemActivated(wxListEvent& event);
    void OnMemberListRightClick(wxListEvent& event);
    
    // Welcome chat
    void ForwardInputToWelcomeChat(const wxString& input);
    bool IsWelcomeChatActive() const;
    
    // Core components
    TelegramClient* m_telegramClient;
    TransferManager m_transferManager;
    wxTimer* m_refreshTimer;
    wxTimer* m_statusTimer;
    wxStopWatch m_sessionTimer;
    
    // Unread message tracking (chatId -> last read message ID)
    std::map<int64_t, int64_t> m_lastReadMessages;
    std::set<int64_t> m_chatsWithUnread;
    
    // Splitters
    wxSplitterWindow* m_mainSplitter;
    wxSplitterWindow* m_rightSplitter;
    
    // Left panel - Chat list widget
    wxPanel* m_leftPanel;
    ChatListWidget* m_chatListWidget;
    
    // Center panel - Chat area
    wxPanel* m_chatPanel;
    WelcomeChat* m_welcomeChat;
    wxTextCtrl* m_chatInfoBar;
    ChatViewWidget* m_chatViewWidget;
    InputBoxWidget* m_inputBoxWidget;
    
    // Right panel - Member list
    wxPanel* m_rightPanel;
    wxListCtrl* m_memberList;
    wxStaticText* m_memberCountLabel;
    
    // Status bar manager
    StatusBarManager* m_statusBar;
    
    // Colors
    wxColour m_bgColor;
    wxColour m_fgColor;
    wxColour m_inputBgColor;
    wxColour m_inputFgColor;
    wxColour m_treeBgColor;
    wxColour m_treeFgColor;
    wxColour m_treeSelBgColor;
    wxColour m_memberListBgColor;
    wxColour m_memberListFgColor;
    wxColour m_chatInfoBgColor;
    wxColour m_chatInfoFgColor;
    wxColour m_timestampColor;
    wxColour m_textColor;
    wxColour m_serviceColor;
    wxColour m_highlightColor;
    wxColour m_actionColor;
    wxColour m_linkColor;
    wxColour m_mediaColor;
    wxColour m_editedColor;
    wxColour m_forwardColor;
    wxColour m_replyColor;
    wxColour m_noticeColor;
    wxColour m_userColors[16];
    
    // Fonts
    wxFont m_chatFont;
    wxFont m_treeFont;
    wxFont m_memberListFont;
    wxFont m_inputFont;
    
    // State
    bool m_showChatList;
    bool m_showMembers;
    bool m_showChatInfo;
    bool m_showUnreadFirst;
    bool m_isLoggedIn;
    wxString m_currentUser;
    int64_t m_currentChatId;
    wxString m_currentChatTitle;
    TelegramChatType m_currentChatType;
    
    // Timer IDs
    static const int ID_REFRESH_TIMER = wxID_HIGHEST + 200;
    static const int ID_STATUS_TIMER = wxID_HIGHEST + 201;

    wxDECLARE_EVENT_TABLE();
};

#endif // MAINFRAME_H