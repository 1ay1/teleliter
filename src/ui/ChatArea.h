#ifndef CHATAREA_H
#define CHATAREA_H

#include <wx/wx.h>
#include <wx/richtext/richtextctrl.h>
#include <wx/settings.h>

// Message delivery/read status for outgoing messages
enum class MessageStatus {
    None,       // Not an outgoing message (or no status to show)
    Sending,    // Message is being sent (...)
    Sent,       // Message sent to server (✓)
    Read        // Message read by recipient (✓✓)
};

// Reusable chat display area with consistent HexChat-style formatting
// Used by WelcomeChat, ChatViewWidget, and any other chat-like views
// This class provides the exact same formatting as WelcomeChat
class ChatArea : public wxPanel
{
public:
    ChatArea(wxWindow* parent, wxWindowID id = wxID_ANY);
    virtual ~ChatArea() = default;

    // Get the underlying rich text control (for advanced operations)
    wxRichTextCtrl* GetDisplay() { return m_chatDisplay; }

    // Clear all content
    void Clear();

    // Scroll control
    void ScrollToBottom();
    void ScrollToBottomIfAtBottom();
    bool IsAtBottom() const;

    // Suppress undo (for initial content)
    void BeginSuppressUndo() { m_chatDisplay->BeginSuppressUndo(); }
    void EndSuppressUndo() { m_chatDisplay->EndSuppressUndo(); }

    // Batch updates (freeze/thaw for performance)
    void BeginBatchUpdate();
    void EndBatchUpdate();

    // Get current timestamp string [HH:MM]
    static wxString GetCurrentTimestamp();

    // ===== Low-level text writing (exactly like WelcomeChat) =====
    
    // Write text with current color
    void WriteText(const wxString& text) { m_chatDisplay->WriteText(text); }
    
    // Color control
    void BeginTextColour(const wxColour& color) { m_chatDisplay->BeginTextColour(color); }
    void EndTextColour() { m_chatDisplay->EndTextColour(); }
    
    // Style control
    void BeginBold() { m_chatDisplay->BeginBold(); }
    void EndBold() { m_chatDisplay->EndBold(); }
    void BeginItalic() { m_chatDisplay->BeginItalic(); }
    void EndItalic() { m_chatDisplay->EndItalic(); }
    void BeginUnderline() { m_chatDisplay->BeginUnderline(); }
    void EndUnderline() { m_chatDisplay->EndUnderline(); }
    
    // Reset all styles to default (prevents style leaking)
    void ResetStyles();

    // ===== High-level message formatting (HexChat style) =====
    
    // Write timestamp prefix: "[HH:MM] " with optional status marker
    void WriteTimestamp();
    void WriteTimestamp(const wxString& timestamp);
    void WriteTimestamp(const wxString& timestamp, MessageStatus status, bool highlight = false);
    
    // Write just the status marker (✓, ✓✓, or ...)
    void WriteStatusMarker(MessageStatus status, bool highlight = false);

    // Info message: [HH:MM] * message (blue)
    void AppendInfo(const wxString& message);
    
    // Error message: [HH:MM] * Error: message (red)
    void AppendError(const wxString& message);
    
    // Success message: [HH:MM] * message (green)
    void AppendSuccess(const wxString& message);
    
    // Prompt message: [HH:MM] >> prompt (orange)
    void AppendPrompt(const wxString& prompt);
    
    // User input echo: [HH:MM] > input (normal text)
    void AppendUserInput(const wxString& input);
    
    // Service/notice message: [HH:MM] * message (gray)
    void AppendService(const wxString& message);

    // Chat message: [HH:MM] <sender> message
    void AppendMessage(const wxString& sender, const wxString& message);
    void AppendMessage(const wxString& timestamp, const wxString& sender, const wxString& message);

    // Action message (/me): [HH:MM] * sender action (orange)
    void AppendAction(const wxString& sender, const wxString& action);
    void AppendAction(const wxString& timestamp, const wxString& sender, const wxString& action);

    // Join message: [HH:MM] --> user has joined
    void AppendJoin(const wxString& user);
    void AppendJoin(const wxString& timestamp, const wxString& user);

    // Leave message: [HH:MM] <-- user has left
    void AppendLeave(const wxString& user);
    void AppendLeave(const wxString& timestamp, const wxString& user);

    // ===== Color accessors - return system colors on-demand =====
    wxColour GetBgColor() const { return wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW); }
    wxColour GetFgColor() const { return wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT); }
    wxColour GetTimestampColor() const { return wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT); }
    wxColour GetInfoColor() const { return wxSystemSettings::GetColour(wxSYS_COLOUR_HOTLIGHT); }
    const wxColour& GetErrorColor() const { return m_errorColor; }
    const wxColour& GetSuccessColor() const { return m_successColor; }
    wxColour GetPromptColor() const { return wxSystemSettings::GetColour(wxSYS_COLOUR_HOTLIGHT); }
    wxColour GetServiceColor() const { return wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT); }
    wxColour GetActionColor() const { return wxSystemSettings::GetColour(wxSYS_COLOUR_HOTLIGHT); }
    wxColour GetLinkColor() const { return wxSystemSettings::GetColour(wxSYS_COLOUR_HOTLIGHT); }
    wxColour GetSelfColor() const { return wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT); }
    
    // Status marker colors
    wxColour GetSentColor() const { return wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT); }
    const wxColour& GetReadColor() const { return m_readColor; }
    const wxColour& GetReadHighlightColor() const { return m_readHighlightColor; }

    // User colors for sender name coloring
    void SetUserColors(const wxColour colors[16]);
    wxColour GetUserColor(const wxString& username) const;
    
    // Set current username (for gray color assignment)
    void SetCurrentUsername(const wxString& username) { m_currentUsername = username; }
    wxString GetCurrentUsername() const { return m_currentUsername; }

    // Get last position (for tracking spans)
    long GetLastPosition() const { return m_chatDisplay->GetLastPosition(); }

    // Cursor control - allows parent widgets to set cursor for clickable elements
    void SetCurrentCursor(wxStockCursor cursor) { m_currentCursor = cursor; }
    wxStockCursor GetCurrentCursor() const { return m_currentCursor; }

protected:
    void SetupColors();
    void CreateUI();
    void OnSetCursor(wxSetCursorEvent& event);

    wxRichTextCtrl* m_chatDisplay;
    wxFont m_chatFont;
    wxString m_currentUsername;  // Current user gets gray color

    // Semantic colors that have no system equivalent
    wxColour m_errorColor;
    wxColour m_successColor;
    wxColour m_readColor;           // Green for read status (✓✓)
    wxColour m_readHighlightColor;  // Brighter green for recently read

    // User colors (16 colors for sender names)
    wxColour m_userColors[16];

    // State
    bool m_wasAtBottom;
    int m_batchDepth;
    
    // Cursor tracking (to override wxRichTextCtrl's I-beam default)
    wxStockCursor m_currentCursor = wxCURSOR_ARROW;
};

#endif // CHATAREA_H