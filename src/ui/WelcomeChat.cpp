#include "WelcomeChat.h"
#include "MainFrame.h"
#include "../telegram/TelegramClient.h"

// Debug logging - disabled for release
// #define WCLOG(msg) std::cerr << "[WelcomeChat] " << msg << std::endl
#define WCLOG(msg) do {} while(0)

WelcomeChat::WelcomeChat(wxWindow* parent, MainFrame* mainFrame)
    : wxPanel(parent, wxID_ANY),
      m_mainFrame(mainFrame),
      m_telegramClient(nullptr),
      m_chatArea(nullptr),
      m_state(LoginState::NotStarted),
      m_codeRetries(0)
{
    CreateUI();
    AppendWelcome();
}

void WelcomeChat::CreateUI()
{
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    
    // Use ChatArea for the display
    m_chatArea = new ChatArea(this);
    sizer->Add(m_chatArea, 1, wxEXPAND);
    
    SetSizer(sizer);
    Layout();
}

void WelcomeChat::AppendAsciiArt()
{
    m_chatArea->BeginTextColour(m_chatArea->GetInfoColor());
    m_chatArea->WriteText("\n");
    m_chatArea->WriteText("  _______   _      _ _ _            \n");
    m_chatArea->WriteText(" |__   __| | |    | (_) |           \n");
    m_chatArea->WriteText("    | | ___| | ___| |_| |_ ___ _ __ \n");
    m_chatArea->WriteText("    | |/ _ \\ |/ _ \\ | | __/ _ \\ '__|\n");
    m_chatArea->WriteText("    | |  __/ |  __/ | | ||  __/ |   \n");
    m_chatArea->WriteText("    |_|\\___|_|\\___|_|_|\\__\\___|_|   \n");
    m_chatArea->WriteText("\n");
    m_chatArea->EndTextColour();
}

void WelcomeChat::AppendWelcome()
{
    m_chatArea->BeginSuppressUndo();
    
    AppendAsciiArt();
    
    wxString timestamp = ChatArea::GetCurrentTimestamp();
    
    // Welcome messages
    m_chatArea->WriteTimestamp(timestamp);
    m_chatArea->BeginTextColour(m_chatArea->GetInfoColor());
    m_chatArea->WriteText("* Welcome to Teleliter - Telegram client with HexChat interface\n");
    m_chatArea->EndTextColour();
    
    m_chatArea->WriteTimestamp(timestamp);
    m_chatArea->BeginTextColour(m_chatArea->GetInfoColor());
    m_chatArea->WriteText("* Version 0.1.0\n");
    m_chatArea->EndTextColour();
    
    m_chatArea->WriteTimestamp(timestamp);
    m_chatArea->BeginTextColour(m_chatArea->GetFgColor());
    m_chatArea->WriteText("*\n");
    m_chatArea->EndTextColour();
    
    // Instructions
    m_chatArea->WriteTimestamp(timestamp);
    m_chatArea->BeginTextColour(m_chatArea->GetFgColor());
    m_chatArea->WriteText("* Type ");
    m_chatArea->EndTextColour();
    
    m_chatArea->BeginTextColour(m_chatArea->GetPromptColor());
    m_chatArea->BeginBold();
    m_chatArea->WriteText("/login");
    m_chatArea->EndBold();
    m_chatArea->EndTextColour();
    
    m_chatArea->BeginTextColour(m_chatArea->GetFgColor());
    m_chatArea->WriteText(" to connect to Telegram\n");
    m_chatArea->EndTextColour();
    
    m_chatArea->WriteTimestamp(timestamp);
    m_chatArea->BeginTextColour(m_chatArea->GetFgColor());
    m_chatArea->WriteText("* Type ");
    m_chatArea->EndTextColour();
    
    m_chatArea->BeginTextColour(m_chatArea->GetPromptColor());
    m_chatArea->BeginBold();
    m_chatArea->WriteText("/help");
    m_chatArea->EndBold();
    m_chatArea->EndTextColour();
    
    m_chatArea->BeginTextColour(m_chatArea->GetFgColor());
    m_chatArea->WriteText(" for available commands\n");
    m_chatArea->EndTextColour();
    
    m_chatArea->WriteTimestamp(timestamp);
    m_chatArea->BeginTextColour(m_chatArea->GetFgColor());
    m_chatArea->WriteText("*\n");
    m_chatArea->EndTextColour();
    
    // Show connection status
    m_chatArea->WriteTimestamp(timestamp);
    m_chatArea->BeginTextColour(m_chatArea->GetInfoColor());
    m_chatArea->WriteText("* Connecting to Telegram servers...\n");
    m_chatArea->EndTextColour();
    
    m_chatArea->EndSuppressUndo();
    m_chatArea->ScrollToBottom();
}

void WelcomeChat::StartLogin()
{
    if (m_state == LoginState::LoggedIn) {
        m_chatArea->AppendInfo("Already logged in!");
        return;
    }
    
    if (m_state == LoginState::WaitingForPhone || 
        m_state == LoginState::WaitingForCode ||
        m_state == LoginState::WaitingFor2FA) {
        m_chatArea->AppendInfo("Login already in progress. Type /cancel to abort.");
        return;
    }
    
    if (!m_telegramClient) {
        m_chatArea->AppendError("TelegramClient not initialized!");
        return;
    }
    
    // Start TelegramClient if not running (shouldn't happen as we start on app launch)
    if (!m_telegramClient->IsRunning()) {
        m_telegramClient->Start();
    }
    
    // Check current auth state and respond immediately
    AuthState authState = m_telegramClient->GetAuthState();
    
    if (authState == AuthState::Ready) {
        m_state = LoginState::LoggedIn;
        m_chatArea->AppendSuccess("Already logged in!");
    } else if (authState == AuthState::WaitPhoneNumber) {
        // TDLib is ready and waiting for phone - prompt immediately
        m_state = LoginState::WaitingForPhone;
        m_phoneNumber.Clear();
        m_enteredCode.Clear();
        m_codeRetries = 0;
        m_chatArea->AppendPrompt("Enter your phone number (with country code, e.g. +1234567890):");
    } else {
        // TDLib still initializing - set state so we get notified when ready
        m_state = LoginState::LoggingIn;
        m_chatArea->AppendInfo("Connecting to Telegram...");
    }
}

void WelcomeChat::CancelLogin()
{
    if (m_state == LoginState::NotStarted || m_state == LoginState::LoggedIn) {
        m_chatArea->AppendInfo("No login in progress.");
        return;
    }
    
    m_state = LoginState::NotStarted;
    m_phoneNumber.Clear();
    m_enteredCode.Clear();
    
    m_chatArea->AppendInfo("Login cancelled.");
}

bool WelcomeChat::IsWelcomeChatCommand(const wxString& command) const
{
    wxString cmd = command.Lower();
    return cmd == "/login" || cmd == "/cancel" || cmd == "/quit" || cmd == "/exit";
}

bool WelcomeChat::ProcessInput(const wxString& input)
{
    wxString trimmed = input;
    trimmed.Trim(true).Trim(false);
    
    if (trimmed.IsEmpty()) {
        return true; // Handled (nothing to do)
    }
    
    // Handle commands
    if (trimmed.StartsWith("/")) {
        wxString command = trimmed.BeforeFirst(' ').Lower();
        
        if (command == "/login") {
            m_chatArea->AppendUserInput(trimmed);
            StartLogin();
            return true;
        } else if (command == "/cancel") {
            m_chatArea->AppendUserInput(trimmed);
            CancelLogin();
            return true;
        } else if (command == "/help" && m_state == LoginState::NotStarted) {
            // In NotStarted state, show WelcomeChat help but also let regular help through
            m_chatArea->AppendUserInput(trimmed);
            m_chatArea->AppendInfo("WelcomeChat commands:");
            m_chatArea->AppendInfo("  /login  - Start Telegram login");
            m_chatArea->AppendInfo("  /cancel - Cancel current login");
            m_chatArea->AppendInfo("  /quit   - Exit Teleliter");
            m_chatArea->AppendInfo("");
            m_chatArea->AppendInfo("Other commands like /me, /clear, /whois work after selecting a chat.");
            return true;
        } else if (command == "/quit" || command == "/exit") {
            m_chatArea->AppendUserInput(trimmed);
            m_chatArea->AppendInfo("Goodbye!");
            if (m_mainFrame) {
                m_mainFrame->Close();
            }
            return true;
        } else if (m_state == LoginState::NotStarted || m_state == LoginState::LoggedIn) {
            // Not in a login flow - pass command to regular handler
            return false;
        } else {
            // In login flow - unknown commands are errors
            m_chatArea->AppendUserInput(trimmed);
            m_chatArea->AppendError("Unknown command: " + trimmed);
            m_chatArea->AppendInfo("Type /help for available commands");
            return true;
        }
    }
    
    // Handle input based on current state
    switch (m_state) {
        case LoginState::WaitingForPhone:
            m_chatArea->AppendUserInput(trimmed);
            HandlePhoneInput(trimmed);
            break;
            
        case LoginState::WaitingForCode:
            // Mask the code in display
            m_chatArea->AppendUserInput(wxString('*', trimmed.Length()));
            HandleCodeInput(trimmed);
            break;
            
        case LoginState::WaitingFor2FA:
            // Mask the password
            m_chatArea->AppendUserInput(wxString('*', trimmed.Length()));
            Handle2FAInput(trimmed);
            break;
            
        case LoginState::NotStarted:
            // Not in login flow - pass to regular handler
            return false;
            
        case LoginState::LoggedIn:
            // Logged in - pass to regular handler
            return false;
            
        case LoginState::LoggingIn:
            m_chatArea->AppendUserInput(trimmed);
            m_chatArea->AppendInfo("Please wait, logging in...");
            break;
            
        case LoginState::Error:
            m_chatArea->AppendUserInput(trimmed);
            m_chatArea->AppendInfo("Type /login to try again");
            return true;
    }
    
    return true; // Default: handled
}

bool WelcomeChat::ValidatePhoneNumber(const wxString& phone)
{
    if (phone.Length() < 7 || phone.Length() > 20) {
        return false;
    }
    
    wxString cleaned;
    for (size_t i = 0; i < phone.Length(); i++) {
        wxUniChar c = phone[i];
        if (c == '+' && i == 0) {
            cleaned += c;
        } else if (c >= '0' && c <= '9') {
            cleaned += c;
        } else if (c == ' ' || c == '-' || c == '(' || c == ')') {
            // Skip formatting characters
            continue;
        } else {
            return false;
        }
    }
    
    return cleaned.Length() >= 7;
}

wxString WelcomeChat::FormatPhoneNumber(const wxString& phone)
{
    wxString cleaned;
    for (size_t i = 0; i < phone.Length(); i++) {
        wxUniChar c = phone[i];
        if (c == '+' || (c >= '0' && c <= '9')) {
            cleaned += c;
        }
    }
    return cleaned;
}

bool WelcomeChat::ValidateCode(const wxString& code)
{
    if (code.Length() < 4 || code.Length() > 8) {
        return false;
    }
    
    for (size_t i = 0; i < code.Length(); i++) {
        wxUniChar c = code[i];
        if (c < '0' || c > '9') {
            return false;
        }
    }
    
    return true;
}

void WelcomeChat::HandlePhoneInput(const wxString& input)
{
    if (!ValidatePhoneNumber(input)) {
        m_chatArea->AppendError("Invalid phone number format. Please include country code (e.g. +1234567890)");
        m_chatArea->AppendPrompt("Enter your phone number:");
        return;
    }
    
    m_phoneNumber = FormatPhoneNumber(input);
    
    m_chatArea->AppendInfo("Phone number: " + m_phoneNumber);
    m_chatArea->AppendInfo("Requesting verification code...");
    
    m_state = LoginState::LoggingIn;
    
    // Send phone number to TDLib
    if (m_telegramClient) {
        m_telegramClient->SetPhoneNumber(m_phoneNumber);
    } else {
        m_chatArea->AppendError("TelegramClient not available!");
        m_state = LoginState::Error;
    }
}

void WelcomeChat::HandleCodeInput(const wxString& input)
{
    if (!ValidateCode(input)) {
        m_codeRetries++;
        if (m_codeRetries >= MAX_CODE_RETRIES) {
            m_chatArea->AppendError("Too many invalid attempts. Login cancelled.");
            m_state = LoginState::Error;
            return;
        }
        m_chatArea->AppendError("Invalid code format. Please enter the numeric code.");
        m_chatArea->AppendPrompt("Enter verification code:");
        return;
    }
    
    m_enteredCode = input;
    m_state = LoginState::LoggingIn;
    
    m_chatArea->AppendInfo("Verifying code...");
    
    // Send code to TDLib
    if (m_telegramClient) {
        m_telegramClient->SetAuthCode(m_enteredCode);
    } else {
        m_chatArea->AppendError("TelegramClient not available!");
        m_state = LoginState::Error;
    }
}

void WelcomeChat::Handle2FAInput(const wxString& input)
{
    if (input.IsEmpty()) {
        m_chatArea->AppendError("Password cannot be empty");
        m_chatArea->AppendPrompt("Enter your 2FA password:");
        return;
    }
    
    m_state = LoginState::LoggingIn;
    
    m_chatArea->AppendInfo("Verifying password...");
    
    // Send 2FA password to TDLib
    if (m_telegramClient) {
        m_telegramClient->SetPassword(input);
    } else {
        m_chatArea->AppendError("TelegramClient not available!");
        m_state = LoginState::Error;
    }
}

void WelcomeChat::OnAuthStateChanged(int state)
{
    WCLOG("OnAuthStateChanged called with state=" << state);
    
    AuthState authState = static_cast<AuthState>(state);
    
    switch (authState) {
        case AuthState::WaitPhoneNumber:
            WCLOG("WaitPhoneNumber, current m_state=" << static_cast<int>(m_state));
            // Show ready status
            if (m_state == LoginState::NotStarted) {
                WCLOG("Showing connected message");
                m_chatArea->AppendSuccess("Connected to Telegram. Type /login to sign in.");
            } else if (m_state == LoginState::LoggingIn) {
                // User already typed /login, prompt for phone
                m_state = LoginState::WaitingForPhone;
                m_phoneNumber.Clear();
                m_enteredCode.Clear();
                m_codeRetries = 0;
                m_chatArea->AppendPrompt("Enter your phone number (with country code, e.g. +1234567890):");
            }
            break;
            
        case AuthState::WaitCode:
            // Handled by OnCodeRequested
            break;
            
        case AuthState::WaitPassword:
            // Handled by On2FARequested
            break;
            
        case AuthState::Ready:
            // Handled by OnLoginSuccess
            break;
            
        case AuthState::Closed:
            m_state = LoginState::NotStarted;
            m_chatArea->AppendInfo("Disconnected from Telegram.");
            break;
            
        default:
            break;
    }
}

void WelcomeChat::OnCodeRequested()
{
    m_state = LoginState::WaitingForCode;
    m_codeRetries = 0;
    
    m_chatArea->AppendSuccess("Verification code sent!");
    m_chatArea->AppendInfo("Check your Telegram app or SMS for the code.");
    m_chatArea->AppendPrompt("Enter verification code:");
}

void WelcomeChat::On2FARequested()
{
    m_state = LoginState::WaitingFor2FA;
    
    m_chatArea->AppendInfo("Two-factor authentication is enabled on this account.");
    m_chatArea->AppendPrompt("Enter your 2FA password:");
}

void WelcomeChat::OnLoginSuccess(const wxString& userName, const wxString& phoneNumber)
{
    bool wasAutoLogin = (m_state == LoginState::NotStarted);
    m_state = LoginState::LoggedIn;
    
    if (wasAutoLogin) {
        m_chatArea->AppendSuccess("Session restored!");
        m_chatArea->AppendInfo("Welcome back, " + userName + " (" + phoneNumber + ")");
    } else {
        m_chatArea->AppendSuccess("Successfully logged in!");
        m_chatArea->AppendInfo("Welcome, " + userName + " (" + phoneNumber + ")");
    }
    m_chatArea->AppendInfo("");
    m_chatArea->AppendInfo("Your chats will appear in the left panel.");
    m_chatArea->AppendInfo("Select a chat to start messaging.");
}

void WelcomeChat::OnLoginError(const wxString& error)
{
    // Don't reset state completely on error - depends on current state
    if (m_state == LoginState::LoggingIn) {
        // Revert to previous input state if we were waiting for verification
        if (!m_enteredCode.IsEmpty()) {
            m_state = LoginState::WaitingForCode;
            m_enteredCode.Clear();
            m_chatArea->AppendError(error);
            m_chatArea->AppendPrompt("Enter verification code:");
        } else if (!m_phoneNumber.IsEmpty()) {
            m_state = LoginState::WaitingForPhone;
            m_phoneNumber.Clear();
            m_chatArea->AppendError(error);
            m_chatArea->AppendPrompt("Enter your phone number:");
        } else {
            m_state = LoginState::Error;
            m_chatArea->AppendError(error);
            m_chatArea->AppendInfo("Type /login to try again");
        }
    } else {
        m_state = LoginState::Error;
        m_chatArea->AppendError(error);
        m_chatArea->AppendInfo("Type /login to try again");
    }
}