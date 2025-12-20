#include "FileDropTarget.h"
FileDropTarget::FileDropTarget(FileDropCallback callback)
    : m_callback(callback)
{
}

bool FileDropTarget::OnDropFiles(wxCoord x, wxCoord y, const wxArrayString& filenames)
{
    if (filenames.IsEmpty()) {
        return false;
    }
    
    // Filter to only supported files
    wxArrayString supportedFiles;
    for (const auto& file : filenames) {
        if (IsSupportedFile(file)) {
            supportedFiles.Add(file);
        }
    }
    
    if (supportedFiles.IsEmpty()) {
        wxMessageBox("No supported files found.\n\nSupported types: images, videos, audio, documents",
                     "Unsupported File", wxOK | wxICON_WARNING);
        return false;
    }
    
    // Call the callback with the files
    if (m_callback) {
        m_callback(supportedFiles);
    }
    
    return true;
}

wxDragResult FileDropTarget::OnDragOver(wxCoord x, wxCoord y, wxDragResult defResult)
{
    return wxDragCopy;
}

wxDragResult FileDropTarget::OnEnter(wxCoord x, wxCoord y, wxDragResult defResult)
{
    return wxDragCopy;
}

void FileDropTarget::OnLeave()
{
    // Nothing special needed
}

bool FileDropTarget::IsSupportedFile(const wxString& filename) const
{
    return GetMediaTypeFromExtension(filename) != FileMediaType::Unknown;
}

wxString FileDropTarget::GetFileType(const wxString& filename) const
{
    FileMediaType type = GetMediaTypeFromExtension(filename);
    switch (type) {
        case FileMediaType::Image:
            return "Photo";
        case FileMediaType::Video:
            return "Video";
        case FileMediaType::Audio:
            return "Audio";
        case FileMediaType::Document:
            return "File";
        default:
            return "File";
    }
}