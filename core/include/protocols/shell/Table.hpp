#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <limits>
#include <optional>
#include <cstddef>
#include <fmt/format.h>

namespace vh::protocols::shell {

enum class Align { Left, Right };

struct Column {
    std::string header;
    Align align = Align::Left;
    std::size_t min = 1;
    std::size_t max = std::numeric_limits<std::size_t>::max();
    bool wrap = false;               // if true, text can flow to multiple lines
    bool ellipsize_middle = false;   // if true, clamp with … in the middle (useful for paths)
};

struct Cell {
    std::string text;
};

class Table {
public:
    explicit Table(std::vector<Column> cols, const int term_width = 0)
        : cols_(std::move(cols)), term_width_(term_width) {}

    void add_row(std::vector<std::string> cells) {
        std::vector<Cell> row;
        row.reserve(cells.size());
        for (auto& s : cells) row.push_back(Cell{std::move(s)});
        rows_.push_back(std::move(row));
    }

    [[nodiscard]] std::string render() const {
        if (cols_.empty()) return {};

        // 1) Compute natural widths from content (before wrapping)
        const std::size_t ncol = cols_.size();
        std::vector<std::size_t> width(ncol, 0);
        for (std::size_t i = 0; i < ncol; ++i) width[i] = std::max(cols_[i].min, cols_[i].header.size());
        for (auto const& r : rows_)
            for (std::size_t i = 0; i < ncol && i < r.size(); ++i)
                width[i] = std::max(width[i], std::min(cols_[i].max, r[i].text.size()));

        // 2) Budget to terminal width: 2 leading spaces + "  " between cols
        constexpr std::size_t pad_left = 2;
        constexpr std::size_t gap = 2;
        auto total_width = [&](const std::vector<std::size_t>& w){
            std::size_t sum = pad_left + (ncol ? (gap * (ncol-1)) : 0);
            for (auto x : w) sum += x;
            return sum;
        };

        constexpr int fallback_term = 100;
        const int tw = term_width_ > 0 ? term_width_ : fallback_term;

        // Choose a preferred "flex" column to soak/shrink space: last wrapping column, else last column
        std::optional<std::size_t> flex_idx;
        for (std::size_t i = ncol; i-- > 0;) if (cols_[i].wrap) { flex_idx = i; break; }
        if (!flex_idx) flex_idx = ncol - 1;

        // Clamp to max and then shrink if needed to fit terminal
        for (std::size_t i = 0; i < ncol; ++i) width[i] = std::clamp(width[i], cols_[i].min, cols_[i].max);
        while (static_cast<int>(total_width(width)) > tw && width[*flex_idx] > cols_[*flex_idx].min) --width[*flex_idx];

        // 3) Emit header
        std::string out;
        out.reserve(128 + rows_.size() * 96);
        out += fmt::format("{}", "");

        out += "  "; // pad_left
        for (std::size_t i = 0; i < ncol; ++i) {
            if (i) out += std::string(gap, ' ');
            // header emit
            if (cols_[i].align == Align::Left)
                fmt::format_to(std::back_inserter(out), FMT_STRING("{:{}}"), cols_[i].header, width[i]);
            else fmt::format_to(std::back_inserter(out), FMT_STRING("{:>{}}"),cols_[i].header, width[i]);
        }
        out += '\n';

        out += "  ";
        for (std::size_t i = 0; i < ncol; ++i) {
            if (i) out += std::string(gap, ' ');
            out += std::string(width[i], '-');
        }
        out += '\n';

        // 4) Emit rows, wrapping/ellipsizing as needed
        for (auto const& r : rows_) {
            std::vector<std::vector<std::string>> lines_per_cell(ncol);
            std::size_t max_lines = 1;
            for (std::size_t i = 0; i < ncol; ++i) {
                const std::string text = (i < r.size() ? r[i].text : "");
                if (cols_[i].wrap) lines_per_cell[i] = wrap_lines(text, width[i]);
                else {
                    std::string s = text;
                    if (s.size() > width[i]) {
                        if (cols_[i].ellipsize_middle && width[i] > 1) s = ellipsize_middle(s, width[i]);
                        else s.resize(width[i]);
                    }
                    lines_per_cell[i] = { std::move(s) };
                }
                max_lines = std::max(max_lines, lines_per_cell[i].size());
            }

            // print each visual line
            for (std::size_t l = 0; l < max_lines; ++l) {
                out += "  ";
                for (std::size_t i = 0; i < ncol; ++i) {
                    if (i) out += std::string(gap, ' ');
                    std::string cell = (l < lines_per_cell[i].size() ? lines_per_cell[i][l] : "");
                    if (cols_[i].align == Align::Left)
                        fmt::format_to(std::back_inserter(out), FMT_STRING("{:{}}"),
                                       cell, width[i]);
                    else fmt::format_to(std::back_inserter(out), FMT_STRING("{:>{}}"),
                                       cell, width[i]);
                }
                out += '\n';
            }
        }

        return out;
    }

    // If you have your own terminal width function, set it here
    void set_term_width(int w) { term_width_ = w; }

private:
    std::vector<Column> cols_;
    std::vector<std::vector<Cell>> rows_;
    int term_width_ = 0;

    // Simple word wrapper; replace if you have a smarter wrap_text()
    static std::vector<std::string> wrap_lines(const std::string& s, std::size_t width) {
        if (width == 0) return {""};
        std::vector<std::string> out;
        std::size_t i = 0;
        while (i < s.size()) {
            const std::size_t end = std::min(i + width, s.size());
            // try to break on space
            std::size_t break_pos = end;
            if (end < s.size() && s[end] != ' ') {
                auto sp = s.rfind(' ', end - 1);
                if (sp != std::string::npos && sp >= i && (end - sp) <= width) break_pos = sp;
            }
            if (break_pos == i) break_pos = end; // no space found
            std::string line = s.substr(i, break_pos - i);
            // trim leading spaces on next chunk
            i = (break_pos < s.size() && s[break_pos] == ' ') ? break_pos + 1 : break_pos;
            out.push_back(std::move(line));
        }
        if (out.empty()) out.push_back("");
        return out;
    }

    // Middle ellipsis; replace with your existing ellipsize_middle() if preferred
    static std::string ellipsize_middle(const std::string& s, std::size_t width) {
        if (s.size() <= width || width <= 1) return s.substr(0, width);
        if (width == 2) return std::string("..");
        const std::size_t keep = width - 1; // one char ellipsis
        const std::size_t left = keep / 2;
        const std::size_t right = keep - left;
        return s.substr(0, left) + "…" + s.substr(s.size() - right);
    }
};

}
