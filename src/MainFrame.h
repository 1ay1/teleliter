#ifndef MAINFRAME_H
#define MAINFRAME_H

#include <wx/wx.h>
#include <wx/splitter.h>
#include <wx/treectrl.h>
#include <wx/listctrl.h>
#include <wx/richtext/richtextctrl.h>

// Menu IDs
enum {
    ID_CONNECT = wxID_HIGHEST + 1,
    ID_DISCONNECT,
    ID_NEW_CHAT,
    ID_JOIN_GROUP,
    ID_JOIN_CHANNEL,
    ID_NETWORK_LIST,
    ID_RAW_LOG,
    ID_CLEAR_WINDOW,
    ID_HIDE_JOIN_PART,
    ID_SHOW_USERLIST,
    ID_SHOW_CHANNEL_TREE,
    ID_FULLSCREEN,
    ID_PREFERENCES,
    ID_CHAT_TREE,
    ID_USER_LIST,
    ID_CHAT_DISPLAY,
    ID_INPUT_BOX,
    ID_TOPIC_BAR
};

class MainFrame : public wxFrame
{
public:
    MainFrame(const wxString& title, const wxPoint& pos, const wxSize& size);
    
private:
    // UI Setup methods
    void CreateMenuBar();
    void CreateMainLayout();
    void CreateChatTree(wxWindow* parent);
    void CreateChatPanel(wxWindow* parent);
    void CreateUserList(wxWindow* parent);
    void CreateInputArea(wxWindow* parent);
    void CreateStatusBar();
    void SetupColors();
    void PopulateDummyData();
    void AppendMessage(const wxString& timestamp, const wxString& nick, 
                       const wxString& message, bool isAction);
    
    // Event handlers - Menu
    void OnExit(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);
    void OnConnect(wxCommandEvent& event);
    void OnDisconnect(wxCommandEvent& event);
    void OnPreferences(wxCommandEvent& event);
    void OnClearWindow(wxCommandEvent& event);
    void OnToggleUserList(wxCommandEvent& event);
    void OnToggleChannelTree(wxCommandEvent& event);
    void OnFullscreen(wxCommandEvent& event);
    
    // Event handlers - UI interaction
    void OnChatTreeSelectionChanged(wxTreeEvent& event);
    void OnUserListItemActivated(wxListEvent& event);
    void OnInputEnter(wxCommandEvent& event);
    void OnInputKeyDown(wxKeyEvent& event);
    
    // Main splitter windows
    wxSplitterWindow* m_mainSplitter;      // Splits left panel from rest
    wxSplitterWindow* m_rightSplitter;     // Splits chat area from user list
    
    // Left panel - Chat/Channel tree
    wxPanel* m_leftPanel;
    wxTreeCtrl* m_chatTree;
    wxTreeItemId m_treeRoot;
    wxTreeItemId m_savedMessages;
    wxTreeItemId m_privateChats;
    wxTreeItemId m_groups;
    wxTreeItemId m_channels;
    wxTreeItemId m_bots;
    
    // Center panel - Chat area
    wxPanel* m_chatPanel;
    wxTextCtrl* m_topicBar;
    wxRichTextCtrl* m_chatDisplay;
    wxTextCtrl* m_inputBox;
    wxStaticText* m_nickLabel;
    
    // Right panel - User list
    wxPanel* m_rightPanel;
    wxListCtrl* m_userList;
    wxStaticText* m_userCountLabel;
    
    // Colors (HexChat style)
    wxColour m_bgColor;
    wxColour m_fgColor;
    wxColour m_inputBgColor;
    wxColour m_inputFgColor;
    wxColour m_treeItemBgColor;
    wxColour m_treeItemFgColor;
    wxColour m_userListBgColor;
    wxColour m_userListFgColor;
    wxColour m_topicBgColor;
    wxColour m_topicFgColor;
    wxColour m_timestampColor;
    wxColour m_nicknameColor;
    wxColour m_actionColor;
    wxColour m_noticeColor;
    wxColour m_highlightColor;
    
    // State
    bool m_showUserList;
    bool m_showChannelTree;
    wxString m_currentNick;
    
    wxDECLARE_EVENT_TABLE();
};

#endif // MAINFRAME_H