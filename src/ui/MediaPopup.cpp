#include "MediaPopup.h"
#include "FileUtils.h"
#include "LottiePlayer.h"
#include "WebmPlayer.h"
#include "FFmpegPlayer.h"
#include <wx/filename.h>
#include <wx/settings.h>
#include <wx/display.h>
#include <iostream>
#include <thread>

wxDEFINE_EVENT(wxEVT_IMAGE_LOADED, wxThreadEvent);
wxDEFINE_EVENT(wxEVT_VIDEO_LOADED, wxThreadEvent);

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
      m_isDownloadingMedia(false),
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
      m_webmAnimTimer(this, WEBM_ANIM_TIMER_ID),
      m_isPlayingFFmpeg(false),
      m_ffmpegAnimTimer(this, FFMPEG_ANIM_TIMER_ID),
      m_asyncLoadTimer(this, ASYNC_LOAD_TIMER_ID),
      m_asyncLoadPending(false),
      m_parentBottom(-1),
      m_videoLoadStartTime(0)
{
    // Set hand cursor to indicate popup is clickable
    SetCursor(wxCursor(wxCURSOR_HAND));

    ApplyHexChatStyle();
    SetSize(MIN_WIDTH, MIN_HEIGHT);
    SetMinSize(wxSize(MIN_WIDTH, MIN_HEIGHT));

    // Bind loading timer event
    Bind(wxEVT_TIMER, &MediaPopup::OnLoadingTimer, this, LOADING_TIMER_ID);

    // Bind lottie animation timer event
    Bind(wxEVT_TIMER, &MediaPopup::OnLottieAnimTimer, this, LOTTIE_ANIM_TIMER_ID);

    // Bind webm animation timer event
    Bind(wxEVT_TIMER, &MediaPopup::OnWebmAnimTimer, this, WEBM_ANIM_TIMER_ID);

    // Bind ffmpeg animation timer event
    Bind(wxEVT_TIMER, &MediaPopup::OnFFmpegAnimTimer, this, FFMPEG_ANIM_TIMER_ID);

    // Bind async load timer event
    Bind(wxEVT_TIMER, &MediaPopup::OnAsyncLoadTimer, this, ASYNC_LOAD_TIMER_ID);

    // Bind image loaded event
    Bind(wxEVT_IMAGE_LOADED, &MediaPopup::OnImageLoaded, this);

    // Bind video loaded event
    Bind(wxEVT_VIDEO_LOADED, &MediaPopup::OnVideoLoaded, this);

    // Bind click event to open media
    Bind(wxEVT_LEFT_DOWN, &MediaPopup::OnLeftDown, this);
}

MediaPopup::~MediaPopup()
{
    StopAllPlayback();
    m_loadingTimer.Stop();
    m_asyncLoadTimer.Stop();
    m_ffmpegAnimTimer.Stop();
    m_ffmpegPlayer.reset();
    DestroyMediaCtrl();
    ClearFailedLoads();
}

void MediaPopup::StopAllPlayback()
{
    // Stop all timers first
    m_lottieAnimTimer.Stop();
    m_webmAnimTimer.Stop();
    m_ffmpegAnimTimer.Stop();
    m_loadingTimer.Stop();
    m_asyncLoadTimer.Stop();

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

    // Stop FFmpeg playback
    if (m_ffmpegPlayer) {
        m_ffmpegPlayer->Stop();
    }
    m_isPlayingFFmpeg = false;

    // Reset loading state
    m_isLoading = false;
    m_isDownloadingMedia = false;
    m_asyncLoadPending = false;
    m_pendingImagePath.Clear();
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
    // Check if this sticker has failed to load recently
    if (HasFailedRecently(path)) {
        MPLOG("PlayAnimatedSticker: skipping recently failed sticker: " << path.ToStdString());
        FallbackToThumbnail();
        return;
    }

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

        // Apply size and position using single source of truth
        ApplySizeAndPosition(STICKER_MAX_WIDTH, STICKER_MAX_HEIGHT);
        Refresh();
    } else {
        MPLOG("PlayAnimatedSticker: failed to load: " << path.ToStdString());
        MarkLoadFailed(path);
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
#if defined(__WXGTK__) && defined(HAVE_FFMPEG)
    // On Linux, use FFmpegPlayer for WebM stickers (more reliable than libvpx)
    MPLOG("PlayWebmSticker: using FFmpegPlayer on Linux for " << path.ToStdString());

    // Check if this sticker has failed to load recently
    if (HasFailedRecently(path)) {
        MPLOG("PlayWebmSticker: skipping recently failed sticker");
        FallbackToThumbnail();
        return;
    }

    // Stop all other playback first
    StopAllPlayback();

    // Create FFmpeg player if needed
    if (!m_ffmpegPlayer) {
        m_ffmpegPlayer = std::make_unique<FFmpegPlayer>();
    }

    // Set render size for stickers
    int stickerWidth = STICKER_MAX_WIDTH - PADDING * 2;
    int stickerHeight = STICKER_MAX_HEIGHT - PADDING * 2 - 20;
    m_ffmpegPlayer->SetRenderSize(stickerWidth, stickerHeight);
    m_ffmpegPlayer->SetLoop(true);
    m_ffmpegPlayer->SetMuted(true);

    // Set up frame callback
    m_ffmpegPlayer->SetFrameCallback([this](const wxBitmap& frame) {
        OnFFmpegFrame(frame);
    });

    // Load the WebM file
    if (!m_ffmpegPlayer->LoadFile(path)) {
        MPLOG("PlayWebmSticker: FFmpeg failed to load: " << path.ToStdString());
        MarkLoadFailed(path);
        m_ffmpegPlayer.reset();
        FallbackToThumbnail();
        return;
    }

    // Get actual dimensions and scale
    int vidWidth = m_ffmpegPlayer->GetWidth();
    int vidHeight = m_ffmpegPlayer->GetHeight();

    if (vidWidth > 0 && vidHeight > 0) {
        double scaleX = (double)stickerWidth / vidWidth;
        double scaleY = (double)stickerHeight / vidHeight;
        double scale = std::min(scaleX, scaleY);

        int scaledWidth = (int)(vidWidth * scale);
        int scaledHeight = (int)(vidHeight * scale);

        m_ffmpegPlayer->SetRenderSize(scaledWidth, scaledHeight);
        int popupWidth = scaledWidth + PADDING * 2 + BORDER_WIDTH * 2;
        int popupHeight = scaledHeight + PADDING * 2 + BORDER_WIDTH * 2 + 20;
        ApplySizeAndPosition(popupWidth, popupHeight);
    } else {
        ApplySizeAndPosition(STICKER_MAX_WIDTH, STICKER_MAX_HEIGHT);
    }

    // Start playback
    m_ffmpegPlayer->Play();
    m_isPlayingFFmpeg = true;
    m_hasImage = true;

    // Start timer for frame updates
    int interval = m_ffmpegPlayer->GetTimerIntervalMs();
    m_ffmpegAnimTimer.Start(interval);

    // ApplySizeAndPosition already called Show(), just refresh
    Refresh();

    MPLOG("PlayWebmSticker: FFmpeg playback started");
    return;

#elif defined(HAVE_WEBM)
    // Use libvpx/libwebm on other platforms
    // Check if this sticker has failed to load recently
    if (HasFailedRecently(path)) {
        MPLOG("PlayWebmSticker: skipping recently failed sticker: " << path.ToStdString());
        FallbackToThumbnail();
        return;
    }

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
        MPLOG("PlayWebmSticker: failed to load: " << path.ToStdString());
        MarkLoadFailed(path);
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
    // Use native system colors - don't explicitly set background, let it inherit
    m_bgColor = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
    m_borderColor = wxSystemSettings::GetColour(wxSYS_COLOUR_3DSHADOW);
    m_textColor = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    m_labelColor = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
}

void MediaPopup::CreateMediaCtrl()
{
#ifdef __WXGTK__
    // On Linux, wxMediaCtrl with GStreamer is unreliable and can crash.
    // Don't create the media control at all on Linux.
    return;
#endif

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

    // Let media control use native background

    // Forward mouse clicks from media control to parent popup
    // This is needed because wxMediaCtrl on macOS captures all mouse events
    m_mediaCtrl->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
        MPLOG("wxMediaCtrl clicked, forwarding to popup callback");
        if (m_clickCallback) {
            m_clickCallback(m_mediaInfo);
        }
    });
    m_mediaCtrl->Bind(wxEVT_LEFT_DCLICK, [this](wxMouseEvent& event) {
        MPLOG("wxMediaCtrl double-clicked, forwarding to popup callback");
        if (m_clickCallback) {
            m_clickCallback(m_mediaInfo);
        }
    });

    // Bind media events
    m_mediaCtrl->Bind(wxEVT_MEDIA_LOADED, &MediaPopup::OnMediaLoaded, this);
    m_mediaCtrl->Bind(wxEVT_MEDIA_FINISHED, &MediaPopup::OnMediaFinished, this);
    m_mediaCtrl->Bind(wxEVT_MEDIA_STOP, &MediaPopup::OnMediaStop, this);
    m_mediaCtrl->Bind(wxEVT_MEDIA_STATECHANGED, &MediaPopup::OnMediaStateChanged, this);
}

void MediaPopup::DestroyMediaCtrl()
{
    if (m_mediaCtrl) {
        m_mediaCtrl->Stop();
        m_mediaCtrl->Unbind(wxEVT_MEDIA_LOADED, &MediaPopup::OnMediaLoaded, this);
        m_mediaCtrl->Unbind(wxEVT_MEDIA_FINISHED, &MediaPopup::OnMediaFinished, this);
        m_mediaCtrl->Unbind(wxEVT_MEDIA_STOP, &MediaPopup::OnMediaStop, this);
        m_mediaCtrl->Unbind(wxEVT_MEDIA_STATECHANGED, &MediaPopup::OnMediaStateChanged, this);
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

    // Store original position immediately so UpdateSize() can use it
    m_originalPosition = pos;

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
            AdjustPositionToScreen(pos);
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
    m_isDownloadingMedia = false;

    // For stickers, detect format and use appropriate renderer
    if (info.type == MediaType::Sticker) {
        bool hasLocalFile = !info.localPath.IsEmpty() && wxFileExists(info.localPath);
        StickerFormat format = hasLocalFile ? DetectStickerFormat(info.localPath) : StickerFormat::Unknown;

        if (hasLocalFile) {
            switch (format) {
                case StickerFormat::Webm:
#ifdef HAVE_WEBM
                    PlayWebmSticker(info.localPath);
                    Refresh();
                    return;
#else
                    PlayVideo(info.localPath, true, true);
                    return;
#endif

                case StickerFormat::Gif:
                    PlayVideo(info.localPath, true, true);
                    return;

                case StickerFormat::Tgs:
#ifdef HAVE_RLOTTIE
                    PlayAnimatedSticker(info.localPath);
                    Refresh();
                    return;
#else
                    break;  // Fall through to thumbnail
#endif

                case StickerFormat::Webp:
                    // Show loading placeholder immediately, then load image async
                    m_isLoading = true;
                    m_loadingFrame = 0;
                    m_loadingTimer.Start(150);
                    ApplySizeAndPosition(MIN_WIDTH, MIN_HEIGHT);
                    LoadImageAsync(info.localPath);
                    Refresh();
                    return;

                case StickerFormat::Unknown:
                default:
                    if (IsSupportedImageFormat(info.localPath)) {
                        // Show loading placeholder immediately, then load image async
                        m_isLoading = true;
                        m_loadingFrame = 0;
                        m_loadingTimer.Start(150);
                        ApplySizeAndPosition(MIN_WIDTH, MIN_HEIGHT);
                        LoadImageAsync(info.localPath);
                        Refresh();
                        return;
                    }
                    break;  // Fall through to thumbnail
            }
        }

        // Fall back to thumbnail for preview (thumbnails are usually static WebP/JPEG)
        if (!info.thumbnailPath.IsEmpty() && wxFileExists(info.thumbnailPath)) {
            MPLOG("ShowMedia: sticker - falling back to thumbnail: " << info.thumbnailPath.ToStdString());
            // Show loading placeholder immediately, then load image async
            m_isLoading = true;
            m_loadingFrame = 0;
            m_loadingTimer.Start(150);
            ApplySizeAndPosition(MIN_WIDTH, MIN_HEIGHT);
            LoadImageAsync(info.thumbnailPath);
            Refresh();
            return;
        }

        // If file not downloaded yet but we have a file ID, show loading
        if (info.fileId != 0 && (info.localPath.IsEmpty() || !wxFileExists(info.localPath))) {
            MPLOG("ShowMedia: sticker file not ready, showing loading for fileId=" << info.fileId);
            m_isLoading = true;
            m_loadingFrame = 0;
            m_loadingTimer.Start(150);
            ApplySizeAndPosition(180, 120);
            Refresh();
            return;
        }

        // If thumbnail not available, check if we need to download it
        if (info.thumbnailFileId != 0 && (info.thumbnailPath.IsEmpty() || !wxFileExists(info.thumbnailPath))) {
            m_isLoading = true;
            m_loadingFrame = 0;
            m_loadingTimer.Start(150);
            ApplySizeAndPosition(180, 120);
            Refresh();
            return;
        }

        // No thumbnail available, show emoji placeholder
        m_hasImage = false;
        ApplySizeAndPosition(200, 150);
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
            // Always mute preview popups - user can click to open with sound
            bool shouldMute = true;
            // LoadVideoAsync will call ApplySizeAndPosition internally
            LoadVideoAsync(info.localPath, shouldLoop, shouldMute);
            return;
        } else if (IsSupportedImageFormat(info.localPath)) {
            // Show loading placeholder immediately, then load image async
            m_isLoading = true;
            m_loadingFrame = 0;
            m_loadingTimer.Start(150);
            ApplySizeAndPosition(MIN_WIDTH, MIN_HEIGHT);
            LoadImageAsync(info.localPath);
            Refresh();
            return;
        }
    }

    // Try thumbnail if main file not available
    if (!info.thumbnailPath.IsEmpty() && wxFileExists(info.thumbnailPath)) {
        // Show loading placeholder immediately, then load image async
        m_isLoading = true;
        m_loadingFrame = 0;
        m_loadingTimer.Start(150);
        ApplySizeAndPosition(MIN_WIDTH, MIN_HEIGHT);
        LoadImageAsync(info.thumbnailPath);
        // If main file is downloading, show downloading overlay on thumbnail
        if (info.isDownloading || (info.fileId != 0 && (info.localPath.IsEmpty() || !wxFileExists(info.localPath)))) {
            m_isDownloadingMedia = true;
            MPLOG("ShowMedia: showing thumbnail with downloading overlay");
        }
        Refresh();
        return;
    }

    // No local file - check if it's ACTUALLY downloading
    bool isTrulyDownloading = info.isDownloading;
    
    // If not marked as downloading in info, double check with TelegramClient if we have a frame
    if (!isTrulyDownloading && info.fileId != 0) {
        // Can't easily check client state from here without access to main frame or client
        // But we can trust info.isDownloading if ChatViewWidget kept it up to date
        // However, if localPath is empty but isDownloading is false, it might mean:
        // 1. Not started yet 
        // 2. Completed but we don't have the path yet (Race Condition)
        // 3. Failed
        
        // We assume "not downloaded" if path is empty.
        // But to avoid the "starts out showing downloading" glitch for already downloaded files,
        // we should start with a PLACEHOLDER unless we are SURE it is downloading.
        
        // BETTER APPROACH: Use the thumbnail size for the placeholder to avoid jumping
        // even if we show a spinner.
    }

    if (info.isDownloading || info.fileId != 0) {
        MPLOG("ShowMedia: no local file, showing downloading state for fileId=" << info.fileId);
        m_isDownloadingMedia = true;
        m_isLoading = true;
        m_loadingFrame = 0;
        m_loadingTimer.Start(150);
        
        // If we are here, we have NO thumbnail file content.
        // But we might have dimensions!
        if (info.width > 0 && info.height > 0) {
            // Calculate scaled dimensions just like SetImage does
            int maxWidth = PHOTO_MAX_WIDTH;
            int maxHeight = PHOTO_MAX_HEIGHT;
            if (info.type != MediaType::Photo && info.type != MediaType::Video && info.type != MediaType::GIF) {
                maxWidth = STICKER_MAX_WIDTH;
                maxHeight = STICKER_MAX_HEIGHT;
            }
            
            int imgWidth = info.width;
            int imgHeight = info.height;

            if (imgWidth > maxWidth || imgHeight > maxHeight) {
                double scaleX = (double)maxWidth / imgWidth;
                double scaleY = (double)maxHeight / imgHeight;
                double scale = std::min(scaleX, scaleY);
                imgWidth = (int)(imgWidth * scale);
                imgHeight = (int)(imgHeight * scale);
            }
            
            int popupWidth = imgWidth + PADDING * 2 + BORDER_WIDTH * 2;
            int popupHeight = imgHeight + PADDING * 2 + BORDER_WIDTH * 2 + 24;
            ApplySizeAndPosition(popupWidth, popupHeight);
        } else {
            // No dimensions, use default
            ApplySizeAndPosition(180, 120);
        }
    } else {
        MPLOG("ShowMedia: no local file and no fileId, showing placeholder");
        ApplySizeAndPosition(180, 120);
    }
    
    Refresh();
}

void MediaPopup::PlayVideo(const wxString& path, bool loop, bool muted)
{
    MPLOG("PlayVideo called: path=" << path.ToStdString() << " loop=" << loop << " muted=" << muted);

#ifdef HAVE_FFMPEG
    // Use FFmpegPlayer instead of wxMediaCtrl for better click handling
    // wxMediaCtrl on macOS uses native AVPlayer that swallows mouse events
    // Use async loading to prevent UI freeze
    LoadVideoAsync(path, loop, muted);
    return;
#elif defined(__WXGTK__)
    // No FFmpeg available on Linux, fall back to thumbnail
    MPLOG("PlayVideo: FFmpeg not available on Linux, falling back to thumbnail");
    FallbackToThumbnail();
    return;
#endif

    // Check if this video has failed to load recently
    if (HasFailedRecently(path)) {
        MPLOG("PlayVideo: skipping recently failed video: " << path.ToStdString());
        FallbackToThumbnail();
        return;
    }

    // IMPORTANT: Video playback behavior differs between platforms:
    //
    // macOS (AVFoundation backend):
    //   - wxMediaCtrl renders video directly inside its parent window
    //   - The popup window and video are properly composited together
    //   - Both Show() on popup and wxMediaCtrl work as expected

    // Stop all other playback first
    StopAllPlayback();

    m_videoPath = path;
    m_loopVideo = loop;
    m_videoMuted = muted;
    m_isPlayingVideo = false;
    m_hasImage = false;
    m_videoLoadStartTime = wxGetUTCTimeMillis().GetValue();  // Track when we started loading

    // Set size for video BEFORE creating media control
    int videoWidth = PHOTO_MAX_WIDTH - (PADDING * 2) - (BORDER_WIDTH * 2);
    int videoHeight = PHOTO_MAX_HEIGHT - (PADDING * 2) - (BORDER_WIDTH * 2) - 24;
    
    // Use single source of truth for size and position - this also shows the window
    if (m_mediaInfo.width > 0 && m_mediaInfo.height > 0) {
        int maxWidth = PHOTO_MAX_WIDTH;
        int maxHeight = PHOTO_MAX_HEIGHT;
        if (m_mediaInfo.type != MediaType::Photo && m_mediaInfo.type != MediaType::Video && m_mediaInfo.type != MediaType::GIF) {
            maxWidth = STICKER_MAX_WIDTH;
            maxHeight = STICKER_MAX_HEIGHT;
        }

        int vidWidth = m_mediaInfo.width;
        int vidHeight = m_mediaInfo.height;

        if (vidWidth > maxWidth || vidHeight > maxHeight) {
             double scaleX = (double)maxWidth / vidWidth;
             double scaleY = (double)maxHeight / vidHeight;
             double scale = std::min(scaleX, scaleY);
             vidWidth = (int)(vidWidth * scale);
             vidHeight = (int)(vidHeight * scale);
        }
        
        int popupWidth = vidWidth + PADDING * 2 + BORDER_WIDTH * 2;
        int popupHeight = vidHeight + PADDING * 2 + BORDER_WIDTH * 2 + 24;
        ApplySizeAndPosition(popupWidth, popupHeight);
    } else {
        ApplySizeAndPosition(PHOTO_MAX_WIDTH, PHOTO_MAX_HEIGHT);
    }

    // Create media control if needed
    CreateMediaCtrl();

    if (!m_mediaCtrl) {
        // Fallback to thumbnail if media control creation failed
        MPLOG("PlayVideo: failed to create media control, falling back to thumbnail");
        FallbackToThumbnail();
        return;
    }

    // Set media control size - position it inside the popup with proper padding
    m_mediaCtrl->SetSize(PADDING + BORDER_WIDTH, PADDING + BORDER_WIDTH, videoWidth, videoHeight);
    m_mediaCtrl->Show();

    // Load and play the video
    MPLOG("PlayVideo: loading video file");

    // Wrap the Load call in try-catch to prevent crashes
    bool loadSuccess = false;
    try {
        loadSuccess = m_mediaCtrl->Load(path);
    } catch (const std::exception& e) {
        MPLOG("PlayVideo: exception during Load(): " << e.what());
        loadSuccess = false;
    } catch (...) {
        MPLOG("PlayVideo: unknown exception during Load()");
        loadSuccess = false;
    }

    if (loadSuccess) {
        // OnMediaLoaded will be called when ready
        m_isLoading = true;
        m_loadingFrame = 0;
        m_loadingTimer.Start(150);
        MPLOG("PlayVideo: video loading started");
    } else {
        MPLOG("PlayVideo: failed to load video, falling back to thumbnail");
        MarkLoadFailed(path);
        m_mediaCtrl->Hide();
        FallbackToThumbnail();
    }

    Refresh();
}

void MediaPopup::LoadVideoAsync(const wxString& path, bool loop, bool muted)
{
#ifdef HAVE_FFMPEG
    MPLOG("LoadVideoAsync: " << path.ToStdString());

    // Check if this video has failed to load recently
    if (HasFailedRecently(path)) {
        MPLOG("LoadVideoAsync: skipping recently failed video");
        FallbackToThumbnail();
        return;
    }

    // Store pending video info
    m_pendingVideoPath = path;
    m_pendingVideoLoop = loop;
    m_pendingVideoMuted = muted;

    // Show loading indicator immediately so UI is responsive
    m_isLoading = true;
    m_loadingFrame = 0;
    m_loadingTimer.Start(150);
    
    // Use single source of truth for size and position - this also shows the window
    if (m_mediaInfo.width > 0 && m_mediaInfo.height > 0) {
        int maxWidth = PHOTO_MAX_WIDTH;
        int maxHeight = PHOTO_MAX_HEIGHT;
        if (m_mediaInfo.type != MediaType::Photo && m_mediaInfo.type != MediaType::Video && m_mediaInfo.type != MediaType::GIF) {
            maxWidth = STICKER_MAX_WIDTH;
            maxHeight = STICKER_MAX_HEIGHT;
        }

        int vidWidth = m_mediaInfo.width;
        int vidHeight = m_mediaInfo.height;

        if (vidWidth > maxWidth || vidHeight > maxHeight) {
             double scaleX = (double)maxWidth / vidWidth;
             double scaleY = (double)maxHeight / vidHeight;
             double scale = std::min(scaleX, scaleY);
             vidWidth = (int)(vidWidth * scale);
             vidHeight = (int)(vidHeight * scale);
        }
        
        int popupWidth = vidWidth + PADDING * 2 + BORDER_WIDTH * 2;
        int popupHeight = vidHeight + PADDING * 2 + BORDER_WIDTH * 2 + 24;
        ApplySizeAndPosition(popupWidth, popupHeight);
    } else {
        ApplySizeAndPosition(PHOTO_MAX_WIDTH, PHOTO_MAX_HEIGHT);
    }
    Refresh();

    // Defer the heavy FFmpeg loading using CallAfter
    // This allows the UI to update before the blocking call
    CallAfter([this]() {
        if (m_pendingVideoPath.IsEmpty()) {
            return;  // Cancelled
        }

        wxString path = m_pendingVideoPath;
        bool loop = m_pendingVideoLoop;
        bool muted = m_pendingVideoMuted;
        m_pendingVideoPath.Clear();

        // Now do the actual FFmpeg loading
        PlayVideoWithFFmpeg(path, loop, muted);
    });
#else
    // FFmpeg not available, fall back to thumbnail
    FallbackToThumbnail();
#endif
}

void MediaPopup::OnVideoLoaded(wxThreadEvent& event)
{
    // Reserved for future true async video loading
    // Currently video loading uses CallAfter for deferred loading
}

void MediaPopup::PlayVideoWithFFmpeg(const wxString& path, bool loop, bool muted)
{
#ifdef HAVE_FFMPEG
    MPLOG("PlayVideoWithFFmpeg: " << path.ToStdString());

    // Stop loading indicator
    m_isLoading = false;
    m_loadingTimer.Stop();

    // Check if this video has failed to load recently
    if (HasFailedRecently(path)) {
        MPLOG("PlayVideoWithFFmpeg: skipping recently failed video");
        FallbackToThumbnail();
        return;
    }

    // Stop all other playback first
    StopAllPlayback();

    m_videoPath = path;
    m_loopVideo = loop;
    m_videoMuted = muted;
    m_hasImage = false;

    // Calculate video display size
    int videoWidth = PHOTO_MAX_WIDTH - (PADDING * 2) - (BORDER_WIDTH * 2);
    int videoHeight = PHOTO_MAX_HEIGHT - (PADDING * 2) - (BORDER_WIDTH * 2) - 24;

    // Create FFmpeg player if needed
    if (!m_ffmpegPlayer) {
        m_ffmpegPlayer = std::make_unique<FFmpegPlayer>();
    }

    // Set render size before loading
    m_ffmpegPlayer->SetRenderSize(videoWidth, videoHeight);
    m_ffmpegPlayer->SetLoop(loop);
    m_ffmpegPlayer->SetMuted(muted);

    // Set up frame callback
    m_ffmpegPlayer->SetFrameCallback([this](const wxBitmap& frame) {
        OnFFmpegFrame(frame);
    });

    // Load the video
    if (!m_ffmpegPlayer->LoadFile(path)) {
        MPLOG("PlayVideoWithFFmpeg: failed to load video");
        MarkLoadFailed(path);
        m_ffmpegPlayer.reset();
        FallbackToThumbnail();
        return;
    }

    // Get actual video dimensions and calculate scaled size
    int vidWidth = m_ffmpegPlayer->GetWidth();
    int vidHeight = m_ffmpegPlayer->GetHeight();

    if (vidWidth > 0 && vidHeight > 0) {
        // Scale to fit within max dimensions while preserving aspect ratio
        double scaleX = (double)videoWidth / vidWidth;
        double scaleY = (double)videoHeight / vidHeight;
        double scale = std::min(scaleX, scaleY);

        int scaledWidth = (int)(vidWidth * scale);
        int scaledHeight = (int)(vidHeight * scale);

        // Update render size
        m_ffmpegPlayer->SetRenderSize(scaledWidth, scaledHeight);

        // Use single source of truth for size and position
        int popupWidth = scaledWidth + PADDING * 2 + BORDER_WIDTH * 2;
        int popupHeight = scaledHeight + PADDING * 2 + BORDER_WIDTH * 2 + 24;
        ApplySizeAndPosition(popupWidth, popupHeight);
    } else {
        ApplySizeAndPosition(PHOTO_MAX_WIDTH, PHOTO_MAX_HEIGHT);
    }

    // Start playback
    m_ffmpegPlayer->Play();
    m_isPlayingFFmpeg = true;
    m_hasImage = true;

    // Start animation timer
    int interval = m_ffmpegPlayer->GetTimerIntervalMs();
    m_ffmpegAnimTimer.Start(interval);

    // ApplySizeAndPosition already called Show()
    Refresh();

    MPLOG("PlayVideoWithFFmpeg: playback started, interval=" << interval << "ms");
#else
    // FFmpeg not available, fall back to thumbnail
    FallbackToThumbnail();
#endif
}

void MediaPopup::OnFFmpegAnimTimer(wxTimerEvent& event)
{
#ifdef HAVE_FFMPEG
    if (!m_ffmpegPlayer || !m_isPlayingFFmpeg) {
        m_ffmpegAnimTimer.Stop();
        return;
    }

    if (!m_ffmpegPlayer->AdvanceFrame()) {
        // Video ended (and not looping)
        m_ffmpegAnimTimer.Stop();
        m_isPlayingFFmpeg = false;
        MPLOG("OnFFmpegAnimTimer: video ended");
    }
#endif
}

void MediaPopup::OnFFmpegFrame(const wxBitmap& frame)
{
    if (!frame.IsOk()) return;

    m_bitmap = frame;
    m_hasImage = true;
    Refresh();
}

void MediaPopup::FallbackToThumbnail()
{
    MPLOG("FallbackToThumbnail: thumbnailPath=" << m_mediaInfo.thumbnailPath.ToStdString()
          << " localPath=" << m_mediaInfo.localPath.ToStdString());

    m_loadingTimer.Stop();

    // Try to use the thumbnail from the current media info
    if (!m_mediaInfo.thumbnailPath.IsEmpty() && wxFileExists(m_mediaInfo.thumbnailPath)) {
        MPLOG("FallbackToThumbnail: using thumbnail");
        m_hasError = false;
        m_errorMessage.Clear();
        LoadImageAsync(m_mediaInfo.thumbnailPath);
        // LoadImageAsync -> SetImage -> UpdateSize -> ApplySizeAndPosition handles positioning
        Refresh();
    } else if (!m_mediaInfo.localPath.IsEmpty() && wxFileExists(m_mediaInfo.localPath) &&
               IsSupportedImageFormat(m_mediaInfo.localPath)) {
        // Try to use the local path if it's an image
        MPLOG("FallbackToThumbnail: using localPath as image");
        m_hasError = false;
        m_errorMessage.Clear();
        LoadImageAsync(m_mediaInfo.localPath);
        Refresh();
    } else if (!m_mediaInfo.emoji.IsEmpty()) {
        // Show emoji placeholder
        MPLOG("FallbackToThumbnail: showing emoji placeholder");
        m_hasError = false;
        m_errorMessage.Clear();
        m_hasImage = false;
        ApplySizeAndPosition(200, 150);
        Refresh();
    } else {
        // Show a placeholder indicating video/media
        MPLOG("FallbackToThumbnail: no thumbnail available, showing placeholder");
        m_hasError = false;
        m_errorMessage.Clear();
        m_hasImage = false;
        ApplySizeAndPosition(180, 120);
        Refresh();
    }
}

void MediaPopup::StopVideo()
{
    MPLOG("StopVideo called");

    // Stop the loading timer first
    m_loadingTimer.Stop();

    if (m_mediaCtrl) {
        try {
            // Stop playback
            m_mediaCtrl->Stop();

            // Set volume to 0 to ensure no audio leak
            m_mediaCtrl->SetVolume(0.0);

            // Hide the control
            m_mediaCtrl->Hide();
        } catch (const std::exception& e) {
            MPLOG("StopVideo: exception stopping media: " << e.what());
        } catch (...) {
            MPLOG("StopVideo: unknown exception stopping media");
        }
    }

    m_isPlayingVideo = false;
    m_isLoading = false;
    m_videoPath.Clear();
    m_videoLoadStartTime = 0;

    // Note: Don't stop animated stickers here - they are independent
}

void MediaPopup::OnMediaLoaded(wxMediaEvent& event)
{
    MPLOG("OnMediaLoaded: video ready to play");
    m_loadingTimer.Stop();
    m_isLoading = false;
    m_isPlayingVideo = true;

    // Ensure popup is visible now that video is ready
    // Position should already be set, but ensure we're visible
    if (!IsShown()) {
        // Re-apply size and position to ensure correct placement
        wxSize size = GetSize();
        ApplySizeAndPosition(size.GetWidth(), size.GetHeight());
    }

    if (m_mediaCtrl) {
        // Set volume BEFORE playing - set it to 0 first, then apply desired volume
        // This helps ensure muting works on all platforms
        m_mediaCtrl->SetVolume(0.0);

        // Now set the actual desired volume
        if (m_videoMuted) {
            m_mediaCtrl->SetVolume(0.0);
            MPLOG("OnMediaLoaded: video muted");
        } else {
            m_mediaCtrl->SetVolume(0.5);  // 50% volume for previews
            MPLOG("OnMediaLoaded: video volume set to 0.5");
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
            // Use single source of truth for size and position
            int popupWidth = vidWidth + PADDING * 2 + BORDER_WIDTH * 2;
            int popupHeight = vidHeight + PADDING * 2 + BORDER_WIDTH * 2 + 24;
            ApplySizeAndPosition(popupWidth, popupHeight);
        } else {
            // Even if size didn't change, ensure position is correct
            wxSize size = GetSize();
            ApplySizeAndPosition(size.GetWidth(), size.GetHeight());
        }

        m_mediaCtrl->Play();

        // Re-apply volume after Play() as some backends reset it
        if (m_videoMuted) {
            m_mediaCtrl->SetVolume(0.0);
        }

        // Ensure media control is visible and raised
        m_mediaCtrl->Show();
        m_mediaCtrl->Raise();
    }

    // Final refresh and raise to ensure visibility
    Refresh();
    Update();
    Raise();
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
    MPLOG("OnMediaStop called");
}

void MediaPopup::OnMediaStateChanged(wxMediaEvent& event)
{
    // Handle state changes for better error detection
    if (!m_mediaCtrl) return;

    wxMediaState state = m_mediaCtrl->GetState();
    MPLOG("OnMediaStateChanged: state=" << static_cast<int>(state));

    // If we were loading and state changed to stopped, it might be an error
    if (m_isLoading && state == wxMEDIASTATE_STOPPED) {
        // Check if we've been loading for too long without success
        int64_t elapsed = wxGetUTCTimeMillis().GetValue() - m_videoLoadStartTime;
        if (elapsed > 2000) {  // If stopped after 2 seconds of loading, probably failed
            MPLOG("OnMediaStateChanged: video loading seems to have failed, falling back");
            m_loadingTimer.Stop();
            m_isLoading = false;
            if (!m_videoPath.IsEmpty()) {
                MarkLoadFailed(m_videoPath);
            }
            m_mediaCtrl->Hide();
            FallbackToThumbnail();
        }
    }
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

    // Calculate size and apply position - this is the single source of truth
    int width = m_bitmap.GetWidth() + (PADDING * 2) + (BORDER_WIDTH * 2);
    int height = m_bitmap.GetHeight() + (PADDING * 2) + (BORDER_WIDTH * 2) + 24;
    ApplySizeAndPosition(width, height);
    
    Refresh();
    MPLOG("SetImage: after ApplySizeAndPosition - pos=" << GetPosition().x << "," << GetPosition().y 
          << " size=" << GetSize().GetWidth() << "," << GetSize().GetHeight());
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
    m_loadingFrame++; // Increment frame count (modulo used in OnPaint)

    // Check for video timeout using actual elapsed time (more reliable)
    // When loading video, m_mediaCtrl is set but m_isPlayingVideo is false
    if (m_isLoading && m_mediaCtrl && !m_isPlayingVideo) {
        int64_t elapsed = wxGetUTCTimeMillis().GetValue() - m_videoLoadStartTime;
        if (elapsed > VIDEO_LOAD_TIMEOUT_MS) {
            MPLOG("OnLoadingTimer: video load timed out after " << elapsed << "ms, falling back to thumbnail");
            m_loadingTimer.Stop();
            // Mark this video as failed to prevent repeated attempts
            if (!m_videoPath.IsEmpty()) {
                MarkLoadFailed(m_videoPath);
            }
            StopVideo();
            FallbackToThumbnail();
            return;
        }
        // Continue running timer even if IsShown() is false (video loads in background/separate window)
    } else if (!IsShown() && !m_isDownloadingMedia) {
        // Stop timer if popup is hidden and we're not waiting for video load or download
        m_loadingTimer.Stop();
        return;
    }

    // Refresh when loading or downloading to animate the spinner
    if (IsShown() && (m_isLoading || m_isDownloadingMedia)) {
        Refresh();
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

void MediaPopup::AdjustPositionToScreen(const wxPoint& pos)
{
    // Store the original position for future adjustments (e.g., after size changes)
    m_originalPosition = pos;

    MPLOG("AdjustPositionToScreen: storing originalPosition=(" << pos.x << "," << pos.y << ")");
    
    // Note: This just stores the position. ApplySizeAndPosition should be called
    // separately to actually apply the size and position.
}

// Single source of truth for setting size and position with GTK workaround
void MediaPopup::ApplySizeAndPosition(int width, int height)
{
    MPLOG("ApplySizeAndPosition: requested size=(" << width << "," << height << ")"
          << " m_originalPosition=(" << m_originalPosition.x << "," << m_originalPosition.y << ")");

    // Calculate the target position
    // Add a small offset so popup appears below the cursor, not overlapping it
    const int CURSOR_OFFSET = 20;
    wxPoint targetPos = m_originalPosition;
    bool isShowingBelow = false;
    
    if (m_originalPosition.x != 0 || m_originalPosition.y != 0) {
        // Get the display that contains this point
        int displayIndex = wxDisplay::GetFromPoint(m_originalPosition);
        if (displayIndex == wxNOT_FOUND) {
            displayIndex = 0;
        }

        wxDisplay display(displayIndex);
        wxRect screenRect = display.GetClientArea();
        
        // Use parent window bottom as the effective screen bottom
        // This is more reliable than guessing taskbar height
        int effectiveScreenBottom = screenRect.GetBottom();
        
        // Get parent window to use its bottom as boundary
        wxWindow* parent = GetParent();
        if (parent) {
            wxPoint parentPos = parent->GetScreenPosition();
            wxSize parentSize = parent->GetSize();
            int parentBottom = parentPos.y + parentSize.GetHeight();
            // Use the smaller of screen bottom and parent bottom
            effectiveScreenBottom = std::min(effectiveScreenBottom, parentBottom);
            MPLOG("ApplySizeAndPosition: parentPos=(" << parentPos.x << "," << parentPos.y << ")"
                  << " parentSize=(" << parentSize.GetWidth() << "," << parentSize.GetHeight() << ")"
                  << " parentBottom=" << parentBottom);
        }
        
        // Add a small margin to ensure popup doesn't touch the edge
        effectiveScreenBottom -= 10;

        MPLOG("ApplySizeAndPosition: size=(" << width << "," << height << ")"
              << " originalPos=(" << m_originalPosition.x << "," << m_originalPosition.y << ")"
              << " screenBottom=" << screenRect.GetBottom()
              << " effectiveScreenBottom=" << effectiveScreenBottom);

    // Adjust horizontal
        if (targetPos.x + width > screenRect.GetRight()) {
            targetPos.x = screenRect.GetRight() - width;
        }
        if (targetPos.x < screenRect.GetLeft()) {
            targetPos.x = screenRect.GetLeft();
        }

        // Calculate available space above and below cursor
        int spaceBelow = effectiveScreenBottom - m_originalPosition.y;
        int spaceAbove = m_originalPosition.y - screenRect.GetTop() - 5; // 5px margin from cursor
        
        MPLOG("ApplySizeAndPosition: spaceBelow=" << spaceBelow << " spaceAbove=" << spaceAbove << " height=" << height);
        
        // Decide whether to show above or below based on available space
        if (height + CURSOR_OFFSET <= spaceBelow) {
            // Fits below
            targetPos.y = m_originalPosition.y + CURSOR_OFFSET;
            isShowingBelow = true;
        } else if (height <= spaceAbove) {
            // Fits above
            targetPos.y = m_originalPosition.y - height - 5;
            isShowingBelow = false;
        } else {
            // Doesn't fit perfectly in either. Pick the one with MORE space.
            if (spaceAbove > spaceBelow) {
                 // Show above, clamping top to screen edge
                 targetPos.y = screenRect.GetTop() + 5; // minimal margin from top
                 isShowingBelow = false;
                 // If it STILL doesn't fit, we might need to clamp height, but for now just clamp position
            } else {
                 // Show below, clamping bottom to effective bottom not strictly enforced yet
                 targetPos.y = m_originalPosition.y + CURSOR_OFFSET;
                 isShowingBelow = true;
            }
        }
        
        // Final sanity check for vertical bounds
        if (targetPos.y < screenRect.GetTop()) {
            targetPos.y = screenRect.GetTop();
        }
        // If the bottom goes off-screen, shift it up if possible
        if (targetPos.y + height > effectiveScreenBottom) {
             int overshoot = (targetPos.y + height) - effectiveScreenBottom;
             // Only shift up if we don't go off the top
             if (targetPos.y - overshoot >= screenRect.GetTop()) {
                 targetPos.y -= overshoot;
             } else {
                 // If we have to clip, simply align to top to show as much as possible
                 targetPos.y = screenRect.GetTop();
             }
        }
    }

    MPLOG("ApplySizeAndPosition: FINAL target=(" << targetPos.x << "," << targetPos.y << "," << width << "," << height << ")");
    
    // On GTK, popup windows are notoriously difficult to reposition and resize.
    // The most reliable sequence is to hide, set new geometry, and then show again,
    // followed by a forced layout and refresh.
    Hide();

    // On GTK, when showing below, the resize sometimes fails. A more aggressive
    // repositioning sequence helps the window manager realize the geometry has changed.
    if (isShowingBelow) {
        MPLOG("ApplySizeAndPosition: using aggressive repositioning for 'below' case");
        // Move to a temporary, invalid position to force a full re-evaluation
        Move(-1000, -1000);
        wxYield(); // Allow GTK to process the move
    }
    
    // Use the atomic SetSize(x,y,w,h) form which sets position and size together
    SetSize(targetPos.x, targetPos.y, width, height);

    // Now show the window at its new size and position
    Show();
    Raise();
    
    // Force a layout and full refresh to ensure content is drawn correctly in the new size
    Layout();
    Refresh(true); // Erase background
    Update();      // Force immediate repaint
    
    wxPoint actualPos = GetPosition();
    wxSize actualSize = GetSize();
    MPLOG("ApplySizeAndPosition: after all - pos=(" << actualPos.x << "," << actualPos.y << ")"
          << " size=(" << actualSize.GetWidth() << "," << actualSize.GetHeight() << ")"
          << " IsShown=" << IsShown());
}

void MediaPopup::UpdateSize()
{
    int width = MIN_WIDTH;
    int height = MIN_HEIGHT;

    if (m_isPlayingVideo && m_mediaCtrl && m_mediaCtrl->IsShown()) {
        // Size already set by video playback, but still apply position
        wxSize size = GetSize();
        ApplySizeAndPosition(size.GetWidth(), size.GetHeight());
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

    MPLOG("UpdateSize: calculated size=(" << width << "," << height << ")");

    // Use single source of truth for size and position
    ApplySizeAndPosition(width, height);
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

    // If video is playing via wxMediaCtrl, the media control handles the display
    if (m_isPlayingVideo && m_mediaCtrl && m_mediaCtrl->IsShown()) {
        // Draw the label at the bottom
        DrawMediaLabel(dc, size);
        return;
    }

    // If video is playing via FFmpeg, draw the frame without any overlay
    if (m_isPlayingFFmpeg && m_hasImage && m_bitmap.IsOk()) {
        // Draw the current video frame
        int imgX = contentX + (contentWidth - m_bitmap.GetWidth()) / 2;
        int imgY = contentY;
        dc.DrawBitmap(m_bitmap, imgX, imgY, true);
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

        // If downloading, show downloading overlay for ALL media types
        if (m_isDownloadingMedia || (isShowingThumbnail && needsDownload)) {
            // Draw semi-transparent dark overlay over the whole image
            dc.SetBrush(wxBrush(wxColour(0, 0, 0, 150)));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(imgX, imgY, m_bitmap.GetWidth(), m_bitmap.GetHeight());

            // Draw downloading circle background
            dc.SetBrush(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT)));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawCircle(centerX, centerY, radius);

            // Draw animated spinner
            static const wxString spinnerChars[] = {"|", "/", "-", "\\", "|", "/", "-", "\\"};
            wxString spinner = spinnerChars[m_loadingFrame % 8];
            dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Bold());
            dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT));
            wxSize spinnerSize = dc.GetTextExtent(spinner);
            dc.DrawText(spinner, centerX - spinnerSize.GetWidth() / 2, centerY - spinnerSize.GetHeight() / 2);

            // Draw "Downloading..." text below
            dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Italic());
            dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT));
            wxString statusText = "Downloading...";
            wxSize statusSize = dc.GetTextExtent(statusText);
            dc.DrawText(statusText, centerX - statusSize.GetWidth() / 2, centerY + radius + 8);
        } else if (isVideoType) {
            // Not downloading - show play button for video types
            // Draw semi-transparent dark overlay behind play button
            dc.SetBrush(wxBrush(wxColour(0, 0, 0, 100)));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawCircle(centerX, centerY, radius + 4);

            // Draw play button circle
            dc.SetBrush(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT)));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawCircle(centerX, centerY, radius);

            // Draw play triangle (file is available, click to open)
            wxPoint triangle[3];
            triangle[0] = wxPoint(centerX - 6, centerY - 10);
            triangle[1] = wxPoint(centerX - 6, centerY + 10);
            triangle[2] = wxPoint(centerX + 10, centerY);
            dc.SetBrush(wxBrush(wxColour(255, 255, 255)));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawPolygon(3, triangle);

            // Draw hint text
            dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Italic());
            dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
            wxString hint = "Click to play";
            wxSize hintSize = dc.GetTextExtent(hint);
            dc.DrawText(hint, centerX - hintSize.GetWidth() / 2, centerY + radius + 8);
        }

        // Draw the label at the bottom
        DrawMediaLabel(dc, size);

    } else if (m_hasError) {
        // Error state
        dc.SetTextForeground(wxColour(0xCC, 0x00, 0x00)); // Red for errors (semantic color)
        dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));

        wxString errorText = m_errorMessage.IsEmpty() ? "Error loading media" : m_errorMessage;
        wxSize textSize = dc.GetTextExtent(errorText);
        int textX = (size.GetWidth() - textSize.GetWidth()) / 2;
        int textY = (size.GetHeight() - textSize.GetHeight()) / 2;
        dc.DrawText(errorText, textX, textY);

    } else if (m_isLoading || m_isDownloadingMedia) {
        // Loading/Downloading state - show animated spinner with "Downloading..."
        static const wxString spinners[] = {"|", "/", "-", "\\", "|", "/", "-", "\\"};
        wxString spinner = spinners[m_loadingFrame % 8];

        // Draw a circle background for the spinner
        int centerX = size.GetWidth() / 2;
        int centerY = contentY + 40;
        int radius = 28;

        dc.SetBrush(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT)));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawCircle(centerX, centerY, radius);

        dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Bold().Scaled(2.0));
        dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT));

        wxSize spinnerSize = dc.GetTextExtent(spinner);
        dc.DrawText(spinner, centerX - spinnerSize.GetWidth() / 2, centerY - spinnerSize.GetHeight() / 2);

        // Draw "Downloading..." text below
        dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
        dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT));
        wxString statusText = "Downloading...";
        wxSize statusSize = dc.GetTextExtent(statusText);
        int statusX = (size.GetWidth() - statusSize.GetWidth()) / 2;
        int statusY = centerY + radius + 10;
        dc.DrawText(statusText, statusX, statusY);

        // Draw the label at the bottom
        DrawMediaLabel(dc, size);

    } else {
        // Placeholder state - show media type icon and info
        wxString icon = GetMediaIcon();

        // For stickers, show the emoji much larger
        double scaleFactor = 3.0;
        if (m_mediaInfo.type == MediaType::Sticker && !m_mediaInfo.emoji.IsEmpty()) {
            scaleFactor = 5.0;  // Larger emoji for stickers
            icon = m_mediaInfo.emoji;
        }

        // Draw large emoji icon
        dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Scaled(scaleFactor));
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

        dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Bold());
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
            dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Smaller());
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
    dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));

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
        dc.SetFont(wxSystemSettings::GetFont(wxSYS_ANSI_FIXED_FONT).Smaller().Italic());

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

// Async image loading helpers
void MediaPopup::LoadImageAsync(const wxString& path)
{
    if (path.IsEmpty() || !wxFileExists(path)) {
        MPLOG("LoadImageAsync: invalid path");
        return;
    }

    // Check if this file has failed recently
    if (HasFailedRecently(path)) {
        MPLOG("LoadImageAsync: skipping recently failed path: " << path.ToStdString());
        FallbackToThumbnail();
        return;
    }

    m_pendingImagePath = path;

    // Offload loading to a background thread to prevent UI freeze
    std::thread([this, path]() {
        wxImage image;
        bool success = LoadImageWithWebPSupport(path, image) && image.IsOk();

        wxThreadEvent* event = new wxThreadEvent(wxEVT_IMAGE_LOADED);
        event->SetString(path); // Pass path to verify relevance

        if (success) {
            event->SetPayload(image);
            event->SetInt(1);
        } else {
            event->SetInt(0);
        }

        wxQueueEvent(this, event);
    }).detach();
}

void MediaPopup::OnImageLoaded(wxThreadEvent& event)
{
    wxString path = event.GetString();

    // Ignore stale events (user switched to another media)
    if (!m_pendingImagePath.IsEmpty() && path != m_pendingImagePath) {
        return;
    }

    if (event.GetInt() == 1) {
        wxImage image = event.GetPayload<wxImage>();
        SetImage(image);
    } else {
        MPLOG("OnImageLoaded: failed to load image: " << path.ToStdString());
        MarkLoadFailed(path);
        FallbackToThumbnail();
    }
}

void MediaPopup::OnAsyncLoadTimer(wxTimerEvent& event)
{
    if (!m_asyncLoadPending || m_pendingImagePath.IsEmpty()) {
        return;
    }

    wxString path = m_pendingImagePath;
    m_pendingImagePath.Clear();
    m_asyncLoadPending = false;

    // Perform the actual load
    wxImage image;
    if (LoadImageWithWebPSupport(path, image) && image.IsOk()) {
        SetImage(image);
    } else {
        MPLOG("OnAsyncLoadTimer: failed to load image: " << path.ToStdString());
        MarkLoadFailed(path);
        FallbackToThumbnail();
    }
}

bool MediaPopup::HasFailedRecently(const wxString& path) const
{
    return m_failedLoads.find(path) != m_failedLoads.end();
}

void MediaPopup::MarkLoadFailed(const wxString& path)
{
    if (!path.IsEmpty()) {
        m_failedLoads.insert(path);
        MPLOG("MarkLoadFailed: " << path.ToStdString() << " (total failures: " << m_failedLoads.size() << ")");

        // Limit the size of the failed loads set to prevent memory growth
        if (m_failedLoads.size() > 100) {
            // Remove oldest entries (just clear and start fresh)
            m_failedLoads.clear();
        }
    }
}

void MediaPopup::ClearFailedLoads()
{
    m_failedLoads.clear();
}
