#ifndef FILEDROPTARGET_H
#define FILEDROPTARGET_H

#include <wx/wx.h>
#include <wx/dnd.h>
#include <functional>
#include "FileUtils.h"

// Forward declaration
class MainFrame;

// Callback type for when files are dropped
using FileDropCallback = std::function<void(const wxArrayString& files)>;

// HexChat-style file drop target for uploading media
class FileDropTarget : public wxFileDropTarget
{
public:
    FileDropTarget(FileDropCallback callback);
    virtual ~FileDropTarget() = default;
    
    virtual bool OnDropFiles(wxCoord x, wxCoord y, const wxArrayString& filenames) override;
    virtual wxDragResult OnDragOver(wxCoord x, wxCoord y, wxDragResult defResult) override;
    virtual wxDragResult OnEnter(wxCoord x, wxCoord y, wxDragResult defResult) override;
    virtual void OnLeave() override;
    
private:
    bool IsSupportedFile(const wxString& filename) const;
    wxString GetFileType(const wxString& filename) const;
    

    FileDropCallback m_callback;
};

#endif // FILEDROPTARGET_H