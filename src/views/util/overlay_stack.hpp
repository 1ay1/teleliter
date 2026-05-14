#pragma once

#include <utility>
#include <vector>

#include <maya/maya.hpp>

namespace tl::views::util {

// Z-stack two layers: a base layer plus an overlay floated above it.
// The overlay is centered in the available space. Used by the shell to
// keep the main app visible behind modal overlays (help / jumper).
//
// Returns the base alone when `overlay` is std::nullopt-ish (an empty
// Element produces an invisible top layer, so callers can pass an empty
// Element when no overlay should show).

[[nodiscard]] inline maya::Element overlay_on(
    maya::Element base,
    maya::Element overlay)
{
    using namespace maya;
    std::vector<Element> layers;
    layers.reserve(2);
    layers.push_back(std::move(base));
    layers.push_back(std::move(overlay));
    return detail::zstack(std::move(layers));
}

[[nodiscard]] inline maya::Element center_overlay(maya::Element overlay)
{
    using namespace maya;
    using namespace maya::dsl;
    return vstack()
        .grow(1)
        .justify(Justify::Center)
        .align_items(Align::Center)
        (std::move(overlay));
}

}  // namespace tl::views::util
