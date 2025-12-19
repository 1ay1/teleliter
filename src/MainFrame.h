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

#include "MediaPopup.h"
#include "FileDropTarget.h"
#include "TransferManager.h"

// Menu IDs - Telegram client menu structure
enum {
    // Teleliter menu
    ID_LOGIN = wxID_HIGHEST + 1,
    ID_LOGOUT,
    ID_RAW_LOG,
    
    // Telegram menu
    ID_NEW_CHAT,
    ID_NEW_GROUP,
    ID_NEW_CHANNEL,
    ID_CONTACTS,
    ID_SEARCH,
    ID_SAVED_MESSAGES,
    ID_UPLOAD_FILE,
    
    // Edit menu
    ID_CLEAR_WINDOW,
    ID_PREFERENCES,
    
    // View menu
    ID_SHOW_CHAT_LIST,
    ID_SHOW_MEMBERS,
    ID_SHOW_CHAT_INFO,
    ID_FULLSCREEN,
    
    // Widget IDs
    ID_CHAT_TREE,
    ID_MEMBER_LIST,
    ID_CHAT_DISPLAY,
    ID_INPUT_BOX,
    ID_CHAT_INFO_BAR
};

// Telegram chat types
enum class TelegramChatType {
    Private,
    Group,
    Supergroup,
    Channel,
    Bot,
    SavedMessages
};

// Simple struct for chat info (will be replaced by TDLib types later)
struct TelegramChat {
    int64_t id;
    wxString title;
    TelegramChatType type;
    wxString lastMessage;
    int unreadCount;
    bool isMuted;
    bool isPinned;
};

// Simple struct for user info
struct TelegramUser {
    int64_t id;
    wxString firstName;
    wxString lastName;
    wxString username;
    wxString status;  // online, last seen, etc.
    bool isAdmin;
    bool isOwner;
    bool isBot;
};

class MainFrame : public wxFrame
{
public:
    MainFrame(const wxString& title, const wxPoint& pos, const wxSize& size);
    
    // Called when files are dropped onto the chat
    void OnFilesDropped(const wxArrayString& files);
    
private:
    // UI Setup methods
    void CreateMenuBar();
    void CreateMainLayout();
    void CreateChatList(wxWindow* parent);
    void CreateChatPanel(wxWindow* parent);
    void CreateMemberList(wxWindow* parent);
    void CreateStatusBar();
    void UpdateTransferProgress(const TransferInfo& info);
    void OnTransferComplete(const TransferInfo& info);
    void OnTransferError(const TransferInfo& info);
    void SetupColors();
    void SetupFonts();
    void PopulateDummyData();
    
    // Message formatting (HexChat style for Telegram)
    void AppendMessage(const wxString& timestamp, const wxString& sender, 
                       const wxString& message);
    void AppendServiceMessage(const wxString& timestamp, const wxString& message);
    void AppendJoinMessage(const wxString& timestamp, const wxString& user);
    void AppendLeaveMessage(const wxString& timestamp, const wxString& user);
    
    // Media message methods - returns the MediaSpan for hover tracking
    void AppendMediaMessage(const wxString& timestamp, const wxString& sender,
                            const MediaInfo& media, const wxString& caption = "");
    void AppendReplyMessage(const wxString& timestamp, const wxString& sender,
                            const wxString& replyTo, const wxString& message);
    void AppendForwardMessage(const wxString& timestamp, const wxString& sender,
                              const wxString& forwardFrom, const wxString& message);
    void AppendEditedMessage(const wxString& timestamp, const wxString& sender,
                             const wxString& message);
    
    wxColour GetUserColor(const wxString& username);
    
    // Media span tracking for hover detection
    void AddMediaSpan(long startPos, long endPos, const MediaInfo& info);
    MediaSpan* GetMediaSpanAtPosition(long pos);
    void ClearMediaSpans();
    
    // Open media file or URL
    void OpenMedia(const MediaInfo& info);
    
    // Event handlers - Menu
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
    
    // Event handlers - UI interaction
    void OnChatTreeSelectionChanged(wxTreeEvent& event);
    void OnChatTreeItemActivated(wxTreeEvent& event);
    void OnMemberListItemActivated(wxListEvent& event);
    void OnMemberListRightClick(wxListEvent& event);
    void OnInputEnter(wxCommandEvent& event);
    void OnInputKeyDown(wxKeyEvent& event);
    
    // Chat display mouse events for hover detection
    void OnChatDisplayMouseMove(wxMouseEvent& event);
    void OnChatDisplayMouseLeave(wxMouseEvent& event);
    void OnChatDisplayLeftDown(wxMouseEvent& event);
    
    // Clipboard paste handler
    void HandleClipboardPaste();
    
    // Main splitter windows
    wxSplitterWindow* m_mainSplitter;      // Splits chat list from rest
    wxSplitterWindow* m_rightSplitter;     // Splits chat area from member list
    
    // Left panel - Chat list (Telegram chats organized by category)
    wxPanel* m_leftPanel;
    wxTreeCtrl* m_chatTree;
    wxTreeItemId m_treeRoot;
    wxTreeItemId m_pinnedChats;
    wxTreeItemId m_privateChats;
    wxTreeItemId m_groups;
    wxTreeItemId m_channels;
    wxTreeItemId m_bots;
    
    // Center panel - Chat area
    wxPanel* m_chatPanel;
    wxTextCtrl* m_chatInfoBar;       // Shows chat name/description
    wxRichTextCtrl* m_chatDisplay;
    wxTextCtrl* m_inputBox;
    
    // Right panel - Member list (for groups/channels)
    wxPanel* m_rightPanel;
    wxListCtrl* m_memberList;
    wxStaticText* m_memberCountLabel;
    
    // Media popup for hover previews
    MediaPopup* m_mediaPopup;
    std::vector<MediaSpan> m_mediaSpans;
    
    // Transfer progress
    TransferManager m_transferManager;
    wxGauge* m_progressGauge;
    wxStaticText* m_progressLabel;
    
    // HexChat-style colors
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
    
    // Message colors
    wxColour m_timestampColor;
    wxColour m_textColor;
    wxColour m_serviceColor;
    wxColour m_highlightColor;
    wxColour m_linkColor;
    wxColour m_mediaColor;
    wxColour m_editedColor;
    wxColour m_forwardColor;
    wxColour m_replyColor;
    
    // User colors (for sender names)
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
    wxString m_currentUser;         // Logged in user
    int64_t m_currentChatId;
    wxString m_currentChatTitle;
    TelegramChatType m_currentChatType;
    
    wxDECLARE_EVENT_TABLE();
};

#endif // MAINFRAME_H