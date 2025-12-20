#include "App.h"
#include "MainFrame.h"

bool App::OnInit()
{
    MainFrame *frame = new MainFrame("Teleliter", wxPoint(50, 50), wxSize(800, 600));
    frame->Show(true);
    return true;
}