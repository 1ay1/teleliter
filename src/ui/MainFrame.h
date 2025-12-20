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
#include <vector>
#include <map>

#include "MenuIds.h"
#include "MediaTypes.h"
#include "MediaPopup.h"
#include "FileDropTarget.h"
#include "MessageFormatter.h"
#include "../telegram/TransferManager.h"

// Forward declarations
class WelcomeChat;
class TelegramClient;
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
    
    TelegramClient* GetTelegramClient() { return m_telegramClient; }
    int64_t GetCurrentChatId() const { return m_currentChatId; }
    
private:
    // UI Setup
    void CreateMenuBar();
    void CreateMainLayout();
    void CreateChatList(wxWindow* parent);
    void CreateChatPanel(wxWindow* parent);
    void CreateMemberList(wxWindow* parent);
    void SetupStatusBar();
    void SetupColors();
    void SetupFonts();
    void PopulateDummyData();
    
    // Transfer handling
    void UpdateTransferProgress(const TransferInfo& info);
    void OnTransferComplete(const TransferInfo& info);
    void OnTransferError(const TransferInfo& info);
    
    // Message display (delegated to MessageFormatter)
    void DisplayMessage(const MessageInfo& msg);
    wxString FormatTimestamp(int64_t unixTime);
    
    // Media span tracking
    void AddMediaSpan(long startPos, long endPos, const MediaInfo& info);
    MediaSpan* GetMediaSpanAtPosition(long pos);
    void ClearMediaSpans();
    void OpenMedia(const MediaInfo& info);
    
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
    
    // UI event handlers
    void OnChatTreeSelectionChanged(wxTreeEvent& event);
    void OnChatTreeItemActivated(wxTreeEvent& event);
    void OnMemberListItemActivated(wxListEvent& event);
    void OnMemberListRightClick(wxListEvent& event);
    void OnInputEnter(wxCommandEvent& event);
    void OnInputKeyDown(wxKeyEvent& event);
    void OnChatDisplayMouseMove(wxMouseEvent& event);
    void OnChatDisplayMouseLeave(wxMouseEvent& event);
    void OnChatDisplayLeftDown(wxMouseEvent& event);
    void HandleClipboardPaste();
    
    // Welcome chat
    void ForwardInputToWelcomeChat(const wxString& input);
    bool IsWelcomeChatActive() const;
    
    // Core components
    TelegramClient* m_telegramClient;
    MessageFormatter* m_messageFormatter;
    TransferManager m_transferManager;
    
    // Splitters
    wxSplitterWindow* m_mainSplitter;
    wxSplitterWindow* m_rightSplitter;
    
    // Left panel - Chat list
    wxPanel* m_leftPanel;
    wxTreeCtrl* m_chatTree;
    wxTreeItemId m_treeRoot;
    wxTreeItemId m_pinnedChats;
    wxTreeItemId m_privateChats;
    wxTreeItemId m_groups;
    wxTreeItemId m_channels;
    wxTreeItemId m_bots;
    wxTreeItemId m_teleliterItem;
    std::map<wxTreeItemId, int64_t> m_treeItemToChatId;
    std::map<int64_t, wxTreeItemId> m_chatIdToTreeItem;
    
    // Center panel - Chat area
    wxPanel* m_chatPanel;
    WelcomeChat* m_welcomeChat;
    wxTextCtrl* m_chatInfoBar;
    wxRichTextCtrl* m_chatDisplay;
    wxTextCtrl* m_inputBox;
    
    // Right panel - Member list
    wxPanel* m_rightPanel;
    wxListCtrl* m_memberList;
    wxStaticText* m_memberCountLabel;
    
    // Media
    MediaPopup* m_mediaPopup;
    std::vector<MediaSpan> m_mediaSpans;
    std::map<int32_t, MediaInfo> m_pendingDownloads;
    
    // Status bar
    wxGauge* m_progressGauge;
    wxStaticText* m_progressLabel;
    
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
    wxColour m_linkColor;
    wxColour m_mediaColor;
    wxColour m_editedColor;
    wxColour m_forwardColor;
    wxColour m_replyColor;
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
    bool m_isLoggedIn;
    wxString m_currentUser;
    int64_t m_currentChatId;
    wxString m_currentChatTitle;
    TelegramChatType m_currentChatType;

    wxDECLARE_EVENT_TABLE();
};

#endif // MAINFRAME_H