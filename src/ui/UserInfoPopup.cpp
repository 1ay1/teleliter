#include "UserInfoPopup.h"
#include "../telegram/TelegramClient.h"
#include <wx/graphics.h>
#include <wx/rawbmp.h>
#include <wx/tokenzr.h>
#include <wx/display.h>

wxBEGIN_EVENT_TABLE(UserInfoPopup, wxPopupWindow)
    EVT_PAINT(UserInfoPopup::OnPaint)
wxEND_EVENT_TABLE()

UserInfoPopup::UserInfoPopup(wxWindow* parent)
    : wxPopupWindow(parent, wxBORDER_NONE)
    , m_uiFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT))
    , m_hasPhoto(false)
    , m_isLoadingPhoto(false)
    , m_telegramClient(nullptr)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    ApplyHexChatStyle();
    SetSize(POPUP_WIDTH, POPUP_MIN_HEIGHT);
    SetMinSize(wxSize(POPUP_WIDTH, POPUP_MIN_HEIGHT));
    
    Bind(wxEVT_ENTER_WINDOW, &UserInfoPopup::OnMouseEnter, this);
    Bind(wxEVT_LEAVE_WINDOW, &UserInfoPopup::OnMouseLeave, this);
    Bind(wxEVT_LEFT_DOWN, &UserInfoPopup::OnLeftDown, this);
}

UserInfoPopup::~UserInfoPopup()
{
}

void UserInfoPopup::ApplyHexChatStyle()
{
    m_bgColor = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
    // Use window text color for visible border (matching MediaPopup)
    m_borderColor = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    m_textColor = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    m_labelColor = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
    m_onlineColor = wxColour(76, 175, 80);   // Green for online
    m_verifiedColor = wxColour(33, 150, 243); // Blue for verified
    m_botColor = wxColour(156, 39, 176);      // Purple for bots
    
    SetBackgroundColour(m_bgColor);
}

void UserInfoPopup::ShowUser(const UserInfo& user, const wxPoint& pos)
{
    m_userInfo = user;
    m_originalPosition = pos;
    m_hasPhoto = false;
    m_isLoadingPhoto = false;
    m_profilePhoto = wxBitmap();
    
    // Try to load profile photo if available
    if (!user.profilePhotoSmallPath.IsEmpty()) {
        LoadProfilePhoto(user.profilePhotoSmallPath);
    } else if (user.profilePhotoSmallFileId != 0 && m_downloadCallback) {
        // Request download of profile photo
        m_isLoadingPhoto = true;
        m_downloadCallback(user.profilePhotoSmallFileId);
    }
    
    // If no photo, create initials avatar
    if (!m_hasPhoto && !m_isLoadingPhoto) {
        m_profilePhoto = CreateInitialsAvatar(user.GetDisplayName(), PHOTO_SIZE);
        m_hasPhoto = true;
    }
    
    UpdateSize();
    AdjustPositionToScreen(pos);
    Show(true);
    Refresh();
}

void UserInfoPopup::UpdateUser(const UserInfo& user)
{
    if (user.id != m_userInfo.id) return;
    
    m_userInfo = user;
    
    // Update photo if now available
    if (!user.profilePhotoSmallPath.IsEmpty() && !m_hasPhoto) {
        LoadProfilePhoto(user.profilePhotoSmallPath);
    }
    
    Refresh();
}

void UserInfoPopup::UpdateProfilePhoto(int32_t fileId, const wxString& localPath)
{
    if (m_userInfo.profilePhotoSmallFileId == fileId && !localPath.IsEmpty()) {
        m_userInfo.profilePhotoSmallPath = localPath;
        LoadProfilePhoto(localPath);
        m_isLoadingPhoto = false;
        Refresh();
    }
}

void UserInfoPopup::LoadProfilePhoto(const wxString& path)
{
    if (path.IsEmpty() || !wxFileExists(path)) {
        return;
    }
    
    wxImage image;
    if (image.LoadFile(path)) {
        // Scale to fit and make circular
        int size = PHOTO_SIZE;
        if (image.GetWidth() != size || image.GetHeight() != size) {
            // Scale maintaining aspect ratio
            int origW = image.GetWidth();
            int origH = image.GetHeight();
            int newW, newH;
            
            if (origW > origH) {
                newH = size;
                newW = (origW * size) / origH;
            } else {
                newW = size;
                newH = (origH * size) / origW;
            }
            
            image.Rescale(newW, newH, wxIMAGE_QUALITY_HIGH);
            
            // Crop to center
            int cropX = (newW - size) / 2;
            int cropY = (newH - size) / 2;
            image = image.GetSubImage(wxRect(cropX, cropY, size, size));
        }
        
        m_profilePhoto = CreateCircularBitmap(wxBitmap(image), size);
        m_hasPhoto = true;
    }
}

wxBitmap UserInfoPopup::CreateCircularBitmap(const wxBitmap& source, int size)
{
    if (!source.IsOk()) {
        return source;
    }
    
    wxImage img = source.ConvertToImage();
    if (!img.HasAlpha()) {
        img.InitAlpha();
    }
    
    // Create circular mask
    int centerX = size / 2;
    int centerY = size / 2;
    int radius = size / 2;
    
    unsigned char* alpha = img.GetAlpha();
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int dx = x - centerX;
            int dy = y - centerY;
            double dist = sqrt(dx * dx + dy * dy);
            
            if (dist > radius) {
                alpha[y * size + x] = 0;
            } else if (dist > radius - 1.5) {
                // Anti-alias the edge
                alpha[y * size + x] = (unsigned char)(255 * (radius - dist) / 1.5);
            }
        }
    }
    
    return wxBitmap(img);
}

wxBitmap UserInfoPopup::CreateInitialsAvatar(const wxString& name, int size)
{
    wxBitmap bmp(size, size, 32);
    wxMemoryDC dc(bmp);
    
    // Generate a color based on the name hash
    unsigned long hash = 0;
    for (size_t i = 0; i < name.length(); i++) {
        hash = hash * 31 + name[i].GetValue();
    }
    
    // Use a set of pleasant colors
    wxColour colors[] = {
        wxColour(229, 115, 115),  // Red
        wxColour(186, 104, 200),  // Purple
        wxColour(121, 134, 203),  // Indigo
        wxColour(79, 195, 247),   // Light Blue
        wxColour(77, 208, 225),   // Cyan
        wxColour(129, 199, 132),  // Green
        wxColour(255, 213, 79),   // Yellow
        wxColour(255, 138, 101),  // Orange
    };
    wxColour bgColor = colors[hash % 8];
    
    // Draw circular background
    dc.SetBackground(wxBrush(m_bgColor));
    dc.Clear();
    
    dc.SetBrush(wxBrush(bgColor));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawCircle(size / 2, size / 2, size / 2);
    
    // Get initials
    wxString initials;
    wxStringTokenizer tokenizer(name, " ");
    while (tokenizer.HasMoreTokens() && initials.length() < 2) {
        wxString token = tokenizer.GetNextToken();
        if (!token.IsEmpty()) {
            wxChar c = token[0];
            // Skip @ symbol for usernames
            if (c == '@' && token.length() > 1) {
                c = token[1];
            }
            if (wxIsalnum(c)) {
                initials += wxToupper(c);
            }
        }
    }
    
    if (initials.IsEmpty() && !name.IsEmpty()) {
        initials = wxToupper(name[0]);
    }
    
    // Draw initials
    dc.SetTextForeground(*wxWHITE);
    wxFont font = m_uiFont.IsOk() ? m_uiFont : wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    font.SetPointSize(size / 3);
    font.SetWeight(wxFONTWEIGHT_BOLD);
    dc.SetFont(font);
    
    wxSize textSize = dc.GetTextExtent(initials);
    int textX = (size - textSize.GetWidth()) / 2;
    int textY = (size - textSize.GetHeight()) / 2;
    dc.DrawText(initials, textX, textY);
    
    dc.SelectObject(wxNullBitmap);
    
    // Convert to circular
    return CreateCircularBitmap(bmp, size);
}

void UserInfoPopup::UpdateSize()
{
    int height = PADDING * 2;
    
    // Photo area (vertical layout with photo on top)
    height += PHOTO_SIZE + PADDING;
    
    // Name line
    height += LINE_HEIGHT;
    
    // Username if present
    if (!m_userInfo.username.IsEmpty()) {
        height += SMALL_LINE_HEIGHT;
    }
    
    // Phone if present
    if (!m_userInfo.phoneNumber.IsEmpty()) {
        height += SMALL_LINE_HEIGHT;
    }
    
    // Bio if present
    if (!m_userInfo.bio.IsEmpty()) {
        // Estimate bio height (may wrap)
        height += LINE_HEIGHT + PADDING;
    }
    
    // Status line (online/last seen)
    height += SMALL_LINE_HEIGHT;
    
    // Bot indicator
    if (m_userInfo.isBot) {
        height += SMALL_LINE_HEIGHT;
    }
    
    height = std::max(height, POPUP_MIN_HEIGHT);
    height = std::min(height, POPUP_MAX_HEIGHT);
    
    SetSize(POPUP_WIDTH, height);
}

void UserInfoPopup::AdjustPositionToScreen(const wxPoint& pos)
{
    wxDisplay display(wxDisplay::GetFromPoint(pos));
    wxRect screenRect = display.GetClientArea();
    
    wxSize size = GetSize();
    wxPoint targetPos = pos;
    
    // Offset slightly from cursor
    targetPos.x += 10;
    targetPos.y += 10;
    
    // Adjust horizontal position if would go off screen
    if (targetPos.x + size.GetWidth() > screenRect.GetRight()) {
        targetPos.x = pos.x - size.GetWidth() - 10;
    }
    if (targetPos.x < screenRect.GetLeft()) {
        targetPos.x = screenRect.GetLeft();
    }
    
    // Adjust vertical position if would go off screen
    if (targetPos.y + size.GetHeight() > screenRect.GetBottom()) {
        targetPos.y = pos.y - size.GetHeight() - 10;
    }
    if (targetPos.y < screenRect.GetTop()) {
        targetPos.y = screenRect.GetTop();
    }
    
    SetPosition(targetPos);
}

void UserInfoPopup::OnPaint(wxPaintEvent& event)
{
    wxAutoBufferedPaintDC dc(this);
    wxSize size = GetClientSize();
    
    // Background and border (matching MediaPopup style)
    dc.SetBrush(wxBrush(m_bgColor));
    dc.SetPen(wxPen(m_borderColor, BORDER_WIDTH));
    dc.DrawRectangle(0, 0, size.GetWidth(), size.GetHeight());
    
    int x = PADDING;
    int y = PADDING;
    
    // Draw profile photo centered at top
    wxRect photoRect((size.GetWidth() - PHOTO_SIZE) / 2, y, PHOTO_SIZE, PHOTO_SIZE);
    DrawProfilePhoto(dc, photoRect);
    y += PHOTO_SIZE + PADDING;
    
    // Draw user details below photo
    DrawUserDetails(dc, x, y, size.GetWidth() - PADDING * 2);
}

void UserInfoPopup::DrawProfilePhoto(wxDC& dc, const wxRect& photoRect)
{
    if (m_hasPhoto && m_profilePhoto.IsOk()) {
        dc.DrawBitmap(m_profilePhoto, photoRect.GetX(), photoRect.GetY(), true);
    } else if (m_isLoadingPhoto) {
        // Draw a placeholder circle with loading indicator
        dc.SetBrush(wxBrush(m_labelColor));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawCircle(photoRect.GetX() + PHOTO_SIZE / 2, 
                     photoRect.GetY() + PHOTO_SIZE / 2, 
                     PHOTO_SIZE / 2);
        
        dc.SetTextForeground(*wxWHITE);
        wxFont font = m_uiFont.IsOk() ? m_uiFont : wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
        font.SetPointSize(10);
        dc.SetFont(font);
        wxString loadingText = "...";
        wxSize textSize = dc.GetTextExtent(loadingText);
        dc.DrawText(loadingText, 
                   photoRect.GetX() + (PHOTO_SIZE - textSize.GetWidth()) / 2,
                   photoRect.GetY() + (PHOTO_SIZE - textSize.GetHeight()) / 2);
    }
    
    // Draw online indicator if online
    if (m_userInfo.IsCurrentlyOnline()) {
        int indicatorSize = 14;
        int indicatorX = photoRect.GetRight() - indicatorSize + 2;
        int indicatorY = photoRect.GetBottom() - indicatorSize + 2;
        
        // White border
        dc.SetBrush(wxBrush(m_bgColor));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawCircle(indicatorX + indicatorSize / 2, indicatorY + indicatorSize / 2, indicatorSize / 2 + 2);
        
        // Green indicator
        dc.SetBrush(wxBrush(m_onlineColor));
        dc.DrawCircle(indicatorX + indicatorSize / 2, indicatorY + indicatorSize / 2, indicatorSize / 2);
    }
}

void UserInfoPopup::DrawUserDetails(wxDC& dc, int x, int y, int width)
{
    // Display name - centered and bold
    wxFont nameFont = m_uiFont.IsOk() ? m_uiFont : wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    nameFont.SetPointSize(12);
    nameFont.SetWeight(wxFONTWEIGHT_BOLD);
    dc.SetFont(nameFont);
    dc.SetTextForeground(m_textColor);
    
    wxString displayName = m_userInfo.GetDisplayName();
    wxSize nameSize = dc.GetTextExtent(displayName);
    int nameX = x + (width - nameSize.GetWidth()) / 2;
    
    // Draw verified badge if applicable
    if (m_userInfo.isVerified) {
        wxString verifiedIcon = wxString::FromUTF8(" \xE2\x9C\x93"); // U+2713 CHECK MARK
        wxSize iconSize = dc.GetTextExtent(verifiedIcon);
        nameX = x + (width - nameSize.GetWidth() - iconSize.GetWidth()) / 2;
        dc.DrawText(displayName, nameX, y);
        
        dc.SetTextForeground(m_verifiedColor);
        dc.DrawText(verifiedIcon, nameX + nameSize.GetWidth(), y);
        dc.SetTextForeground(m_textColor);
    } else {
        dc.DrawText(displayName, nameX, y);
    }
    y += LINE_HEIGHT;
    
    // Username - centered in gray
    if (!m_userInfo.username.IsEmpty()) {
        wxFont smallFont = m_uiFont.IsOk() ? m_uiFont : wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
        smallFont.SetPointSize(10);
        dc.SetFont(smallFont);
        dc.SetTextForeground(m_labelColor);
        
        wxString username = "@" + m_userInfo.username;
        wxSize usernameSize = dc.GetTextExtent(username);
        dc.DrawText(username, x + (width - usernameSize.GetWidth()) / 2, y);
        y += SMALL_LINE_HEIGHT;
    }
    
    // Phone number - centered
    if (!m_userInfo.phoneNumber.IsEmpty()) {
        wxFont smallFont = m_uiFont.IsOk() ? m_uiFont : wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
        smallFont.SetPointSize(10);
        dc.SetFont(smallFont);
        dc.SetTextForeground(m_labelColor);
        
        wxString phone = FormatPhoneNumber(m_userInfo.phoneNumber);
        wxSize phoneSize = dc.GetTextExtent(phone);
        dc.DrawText(phone, x + (width - phoneSize.GetWidth()) / 2, y);
        y += SMALL_LINE_HEIGHT;
    }
    
    // Bio if present
    if (!m_userInfo.bio.IsEmpty()) {
        y += PADDING / 2;
        wxFont bioFont = m_uiFont.IsOk() ? m_uiFont : wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
        bioFont.SetPointSize(9);
        bioFont.SetStyle(wxFONTSTYLE_ITALIC);
        dc.SetFont(bioFont);
        dc.SetTextForeground(m_textColor);
        
        // Truncate bio if too long
        wxString bio = m_userInfo.bio;
        if (bio.length() > 100) {
            bio = bio.Left(97) + "...";
        }
        
        // Simple word wrap - just draw first line
        wxSize bioSize = dc.GetTextExtent(bio);
        if (bioSize.GetWidth() > width) {
            // Find a good break point
            int charWidth = bioSize.GetWidth() / bio.length();
            int maxChars = width / charWidth;
            bio = bio.Left(maxChars - 3) + "...";
        }
        
        wxSize finalBioSize = dc.GetTextExtent(bio);
        dc.DrawText(bio, x + (width - finalBioSize.GetWidth()) / 2, y);
        y += LINE_HEIGHT;
    }
    
    // Status line - centered
    y += PADDING / 2;
    wxFont statusFont = m_uiFont.IsOk() ? m_uiFont : wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    statusFont.SetPointSize(10);
    dc.SetFont(statusFont);
    
    wxString statusText = m_userInfo.GetLastSeenString();
    if (m_userInfo.IsCurrentlyOnline()) {
        dc.SetTextForeground(m_onlineColor);
    } else {
        dc.SetTextForeground(m_labelColor);
    }
    
    wxSize statusSize = dc.GetTextExtent(statusText);
    dc.DrawText(statusText, x + (width - statusSize.GetWidth()) / 2, y);
    y += SMALL_LINE_HEIGHT;
    
    // Bot indicator
    if (m_userInfo.isBot) {
        dc.SetTextForeground(m_botColor);
        wxString botText = "🤖 Bot";
        wxSize botSize = dc.GetTextExtent(botText);
        dc.DrawText(botText, x + (width - botSize.GetWidth()) / 2, y);
    }
}

wxString UserInfoPopup::FormatPhoneNumber(const wxString& phone) const
{
    if (phone.IsEmpty()) return phone;
    
    // If already formatted (has spaces or dashes), return as-is
    if (phone.Contains(" ") || phone.Contains("-")) {
        return "+" + phone;
    }
    
    // Simple formatting: +X XXX XXX XXXX
    wxString formatted = "+";
    for (size_t i = 0; i < phone.length(); i++) {
        formatted += phone[i];
        // Add spaces at common break points
        if (i == 0 || i == 3 || i == 6) {
            if (i < phone.length() - 1) {
                formatted += " ";
            }
        }
    }
    
    return formatted;
}

void UserInfoPopup::OnMouseEnter(wxMouseEvent& event)
{
    // Keep popup visible when mouse enters it
    event.Skip();
}

void UserInfoPopup::OnMouseLeave(wxMouseEvent& event)
{
    // Hide popup when mouse leaves
    Hide();
    event.Skip();
}

void UserInfoPopup::OnLeftDown(wxMouseEvent& event)
{
    // Could open full profile view here in the future
    Hide();
    event.Skip();
}