#ifndef WELCOMECHAT_H
#define WELCOMECHAT_H

#include <wx/wx.h>
#include <wx/richtext/richtextctrl.h>

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
    void ProcessInput(const wxString& input);
    
    // TDLib callbacks (called by TelegramClient when auth state changes)
    void OnAuthStateChanged(int state);
    void OnCodeRequested();
    void On2FARequested();
    void OnLoginSuccess(const wxString& userName, const wxString& phoneNumber);
    void OnLoginError(const wxString& error);
    
    // Get current state
    LoginState GetState() const { return m_state; }
    bool IsLoggedIn() const { return m_state == LoginState::LoggedIn; }
    
    // Get the chat display for external access
    wxRichTextCtrl* GetChatDisplay() { return m_chatDisplay; }
    
private:
    void CreateUI();
    void SetupColors();
    
    // Message display helpers (HexChat style)
    void AppendWelcome();
    void AppendInfo(const wxString& message);
    void AppendError(const wxString& message);
    void AppendSuccess(const wxString& message);
    void AppendPrompt(const wxString& prompt);
    void AppendUserInput(const wxString& input);
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
    
    // UI elements
    wxRichTextCtrl* m_chatDisplay;
    
    // Colors (HexChat style)
    wxColour m_bgColor;
    wxColour m_fgColor;
    wxColour m_timestampColor;
    wxColour m_infoColor;
    wxColour m_errorColor;
    wxColour m_successColor;
    wxColour m_promptColor;
    wxColour m_userInputColor;
    wxColour m_asciiArtColor;
    
    // Font
    wxFont m_chatFont;
    
    // State
    LoginState m_state;
    wxString m_phoneNumber;
    wxString m_enteredCode;
    int m_codeRetries;
    
    static const int MAX_CODE_RETRIES = 3;
};

#endif // WELCOMECHAT_H