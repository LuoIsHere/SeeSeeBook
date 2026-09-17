#pragma once

#include <cstddef>
#include <limits>

class selection_controller {
public:
    static constexpr std::size_t no_selection =
        std::numeric_limits<std::size_t>::max();

    void clear()
    {
        index_ = no_selection;
    }

    bool selected(std::size_t first, std::size_t end) const
    {
        return index_ >= first && index_ < end;
    }

    std::size_t index() const
    {
        return index_;
    }

    bool move_previous(std::size_t first, std::size_t end)
    {
        if (first >= end) {
            clear();
            return false;
        }
        index_ = selected(first, end) && index_ > first ? index_ - 1U : end - 1U;
        return true;
    }

    bool move_next(std::size_t first, std::size_t end)
    {
        if (first >= end) {
            clear();
            return false;
        }
        index_ = selected(first, end) && index_ + 1U < end ? index_ + 1U : first;
        return true;
    }

    bool retain(std::size_t first, std::size_t end)
    {
        if (selected(first, end)) {
            return true;
        }
        clear();
        return false;
    }

private:
    std::size_t index_ = no_selection;
};
