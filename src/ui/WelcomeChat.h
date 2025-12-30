#ifndef WELCOMECHAT_H
#define WELCOMECHAT_H

#include <wx/wx.h>
#include "ChatArea.h"

// Login state machine
enum class LoginState {
    NotStarted,
    WaitingForPhone,
    WaitingForCode,
    WaitingFor2FA,
    LoggingIn,
    LoggedIn,
    Error
};

// Forward declarations
class MainFrame;
class TelegramClient;

// Welcome chat window - handles login flow like HexChat's network tab
// Uses ChatArea for consistent formatting across the application
class WelcomeChat : public wxPanel
{
public:
    WelcomeChat(wxWindow* parent, MainFrame* mainFrame);
    virtual ~WelcomeChat() = default;
    
    // Set the TelegramClient to use for authentication
    void SetTelegramClient(TelegramClient* client) { m_telegramClient = client; }
    
    // Start the login flow
    void StartLogin();
    
    // Cancel login
    void CancelLogin();
    
    // Process user input from the input box
    // Returns true if the input was handled, false if it should be passed to the regular handler
    bool ProcessInput(const wxString& input);
    
    // Check if a command is a WelcomeChat-specific command
    bool IsWelcomeChatCommand(const wxString& command) const;
    
    // TDLib callbacks (called by TelegramClient when auth state changes)
    void OnAuthStateChanged(int state);
    void OnCodeRequested();
    void On2FARequested();
    void OnLoginSuccess(const wxString& userName, const wxString& phoneNumber);
    void OnLoginError(const wxString& error);
    
    // Get current state
    LoginState GetState() const { return m_state; }
    bool IsLoggedIn() const { return m_state == LoginState::LoggedIn; }
    
    // Get the chat area for external access
    ChatArea* GetChatArea() { return m_chatArea; }
    
    // Get the underlying display (for compatibility)
    wxRichTextCtrl* GetChatDisplay() { return m_chatArea ? m_chatArea->GetDisplay() : nullptr; }
    
    // Refresh display after font change (redraws welcome text with new font)
    void RefreshDisplay();
    
    // Initial display - called by MainFrame after fonts are configured
    void InitialDisplay();
    
private:
    void CreateUI();
    
    // Message display helpers (delegate to ChatArea)
    void AppendWelcome();
    void AppendAsciiArt();
    
    // Input handling for each state
    void HandlePhoneInput(const wxString& input);
    void HandleCodeInput(const wxString& input);
    void Handle2FAInput(const wxString& input);
    
    // Validation
    bool ValidatePhoneNumber(const wxString& phone);
    bool ValidateCode(const wxString& code);
    wxString FormatPhoneNumber(const wxString& phone);
    
    // Parent frame and TelegramClient
    MainFrame* m_mainFrame;
    TelegramClient* m_telegramClient;
    
    // UI elements - uses ChatArea for display
    ChatArea* m_chatArea;
    
    // State
    LoginState m_state;
    wxString m_phoneNumber;
    wxString m_enteredCode;
    int m_codeRetries;
    
    static const int MAX_CODE_RETRIES = 3;
};

#endif // WELCOMECHAT_H