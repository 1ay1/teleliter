#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/molecules/media_cell.hpp"
#include "views/theme.hpp"

namespace tl::views {

// N-column grid of media cells. Items are laid out row by row; extra
// items beyond rows*cols are dropped silently.

[[nodiscard]] inline maya::Element render_media_grid(
    std::span<const model::MediaItemVM> items,
    int columns = 2,
    int cell_width = 12)
{
    using namespace maya;
    using namespace maya::dsl;

    if (items.empty() || columns <= 0) return text(std::string{});

    std::vector<Element> rows;
    for (std::size_t i = 0; i < items.size(); i += static_cast<std::size_t>(columns)) {
        std::vector<Element> cells;
        for (int c = 0; c < columns && (i + static_cast<std::size_t>(c)) < items.size(); ++c) {
            cells.push_back(render_media_cell(items[i + static_cast<std::size_t>(c)], cell_width));
            if (c + 1 < columns) cells.push_back(text(std::string{" "}));
        }
        rows.push_back(hstack()(cells));
        rows.push_back(text(std::string{}));
    }

    return vstack().gap(0)(rows);
}

}  // namespace tl::views
