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
      m_chatDisplay(nullptr),
      m_state(LoginState::NotStarted),
      m_codeRetries(0)
{
    SetupColors();
    CreateUI();
    AppendWelcome();
}

void WelcomeChat::SetupColors()
{
    // HexChat dark theme
    m_bgColor = wxColour(0x2B, 0x2B, 0x2B);
    m_fgColor = wxColour(0xD3, 0xD7, 0xCF);
    m_timestampColor = wxColour(0x88, 0x88, 0x88);
    m_infoColor = wxColour(0x72, 0x9F, 0xCF);       // Blue
    m_errorColor = wxColour(0xEF, 0x29, 0x29);      // Red
    m_successColor = wxColour(0x8A, 0xE2, 0x34);    // Green
    m_promptColor = wxColour(0xFC, 0xAF, 0x3E);     // Orange
    m_userInputColor = wxColour(0xD3, 0xD7, 0xCF);  // Normal text
    m_asciiArtColor = wxColour(0x72, 0x9F, 0xCF);   // Blue
    
    // Monospace font
    m_chatFont = wxFont(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
}

void WelcomeChat::CreateUI()
{
    SetBackgroundColour(m_bgColor);
    
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    
    // Chat display
    m_chatDisplay = new wxRichTextCtrl(this, wxID_ANY,
        wxEmptyString, wxDefaultPosition, wxDefaultSize,
        wxRE_MULTILINE | wxRE_READONLY | wxBORDER_NONE | wxVSCROLL);
    m_chatDisplay->SetBackgroundColour(m_bgColor);
    m_chatDisplay->SetFont(m_chatFont);
    
    wxRichTextAttr defaultStyle;
    defaultStyle.SetTextColour(m_fgColor);
    defaultStyle.SetBackgroundColour(m_bgColor);
    defaultStyle.SetFont(m_chatFont);
    m_chatDisplay->SetDefaultStyle(defaultStyle);
    m_chatDisplay->SetBasicStyle(defaultStyle);
    
    sizer->Add(m_chatDisplay, 1, wxEXPAND);
    SetSizer(sizer);
    Layout();
    m_chatDisplay->Show();
}

void WelcomeChat::AppendAsciiArt()
{
    m_chatDisplay->BeginTextColour(m_asciiArtColor);
    m_chatDisplay->WriteText("\n");
    m_chatDisplay->WriteText("  _______   _      _ _ _            \n");
    m_chatDisplay->WriteText(" |__   __| | |    | (_) |           \n");
    m_chatDisplay->WriteText("    | | ___| | ___| |_| |_ ___ _ __ \n");
    m_chatDisplay->WriteText("    | |/ _ \\ |/ _ \\ | | __/ _ \\ '__|\n");
    m_chatDisplay->WriteText("    | |  __/ |  __/ | | ||  __/ |   \n");
    m_chatDisplay->WriteText("    |_|\\___|_|\\___|_|_|\\__\\___|_|   \n");
    m_chatDisplay->WriteText("\n");
    m_chatDisplay->EndTextColour();
}

void WelcomeChat::AppendWelcome()
{
    m_chatDisplay->BeginSuppressUndo();
    
    AppendAsciiArt();
    
    wxDateTime now = wxDateTime::Now();
    wxString timestamp = now.Format("%H:%M");
    
    // Welcome messages
    m_chatDisplay->BeginTextColour(m_timestampColor);
    m_chatDisplay->WriteText("[" + timestamp + "] ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_infoColor);
    m_chatDisplay->WriteText("* Welcome to Teleliter - Telegram client with HexChat interface\n");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_timestampColor);
    m_chatDisplay->WriteText("[" + timestamp + "] ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_infoColor);
    m_chatDisplay->WriteText("* Version 0.1.0\n");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_timestampColor);
    m_chatDisplay->WriteText("[" + timestamp + "] ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_fgColor);
    m_chatDisplay->WriteText("*\n");
    m_chatDisplay->EndTextColour();
    
    // Instructions
    m_chatDisplay->BeginTextColour(m_timestampColor);
    m_chatDisplay->WriteText("[" + timestamp + "] ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_fgColor);
    m_chatDisplay->WriteText("* Type ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_promptColor);
    m_chatDisplay->BeginBold();
    m_chatDisplay->WriteText("/login");
    m_chatDisplay->EndBold();
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_fgColor);
    m_chatDisplay->WriteText(" to connect to Telegram\n");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_timestampColor);
    m_chatDisplay->WriteText("[" + timestamp + "] ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_fgColor);
    m_chatDisplay->WriteText("* Type ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_promptColor);
    m_chatDisplay->BeginBold();
    m_chatDisplay->WriteText("/help");
    m_chatDisplay->EndBold();
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_fgColor);
    m_chatDisplay->WriteText(" for available commands\n");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_timestampColor);
    m_chatDisplay->WriteText("[" + timestamp + "] ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_fgColor);
    m_chatDisplay->WriteText("*\n");
    m_chatDisplay->EndTextColour();
    
    // Show connection status
    m_chatDisplay->BeginTextColour(m_timestampColor);
    m_chatDisplay->WriteText("[" + timestamp + "] ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_infoColor);
    m_chatDisplay->WriteText("* Connecting to Telegram servers...\n");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->EndSuppressUndo();
    m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
}

void WelcomeChat::AppendInfo(const wxString& message)
{
    wxDateTime now = wxDateTime::Now();
    wxString timestamp = now.Format("%H:%M");
    
    m_chatDisplay->BeginTextColour(m_timestampColor);
    m_chatDisplay->WriteText("[" + timestamp + "] ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_infoColor);
    m_chatDisplay->WriteText("* " + message + "\n");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
}

void WelcomeChat::AppendError(const wxString& message)
{
    wxDateTime now = wxDateTime::Now();
    wxString timestamp = now.Format("%H:%M");
    
    m_chatDisplay->BeginTextColour(m_timestampColor);
    m_chatDisplay->WriteText("[" + timestamp + "] ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_errorColor);
    m_chatDisplay->WriteText("* Error: " + message + "\n");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
}

void WelcomeChat::AppendSuccess(const wxString& message)
{
    wxDateTime now = wxDateTime::Now();
    wxString timestamp = now.Format("%H:%M");
    
    m_chatDisplay->BeginTextColour(m_timestampColor);
    m_chatDisplay->WriteText("[" + timestamp + "] ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_successColor);
    m_chatDisplay->WriteText("* " + message + "\n");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
}

void WelcomeChat::AppendPrompt(const wxString& prompt)
{
    wxDateTime now = wxDateTime::Now();
    wxString timestamp = now.Format("%H:%M");
    
    m_chatDisplay->BeginTextColour(m_timestampColor);
    m_chatDisplay->WriteText("[" + timestamp + "] ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_promptColor);
    m_chatDisplay->WriteText(">> " + prompt + "\n");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
}

void WelcomeChat::AppendUserInput(const wxString& input)
{
    wxDateTime now = wxDateTime::Now();
    wxString timestamp = now.Format("%H:%M");
    
    m_chatDisplay->BeginTextColour(m_timestampColor);
    m_chatDisplay->WriteText("[" + timestamp + "] ");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->BeginTextColour(m_userInputColor);
    m_chatDisplay->WriteText("> " + input + "\n");
    m_chatDisplay->EndTextColour();
    
    m_chatDisplay->ShowPosition(m_chatDisplay->GetLastPosition());
}

void WelcomeChat::StartLogin()
{
    if (m_state == LoginState::LoggedIn) {
        AppendInfo("Already logged in!");
        return;
    }
    
    if (m_state == LoginState::WaitingForPhone || 
        m_state == LoginState::WaitingForCode ||
        m_state == LoginState::WaitingFor2FA) {
        AppendInfo("Login already in progress. Type /cancel to abort.");
        return;
    }
    
    if (!m_telegramClient) {
        AppendError("TelegramClient not initialized!");
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
        AppendSuccess("Already logged in!");
    } else if (authState == AuthState::WaitPhoneNumber) {
        // TDLib is ready and waiting for phone - prompt immediately
        m_state = LoginState::WaitingForPhone;
        m_phoneNumber.Clear();
        m_enteredCode.Clear();
        m_codeRetries = 0;
        AppendPrompt("Enter your phone number (with country code, e.g. +1234567890):");
    } else {
        // TDLib still initializing - set state so we get notified when ready
        m_state = LoginState::LoggingIn;
        AppendInfo("Connecting to Telegram...");
    }
}

void WelcomeChat::CancelLogin()
{
    if (m_state == LoginState::NotStarted || m_state == LoginState::LoggedIn) {
        AppendInfo("No login in progress.");
        return;
    }
    
    m_state = LoginState::NotStarted;
    m_phoneNumber.Clear();
    m_enteredCode.Clear();
    
    AppendInfo("Login cancelled.");
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
            AppendUserInput(trimmed);
            StartLogin();
            return true;
        } else if (command == "/cancel") {
            AppendUserInput(trimmed);
            CancelLogin();
            return true;
        } else if (command == "/help" && m_state == LoginState::NotStarted) {
            // In NotStarted state, show WelcomeChat help but also let regular help through
            AppendUserInput(trimmed);
            AppendInfo("WelcomeChat commands:");
            AppendInfo("  /login  - Start Telegram login");
            AppendInfo("  /cancel - Cancel current login");
            AppendInfo("  /quit   - Exit Teleliter");
            AppendInfo("");
            AppendInfo("Other commands like /me, /clear, /whois work after selecting a chat.");
            return true;
        } else if (command == "/quit" || command == "/exit") {
            AppendUserInput(trimmed);
            AppendInfo("Goodbye!");
            if (m_mainFrame) {
                m_mainFrame->Close();
            }
            return true;
        } else if (m_state == LoginState::NotStarted || m_state == LoginState::LoggedIn) {
            // Not in a login flow - pass command to regular handler
            return false;
        } else {
            // In login flow - unknown commands are errors
            AppendUserInput(trimmed);
            AppendError("Unknown command: " + trimmed);
            AppendInfo("Type /help for available commands");
            return true;
        }
    }
    
    // Handle input based on current state
    switch (m_state) {
        case LoginState::WaitingForPhone:
            AppendUserInput(trimmed);
            HandlePhoneInput(trimmed);
            break;
            
        case LoginState::WaitingForCode:
            // Mask the code in display
            AppendUserInput(wxString('*', trimmed.Length()));
            HandleCodeInput(trimmed);
            break;
            
        case LoginState::WaitingFor2FA:
            // Mask the password
            AppendUserInput(wxString('*', trimmed.Length()));
            Handle2FAInput(trimmed);
            break;
            
        case LoginState::NotStarted:
            // Not in login flow - pass to regular handler
            return false;
            
        case LoginState::LoggedIn:
            // Logged in - pass to regular handler
            return false;
            
        case LoginState::LoggingIn:
            AppendUserInput(trimmed);
            AppendInfo("Please wait, logging in...");
            break;
            
        case LoginState::Error:
            AppendUserInput(trimmed);
            AppendInfo("Type /login to try again");
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
        AppendError("Invalid phone number format. Please include country code (e.g. +1234567890)");
        AppendPrompt("Enter your phone number:");
        return;
    }
    
    m_phoneNumber = FormatPhoneNumber(input);
    
    AppendInfo("Phone number: " + m_phoneNumber);
    AppendInfo("Requesting verification code...");
    
    m_state = LoginState::LoggingIn;
    
    // Send phone number to TDLib
    if (m_telegramClient) {
        m_telegramClient->SetPhoneNumber(m_phoneNumber);
    } else {
        AppendError("TelegramClient not available!");
        m_state = LoginState::Error;
    }
}

void WelcomeChat::HandleCodeInput(const wxString& input)
{
    if (!ValidateCode(input)) {
        m_codeRetries++;
        if (m_codeRetries >= MAX_CODE_RETRIES) {
            AppendError("Too many invalid attempts. Login cancelled.");
            m_state = LoginState::Error;
            return;
        }
        AppendError("Invalid code format. Please enter the numeric code.");
        AppendPrompt("Enter verification code:");
        return;
    }
    
    m_enteredCode = input;
    m_state = LoginState::LoggingIn;
    
    AppendInfo("Verifying code...");
    
    // Send code to TDLib
    if (m_telegramClient) {
        m_telegramClient->SetAuthCode(m_enteredCode);
    } else {
        AppendError("TelegramClient not available!");
        m_state = LoginState::Error;
    }
}

void WelcomeChat::Handle2FAInput(const wxString& input)
{
    if (input.IsEmpty()) {
        AppendError("Password cannot be empty");
        AppendPrompt("Enter your 2FA password:");
        return;
    }
    
    m_state = LoginState::LoggingIn;
    
    AppendInfo("Verifying password...");
    
    // Send 2FA password to TDLib
    if (m_telegramClient) {
        m_telegramClient->SetPassword(input);
    } else {
        AppendError("TelegramClient not available!");
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
                AppendSuccess("Connected to Telegram. Type /login to sign in.");
            } else if (m_state == LoginState::LoggingIn) {
                // User already typed /login, prompt for phone
                m_state = LoginState::WaitingForPhone;
                m_phoneNumber.Clear();
                m_enteredCode.Clear();
                m_codeRetries = 0;
                AppendPrompt("Enter your phone number (with country code, e.g. +1234567890):");
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
            AppendInfo("Disconnected from Telegram.");
            break;
            
        default:
            break;
    }
}

void WelcomeChat::OnCodeRequested()
{
    m_state = LoginState::WaitingForCode;
    m_codeRetries = 0;
    
    AppendSuccess("Verification code sent!");
    AppendInfo("Check your Telegram app or SMS for the code.");
    AppendPrompt("Enter verification code:");
}

void WelcomeChat::On2FARequested()
{
    m_state = LoginState::WaitingFor2FA;
    
    AppendInfo("Two-factor authentication is enabled on this account.");
    AppendPrompt("Enter your 2FA password:");
}

void WelcomeChat::OnLoginSuccess(const wxString& userName, const wxString& phoneNumber)
{
    bool wasAutoLogin = (m_state == LoginState::NotStarted);
    m_state = LoginState::LoggedIn;
    
    if (wasAutoLogin) {
        AppendSuccess("Session restored!");
        AppendInfo("Welcome back, " + userName + " (" + phoneNumber + ")");
    } else {
        AppendSuccess("Successfully logged in!");
        AppendInfo("Welcome, " + userName + " (" + phoneNumber + ")");
    }
    AppendInfo("");
    AppendInfo("Your chats will appear in the left panel.");
    AppendInfo("Select a chat to start messaging.");
}

void WelcomeChat::OnLoginError(const wxString& error)
{
    // Don't reset state completely on error - depends on current state
    if (m_state == LoginState::LoggingIn) {
        // Revert to previous input state if we were waiting for verification
        if (!m_enteredCode.IsEmpty()) {
            m_state = LoginState::WaitingForCode;
            m_enteredCode.Clear();
            AppendError(error);
            AppendPrompt("Enter verification code:");
        } else if (!m_phoneNumber.IsEmpty()) {
            m_state = LoginState::WaitingForPhone;
            m_phoneNumber.Clear();
            AppendError(error);
            AppendPrompt("Enter your phone number:");
        } else {
            m_state = LoginState::Error;
            AppendError(error);
            AppendInfo("Type /login to try again");
        }
    } else {
        m_state = LoginState::Error;
        AppendError(error);
        AppendInfo("Type /login to try again");
    }
}