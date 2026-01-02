#ifndef APP_H
#define APP_H

#include <wx/wx.h>

class App : public wxApp
{
public:
    virtual bool OnInit();
    virtual void OnInitCmdLine(wxCmdLineParser& parser);
    virtual bool OnCmdLineParsed(wxCmdLineParser& parser);
    
    static bool IsDemoMode() { return s_demoMode; }
    
private:
    static bool s_demoMode;
};

#endif // APP_H