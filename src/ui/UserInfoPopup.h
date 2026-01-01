#ifndef USERINFOPOPUP_H
#define USERINFOPOPUP_H

#include <wx/wx.h>
#include <wx/popupwin.h>
#include <wx/dcbuffer.h>
#include <wx/timer.h>
#include "../telegram/Types.h"
#include <functional>

// Forward declarations
class TelegramClient;

// Timer IDs for UserInfoPopup
static const int USER_POPUP_HOVER_TIMER_ID = 10100;
static const int USER_POPUP_HIDE_TIMER_ID = 10101;

// HexChat-style popup for displaying user information on hover
class UserInfoPopup : public wxPopupWindow
{
public:
    UserInfoPopup(wxWindow* parent);
    virtual ~UserInfoPopup();

    // Show popup for a user at the given position
    void ShowUser(const UserInfo& user, const wxPoint& pos);
    
    // Update the user info (e.g., when photo downloads)
    void UpdateUser(const UserInfo& user);
    
    // Update profile photo path when download completes
    void UpdateProfilePhoto(int32_t fileId, const wxString& localPath);
    
    // Get the current user ID being displayed
    int64_t GetCurrentUserId() const { return m_userInfo.id; }
    
    // Check if popup is showing a specific user
    bool IsShowingUser(int64_t userId) const { return IsShown() && m_userInfo.id == userId; }
    
    // Set callback for requesting profile photo download
    void SetDownloadCallback(std::function<void(int32_t fileId)> callback) { 
        m_downloadCallback = callback; 
    }
    
    // Set the Telegram client for fetching additional user info
    void SetTelegramClient(TelegramClient* client) { m_telegramClient = client; }

protected:
    void OnPaint(wxPaintEvent& event);
    void OnMouseEnter(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    void OnLeftDown(wxMouseEvent& event);

private:
    void UpdateSize();
    void AdjustPositionToScreen(const wxPoint& pos);
    void ApplyHexChatStyle();
    void DrawProfilePhoto(wxDC& dc, const wxRect& photoRect);
    void DrawUserDetails(wxDC& dc, int x, int y, int width);
    wxString FormatPhoneNumber(const wxString& phone) const;
    
    // Load profile photo from path
    void LoadProfilePhoto(const wxString& path);
    
    // Create a circular mask for the profile photo
    wxBitmap CreateCircularBitmap(const wxBitmap& source, int size);
    
    // Generate initials avatar when no photo is available
    wxBitmap CreateInitialsAvatar(const wxString& name, int size);
    
    // Colors from system theme
    wxColour m_bgColor;
    wxColour m_borderColor;
    wxColour m_textColor;
    wxColour m_labelColor;
    wxColour m_onlineColor;
    wxColour m_verifiedColor;
    wxColour m_botColor;
    
    // User data
    UserInfo m_userInfo;
    wxBitmap m_profilePhoto;
    bool m_hasPhoto;
    bool m_isLoadingPhoto;
    
    // Telegram client for additional operations
    TelegramClient* m_telegramClient;
    
    // Callback for downloading profile photos
    std::function<void(int32_t fileId)> m_downloadCallback;
    
    // Original position for screen bounds adjustment
    wxPoint m_originalPosition;
    
    // Size constants
    static constexpr int POPUP_WIDTH = 280;
    static constexpr int POPUP_MIN_HEIGHT = 100;
    static constexpr int POPUP_MAX_HEIGHT = 300;
    static constexpr int PHOTO_SIZE = 64;
    static constexpr int PADDING = 12;
    static constexpr int BORDER_WIDTH = 1;
    static constexpr int LINE_HEIGHT = 20;
    static constexpr int SMALL_LINE_HEIGHT = 16;
    
    wxDECLARE_EVENT_TABLE();
};

#endif // USERINFOPOPUP_H