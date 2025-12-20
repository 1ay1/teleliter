#include "MediaPopup.h"

wxBEGIN_EVENT_TABLE(MediaPopup, wxPopupWindow)
    EVT_PAINT(MediaPopup::OnPaint)
wxEND_EVENT_TABLE()

MediaPopup::MediaPopup(wxWindow* parent)
    : wxPopupWindow(parent, wxBORDER_NONE),
      m_hasImage(false),
      m_isLoading(false),
      m_hasError(false)
{
    ApplyHexChatStyle();
    SetSize(MIN_WIDTH, MIN_HEIGHT);
}

MediaPopup::~MediaPopup()
{
}

void MediaPopup::ApplyHexChatStyle()
{
    // HexChat dark theme colors
    m_bgColor = wxColour(0x1A, 0x1A, 0x1A);       // Darker than main bg
    m_borderColor = wxColour(0x55, 0x57, 0x53);   // Gray border
    m_textColor = wxColour(0xD3, 0xD7, 0xCF);     // Light text
    m_labelColor = wxColour(0x88, 0x88, 0x88);    // Dimmer label
    
    SetBackgroundColour(m_bgColor);
}

void MediaPopup::ShowMedia(const MediaInfo& info, const wxPoint& pos)
{
    m_mediaInfo = info;
    m_hasError = false;
    m_errorMessage.Clear();
    
    // If we have a local path, try to load it
    if (!info.localPath.IsEmpty()) {
        SetImage(info.localPath);
    } else {
        // Show loading state while TDLib downloads
        ShowLoading();
    }
    
    UpdateSize();
    SetPosition(pos);
    Show();
}

void MediaPopup::SetImage(const wxImage& image)
{
    if (!image.IsOk()) {
        ShowError("Failed to load image");
        return;
    }
    
    m_isLoading = false;
    m_hasError = false;
    
    // Scale image to fit within max dimensions while preserving aspect ratio
    int imgWidth = image.GetWidth();
    int imgHeight = image.GetHeight();
    
    if (imgWidth > MAX_WIDTH || imgHeight > MAX_HEIGHT) {
        double scaleX = (double)MAX_WIDTH / imgWidth;
        double scaleY = (double)MAX_HEIGHT / imgHeight;
        double scale = std::min(scaleX, scaleY);
        
        imgWidth = (int)(imgWidth * scale);
        imgHeight = (int)(imgHeight * scale);
    }
    
    wxImage scaled = image.Scale(imgWidth, imgHeight, wxIMAGE_QUALITY_HIGH);
    m_bitmap = wxBitmap(scaled);
    m_hasImage = true;
    
    UpdateSize();
    Refresh();
}

void MediaPopup::SetImage(const wxString& path)
{
    wxImage image;
    if (image.LoadFile(path)) {
        SetImage(image);
    } else {
        ShowError("Failed to load: " + path);
    }
}

void MediaPopup::ShowLoading()
{
    m_isLoading = true;
    m_hasImage = false;
    m_hasError = false;
    UpdateSize();
    Refresh();
}

void MediaPopup::ShowError(const wxString& message)
{
    m_hasError = true;
    m_errorMessage = message;
    m_isLoading = false;
    m_hasImage = false;
    UpdateSize();
    Refresh();
}

wxString MediaPopup::GetMediaLabel() const
{
    switch (m_mediaInfo.type) {
        case MediaType::Photo:
            return "Photo";
        case MediaType::Video:
            return "Video";
        case MediaType::Sticker:
            return "Sticker" + (m_mediaInfo.emoji.IsEmpty() ? "" : " " + m_mediaInfo.emoji);
        case MediaType::GIF:
            return "GIF";
        case MediaType::Voice:
            return "Voice Message";
        case MediaType::VideoNote:
            return "Video Message";
        case MediaType::File:
            return "File: " + m_mediaInfo.fileName;
        case MediaType::Reaction:
            return m_mediaInfo.emoji + " from " + m_mediaInfo.reactedBy;
        default:
            return "Media";
    }
}

void MediaPopup::UpdateSize()
{
    int width = MIN_WIDTH;
    int height = MIN_HEIGHT;
    
    if (m_hasImage && m_bitmap.IsOk()) {
        width = m_bitmap.GetWidth() + (PADDING * 2) + (BORDER_WIDTH * 2);
        height = m_bitmap.GetHeight() + (PADDING * 2) + (BORDER_WIDTH * 2) + 20; // +20 for label
    }
    
    // Ensure minimum size
    width = std::max(width, MIN_WIDTH);
    height = std::max(height, MIN_HEIGHT);
    
    SetSize(width, height);
}

void MediaPopup::OnPaint(wxPaintEvent& event)
{
    wxBufferedPaintDC dc(this);
    wxSize size = GetSize();
    
    // Background
    dc.SetBrush(wxBrush(m_bgColor));
    dc.SetPen(wxPen(m_borderColor, BORDER_WIDTH));
    dc.DrawRectangle(0, 0, size.GetWidth(), size.GetHeight());
    
    // Content area
    int contentX = PADDING + BORDER_WIDTH;
    int contentY = PADDING + BORDER_WIDTH;
    int contentWidth = size.GetWidth() - (PADDING * 2) - (BORDER_WIDTH * 2);
    int contentHeight = size.GetHeight() - (PADDING * 2) - (BORDER_WIDTH * 2) - 20;
    
    if (m_isLoading) {
        // Loading state
        dc.SetTextForeground(m_labelColor);
        dc.SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        
        wxString loadingText = "Loading...";
        wxSize textSize = dc.GetTextExtent(loadingText);
        int textX = (size.GetWidth() - textSize.GetWidth()) / 2;
        int textY = (size.GetHeight() - textSize.GetHeight()) / 2;
        dc.DrawText(loadingText, textX, textY);
        
    } else if (m_hasError) {
        // Error state
        dc.SetTextForeground(wxColour(0xCC, 0x00, 0x00)); // Red
        dc.SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        
        wxString errorText = m_errorMessage.IsEmpty() ? "Error loading media" : m_errorMessage;
        wxSize textSize = dc.GetTextExtent(errorText);
        int textX = (size.GetWidth() - textSize.GetWidth()) / 2;
        int textY = (size.GetHeight() - textSize.GetHeight()) / 2;
        dc.DrawText(errorText, textX, textY);
        
    } else if (m_hasImage && m_bitmap.IsOk()) {
        // Draw image centered in content area
        int imgX = contentX + (contentWidth - m_bitmap.GetWidth()) / 2;
        int imgY = contentY;
        dc.DrawBitmap(m_bitmap, imgX, imgY, true);
    }
    
    // Draw label at bottom
    dc.SetTextForeground(m_labelColor);
    dc.SetFont(wxFont(8, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    
    wxString label = GetMediaLabel();
    if (!m_mediaInfo.caption.IsEmpty()) {
        label += " - " + m_mediaInfo.caption;
    }
    
    // Truncate if too long
    int maxLabelWidth = size.GetWidth() - (PADDING * 2);
    wxSize labelSize = dc.GetTextExtent(label);
    if (labelSize.GetWidth() > maxLabelWidth) {
        while (label.Length() > 3 && dc.GetTextExtent(label + "...").GetWidth() > maxLabelWidth) {
            label = label.Left(label.Length() - 1);
        }
        label += "...";
    }
    
    int labelX = PADDING + BORDER_WIDTH;
    int labelY = size.GetHeight() - 20;
    dc.DrawText(label, labelX, labelY);
}