#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

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

    [[nodiscard]] std::string render() const;

    // If you have your own terminal width function, set it here
    void set_term_width(int w) { term_width_ = w; }

private:
    std::vector<Column> cols_;
    std::vector<std::vector<Cell>> rows_;
    int term_width_ = 0;

    // Simple word wrapper; replace if you have a smarter wrap_text()
    static std::vector<std::string> wrap_lines(const std::string& s, std::size_t width);

    // Middle ellipsis; replace with your existing ellipsize_middle() if preferred
    static std::string ellipsize_middle(const std::string& s, std::size_t width);
};

}
