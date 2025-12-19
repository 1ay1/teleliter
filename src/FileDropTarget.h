#ifndef FILEDROPTARGET_H
#define FILEDROPTARGET_H

#include <wx/wx.h>
#include <wx/dnd.h>
#include <functional>

// Forward declaration
class MainFrame;

// Callback type for when files are dropped
using FileDropCallback = std::function<void(const wxArrayString& files)>;

// Utility to determine media type from file extension
enum class FileMediaType {
    Image,
    Video,
    Audio,
    Document,
    Unknown
};

// Utility functions
wxString FormatFileSize(wxULongLong bytes);
FileMediaType GetMediaTypeFromExtension(const wxString& filename);
const wxArrayString& GetImageExtensions();
const wxArrayString& GetVideoExtensions();
const wxArrayString& GetAudioExtensions();

// HexChat-style file drop target for uploading media
class FileDropTarget : public wxFileDropTarget
{
public:
    FileDropTarget(MainFrame* frame, FileDropCallback callback);
    virtual ~FileDropTarget() = default;
    
    // Called when files are dropped
    virtual bool OnDropFiles(wxCoord x, wxCoord y, const wxArrayString& filenames) override;
    
    // Called when dragging over the window
    virtual wxDragResult OnDragOver(wxCoord x, wxCoord y, wxDragResult defResult) override;
    
    // Called when drag enters the window
    virtual wxDragResult OnEnter(wxCoord x, wxCoord y, wxDragResult defResult) override;
    
    // Called when drag leaves the window
    virtual void OnLeave() override;
    
private:
    // Check if file is a supported media type
    bool IsSupportedFile(const wxString& filename) const;
    wxString GetFileType(const wxString& filename) const;
    
    MainFrame* m_frame;
    FileDropCallback m_callback;
};

#endif // FILEDROPTARGET_H