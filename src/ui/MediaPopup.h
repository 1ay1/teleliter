#ifndef MEDIAPOPUP_H
#define MEDIAPOPUP_H

#include <wx/wx.h>
#include <wx/popupwin.h>
#include <wx/dcbuffer.h>
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
    
    const MediaInfo& GetMediaInfo() const { return m_mediaInfo; }
    
protected:
    void OnPaint(wxPaintEvent& event);
    
private:
    void UpdateSize();
    void ApplyHexChatStyle();
    wxString GetMediaLabel() const;
    
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
    
    static constexpr int MAX_WIDTH = 400;
    static constexpr int MAX_HEIGHT = 300;
    static constexpr int MIN_WIDTH = 150;
    static constexpr int MIN_HEIGHT = 80;
    static constexpr int PADDING = 8;
    static constexpr int BORDER_WIDTH = 1;
    
    wxDECLARE_EVENT_TABLE();
};

#endif // MEDIAPOPUP_H