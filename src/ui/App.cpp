#include "App.h"
#include "MainFrame.h"
#include <wx/image.h>

bool App::OnInit()
{
    // Initialize all image handlers (JPEG, PNG, GIF, BMP, etc.)
    // This is required before wxImage can load these formats
    wxInitAllImageHandlers();
    
    MainFrame *frame = new MainFrame("Teleliter", wxPoint(50, 50), wxSize(800, 600));
    frame->Show(true);
    return true;
}