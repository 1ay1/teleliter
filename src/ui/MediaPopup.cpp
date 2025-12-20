#include "MediaPopup.h"
#include "FileUtils.h"
#include "LottiePlayer.h"
#include "WebmPlayer.h"
#include <wx/filename.h>
#include <iostream>

#define MPLOG(msg) std::cerr << "[MediaPopup] " << msg << std::endl
// #define MPLOG(msg) do {} while(0)

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
      m_videoMuted(true),
      m_isPlayingSticker(false),
      m_lottieAnimTimer(this, LOTTIE_ANIM_TIMER_ID),
      m_isPlayingWebm(false),
      m_webmAnimTimer(this, WEBM_ANIM_TIMER_ID)
{
    // Set hand cursor to indicate popup is clickable
    SetCursor(wxCursor(wxCURSOR_HAND));
    
    ApplyHexChatStyle();
    SetSize(MIN_WIDTH, MIN_HEIGHT);
    
    // Bind loading timer event
    Bind(wxEVT_TIMER, &MediaPopup::OnLoadingTimer, this, LOADING_TIMER_ID);
    
    // Bind lottie animation timer event
    Bind(wxEVT_TIMER, &MediaPopup::OnLottieAnimTimer, this, LOTTIE_ANIM_TIMER_ID);
    
    // Bind webm animation timer event
    Bind(wxEVT_TIMER, &MediaPopup::OnWebmAnimTimer, this, WEBM_ANIM_TIMER_ID);
    
    // Bind click event to open media
    Bind(wxEVT_LEFT_DOWN, &MediaPopup::OnLeftDown, this);
}

MediaPopup::~MediaPopup()
{
    StopAllPlayback();
    m_loadingTimer.Stop();
    DestroyMediaCtrl();
}

void MediaPopup::StopAllPlayback()
{
    // Stop all timers first
    m_lottieAnimTimer.Stop();
    m_webmAnimTimer.Stop();
    m_loadingTimer.Stop();
    
    // Stop video playback
    if (m_mediaCtrl) {
        m_mediaCtrl->Stop();
        m_mediaCtrl->Hide();
    }
    m_isPlayingVideo = false;
    
    // Stop Lottie animation
    if (m_lottiePlayer) {
        m_lottiePlayer->Stop();
    }
    m_isPlayingSticker = false;
    
    // Stop WebM playback
    if (m_webmPlayer) {
        m_webmPlayer->Stop();
    }
    m_isPlayingWebm = false;
    
    // Reset loading state
    m_isLoading = false;
}

// Sticker/animation format detection
enum class StickerFormat {
    Unknown,
    Tgs,        // Lottie animation (gzip-compressed JSON) - needs rlottie
    Webm,       // VP9 video sticker - play as video
    Webp,       // Static or animated WebP - can be animated
    Gif         // GIF animation - play as video
};

static StickerFormat DetectStickerFormat(const wxString& path)
{
    if (path.IsEmpty()) return StickerFormat::Unknown;
    
    wxFileName fn(path);
    wxString ext = fn.GetExt().Lower();
    
    if (ext == "tgs") return StickerFormat::Tgs;
    if (ext == "webm") return StickerFormat::Webm;
    if (ext == "webp") return StickerFormat::Webp;
    if (ext == "gif") return StickerFormat::Gif;
    
    return StickerFormat::Unknown;
}



void MediaPopup::PlayAnimatedSticker(const wxString& path)
{
    // Stop all other playback first
    StopAllPlayback();
    
    if (!m_lottiePlayer) {
        m_lottiePlayer = std::make_unique<LottiePlayer>();
    }
    
    // Set render size to fit popup (use sticker size for animated stickers)
    m_lottiePlayer->SetRenderSize(STICKER_MAX_WIDTH - PADDING * 2, STICKER_MAX_HEIGHT - PADDING * 2 - 20);
    m_lottiePlayer->SetLoop(true);
    
    // Set callback to receive frames
    m_lottiePlayer->SetFrameCallback([this](const wxBitmap& frame) {
        OnLottieFrame(frame);
    });
    
    if (m_lottiePlayer->LoadTgsFile(path)) {
        m_isPlayingSticker = true;
        m_hasImage = true;  // Mark that we have content to display
        m_lottiePlayer->Play();
        
        // Start timer to drive the animation
        int intervalMs = m_lottiePlayer->GetTimerIntervalMs();
        m_lottieAnimTimer.Start(intervalMs);
    } else {
        m_isPlayingSticker = false;
        FallbackToThumbnail();
    }
}

void MediaPopup::StopAnimatedSticker()
{
    m_lottieAnimTimer.Stop();
    if (m_lottiePlayer) {
        m_lottiePlayer->Stop();
    }
    m_isPlayingSticker = false;
}

void MediaPopup::PlayWebmSticker(const wxString& path)
{
#ifdef HAVE_WEBM
    // Stop all other playback first
    StopAllPlayback();
    
    if (!m_webmPlayer) {
        m_webmPlayer = std::make_unique<WebmPlayer>();
    }
    
    // Set render size to fit popup (use sticker size for webm stickers)
    m_webmPlayer->SetRenderSize(STICKER_MAX_WIDTH - PADDING * 2, STICKER_MAX_HEIGHT - PADDING * 2 - 20);
    m_webmPlayer->SetLoop(true);
    
    // Set callback to receive frames
    m_webmPlayer->SetFrameCallback([this](const wxBitmap& frame) {
        OnWebmFrame(frame);
    });
    
    if (m_webmPlayer->LoadFile(path)) {
        m_isPlayingWebm = true;
        m_hasImage = true;  // Mark that we have content to display
        m_webmPlayer->Play();
        
        // Start timer to drive the animation
        int intervalMs = m_webmPlayer->GetTimerIntervalMs();
        m_webmAnimTimer.Start(intervalMs);
    } else {
        m_isPlayingWebm = false;
        FallbackToThumbnail();
    }
#else
    FallbackToThumbnail();
#endif
}

void MediaPopup::StopWebmSticker()
{
    m_webmAnimTimer.Stop();
    if (m_webmPlayer) {
        m_webmPlayer->Stop();
    }
    m_isPlayingWebm = false;
}

void MediaPopup::OnWebmAnimTimer(wxTimerEvent& event)
{
#ifdef HAVE_WEBM
    if (m_webmPlayer && m_isPlayingWebm && IsShown()) {
        if (!m_webmPlayer->AdvanceFrame()) {
            // Animation ended (non-looping)
            m_webmAnimTimer.Stop();
            m_isPlayingWebm = false;
        }
    } else {
        // Not playing or not shown - stop timer
        m_webmAnimTimer.Stop();
    }
#endif
}

void MediaPopup::OnWebmFrame(const wxBitmap& frame)
{
    if (frame.IsOk() && m_isPlayingWebm) {
        m_bitmap = frame;
        m_hasImage = true;
        // Use CallAfter to ensure refresh happens on main thread properly
        CallAfter([this]() {
            if (IsShown() && m_isPlayingWebm) {
                Refresh();
                Update();
            }
        });
    }
}

void MediaPopup::OnLottieAnimTimer(wxTimerEvent& event)
{
    if (m_lottiePlayer && m_isPlayingSticker && IsShown()) {
        if (!m_lottiePlayer->AdvanceFrame()) {
            // Animation ended (non-looping)
            m_lottieAnimTimer.Stop();
            m_isPlayingSticker = false;
        }
    } else {
        // Not playing or not shown - stop timer
        m_lottieAnimTimer.Stop();
    }
}

void MediaPopup::OnLottieFrame(const wxBitmap& frame)
{
    if (frame.IsOk() && m_isPlayingSticker) {
        m_bitmap = frame;
        m_hasImage = true;
        // Use CallAfter to ensure refresh happens on main thread properly
        CallAfter([this]() {
            if (IsShown() && m_isPlayingSticker) {
                Refresh();
                Update();
            }
        });
    }
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
                              wxSize(PHOTO_MAX_WIDTH - PADDING * 2, PHOTO_MAX_HEIGHT - PADDING * 2 - 24),
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

bool MediaPopup::IsSameMedia(const MediaInfo& a, const MediaInfo& b) const
{
    // Must be same type
    if (a.type != b.type) return false;
    
    // Compare by fileId if both have valid IDs
    if (a.fileId != 0 && b.fileId != 0) {
        return a.fileId == b.fileId;
    }
    // Fall back to comparing by localPath
    if (!a.localPath.IsEmpty() && !b.localPath.IsEmpty()) {
        return a.localPath == b.localPath;
    }
    // Compare by thumbnail if available
    if (a.thumbnailFileId != 0 && b.thumbnailFileId != 0) {
        return a.thumbnailFileId == b.thumbnailFileId;
    }
    return false;
}

void MediaPopup::ShowMedia(const MediaInfo& info, const wxPoint& pos)
{
    MPLOG("ShowMedia called: fileId=" << info.fileId << " type=" << static_cast<int>(info.type)
          << " localPath=" << info.localPath.ToStdString()
          << " thumbnailPath=" << info.thumbnailPath.ToStdString()
          << " isDownloading=" << info.isDownloading);
    
    // Check if we're already showing the same media - avoid unnecessary reloads
    // BUT: if localPath changed (download completed), we need to reload!
    if (IsShown() && IsSameMedia(m_mediaInfo, info)) {
        // Check if the localPath has changed (download completed)
        bool localPathChanged = (m_mediaInfo.localPath != info.localPath && 
                                 !info.localPath.IsEmpty() && wxFileExists(info.localPath));
        bool thumbnailPathChanged = (m_mediaInfo.thumbnailPath != info.thumbnailPath && 
                                     !info.thumbnailPath.IsEmpty() && wxFileExists(info.thumbnailPath));
        
        MPLOG("ShowMedia: same media check - localPathChanged=" << localPathChanged 
              << " thumbnailPathChanged=" << thumbnailPathChanged
              << " current.localPath=" << m_mediaInfo.localPath.ToStdString()
              << " new.localPath=" << info.localPath.ToStdString());
        
        if (!localPathChanged && !thumbnailPathChanged) {
            // Same media, no path changes, just update position if needed
            MPLOG("ShowMedia: same media, no changes, skipping reload");
            SetPosition(pos);
            return;
        }
        MPLOG("ShowMedia: path changed, reloading media");
        // Path changed - fall through to reload with new file
    }
    
    // Stop all current playback and reset state
    StopAllPlayback();
    
    m_mediaInfo = info;
    m_hasError = false;
    m_errorMessage.Clear();
    m_hasImage = false;
    
    // For stickers, detect format and use appropriate renderer
    if (info.type == MediaType::Sticker) {
        bool hasLocalFile = !info.localPath.IsEmpty() && wxFileExists(info.localPath);
        StickerFormat format = hasLocalFile ? DetectStickerFormat(info.localPath) : StickerFormat::Unknown;
        
        if (hasLocalFile) {
            switch (format) {
                case StickerFormat::Webm:
#ifdef HAVE_WEBM
                    PlayWebmSticker(info.localPath);
                    UpdateSize();
                    SetPosition(pos);
                    Show();
                    Refresh();
                    return;
#else
                    SetPosition(pos);
                    PlayVideo(info.localPath, true, true);
                    return;
#endif
                    
                case StickerFormat::Gif:
                    SetPosition(pos);
                    PlayVideo(info.localPath, true, true);
                    return;
                    
                case StickerFormat::Tgs:
#ifdef HAVE_RLOTTIE
                    PlayAnimatedSticker(info.localPath);
                    UpdateSize();
                    SetPosition(pos);
                    Show();
                    Refresh();
                    return;
#else
                    break;  // Fall through to thumbnail
#endif
                    
                case StickerFormat::Webp:
                    SetImage(info.localPath);
                    UpdateSize();
                    SetPosition(pos);
                    Show();
                    Refresh();
                    return;
                    
                case StickerFormat::Unknown:
                default:
                    if (IsSupportedImageFormat(info.localPath)) {
                        SetImage(info.localPath);
                        UpdateSize();
                        SetPosition(pos);
                        Show();
                        Refresh();
                        return;
                    }
                    break;  // Fall through to thumbnail
            }
        }
        
        // Fall back to thumbnail for preview (thumbnails are usually static WebP/JPEG)
        if (!info.thumbnailPath.IsEmpty() && wxFileExists(info.thumbnailPath)) {
            MPLOG("ShowMedia: sticker - falling back to thumbnail: " << info.thumbnailPath.ToStdString());
            SetImage(info.thumbnailPath);
            UpdateSize();
            SetPosition(pos);
            Show();
            Refresh();
            return;
        }
        
        // If file not downloaded yet but we have a file ID, show loading
        if (info.fileId != 0 && (info.localPath.IsEmpty() || !wxFileExists(info.localPath))) {
            MPLOG("ShowMedia: sticker file not ready, showing loading for fileId=" << info.fileId);
            m_isLoading = true;
            m_loadingFrame = 0;
            m_loadingTimer.Start(150);
            UpdateSize();
            SetPosition(pos);
            Show();
            Refresh();
            return;
        }
        
        // If thumbnail not available, check if we need to download it
        if (info.thumbnailFileId != 0 && (info.thumbnailPath.IsEmpty() || !wxFileExists(info.thumbnailPath))) {
            m_isLoading = true;
            m_loadingFrame = 0;
            m_loadingTimer.Start(150);
            UpdateSize();
            SetPosition(pos);
            Show();
            Refresh();
            return;
        }
        
        // No thumbnail available, show emoji placeholder
        m_hasImage = false;
        UpdateSize();
        SetPosition(pos);
        Show();
        Refresh();
        return;
    }
    
    // Non-sticker media types (Photo, Video, GIF, VideoNote, Audio, Voice, File)
    
    // If we have a local path, try to load it
    if (!info.localPath.IsEmpty() && wxFileExists(info.localPath)) {
        // Check if it's a video/GIF that should be played
        if ((info.type == MediaType::Video || info.type == MediaType::GIF || 
             info.type == MediaType::VideoNote) && IsVideoFormat(info.localPath)) {
            bool shouldLoop = (info.type == MediaType::GIF);
            bool shouldMute = (info.type == MediaType::GIF || info.type == MediaType::VideoNote);
            // Set position BEFORE PlayVideo so the popup is in the right place
            SetPosition(pos);
            // PlayVideo will call Show() internally after setting up the media control
            // Don't call Show() again here to avoid duplicate window issues
            PlayVideo(info.localPath, shouldLoop, shouldMute);
            return;
        } else if (IsSupportedImageFormat(info.localPath)) {
            SetImage(info.localPath);
            UpdateSize();
            SetPosition(pos);
            Show();
            Refresh();
            return;
        }
    }
    
    // Try thumbnail if main file not available
    if (!info.thumbnailPath.IsEmpty() && wxFileExists(info.thumbnailPath)) {
        SetImage(info.thumbnailPath);
        // If main file is downloading, show that we're still loading
        if (info.isDownloading || (info.fileId != 0 && (info.localPath.IsEmpty() || !wxFileExists(info.localPath)))) {
            // We have thumbnail but still downloading - could show a small indicator
        }
        UpdateSize();
        SetPosition(pos);
        Show();
        Refresh();
        return;
    }
    
    // No local file - show loading if downloading
    if (info.isDownloading || info.fileId != 0) {
        MPLOG("ShowMedia: no local file, showing loading state for fileId=" << info.fileId);
        m_isLoading = true;
        m_loadingFrame = 0;
        m_loadingTimer.Start(150);
    } else {
        MPLOG("ShowMedia: no local file and no fileId, showing placeholder");
    }
    
    UpdateSize();
    SetPosition(pos);
    Show();
    Refresh();
}

void MediaPopup::PlayVideo(const wxString& path, bool loop, bool muted)
{
    MPLOG("PlayVideo called: path=" << path.ToStdString() << " loop=" << loop << " muted=" << muted);

    // IMPORTANT: Video playback behavior differs between platforms:
    //
    // macOS (AVFoundation backend):
    //   - wxMediaCtrl renders video directly inside its parent window
    //   - The popup window and video are properly composited together
    //   - Both Show() on popup and wxMediaCtrl work as expected
    //
    // Linux/Wayland (GStreamer backend):
    //   - GStreamer creates its OWN separate top-level window for video output
    //   - This window is independent of the wxMediaCtrl's parent (our popup)
    //   - If we call Show() on the popup, we get TWO windows:
    //     1. The MediaPopup (shows as a black square - just the background)
    //     2. GStreamer's video window (shows the actual video playing)
    //   - Solution: Don't show the popup at all, let GStreamer's window be the only visible one
    //
    // This is why we DON'T call Show() on the popup for video playback on Linux.

    // Stop all other playback first
    StopAllPlayback();

    m_videoPath = path;
    m_loopVideo = loop;
    m_videoMuted = muted;
    m_isPlayingVideo = false;
    m_hasImage = false;

    // Set size for video BEFORE creating media control
    int videoWidth = PHOTO_MAX_WIDTH - (PADDING * 2) - (BORDER_WIDTH * 2);
    int videoHeight = PHOTO_MAX_HEIGHT - (PADDING * 2) - (BORDER_WIDTH * 2) - 24;
    SetSize(PHOTO_MAX_WIDTH, PHOTO_MAX_HEIGHT);

    // DON'T call Show() here - see explanation at top of this function.
    // On Linux/GStreamer, calling Show() would create a duplicate black square
    // because GStreamer creates its own video window separately.
    // On macOS this wouldn't be needed, but not calling Show() is harmless there.

    // Create media control if needed
    CreateMediaCtrl();

    if (!m_mediaCtrl) {
        // Fallback to thumbnail if media control creation failed
        MPLOG("PlayVideo: failed to create media control, falling back to thumbnail");
        FallbackToThumbnail();
        return;
    }

    // Set media control size - position at 0,0 since we're not showing the popup frame
    m_mediaCtrl->SetSize(0, 0, videoWidth, videoHeight);
    m_mediaCtrl->Show();

    // Load and play the video
    MPLOG("PlayVideo: loading video file");
    if (m_mediaCtrl->Load(path)) {
        // OnMediaLoaded will be called when ready
        m_isLoading = true;
        MPLOG("PlayVideo: video loading started");
    } else {
        MPLOG("PlayVideo: failed to load video, falling back to thumbnail");
        m_mediaCtrl->Hide();
        FallbackToThumbnail();
    }

    Refresh();
}

void MediaPopup::FallbackToThumbnail()
{
    MPLOG("FallbackToThumbnail: thumbnailPath=" << m_mediaInfo.thumbnailPath.ToStdString()
          << " localPath=" << m_mediaInfo.localPath.ToStdString());
    
    // Try to use the thumbnail from the current media info
    if (!m_mediaInfo.thumbnailPath.IsEmpty() && wxFileExists(m_mediaInfo.thumbnailPath)) {
        MPLOG("FallbackToThumbnail: using thumbnail");
        m_hasError = false;
        m_errorMessage.Clear();
        SetImage(m_mediaInfo.thumbnailPath);
        UpdateSize();
        Refresh();
    } else if (!m_mediaInfo.localPath.IsEmpty() && wxFileExists(m_mediaInfo.localPath) &&
               IsSupportedImageFormat(m_mediaInfo.localPath)) {
        // Try to use the local path if it's an image
        MPLOG("FallbackToThumbnail: using localPath as image");
        m_hasError = false;
        m_errorMessage.Clear();
        SetImage(m_mediaInfo.localPath);
        UpdateSize();
        Refresh();
    } else if (!m_mediaInfo.emoji.IsEmpty()) {
        // Show emoji placeholder
        MPLOG("FallbackToThumbnail: showing emoji placeholder");
        m_hasError = false;
        m_errorMessage.Clear();
        m_hasImage = false;
        UpdateSize();
        Refresh();
    } else {
        // Show a placeholder indicating video/media
        MPLOG("FallbackToThumbnail: no thumbnail available, showing placeholder");
        m_hasError = false;
        m_errorMessage.Clear();
        m_hasImage = false;
        UpdateSize();
        Refresh();
    }
}

void MediaPopup::StopVideo()
{
    if (m_mediaCtrl) {
        m_mediaCtrl->Stop();
        m_mediaCtrl->Hide();
    }
    m_isPlayingVideo = false;
    m_videoPath.Clear();
    // Note: Don't stop animated stickers here - they are independent
}

void MediaPopup::OnMediaLoaded(wxMediaEvent& event)
{
    MPLOG("OnMediaLoaded: video ready to play");
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
            if (vidWidth > PHOTO_MAX_WIDTH - PADDING * 2 || vidHeight > PHOTO_MAX_HEIGHT - PADDING * 2 - 24) {
                double scaleX = (double)(PHOTO_MAX_WIDTH - PADDING * 2) / vidWidth;
                double scaleY = (double)(PHOTO_MAX_HEIGHT - PADDING * 2 - 24) / vidHeight;
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

        m_mediaCtrl->Play();
    }

    Refresh();
}

void MediaPopup::OnMediaFinished(wxMediaEvent& event)
{
    if (m_loopVideo && m_mediaCtrl) {
        // Seek to beginning and play again
        m_mediaCtrl->Seek(0);
        m_mediaCtrl->Play();
    }
}

void MediaPopup::OnMediaStop(wxMediaEvent& event)
{
    // Video stopped - could be user action or error
}

void MediaPopup::SetImage(const wxImage& image)
{
    if (!image.IsOk()) {
        m_hasImage = false;
        return;
    }
    
    // Validate dimensions to prevent pixman errors
    if (image.GetWidth() <= 0 || image.GetHeight() <= 0) {
        m_hasImage = false;
        return;
    }
    
    // Stop all playback when setting a static image
    StopAllPlayback();
    
    m_isLoading = false;
    m_loadingTimer.Stop();
    m_hasError = false;
    
    // Use larger size for photos/videos, smaller for stickers/emojis
    int maxWidth, maxHeight;
    if (m_mediaInfo.type == MediaType::Photo || m_mediaInfo.type == MediaType::Video ||
        m_mediaInfo.type == MediaType::GIF) {
        maxWidth = PHOTO_MAX_WIDTH;
        maxHeight = PHOTO_MAX_HEIGHT;
    } else {
        maxWidth = STICKER_MAX_WIDTH;
        maxHeight = STICKER_MAX_HEIGHT;
    }
    
    // Scale image to fit within max dimensions while preserving aspect ratio
    int imgWidth = image.GetWidth();
    int imgHeight = image.GetHeight();
    
    if (imgWidth > maxWidth || imgHeight > maxHeight) {
        double scaleX = (double)maxWidth / imgWidth;
        double scaleY = (double)maxHeight / imgHeight;
        double scale = std::min(scaleX, scaleY);
        
        imgWidth = (int)(imgWidth * scale);
        imgHeight = (int)(imgHeight * scale);
    }
    
    // Ensure valid dimensions after scaling
    if (imgWidth <= 0 || imgHeight <= 0) {
        m_hasImage = false;
        return;
    }
    
    wxImage scaled = image.Scale(imgWidth, imgHeight, wxIMAGE_QUALITY_HIGH);
    if (!scaled.IsOk()) {
        m_hasImage = false;
        return;
    }
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
            return "[Photo]";
        case MediaType::Video:
            return "[Video]";
        case MediaType::Sticker:
            return m_mediaInfo.emoji.IsEmpty() ? "[Sticker]" : m_mediaInfo.emoji;
        case MediaType::GIF:
            return "[GIF]";
        case MediaType::Voice:
            return "[Voice]";
        case MediaType::VideoNote:
            return "[VideoMsg]";
        case MediaType::File:
            return "[File]";
        case MediaType::Reaction:
            return m_mediaInfo.emoji;
        default:
            return "[Media]";
    }
}

void MediaPopup::OnLeftDown(wxMouseEvent& event)
{
    MPLOG("MediaPopup clicked, invoking callback if set");
    if (m_clickCallback) {
        m_clickCallback(m_mediaInfo);
    }
    event.Skip();
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
    
    // Content area dimensions
    int contentX = PADDING + BORDER_WIDTH;
    int contentY = PADDING + BORDER_WIDTH;
    int contentWidth = size.GetWidth() - (PADDING * 2) - (BORDER_WIDTH * 2);
    
    // If video is playing, the media control handles the display
    if (m_isPlayingVideo && m_mediaCtrl && m_mediaCtrl->IsShown()) {
        // Draw the label at the bottom
        DrawMediaLabel(dc, size);
        return;
    }
    
    // Draw main content based on state
    if (m_hasImage && m_bitmap.IsOk()) {
        // Draw image centered in content area
        int imgX = contentX + (contentWidth - m_bitmap.GetWidth()) / 2;
        int imgY = contentY;
        dc.DrawBitmap(m_bitmap, imgX, imgY, true);
        
        // Check if this is a video type that should show a play button
        bool isVideoType = (m_mediaInfo.type == MediaType::Video || 
                           m_mediaInfo.type == MediaType::GIF || 
                           m_mediaInfo.type == MediaType::VideoNote);
        
        // If this is a thumbnail and the main file is still downloading, show download indicator
        bool isShowingThumbnail = !m_mediaInfo.thumbnailPath.IsEmpty() && 
                                   wxFileExists(m_mediaInfo.thumbnailPath) &&
                                   (m_mediaInfo.localPath.IsEmpty() || !wxFileExists(m_mediaInfo.localPath));
        bool needsDownload = m_mediaInfo.fileId != 0 && 
                             (m_mediaInfo.localPath.IsEmpty() || !wxFileExists(m_mediaInfo.localPath));
        
        // Calculate center for overlay icons
        int centerX = imgX + m_bitmap.GetWidth() / 2;
        int centerY = imgY + m_bitmap.GetHeight() / 2;
        int radius = 24;
        
        if (isVideoType) {
            // Draw semi-transparent dark overlay behind play button
            dc.SetBrush(wxBrush(wxColour(0, 0, 0, 100)));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawCircle(centerX, centerY, radius + 4);
            
            // Draw play button circle
            dc.SetBrush(wxBrush(wxColour(0x72, 0x9F, 0xCF, 220)));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawCircle(centerX, centerY, radius);
            
            if (isShowingThumbnail && needsDownload) {
                // Draw download arrow (file needs to be downloaded first)
                dc.SetPen(wxPen(wxColour(255, 255, 255), 2));
                dc.DrawLine(centerX, centerY - 8, centerX, centerY + 5);
                dc.DrawLine(centerX - 6, centerY, centerX, centerY + 8);
                dc.DrawLine(centerX + 6, centerY, centerX, centerY + 8);
                
                // Draw hint text
                dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_ITALIC, wxFONTWEIGHT_NORMAL));
                dc.SetTextForeground(wxColour(0xAA, 0xAA, 0xAA));
                wxString hint = "Click to download";
                wxSize hintSize = dc.GetTextExtent(hint);
                dc.DrawText(hint, centerX - hintSize.GetWidth() / 2, centerY + radius + 8);
            } else {
                // Draw play triangle (file is available, click to open)
                wxPoint triangle[3];
                triangle[0] = wxPoint(centerX - 6, centerY - 10);
                triangle[1] = wxPoint(centerX - 6, centerY + 10);
                triangle[2] = wxPoint(centerX + 10, centerY);
                dc.SetBrush(wxBrush(wxColour(255, 255, 255)));
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.DrawPolygon(3, triangle);
                
                // Draw hint text
                dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_ITALIC, wxFONTWEIGHT_NORMAL));
                dc.SetTextForeground(wxColour(0xAA, 0xAA, 0xAA));
                wxString hint = "Click to play";
                wxSize hintSize = dc.GetTextExtent(hint);
                dc.DrawText(hint, centerX - hintSize.GetWidth() / 2, centerY + radius + 8);
            }
        }
        
        // Draw the label at the bottom
        DrawMediaLabel(dc, size);
        
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
        static const wxString spinners[] = {"|", "/", "-", "\\", "|", "/", "-", "\\"};
        wxString spinner = spinners[m_loadingFrame % 8];
        
        dc.SetFont(wxFont(32, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        dc.SetTextForeground(wxColour(0x72, 0x9F, 0xCF));  // Blue for downloading
        
        wxSize spinnerSize = dc.GetTextExtent(spinner);
        int spinnerX = (size.GetWidth() - spinnerSize.GetWidth()) / 2;
        int spinnerY = contentY + 10;
        dc.DrawText(spinner, spinnerX, spinnerY);
        
        // Draw "Downloading..." text below
        dc.SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_ITALIC, wxFONTWEIGHT_NORMAL));
        dc.SetTextForeground(m_labelColor);
        wxString statusText = "Downloading...";
        wxSize statusSize = dc.GetTextExtent(statusText);
        int statusX = (size.GetWidth() - statusSize.GetWidth()) / 2;
        int statusY = spinnerY + spinnerSize.GetHeight() + 8;
        dc.DrawText(statusText, statusX, statusY);
        
        // Draw the label at the bottom
        DrawMediaLabel(dc, size);
        
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
        
        // Draw media type below icon
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
}

void MediaPopup::DrawMediaLabel(wxDC& dc, const wxSize& size)
{
    // Get the label text
    wxString label = GetMediaLabel();
    
    // Draw media type label
    dc.SetTextForeground(m_labelColor);
    dc.SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    
    int maxLabelWidth = size.GetWidth() - (PADDING * 2);
    wxSize labelSize = dc.GetTextExtent(label);
    
    // Truncate if needed
    if (labelSize.GetWidth() > maxLabelWidth) {
        while (label.Length() > 3 && dc.GetTextExtent(label + "...").GetWidth() > maxLabelWidth) {
            label = label.Left(label.Length() - 1);
        }
        label += "...";
        labelSize = dc.GetTextExtent(label);
    }
    
    int labelX = (size.GetWidth() - labelSize.GetWidth()) / 2;
    int labelY = size.GetHeight() - 18;
    dc.DrawText(label, labelX, labelY);
    
    // Draw caption below label if available (in smaller text)
    if (!m_mediaInfo.caption.IsEmpty()) {
        dc.SetFont(wxFont(8, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_ITALIC, wxFONTWEIGHT_NORMAL));
        
        wxString caption = m_mediaInfo.caption;
        wxSize captionSize = dc.GetTextExtent(caption);
        
        // Truncate if too long
        if (captionSize.GetWidth() > maxLabelWidth) {
            while (caption.Length() > 3 && dc.GetTextExtent(caption + "...").GetWidth() > maxLabelWidth) {
                caption = caption.Left(caption.Length() - 1);
            }
            caption += "...";
        }
        
        // Draw caption left-aligned below the centered label
        int captionX = PADDING + BORDER_WIDTH;
        int captionY = labelY - 14;  // Above the label
        dc.DrawText(caption, captionX, captionY);
    }
}