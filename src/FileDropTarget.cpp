#include "FileDropTarget.h"
#include "MainFrame.h"

// Utility functions (free functions, not class members)

const wxArrayString& GetImageExtensions()
{
    static wxArrayString extensions;
    if (extensions.IsEmpty()) {
        extensions.Add(".jpg");
        extensions.Add(".jpeg");
        extensions.Add(".png");
        extensions.Add(".gif");
        extensions.Add(".webp");
        extensions.Add(".bmp");
        extensions.Add(".tiff");
        extensions.Add(".tif");
    }
    return extensions;
}

const wxArrayString& GetVideoExtensions()
{
    static wxArrayString extensions;
    if (extensions.IsEmpty()) {
        extensions.Add(".mp4");
        extensions.Add(".mkv");
        extensions.Add(".avi");
        extensions.Add(".mov");
        extensions.Add(".webm");
        extensions.Add(".m4v");
        extensions.Add(".wmv");
        extensions.Add(".flv");
    }
    return extensions;
}

const wxArrayString& GetAudioExtensions()
{
    static wxArrayString extensions;
    if (extensions.IsEmpty()) {
        extensions.Add(".mp3");
        extensions.Add(".ogg");
        extensions.Add(".wav");
        extensions.Add(".flac");
        extensions.Add(".m4a");
        extensions.Add(".aac");
        extensions.Add(".wma");
        extensions.Add(".opus");
    }
    return extensions;
}

wxString FormatFileSize(wxULongLong bytes)
{
    double size = bytes.ToDouble();
    
    if (size >= 1024.0 * 1024.0 * 1024.0) {
        return wxString::Format("%.2f GB", size / (1024.0 * 1024.0 * 1024.0));
    } else if (size >= 1024.0 * 1024.0) {
        return wxString::Format("%.2f MB", size / (1024.0 * 1024.0));
    } else if (size >= 1024.0) {
        return wxString::Format("%.1f KB", size / 1024.0);
    } else {
        return wxString::Format("%.0f bytes", size);
    }
}

FileMediaType GetMediaTypeFromExtension(const wxString& filename)
{
    wxString ext = filename.AfterLast('.').Lower();
    if (ext.IsEmpty() || ext == filename.Lower()) {
        // No extension, treat as document
        return FileMediaType::Document;
    }
    
    ext = "." + ext;
    
    // Check image extensions
    const wxArrayString& imageExts = GetImageExtensions();
    for (const auto& e : imageExts) {
        if (ext == e) {
            return FileMediaType::Image;
        }
    }
    
    // Check video extensions
    const wxArrayString& videoExts = GetVideoExtensions();
    for (const auto& e : videoExts) {
        if (ext == e) {
            return FileMediaType::Video;
        }
    }
    
    // Check audio extensions
    const wxArrayString& audioExts = GetAudioExtensions();
    for (const auto& e : audioExts) {
        if (ext == e) {
            return FileMediaType::Audio;
        }
    }
    
    // Default to document for any other file type
    return FileMediaType::Document;
}

// FileDropTarget class implementation

FileDropTarget::FileDropTarget(MainFrame* frame, FileDropCallback callback)
    : m_frame(frame),
      m_callback(callback)
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