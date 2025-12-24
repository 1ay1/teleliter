#ifndef MEDIAPOPUP_H
#define MEDIAPOPUP_H

#include <wx/wx.h>
#include <wx/popupwin.h>
#include <wx/dcbuffer.h>
#include <wx/mediactrl.h>
#include <wx/timer.h>
#include "MediaTypes.h"
#include <memory>
#include <functional>
#include <set>

// Forward declarations
class LottiePlayer;
class WebmPlayer;
class FFmpegPlayer;

// Timer IDs
static const int LOADING_TIMER_ID = 10001;
static const int LOTTIE_ANIM_TIMER_ID = 10002;
static const int FFMPEG_ANIM_TIMER_ID = 10006;
static const int WEBM_ANIM_TIMER_ID = 10003;
static const int VIDEO_LOAD_TIMER_ID = 10004;
static const int ASYNC_LOAD_TIMER_ID = 10005;

// Maximum time to wait for video to load before falling back to thumbnail
static const int VIDEO_LOAD_TIMEOUT_MS = 8000;

// Maximum number of failed load attempts before giving up on a file
static const int MAX_LOAD_FAILURES = 3;

// HexChat-style popup for media preview
class MediaPopup : public wxPopupWindow
{
public:
    MediaPopup(wxWindow* parent);
    virtual ~MediaPopup();

    void ShowMedia(const MediaInfo& info, const wxPoint& pos);
    void SetImage(const wxImage& image);
    void SetImage(const wxString& path);
    void ShowLoading();
    void ShowError(const wxString& message);

    // Video/GIF playback
    void PlayVideo(const wxString& path, bool loop = false, bool muted = true);
    void StopVideo();
    bool IsPlayingVideo() const { return m_isPlayingVideo; }

    // Animated sticker (.tgs) playback
    void PlayAnimatedSticker(const wxString& path);
    void StopAnimatedSticker();
    bool IsPlayingAnimatedSticker() const { return m_isPlayingSticker; }

    // WebM video sticker playback
    void PlayWebmSticker(const wxString& path);
    void StopWebmSticker();
    bool IsPlayingWebmSticker() const { return m_isPlayingWebm; }

    // Stop all playback (video, lottie, webm) - call before switching media or hiding
    void StopAllPlayback();

    const MediaInfo& GetMediaInfo() const { return m_mediaInfo; }

    // Set parent window bottom bound for positioning
    void SetParentBottom(int bottom) { m_parentBottom = bottom; }

    // Set callback for when popup is clicked
    void SetClickCallback(std::function<void(const MediaInfo&)> callback) { m_clickCallback = callback; }

protected:
    void OnPaint(wxPaintEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnMediaLoaded(wxMediaEvent& event);
    void OnMediaFinished(wxMediaEvent& event);
    void OnMediaStop(wxMediaEvent& event);
    void OnMediaStateChanged(wxMediaEvent& event);
    void OnLoadingTimer(wxTimerEvent& event);
    void OnVideoLoadTimer(wxTimerEvent& event);
    void OnLottieAnimTimer(wxTimerEvent& event);
    void OnLottieFrame(const wxBitmap& frame);
    void OnWebmAnimTimer(wxTimerEvent& event);
    void OnWebmFrame(const wxBitmap& frame);
    void OnFFmpegAnimTimer(wxTimerEvent& event);
    void OnFFmpegFrame(const wxBitmap& frame);

private:
    void UpdateSize();
    void AdjustPositionToScreen(const wxPoint& pos);
    void ApplySizeAndPosition(int width, int height);  // Single source of truth for size + position
    void ApplyHexChatStyle();
    wxString GetMediaLabel() const;
    wxString GetMediaIcon() const;
    void CreateMediaCtrl();
    void DestroyMediaCtrl();
    void FallbackToThumbnail();
    void PlayVideoWithFFmpeg(const wxString& path, bool loop, bool muted);
    void DrawMediaLabel(wxDC& dc, const wxSize& size);
    bool IsSameMedia(const MediaInfo& a, const MediaInfo& b) const;

    // Async image loading to prevent UI blocking
    void LoadImageAsync(const wxString& path);
    void OnAsyncLoadTimer(wxTimerEvent& event);
    void OnImageLoaded(wxThreadEvent& event);

    // Async video loading to prevent UI blocking
    void LoadVideoAsync(const wxString& path, bool loop, bool muted);
    void OnVideoLoaded(wxThreadEvent& event);

    // Track failed loads to avoid repeated attempts
    bool HasFailedRecently(const wxString& path) const;
    void MarkLoadFailed(const wxString& path);
    void ClearFailedLoads();

    wxColour m_bgColor;
    wxColour m_borderColor;
    wxColour m_textColor;
    wxColour m_labelColor;

    MediaInfo m_mediaInfo;
    wxBitmap m_bitmap;
    bool m_hasImage;
    bool m_isLoading;
    bool m_isDownloadingMedia;  // True when showing thumbnail with download in progress
    bool m_hasError;
    wxString m_errorMessage;

    // Loading animation
    wxTimer m_loadingTimer;
    int m_loadingFrame;

    // Video/GIF playback
    wxMediaCtrl* m_mediaCtrl;
    bool m_isPlayingVideo;
    bool m_loopVideo;
    bool m_videoMuted;
    wxString m_videoPath;
    wxTimer m_videoLoadTimer;

    // Animated sticker playback (Lottie .tgs)
    std::unique_ptr<LottiePlayer> m_lottiePlayer;
    bool m_isPlayingSticker;
    wxTimer m_lottieAnimTimer;

    // WebM video sticker playback
    std::unique_ptr<WebmPlayer> m_webmPlayer;
    bool m_isPlayingWebm;
    wxTimer m_webmAnimTimer;

    // FFmpeg video playback (Linux)
    std::unique_ptr<FFmpegPlayer> m_ffmpegPlayer;
    bool m_isPlayingFFmpeg;
    wxTimer m_ffmpegAnimTimer;

    // Click callback
    std::function<void(const MediaInfo&)> m_clickCallback;

    // Async image loading state
    wxString m_pendingImagePath;
    wxTimer m_asyncLoadTimer;
    bool m_asyncLoadPending;

    // Async video loading state
    wxString m_pendingVideoPath;
    bool m_pendingVideoLoop;
    bool m_pendingVideoMuted;

    // Store original position for screen bounds adjustment
    wxPoint m_originalPosition;
    
    // Parent window bottom bound (in screen coordinates)
    int m_parentBottom;

    // Track files that failed to load (to avoid repeated attempts)
    std::set<wxString> m_failedLoads;

    // Video load start time for timeout detection
    int64_t m_videoLoadStartTime;

    // Size constraints for stickers/emojis (smaller)
    static constexpr int STICKER_MAX_WIDTH = 180;
    static constexpr int STICKER_MAX_HEIGHT = 150;

    // Size constraints for photos/videos (compact preview)
    static constexpr int PHOTO_MAX_WIDTH = 300;
    static constexpr int PHOTO_MAX_HEIGHT = 240;

    static constexpr int MIN_WIDTH = 100;
    static constexpr int MIN_HEIGHT = 60;
    static constexpr int PADDING = 8;
    static constexpr int BORDER_WIDTH = 1;

    wxDECLARE_EVENT_TABLE();
};

#endif // MEDIAPOPUP_H
