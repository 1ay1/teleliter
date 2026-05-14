#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include <maya/maya.hpp>
#include <maya/widget/scrollable.hpp>
#include <maya/widget/scrollbar.hpp>

#include "model/view_models.hpp"
#include "views/atoms/tab.hpp"
#include "views/atoms/underline.hpp"
#include "views/theme.hpp"

namespace tl::views {

// A horizontal strip of TabVM atoms separated by " · " plus a short
// underline below the active tab.
//
// When `viewport_w` is non-zero and the labels exceed it, the row gets
// wrapped in maya::scrollx so the overflow is reachable, and a
// scrollbar_x is drawn below the underline. The caller supplies the
// persistent scroll state so it survives renders.

namespace detail::tabs {

[[nodiscard]] inline std::size_t total_width(std::span<const model::TabVM> tabs)
{
    if (tabs.empty()) return 0;
    std::size_t w = 1;  // leading " "
    for (std::size_t i = 0; i < tabs.size(); ++i) {
        w += tabs[i].label.size();
        if (i + 1 < tabs.size()) w += 5;  // "  ·  "
    }
    return w;
}

}  // namespace detail::tabs

[[nodiscard]] inline maya::Element render_tabs_row(
    std::span<const model::TabVM> tabs,
    std::size_t active_index,
    maya::ScrollState* scroll = nullptr,
    int viewport_w = 0)
{
    using namespace maya;
    using namespace maya::dsl;

    if (tabs.empty()) return text(std::string{});

    std::vector<Element> row_children;
    row_children.reserve(tabs.size() * 2 + 1);
    row_children.push_back(text(std::string{" "}));

    std::size_t underline_offset = 1;
    std::size_t underline_width  = tabs[active_index].label.size();

    for (std::size_t i = 0; i < tabs.size(); ++i) {
        const bool active = (i == active_index);
        row_children.push_back(render_tab(tabs[i], active));
        if (i + 1 < tabs.size()) {
            row_children.push_back(text(std::string{"  ·  "},
                Style{}.with_fg(palette::dim())));
        }
        if (i < active_index) {
            underline_offset += tabs[i].label.size() + 5;
        }
    }

    auto labels = hstack()(row_children);
    auto under  = hstack()(
        text(std::string(underline_offset, ' ')),
        render_underline(underline_width, palette::accent())
    );

    auto inner = vstack()(labels, under);

    const bool needs_scroll =
        scroll != nullptr
     && viewport_w > 0
     && static_cast<int>(detail::tabs::total_width(tabs)) > viewport_w;

    if (!needs_scroll) {
        return inner;
    }

    // overflow_w mode: viewport_w clips the inner; the bar appears below.
    return vstack()(
        std::move(inner) | scrollx(*scroll, viewport_w),
        scrollbar_x(*scroll, viewport_w, ScrollbarStyle::minimal())
    );
}

}  // namespace tl::views
