#include <maya/maya.hpp>

#include "app/program.hpp"

int main()
{
    maya::run<tl::app::TeleliterProgram>({
        .title = "teleliter",
        .mouse = true,
        .mode  = maya::Mode::Fullscreen,
        .theme = maya::theme::dark_ansi,
    });
}
