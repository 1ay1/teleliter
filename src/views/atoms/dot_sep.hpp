#pragma once

#include <string>

#include <maya/maya.hpp>

#include "views/theme.hpp"

namespace tl::views {

// A dim middle-dot separator "  ·  " for use between inline metadata.

[[nodiscard]] inline maya::Element render_dot_sep()
{
    using namespace maya;
    using namespace maya::dsl;
    return text(std::string{"  ·  "}, Style{}.with_fg(palette::dim()));
}

[[nodiscard]] inline maya::Element render_pipe_sep()
{
    using namespace maya;
    using namespace maya::dsl;
    return text(std::string{"│"}, Style{}.with_fg(palette::muted()));
}

}  // namespace tl::views
