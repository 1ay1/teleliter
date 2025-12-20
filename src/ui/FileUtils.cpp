#include "FileUtils.h"
#include <wx/filename.h>

#ifdef HAVE_WEBP
#include <webp/decode.h>
#include <fstream>
#include <vector>
#endif

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

bool HasWebPSupport()
{
#ifdef HAVE_WEBP
    return true;
#else
    return false;
#endif
}

bool IsNativelySupportedImageFormat(const wxString& path)
{
    wxFileName fn(path);
    wxString ext = fn.GetExt().Lower();
    
    // These formats are natively supported by wxWidgets
    if (ext == "jpg" || ext == "jpeg" || ext == "png" || 
        ext == "gif" || ext == "bmp" || ext == "ico" || 
        ext == "tiff" || ext == "tif" || ext == "xpm" ||
        ext == "pcx" || ext == "pnm") {
        return true;
    }
    
    return false;
}

#ifdef HAVE_WEBP
static bool LoadWebPImage(const wxString& path, wxImage& outImage)
{
    // Read file into memory
    std::ifstream file(path.ToStdString(), std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }
    
    std::streamsize size = file.tellg();
    if (size <= 0) {
        return false;
    }
    
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return false;
    }
    file.close();
    
    // Get WebP image info
    int width = 0, height = 0;
    if (!WebPGetInfo(buffer.data(), buffer.size(), &width, &height)) {
        return false;
    }
    
    if (width <= 0 || height <= 0) {
        return false;
    }
    
    // Decode WebP to RGBA
    uint8_t* rgba = WebPDecodeRGBA(buffer.data(), buffer.size(), &width, &height);
    if (!rgba) {
        return false;
    }
    
    // Create wxImage from RGBA data
    // wxImage expects separate RGB and alpha channels
    outImage.Create(width, height, false);
    if (!outImage.IsOk()) {
        WebPFree(rgba);
        return false;
    }
    
    outImage.InitAlpha();
    
    unsigned char* imgData = outImage.GetData();
    unsigned char* alphaData = outImage.GetAlpha();
    
    for (int i = 0; i < width * height; i++) {
        imgData[i * 3 + 0] = rgba[i * 4 + 0];  // R
        imgData[i * 3 + 1] = rgba[i * 4 + 1];  // G
        imgData[i * 3 + 2] = rgba[i * 4 + 2];  // B
        alphaData[i] = rgba[i * 4 + 3];         // A
    }
    
    WebPFree(rgba);
    return true;
}
#endif

bool LoadImageWithWebPSupport(const wxString& path, wxImage& outImage)
{
    if (!wxFileExists(path)) {
        return false;
    }
    
    wxFileName fn(path);
    wxString ext = fn.GetExt().Lower();
    
    // Handle WebP files specially
    if (ext == "webp") {
#ifdef HAVE_WEBP
        return LoadWebPImage(path, outImage);
#else
        // WebP not supported - return false
        return false;
#endif
    }
    
    // For other formats, use wxImage's native loading
    if (IsNativelySupportedImageFormat(path)) {
        return outImage.LoadFile(path);
    }
    
    // Unsupported format
    return false;
}