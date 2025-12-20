#include "MediaPopup.h"
#include "FileUtils.h"
#include <wx/filename.h>
#include <iostream>

// Helper to check if file extension is a supported image format
static bool IsSupportedImageFormat(const wxString& path)
{
    wxFileName fn(path);
    wxString ext = fn.GetExt().Lower();
    
    // Natively supported static image formats
    if (ext == "jpg" || ext == "jpeg" || ext == "png" || 
        ext == "bmp" || ext == "ico" || ext == "tiff" || ext == "tif") {
        return true;
    }
    
    // WebP is supported if libwebp is available
    if (ext == "webp") {
        return HasWebPSupport();
    }
    
    return false;
}

// Helper to check if file extension is a video/animation format
static bool IsVideoFormat(const wxString& path)
{
    wxFileName fn(path);
    wxString ext = fn.GetExt().Lower();
    
    // Video formats that wxMediaCtrl can typically handle
    if (ext == "mp4" || ext == "webm" || ext == "avi" || 
        ext == "mov" || ext == "mkv" || ext == "gif" ||
        ext == "m4v" || ext == "ogv") {
        return true;
    }
    
    return false;
}

wxBEGIN_EVENT_TABLE(MediaPopup, wxPopupWindow)
    EVT_PAINT(MediaPopup::OnPaint)
wxEND_EVENT_TABLE()

MediaPopup::MediaPopup(wxWindow* parent)
    : wxPopupWindow(parent, wxBORDER_NONE),
      m_hasImage(false),
      m_isLoading(false),
      m_hasError(false),
      m_loadingTimer(this, LOADING_TIMER_ID),
      m_loadingFrame(0),
      m_mediaCtrl(nullptr),
      m_isPlayingVideo(false),
      m_loopVideo(false),
      m_videoMuted(true)
{
    ApplyHexChatStyle();
    SetSize(MIN_WIDTH, MIN_HEIGHT);
    
    // Bind loading timer event
    Bind(wxEVT_TIMER, &MediaPopup::OnLoadingTimer, this, LOADING_TIMER_ID);
}

MediaPopup::~MediaPopup()
{
    m_loadingTimer.Stop();
    DestroyMediaCtrl();
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

void MediaPopup::CreateMediaCtrl()
{
    if (m_mediaCtrl) return;
    
    m_mediaCtrl = new wxMediaCtrl();
    // Use default backend - wxMEDIABACKEND_GSTREAMER on Linux
    if (!m_mediaCtrl->Create(this, wxID_ANY, wxEmptyString,
                              wxPoint(PADDING + BORDER_WIDTH, PADDING + BORDER_WIDTH),
                              wxSize(MAX_WIDTH - PADDING * 2, MAX_HEIGHT - PADDING * 2 - 24),
                              wxMC_NO_AUTORESIZE)) {
        delete m_mediaCtrl;
        m_mediaCtrl = nullptr;
        return;
    }
    
    m_mediaCtrl->SetBackgroundColour(m_bgColor);
    
    // Bind media events
    m_mediaCtrl->Bind(wxEVT_MEDIA_LOADED, &MediaPopup::OnMediaLoaded, this);
    m_mediaCtrl->Bind(wxEVT_MEDIA_FINISHED, &MediaPopup::OnMediaFinished, this);
    m_mediaCtrl->Bind(wxEVT_MEDIA_STOP, &MediaPopup::OnMediaStop, this);
}

void MediaPopup::DestroyMediaCtrl()
{
    if (m_mediaCtrl) {
        m_mediaCtrl->Stop();
        m_mediaCtrl->Unbind(wxEVT_MEDIA_LOADED, &MediaPopup::OnMediaLoaded, this);
        m_mediaCtrl->Unbind(wxEVT_MEDIA_FINISHED, &MediaPopup::OnMediaFinished, this);
        m_mediaCtrl->Unbind(wxEVT_MEDIA_STOP, &MediaPopup::OnMediaStop, this);
        m_mediaCtrl->Destroy();
        m_mediaCtrl = nullptr;
    }
    m_isPlayingVideo = false;
}

void MediaPopup::ShowMedia(const MediaInfo& info, const wxPoint& pos)
{
    std::cerr << "[MediaPopup] ShowMedia called: type=" << static_cast<int>(info.type)
              << " fileId=" << info.fileId
              << " localPath=" << info.localPath.ToStdString()
              << " emoji=" << info.emoji.ToStdString() << std::endl;
    
    m_mediaInfo = info;
    m_hasError = false;
    m_errorMessage.Clear();
    m_hasImage = false;
    
    // Stop any playing video first
    StopVideo();
    
    // Check if file needs to be downloaded
    bool needsDownload = info.localPath.IsEmpty() || !wxFileExists(info.localPath);
    if (needsDownload && info.fileId != 0) {
        // Show loading state - file is being downloaded
        m_isLoading = true;
        m_loadingFrame = 0;
        if (!m_loadingTimer.IsRunning()) {
            m_loadingTimer.Start(150);  // Animate every 150ms
        }
        std::cerr << "[MediaPopup] File needs download, showing loading state" << std::endl;
    } else {
        m_isLoading = false;
        m_loadingTimer.Stop();
    }
    
    // If we have a local path, try to load it
    if (!info.localPath.IsEmpty() && wxFileExists(info.localPath)) {
        std::cerr << "[MediaPopup] File exists: " << info.localPath.ToStdString() << std::endl;
        // Check if it's a video/GIF that should be played
        if ((info.type == MediaType::Video || info.type == MediaType::GIF || 
             info.type == MediaType::VideoNote) && IsVideoFormat(info.localPath)) {
            std::cerr << "[MediaPopup] Playing as video" << std::endl;
            // Play video/GIF - GIFs loop, videos don't; GIFs are muted
            bool shouldLoop = (info.type == MediaType::GIF);
            bool shouldMute = (info.type == MediaType::GIF || info.type == MediaType::VideoNote);
            PlayVideo(info.localPath, shouldLoop, shouldMute);
            SetPosition(pos);
            Show();
            return;
        } else if (IsSupportedImageFormat(info.localPath)) {
            std::cerr << "[MediaPopup] Loading as image" << std::endl;
            SetImage(info.localPath);
        } else {
            std::cerr << "[MediaPopup] Unsupported format, showing placeholder" << std::endl;
        }
        // For unsupported formats (.tgs animated stickers, etc.), we'll show emoji placeholder
    } else {
        std::cerr << "[MediaPopup] No local file or file doesn't exist, isDownloading=" << info.isDownloading << std::endl;
        // If downloading, keep showing loading state
        if (info.isDownloading || info.fileId != 0) {
            m_isLoading = true;
            if (!m_loadingTimer.IsRunning()) {
                m_loadingTimer.Start(150);
            }
        }
    }
    // Otherwise we'll show a placeholder in OnPaint
    
    // For stickers, ensure we have the emoji to display
    if (info.type == MediaType::Sticker && !info.emoji.IsEmpty()) {
        // Stickers will show the emoji prominently
        m_hasImage = false;  // Force placeholder display with emoji
    }
    
    std::cerr << "[MediaPopup] hasImage=" << m_hasImage << " showing popup" << std::endl;
    UpdateSize();
    SetPosition(pos);
    Show();
    Refresh();
}

void MediaPopup::PlayVideo(const wxString& path, bool loop, bool muted)
{
    std::cerr << "[MediaPopup] PlayVideo called: " << path.ToStdString() 
              << " loop=" << loop << " muted=" << muted << std::endl;
    
    m_videoPath = path;
    m_loopVideo = loop;
    m_videoMuted = muted;
    m_isPlayingVideo = false;
    m_hasImage = false;
    
    // Create media control if needed
    CreateMediaCtrl();
    
    if (!m_mediaCtrl) {
        // Fallback to placeholder if media control creation failed
        std::cerr << "[MediaPopup] Failed to create media control" << std::endl;
        m_hasError = true;
        m_errorMessage = "Video playback not available";
        UpdateSize();
        Refresh();
        return;
    }
    
    // Set size for video
    int videoWidth = MAX_WIDTH - (PADDING * 2) - (BORDER_WIDTH * 2);
    int videoHeight = MAX_HEIGHT - (PADDING * 2) - (BORDER_WIDTH * 2) - 24;
    
    SetSize(MAX_WIDTH, MAX_HEIGHT);
    m_mediaCtrl->SetSize(PADDING + BORDER_WIDTH, PADDING + BORDER_WIDTH, 
                         videoWidth, videoHeight);
    m_mediaCtrl->Show();
    
    // Load and play the video
    std::cerr << "[MediaPopup] Loading video: " << path.ToStdString() << std::endl;
    if (m_mediaCtrl->Load(path)) {
        // OnMediaLoaded will be called when ready
        std::cerr << "[MediaPopup] Video load initiated, waiting for OnMediaLoaded" << std::endl;
        m_isLoading = true;
    } else {
        std::cerr << "[MediaPopup] Failed to load video" << std::endl;
        m_hasError = true;
        m_errorMessage = "Failed to load video";
        m_mediaCtrl->Hide();
    }
    
    Refresh();
}

void MediaPopup::StopVideo()
{
    if (m_mediaCtrl) {
        m_mediaCtrl->Stop();
        m_mediaCtrl->Hide();
    }
    m_isPlayingVideo = false;
    m_videoPath.Clear();
}

void MediaPopup::OnMediaLoaded(wxMediaEvent& event)
{
    std::cerr << "[MediaPopup] OnMediaLoaded called" << std::endl;
    m_isLoading = false;
    m_isPlayingVideo = true;
    
    if (m_mediaCtrl) {
        // Set volume (0 = muted, 1.0 = full)
        if (m_videoMuted) {
            m_mediaCtrl->SetVolume(0.0);
        } else {
            m_mediaCtrl->SetVolume(0.5);  // 50% volume for previews
        }
        
        // Get video size and resize popup accordingly
        wxSize videoSize = m_mediaCtrl->GetBestSize();
        if (videoSize.GetWidth() > 0 && videoSize.GetHeight() > 0) {
            int vidWidth = videoSize.GetWidth();
            int vidHeight = videoSize.GetHeight();
            
            // Scale to fit within max dimensions
            if (vidWidth > MAX_WIDTH - PADDING * 2 || vidHeight > MAX_HEIGHT - PADDING * 2 - 24) {
                double scaleX = (double)(MAX_WIDTH - PADDING * 2) / vidWidth;
                double scaleY = (double)(MAX_HEIGHT - PADDING * 2 - 24) / vidHeight;
                double scale = std::min(scaleX, scaleY);
                
                vidWidth = (int)(vidWidth * scale);
                vidHeight = (int)(vidHeight * scale);
            }
            
            // Ensure minimum size
            vidWidth = std::max(vidWidth, MIN_WIDTH - PADDING * 2);
            vidHeight = std::max(vidHeight, MIN_HEIGHT - PADDING * 2 - 24);
            
            m_mediaCtrl->SetSize(PADDING + BORDER_WIDTH, PADDING + BORDER_WIDTH,
                                vidWidth, vidHeight);
            SetSize(vidWidth + PADDING * 2 + BORDER_WIDTH * 2,
                   vidHeight + PADDING * 2 + BORDER_WIDTH * 2 + 24);
        }
        
        if (m_mediaCtrl->Play()) {
            std::cerr << "[MediaPopup] Video playback started" << std::endl;
        } else {
            std::cerr << "[MediaPopup] Failed to start video playback" << std::endl;
        }
    }
    
    Refresh();
}

void MediaPopup::OnMediaFinished(wxMediaEvent& event)
{
    std::cerr << "[MediaPopup] OnMediaFinished called, loop=" << m_loopVideo << std::endl;
    if (m_loopVideo && m_mediaCtrl) {
        // Seek to beginning and play again
        m_mediaCtrl->Seek(0);
        m_mediaCtrl->Play();
    }
}

void MediaPopup::OnMediaStop(wxMediaEvent& event)
{
    std::cerr << "[MediaPopup] OnMediaStop called" << std::endl;
    // Video stopped - could be user action or error
}

void MediaPopup::SetImage(const wxImage& image)
{
    if (!image.IsOk()) {
        m_hasImage = false;
        return;
    }
    
    // Stop any video playback
    StopVideo();
    
    m_isLoading = false;
    m_loadingTimer.Stop();
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
    if (LoadImageWithWebPSupport(path, image)) {
        SetImage(image);
    } else {
        m_hasImage = false;
    }
}

void MediaPopup::ShowLoading()
{
    StopVideo();
    m_isLoading = true;
    m_loadingFrame = 0;
    if (!m_loadingTimer.IsRunning()) {
        m_loadingTimer.Start(150);
    }
    m_hasImage = false;
    m_hasError = false;
    UpdateSize();
    Refresh();
}

void MediaPopup::OnLoadingTimer(wxTimerEvent& event)
{
    m_loadingFrame = (m_loadingFrame + 1) % 8;
    if (IsShown() && m_isLoading) {
        Refresh();
    } else {
        m_loadingTimer.Stop();
    }
}

void MediaPopup::ShowError(const wxString& message)
{
    StopVideo();
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

wxString MediaPopup::GetMediaIcon() const
{
    switch (m_mediaInfo.type) {
        case MediaType::Photo:
            return wxString::FromUTF8("\xF0\x9F\x96\xBC\xEF\xB8\x8F");  // 🖼️
        case MediaType::Video:
            return wxString::FromUTF8("\xF0\x9F\x8E\xAC");  // 🎬
        case MediaType::Sticker:
            return m_mediaInfo.emoji.IsEmpty() ? wxString::FromUTF8("\xF0\x9F\x8E\xAD") : m_mediaInfo.emoji;  // 🎭
        case MediaType::GIF:
            return wxString::FromUTF8("\xF0\x9F\x8E\x9E\xEF\xB8\x8F");  // 🎞️
        case MediaType::Voice:
            return wxString::FromUTF8("\xF0\x9F\x8E\xA4");  // 🎤
        case MediaType::VideoNote:
            return wxString::FromUTF8("\xF0\x9F\x93\xB9");  // 📹
        case MediaType::File:
            return wxString::FromUTF8("\xF0\x9F\x93\x84");  // 📄
        case MediaType::Reaction:
            return m_mediaInfo.emoji;
        default:
            return wxString::FromUTF8("\xF0\x9F\x93\x8E");  // 📎
    }
}

void MediaPopup::UpdateSize()
{
    int width = MIN_WIDTH;
    int height = MIN_HEIGHT;
    
    if (m_isPlayingVideo && m_mediaCtrl && m_mediaCtrl->IsShown()) {
        // Size already set by video playback
        return;
    }
    
    if (m_hasImage && m_bitmap.IsOk()) {
        width = m_bitmap.GetWidth() + (PADDING * 2) + (BORDER_WIDTH * 2);
        height = m_bitmap.GetHeight() + (PADDING * 2) + (BORDER_WIDTH * 2) + 24; // +24 for label
    } else if (m_mediaInfo.type == MediaType::Sticker && !m_mediaInfo.emoji.IsEmpty()) {
        // Larger size for sticker emoji display
        width = 200;
        height = 150;
    } else {
        // Placeholder size - make it a nice square for the icon
        width = 180;
        height = 120;
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
    
    // If video is playing, the media control handles the display
    if (m_isPlayingVideo && m_mediaCtrl && m_mediaCtrl->IsShown()) {
        // Just draw the label at the bottom
        wxString label = GetMediaLabel();
        dc.SetTextForeground(m_labelColor);
        dc.SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        
        wxSize labelSize = dc.GetTextExtent(label);
        int labelX = (size.GetWidth() - labelSize.GetWidth()) / 2;
        int labelY = size.GetHeight() - 18;
        dc.DrawText(label, labelX, labelY);
        return;
    }
    
    // Content area
    int contentX = PADDING + BORDER_WIDTH;
    int contentY = PADDING + BORDER_WIDTH;
    int contentWidth = size.GetWidth() - (PADDING * 2) - (BORDER_WIDTH * 2);
    
    if (m_hasImage && m_bitmap.IsOk()) {
        // Draw image centered in content area
        int imgX = contentX + (contentWidth - m_bitmap.GetWidth()) / 2;
        int imgY = contentY;
        dc.DrawBitmap(m_bitmap, imgX, imgY, true);
    } else if (m_hasError) {
        // Error state
        dc.SetTextForeground(wxColour(0xCC, 0x00, 0x00)); // Red
        dc.SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        
        wxString errorText = m_errorMessage.IsEmpty() ? "Error loading media" : m_errorMessage;
        wxSize textSize = dc.GetTextExtent(errorText);
        int textX = (size.GetWidth() - textSize.GetWidth()) / 2;
        int textY = (size.GetHeight() - textSize.GetHeight()) / 2;
        dc.DrawText(errorText, textX, textY);
    } else if (m_isLoading) {
        // Loading state - show animated spinner with "Downloading..."
        static const wxString spinners[] = {"◐", "◓", "◑", "◒", "◐", "◓", "◑", "◒"};
        wxString spinner = spinners[m_loadingFrame % 8];
        
        dc.SetFont(wxFont(32, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        dc.SetTextForeground(wxColour(0x72, 0x9F, 0xCF));  // Blue for downloading
        
        wxSize spinnerSize = dc.GetTextExtent(spinner);
        int spinnerX = (size.GetWidth() - spinnerSize.GetWidth()) / 2;
        int spinnerY = contentY + 5;
        dc.DrawText(spinner, spinnerX, spinnerY);
        
        // Draw "Downloading..." text below
        dc.SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_ITALIC, wxFONTWEIGHT_NORMAL));
        dc.SetTextForeground(m_labelColor);
        wxString statusText = "Downloading...";
        wxSize statusSize = dc.GetTextExtent(statusText);
        int statusX = (size.GetWidth() - statusSize.GetWidth()) / 2;
        int statusY = spinnerY + spinnerSize.GetHeight() + 8;
        dc.DrawText(statusText, statusX, statusY);
    } else {
        // Placeholder state - show media type icon and info
        wxString icon = GetMediaIcon();
        
        // For stickers, show the emoji much larger
        int fontSize = 36;
        if (m_mediaInfo.type == MediaType::Sticker && !m_mediaInfo.emoji.IsEmpty()) {
            fontSize = 64;  // Larger emoji for stickers
            icon = m_mediaInfo.emoji;
        }
        
        // Draw large emoji icon
        dc.SetFont(wxFont(fontSize, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        dc.SetTextForeground(m_textColor);
        
        wxSize iconSize = dc.GetTextExtent(icon);
        int iconX = (size.GetWidth() - iconSize.GetWidth()) / 2;
        int iconY = contentY + 5;
        dc.DrawText(icon, iconX, iconY);
        
        // Draw media type below icon (skip for stickers if emoji is shown)
        wxString typeText = GetMediaLabel();
        if (m_mediaInfo.type == MediaType::Sticker && !m_mediaInfo.emoji.IsEmpty()) {
            typeText = "Sticker";  // Just show "Sticker" without repeating emoji
        }
        
        dc.SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        dc.SetTextForeground(m_textColor);
        
        wxSize typeSize = dc.GetTextExtent(typeText);
        int typeX = (size.GetWidth() - typeSize.GetWidth()) / 2;
        int typeY = iconY + iconSize.GetHeight() + 5;
        dc.DrawText(typeText, typeX, typeY);
        
        // Draw file info if available (size, filename)
        wxString infoText;
        if (!m_mediaInfo.fileSize.IsEmpty()) {
            infoText = m_mediaInfo.fileSize;
        }
        if (!m_mediaInfo.fileName.IsEmpty() && m_mediaInfo.type != MediaType::File) {
            if (!infoText.IsEmpty()) infoText += " - ";
            infoText += m_mediaInfo.fileName;
        }
        
        if (!infoText.IsEmpty()) {
            dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
            dc.SetTextForeground(m_labelColor);
            
            wxSize infoSize = dc.GetTextExtent(infoText);
            // Truncate if needed
            if (infoSize.GetWidth() > contentWidth) {
                while (infoText.Length() > 3 && dc.GetTextExtent(infoText + "...").GetWidth() > contentWidth) {
                    infoText = infoText.Left(infoText.Length() - 1);
                }
                infoText += "...";
                infoSize = dc.GetTextExtent(infoText);
            }
            
            int infoX = (size.GetWidth() - infoSize.GetWidth()) / 2;
            int infoY = typeY + typeSize.GetHeight() + 3;
            dc.DrawText(infoText, infoX, infoY);
        }
    }
    
    // Draw caption at bottom if available
    if (!m_mediaInfo.caption.IsEmpty()) {
        dc.SetTextForeground(m_labelColor);
        dc.SetFont(wxFont(8, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_ITALIC, wxFONTWEIGHT_NORMAL));
        
        wxString caption = m_mediaInfo.caption;
        
        // Truncate if too long
        int maxLabelWidth = size.GetWidth() - (PADDING * 2);
        wxSize captionSize = dc.GetTextExtent(caption);
        if (captionSize.GetWidth() > maxLabelWidth) {
            while (caption.Length() > 3 && dc.GetTextExtent(caption + "...").GetWidth() > maxLabelWidth) {
                caption = caption.Left(caption.Length() - 1);
            }
            caption += "...";
        }
        
        int captionX = PADDING + BORDER_WIDTH;
        int captionY = size.GetHeight() - 20;
        dc.DrawText(caption, captionX, captionY);
    }
}