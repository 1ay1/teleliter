#include "App.h"
#include "MainFrame.h"
#include <wx/cmdline.h>
#include <wx/config.h>
#include <wx/fileconf.h>
#include <wx/image.h>
#include <wx/stdpaths.h>
#include <wx/tooltip.h>

bool App::s_demoMode = false;

void App::OnInitCmdLine(wxCmdLineParser &parser) {
  wxApp::OnInitCmdLine(parser);

  parser.AddSwitch("d", "demo", "Run in demo mode with dummy data",
                   wxCMD_LINE_PARAM_OPTIONAL);
  parser.AddSwitch("h", "help", "Show help message", wxCMD_LINE_PARAM_OPTIONAL);
}

bool App::OnCmdLineParsed(wxCmdLineParser &parser) {
  if (parser.Found("demo")) {
    s_demoMode = true;
  }

  return wxApp::OnCmdLineParsed(parser);
}

bool App::OnInit() {
  // Parse command line
  if (!wxApp::OnInit()) {
    return false;
  }

  // Initialize all image handlers (JPEG, PNG, GIF, BMP, etc.)
  // This is required before wxImage can load these formats
  wxInitAllImageHandlers();

  // Make tooltips appear instantly
  wxToolTip::SetDelay(0);

  // Set up persistent config file
  SetAppName("teleliter");
  SetVendorName("teleliter");
  wxConfig::Set(new wxFileConfig(
      "teleliter", "",
      wxStandardPaths::Get().GetUserConfigDir() + "/teleliter.conf", "",
      wxCONFIG_USE_LOCAL_FILE));

  MainFrame *frame =
      new MainFrame("Teleliter", wxPoint(50, 50), wxSize(1200, 700));
  frame->Show(true);

  return true;
}