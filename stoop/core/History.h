// History.h — undo, redo and A/B compare, on whole-patch snapshots.
//
// A snapshot is every parameter's value, in table order. Working on whole
// snapshots rather than on single parameter edits keeps this small enough to
// trust: undoing a preset load or a paste is the same operation as undoing one
// knob, and there is no edit that can be half undone.
//
// When a step is taken is the wrapper's business (Source/Session.h: at the end
// of every gesture, and after a preset or a paste). This file only keeps the
// stacks, and is tested on its own by test_core.
//
// Shared, identically, between CRATE and FRACTURE (core/History.h in each).
#pragma once
#include <vector>
#include <cstddef>

namespace session {

using Snapshot = std::vector<float>;

class History {
public:
    explicit History(std::size_t limit = 100) : limit_(limit) {}

    // the state everything is measured from; clears both stacks
    void reset(const Snapshot& now){ base_ = now; undo_.clear(); redo_.clear(); }

    // record now as a step if it differs from the last one; true if it did
    bool commit(const Snapshot& now){
        if (now == base_) return false;
        undo_.push_back(base_);
        if (undo_.size() > limit_) undo_.erase(undo_.begin());
        base_ = now;
        redo_.clear();
        return true;
    }

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }

    // now is what is live at the moment of asking, which can be ahead of the
    // last step (host automation moves parameters without a gesture). It is
    // committed first, so undo always goes back exactly one step from what is
    // heard, and redo returns to it.
    Snapshot undo(const Snapshot& now){
        commit(now);
        if (undo_.empty()) return base_;
        redo_.push_back(base_);
        base_ = undo_.back();
        undo_.pop_back();
        return base_;
    }
    Snapshot redo(const Snapshot& now){
        if (now != base_){ commit(now); return base_; }   // a fresh edit has already dropped the redo
        if (redo_.empty()) return base_;
        undo_.push_back(base_);
        base_ = redo_.back();
        redo_.pop_back();
        return base_;
    }

    // after applying what undo or redo returned: the live values can differ
    // from it in the last bit (a skewed range does not round-trip exactly), and
    // that difference must not read as a new edit. Moves the base, not the stacks.
    void settle(const Snapshot& live){ base_ = live; }

    const Snapshot& base() const { return base_; }
    std::size_t undoDepth() const { return undo_.size(); }
    std::size_t redoDepth() const { return redo_.size(); }

private:
    std::size_t limit_;
    Snapshot base_;
    std::vector<Snapshot> undo_, redo_;
};

// A/B compare, each side with its own history. The side not showing is held
// here; the side showing is the live parameters. Undo works within the side
// you are on: go to B, make changes, come back to A, and undo carries on with
// A's changes where it left off. A switch itself is not an undo step, so undo
// can never leave the panel showing A's settings with B lit.
class Workspace {
public:
    explicit Workspace(std::size_t limit = 100) : hist_{ History(limit), History(limit) } {}

    int active() const { return active_; }
    bool hasOther() const { return !other_.empty(); }
    const Snapshot& other() const { return other_; }
    const History& history() const { return hist_[active_]; }

    bool commit(const Snapshot& now){ return hist_[active_].commit(now); }
    // live parameters ahead of the last step (automation, an uncommitted
    // change) still give undo something to do
    bool canUndo(const Snapshot& now) const { return hist_[active_].canUndo() || now != hist_[active_].base(); }
    bool canRedo(const Snapshot& now) const { return hist_[active_].canRedo() && now == hist_[active_].base(); }
    Snapshot undo(const Snapshot& now){ return hist_[active_].undo(now); }
    Snapshot redo(const Snapshot& now){ return hist_[active_].redo(now); }
    void settle(const Snapshot& live){ hist_[active_].settle(live); }

    // switch to side `which`, given what is live now; returns what to apply,
    // after which the caller settles. B opened for the first time starts as a
    // copy of A, so a switch never lands on defaults.
    Snapshot select(int which, const Snapshot& now){
        which = which ? 1 : 0;
        if (which == active_) return now;
        hist_[active_].commit(now);
        const Snapshot next = visited_[which] ? other_ : now;
        if (!visited_[which]){ hist_[which].reset(next); visited_[which] = true; }
        other_ = now;
        active_ = which;
        return next;
    }
    // make the hidden side a copy of what is live, as a step in its history
    void copyToOther(const Snapshot& now){
        const int o = 1 - active_;
        if (visited_[o]) hist_[o].commit(now);
        else { hist_[o].reset(now); visited_[o] = true; }
        other_ = now;
    }

    // a new starting point for both histories, keeping which side is showing
    // and what the hidden side holds (a restored session sets those first)
    void reset(const Snapshot& live){
        hist_[active_].reset(live);
        visited_[active_] = true;
        const int o = 1 - active_;
        visited_[o] = !other_.empty();
        hist_[o].reset(other_.empty() ? live : other_);
    }
    void restore(int active, const Snapshot& other){ active_ = active ? 1 : 0; other_ = other; }

private:
    History hist_[2];
    bool visited_[2] { true, false };
    int active_ = 0;
    Snapshot other_;
};

} // namespace session
