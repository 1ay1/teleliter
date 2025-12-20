#include "FileUtils.h"

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
        return FileMediaType::Document;
    }
    
    ext = "." + ext;
    
    const wxArrayString& imageExts = GetImageExtensions();
    for (const auto& e : imageExts) {
        if (ext == e) {
            return FileMediaType::Image;
        }
    }
    
    const wxArrayString& videoExts = GetVideoExtensions();
    for (const auto& e : videoExts) {
        if (ext == e) {
            return FileMediaType::Video;
        }
    }
    
    const wxArrayString& audioExts = GetAudioExtensions();
    for (const auto& e : audioExts) {
        if (ext == e) {
            return FileMediaType::Audio;
        }
    }
    
    return FileMediaType::Document;
}