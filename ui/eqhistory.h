#pragma once

#include "audio/eqstate.h"

#include <vector>

class EqHistory
{
public:
    static constexpr int kMaxDepth = 64;

    void push(const EqState &before)
    {
        if (m_undo.size() >= static_cast<size_t>(kMaxDepth)) {
            m_undo.erase(m_undo.begin());
        }
        m_undo.push_back(before);
        m_redo.clear();
    }

    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }

    const EqState &undoTop() const { return m_undo.back(); }

    void popUndo() { m_undo.pop_back(); }

    EqState undo(const EqState &current)
    {
        EqState previous = m_undo.back();
        m_undo.pop_back();
        m_redo.push_back(current);
        return previous;
    }

    EqState redo(const EqState &current)
    {
        EqState next = m_redo.back();
        m_redo.pop_back();
        m_undo.push_back(current);
        return next;
    }

    void clear()
    {
        m_undo.clear();
        m_redo.clear();
    }

private:
    std::vector<EqState> m_undo;
    std::vector<EqState> m_redo;
};
