#ifndef MEDIAPOPUP_H
#define MEDIAPOPUP_H

#include <wx/wx.h>
#include <wx/popupwin.h>
#include <wx/dcbuffer.h>
#include <wx/mediactrl.h>
#include <wx/timer.h>
#include "MediaTypes.h"
#include <memory>

// Forward declarations
class LottiePlayer;
class WebmPlayer;

// Timer IDs
static const int LOADING_TIMER_ID = 10001;
static const int LOTTIE_ANIM_TIMER_ID = 10002;
static const int WEBM_ANIM_TIMER_ID = 10003;

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
    
protected:
    void OnPaint(wxPaintEvent& event);
    void OnMediaLoaded(wxMediaEvent& event);
    void OnMediaFinished(wxMediaEvent& event);
    void OnMediaStop(wxMediaEvent& event);
    void OnLoadingTimer(wxTimerEvent& event);
    void OnLottieAnimTimer(wxTimerEvent& event);
    void OnLottieFrame(const wxBitmap& frame);
    void OnWebmAnimTimer(wxTimerEvent& event);
    void OnWebmFrame(const wxBitmap& frame);
    
private:
    void UpdateSize();
    void ApplyHexChatStyle();
    wxString GetMediaLabel() const;
    wxString GetMediaIcon() const;
    void CreateMediaCtrl();
    void DestroyMediaCtrl();
    void FallbackToThumbnail();
    void DrawMediaLabel(wxDC& dc, const wxSize& size);
    bool IsSameMedia(const MediaInfo& a, const MediaInfo& b) const;
    
    wxColour m_bgColor;
    wxColour m_borderColor;
    wxColour m_textColor;
    wxColour m_labelColor;
    
    MediaInfo m_mediaInfo;
    wxBitmap m_bitmap;
    bool m_hasImage;
    bool m_isLoading;
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
    
    // Animated sticker playback (Lottie .tgs)
    std::unique_ptr<LottiePlayer> m_lottiePlayer;
    bool m_isPlayingSticker;
    wxTimer m_lottieAnimTimer;
    
    // WebM video sticker playback
    std::unique_ptr<WebmPlayer> m_webmPlayer;
    bool m_isPlayingWebm;
    wxTimer m_webmAnimTimer;
    
    static constexpr int MAX_WIDTH = 250;
    static constexpr int MAX_HEIGHT = 200;
    static constexpr int MIN_WIDTH = 120;
    static constexpr int MIN_HEIGHT = 70;
    static constexpr int PADDING = 8;
    static constexpr int BORDER_WIDTH = 1;
    
    wxDECLARE_EVENT_TABLE();
};

#endif // MEDIAPOPUP_H