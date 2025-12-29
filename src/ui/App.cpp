#include "App.h"
#include "MainFrame.h"
#include <wx/image.h>
#include <wx/config.h>
#include <wx/fileconf.h>
#include <wx/stdpaths.h>
#include <wx/tooltip.h>

bool App::OnInit()
{
    // Initialize all image handlers (JPEG, PNG, GIF, BMP, etc.)
    // This is required before wxImage can load these formats
    wxInitAllImageHandlers();
    
    // Make tooltips appear instantly
    wxToolTip::SetDelay(0);
    
    // Set up persistent config file
    SetAppName("teleliter");
    SetVendorName("teleliter");
    wxConfig::Set(new wxFileConfig("teleliter", "", 
        wxStandardPaths::Get().GetUserConfigDir() + "/teleliter.conf",
        "", wxCONFIG_USE_LOCAL_FILE));

    MainFrame *frame = new MainFrame("Teleliter", wxPoint(50, 50), wxSize(1024, 600));
    frame->Show(true);
    return true;
}
