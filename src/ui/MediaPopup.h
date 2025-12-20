#ifndef MEDIAPOPUP_H
#define MEDIAPOPUP_H

#include <wx/wx.h>
#include <wx/popupwin.h>
#include <wx/dcbuffer.h>
#include <wx/mediactrl.h>
#include "MediaTypes.h"

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
    
    const MediaInfo& GetMediaInfo() const { return m_mediaInfo; }
    
protected:
    void OnPaint(wxPaintEvent& event);
    void OnMediaLoaded(wxMediaEvent& event);
    void OnMediaFinished(wxMediaEvent& event);
    void OnMediaStop(wxMediaEvent& event);
    
private:
    void UpdateSize();
    void ApplyHexChatStyle();
    wxString GetMediaLabel() const;
    wxString GetMediaIcon() const;
    void CreateMediaCtrl();
    void DestroyMediaCtrl();
    
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
    
    // Video/GIF playback
    wxMediaCtrl* m_mediaCtrl;
    bool m_isPlayingVideo;
    bool m_loopVideo;
    bool m_videoMuted;
    wxString m_videoPath;
    
    static constexpr int MAX_WIDTH = 250;
    static constexpr int MAX_HEIGHT = 200;
    static constexpr int MIN_WIDTH = 120;
    static constexpr int MIN_HEIGHT = 70;
    static constexpr int PADDING = 8;
    static constexpr int BORDER_WIDTH = 1;
    
    wxDECLARE_EVENT_TABLE();
};

#endif // MEDIAPOPUP_H