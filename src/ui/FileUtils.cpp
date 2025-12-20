#include "FileUtils.h"
#include <wx/filename.h>
#include <iostream>

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
    std::cerr << "[FileUtils] LoadWebPImage: " << path.ToStdString() << std::endl;
    
    // Read file into memory
    std::ifstream file(path.ToStdString(), std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[FileUtils] LoadWebPImage: failed to open file" << std::endl;
        return false;
    }
    
    std::streamsize size = file.tellg();
    std::cerr << "[FileUtils] LoadWebPImage: file size=" << size << std::endl;
    if (size <= 0) {
        std::cerr << "[FileUtils] LoadWebPImage: invalid file size" << std::endl;
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
        std::cerr << "[FileUtils] LoadWebPImage: WebPGetInfo failed" << std::endl;
        return false;
    }
    std::cerr << "[FileUtils] LoadWebPImage: dimensions " << width << "x" << height << std::endl;
    
    if (width <= 0 || height <= 0) {
        return false;
    }
    
    // Decode WebP to RGBA
    uint8_t* rgba = WebPDecodeRGBA(buffer.data(), buffer.size(), &width, &height);
    if (!rgba) {
        std::cerr << "[FileUtils] LoadWebPImage: WebPDecodeRGBA failed" << std::endl;
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
    std::cerr << "[FileUtils] LoadWebPImage: successfully decoded" << std::endl;
    
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
    std::cerr << "[FileUtils] LoadImageWithWebPSupport: " << path.ToStdString() << std::endl;
    
    if (!wxFileExists(path)) {
        std::cerr << "[FileUtils] File does not exist" << std::endl;
        return false;
    }
    
    wxFileName fn(path);
    wxString ext = fn.GetExt().Lower();
    std::cerr << "[FileUtils] Extension: " << ext.ToStdString() << std::endl;
    
    // Handle WebP files specially
    if (ext == "webp") {
#ifdef HAVE_WEBP
        std::cerr << "[FileUtils] Loading as WebP" << std::endl;
        return LoadWebPImage(path, outImage);
#else
        std::cerr << "[FileUtils] WebP not supported (HAVE_WEBP not defined)" << std::endl;
        // WebP not supported - return false
        return false;
#endif
    }
    
    // For other formats, use wxImage's native loading
    if (IsNativelySupportedImageFormat(path)) {
        std::cerr << "[FileUtils] Loading with wxImage::LoadFile" << std::endl;
        bool result = outImage.LoadFile(path);
        std::cerr << "[FileUtils] wxImage::LoadFile result: " << result << std::endl;
        if (!result) {
            std::cerr << "[FileUtils] wxImage error - trying to get more info" << std::endl;
            // Try loading with explicit type
            wxImage testImg;
            if (ext == "jpg" || ext == "jpeg") {
                result = testImg.LoadFile(path, wxBITMAP_TYPE_JPEG);
                std::cerr << "[FileUtils] Explicit JPEG load: " << result << std::endl;
            } else if (ext == "png") {
                result = testImg.LoadFile(path, wxBITMAP_TYPE_PNG);
                std::cerr << "[FileUtils] Explicit PNG load: " << result << std::endl;
            }
            if (result) {
                outImage = testImg;
            }
        }
        return result;
    }
    
    std::cerr << "[FileUtils] Unsupported format: " << ext.ToStdString() << std::endl;
    // Unsupported format
    return false;
}