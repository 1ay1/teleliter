#ifndef INPUTBOXWIDGET_H
#define INPUTBOXWIDGET_H

#include <wx/wx.h>
#include <wx/menu.h>
#include <deque>
#include <vector>

// Forward declarations
class MainFrame;
class ChatViewWidget;
class MessageFormatter;
class WelcomeChat;
class wxListCtrl;

class InputBoxWidget : public wxPanel
{
public:
    InputBoxWidget(wxWindow* parent, MainFrame* mainFrame);
    virtual ~InputBoxWidget();
    
    // Input control
    void Clear();
    void SetValue(const wxString& value);
    wxString GetValue() const;
    void SetFocus();
    void SetInsertionPointEnd();
    
    // Configuration
    void SetColors(const wxColour& bg, const wxColour& fg);
    void SetInputFont(const wxFont& font);
    void SetHint(const wxString& hint);
    
    // Chat view connection (for scrolling with PageUp/Down)
    void SetChatView(ChatViewWidget* chatView) { m_chatView = chatView; }
    
    // Member list connection (for tab completion)
    void SetMemberList(wxListCtrl* memberList) { m_memberList = memberList; }
    
    // Message formatter connection (for service messages)
    void SetMessageFormatter(MessageFormatter* formatter) { m_messageFormatter = formatter; }
    
    // WelcomeChat connection (for login input forwarding)
    void SetWelcomeChat(WelcomeChat* welcomeChat) { m_welcomeChat = welcomeChat; }
    
    // Access to underlying text control
    wxTextCtrl* GetTextCtrl() { return m_inputBox; }
    
    // Current user name (for message display)
    void SetCurrentUser(const wxString& user) { m_currentUser = user; }
    wxString GetCurrentUser() const { return m_currentUser; }
    
    // History management
    void AddToHistory(const wxString& text);
    void ClearHistory();
    size_t GetHistorySize() const { return m_inputHistory.size(); }
    
    // Tab completion
    void ResetTabCompletion();
    
    // Enable/disable upload buttons (e.g., when not logged in or no chat selected)
    void EnableUploadButtons(bool enable);
    
private:
    void CreateLayout();
    void CreateButtons();
    
    // Event handlers
    void OnTextEnter(wxCommandEvent& event);
    void OnKeyDown(wxKeyEvent& event);
    void OnUploadClick(wxCommandEvent& event);
    void OnUploadPhoto(wxCommandEvent& event);
    void OnUploadVideo(wxCommandEvent& event);
    void OnUploadFile(wxCommandEvent& event);
    
    // Command processing
    bool ProcessCommand(const wxString& command);
    void ProcessMeCommand(const wxString& args);
    void ProcessClearCommand();
    void ProcessQueryCommand(const wxString& args);
    void ProcessLeaveCommand();
    void ProcessTopicCommand(const wxString& args);
    void ProcessWhoisCommand(const wxString& args);
    void ProcessAwayCommand(const wxString& args);
    void ProcessBackCommand();
    void ProcessHelpCommand();
    
    // History navigation
    void NavigateHistoryUp();
    void NavigateHistoryDown();
    
    // Tab completion
    void DoTabCompletion();
    wxArrayString GetMatchingMembers(const wxString& prefix);
    
    // Clipboard handling
    void HandleClipboardPaste();
    
    // Helper
    wxString GetCurrentTimestamp() const;
    
    MainFrame* m_mainFrame;
    ChatViewWidget* m_chatView;
    wxListCtrl* m_memberList;
    MessageFormatter* m_messageFormatter;
    WelcomeChat* m_welcomeChat;
    wxTextCtrl* m_inputBox;
    
    // Upload button
    wxButton* m_uploadBtn;
    
    // Current user
    wxString m_currentUser;
    
    // Input history (HexChat-style command recall)
    std::deque<wxString> m_inputHistory;
    size_t m_historyIndex;
    static const size_t MAX_HISTORY_SIZE = 100;
    
    // Tab completion state
    wxString m_tabCompletionPrefix;
    size_t m_tabCompletionIndex;
    bool m_tabCompletionActive;
    
    // Colors
    wxColour m_bgColor;
    wxColour m_fgColor;
    wxFont m_font;
    
    // Button and menu IDs
    static const int ID_UPLOAD_BTN = wxID_HIGHEST + 100;
    static const int ID_UPLOAD_PHOTO = wxID_HIGHEST + 101;
    static const int ID_UPLOAD_VIDEO = wxID_HIGHEST + 102;
    static const int ID_UPLOAD_FILE = wxID_HIGHEST + 103;
};

#endif // INPUTBOXWIDGET_H