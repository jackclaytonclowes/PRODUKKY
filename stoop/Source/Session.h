// Session.h — undo, redo, A/B compare and your own presets, for the wrapper.
//
// The stacks themselves are core/History.h and have no JUCE in them. This file
// decides when a step is taken and moves values in and out of the host's
// parameters:
//
//   - one step per gesture. Every control on the panel brackets its change in
//     begin/endChangeGesture, so a whole knob drag is one undo, not a hundred.
//     The step is taken when no gesture is open any more, so a control that
//     moves several parameters (the XY pad, a stroke across the drawn steps)
//     is one undo too. Host automation sends no gestures and is never
//     recorded on its own.
//   - one step after anything that changes many parameters at once: a preset
//     or a paste. The processor calls commit() for those.
//   - A and B each keep their own history (core/History.h, Workspace), so
//     undo never crosses a switch.
//   - a restored session starts a fresh history. Undoing past the moment a
//     project was opened would be undoing the host's work, not the user's.
//
// Snapshots are the parameters' normalised values in table order, so a
// snapshot means the same thing to every parameter type.
//
// Presets you save are plain JSON files, one per preset, in
// Documents/<plugin>/Presets, so they can be backed up, copied between
// machines or mailed to someone like any other file.
//
// Shared, identically, between CRATE and FRACTURE (Source/Session.h in each).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <mutex>
#include <set>
#include <atomic>
#include "History.h"

namespace session {

class Session : private juce::AudioProcessorParameter::Listener {
public:
    // params in table order: the order snapshots are taken in
    explicit Session(std::vector<juce::RangedAudioParameter*> params) : params_(std::move(params)){
        for (auto* p : params_) p->addListener(this);
        ws_.reset(snapshot());
    }
    ~Session() override { for (auto* p : params_) p->removeListener(this); }

    Snapshot snapshot() const {
        Snapshot s;
        s.reserve(params_.size());
        for (auto* p : params_) s.push_back(p->getValue());
        return s;
    }

    // after a preset, a paste, or anything else that moves many parameters
    void commit(){ std::lock_guard<std::recursive_mutex> l(lock_); ws_.commit(snapshot()); }
    // a new starting point: nothing before it can be undone
    void reset(){ std::lock_guard<std::recursive_mutex> l(lock_); ws_.reset(snapshot()); }

    bool canUndo() const { std::lock_guard<std::recursive_mutex> l(lock_); return ws_.canUndo(snapshot()); }
    bool canRedo() const { std::lock_guard<std::recursive_mutex> l(lock_); return ws_.canRedo(snapshot()); }
    void undo(){ std::lock_guard<std::recursive_mutex> l(lock_); apply(ws_.undo(snapshot())); ws_.settle(snapshot()); }
    void redo(){ std::lock_guard<std::recursive_mutex> l(lock_); apply(ws_.redo(snapshot())); ws_.settle(snapshot()); }

    int activeSlot() const { std::lock_guard<std::recursive_mutex> l(lock_); return ws_.active(); }
    void selectSlot(int which){
        std::lock_guard<std::recursive_mutex> l(lock_);
        apply(ws_.select(which, snapshot()));
        ws_.settle(snapshot());
    }
    void copyToOther(){ std::lock_guard<std::recursive_mutex> l(lock_); ws_.copyToOther(snapshot()); }

    // The hidden side is part of the session: close the project on B and
    // reopen it, and A is still there. Stored by parameter id, not position,
    // so a later version with more parameters reads it correctly.
    void saveInto(juce::XmlElement& xml) const {
        std::lock_guard<std::recursive_mutex> l(lock_);
        auto* c = xml.createNewChildElement(tag());
        c->setAttribute("active", ws_.active());
        if (ws_.hasOther()){
            auto* o = c->createNewChildElement("OTHER");
            for (size_t i = 0; i < params_.size(); ++i)
                o->setAttribute(params_[i]->getParameterID(), static_cast<double>(ws_.other()[i]));
        }
    }
    // takes the COMPARE element back out of xml, so what is left is exactly
    // what the parameter tree expects. The caller restores the parameters and
    // then calls reset().
    void restoreFrom(juce::XmlElement& xml){
        std::lock_guard<std::recursive_mutex> l(lock_);
        Snapshot other;
        int active = 0;
        if (auto* c = xml.getChildByName(tag())){
            active = c->getIntAttribute("active", 0);
            if (auto* o = c->getChildByName("OTHER"))
                for (auto* p : params_)
                    other.push_back(static_cast<float>(o->getDoubleAttribute(
                        p->getParameterID(), static_cast<double>(p->getDefaultValue()))));
            xml.removeChildElement(c, true);
        }
        ws_.restore(active, other);
    }
    static juce::String tag(){ return "COMPARE"; }

    size_t undoDepth() const { std::lock_guard<std::recursive_mutex> l(lock_); return ws_.history().undoDepth(); }

private:
    void apply(const Snapshot& s){
        if (s.size() != params_.size()) return;
        applying_ = true;
        for (size_t i = 0; i < params_.size(); ++i)
            if (params_[i]->getValue() != s[i]) params_[i]->setValueNotifyingHost(s[i]);
        applying_ = false;
    }

    void parameterValueChanged(int, float) override {}
    void parameterGestureChanged(int index, bool starting) override {
        if (applying_) return;
        std::lock_guard<std::recursive_mutex> l(lock_);
        // a set, not a count: a host that begins a gesture twice, or ends one
        // that never began, does not throw the bookkeeping off
        if (starting){ open_.insert(index); return; }
        open_.erase(index);
        if (open_.empty()) ws_.commit(snapshot());
    }

    std::vector<juce::RangedAudioParameter*> params_;
    mutable std::recursive_mutex lock_;
    std::atomic<bool> applying_ { false };
    Workspace ws_;
    std::set<int> open_;                               // parameters mid-gesture
};

// ---------------------------------------------------------------- your presets
class UserPresets {
public:
    explicit UserPresets(juce::String product) : product_(std::move(product)) {}

    // tests point this somewhere temporary
    void setFolder(const juce::File& f){ override_ = f; }
    juce::File folder() const {
        if (override_ != juce::File()) return override_;
        return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                   .getChildFile(product_).getChildFile("Presets");
    }

    // sorted the way people number things: "10 kick" after "2 kick"
    juce::Array<juce::File> list() const {
        juce::Array<juce::File> files;
        if (folder().isDirectory())
            files = folder().findChildFiles(juce::File::findFiles, false, "*.json");
        std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b){
            return a.getFileNameWithoutExtension().compareNatural(b.getFileNameWithoutExtension()) < 0;
        });
        return files;
    }

    // a name typed by a person, made safe for a file name on every system
    static juce::String safeName(const juce::String& name){
        auto s = juce::File::createLegalFileName(name.trim());
        return s.isEmpty() ? juce::String("Untitled") : s;
    }

    bool save(const juce::File& file, const juce::String& json) const {
        return file.getParentDirectory().createDirectory() && file.replaceWithText(json);
    }

    // Favourites: one per line in favourites.txt beside the presets, "f:" and
    // a factory preset's name or "u:" and one of yours, so they survive new
    // presets being added and can be backed up with the rest
    juce::File favouritesFile() const { return folder().getChildFile("favourites.txt"); }
    juce::StringArray favourites() const {
        juce::StringArray a;
        if (favouritesFile().existsAsFile()) a.addLines(favouritesFile().loadFileAsString());
        a.trim(); a.removeEmptyStrings();
        return a;
    }
    bool setFavourite(const juce::String& key, bool on) const {
        auto a = favourites();
        a.removeString(key);
        if (on) a.add(key);
        return save(favouritesFile(), a.joinIntoString("\n") + "\n");
    }

private:
    juce::String product_;
    juce::File override_;
};

// ------------------------------------------------------------------ the panel
// Undo, Redo, A, B and the copy between them, as one strip for the header.
// The buttons are public so the host-level test can press them.
class SessionBar : public juce::Component, private juce::Timer {
public:
    SessionBar(Session& s, juce::Colour accent) : session_(s){
        for (auto* b : { &undo, &redo, &slotA, &slotB, &copy }) addAndMakeVisible(*b);
        for (auto* b : { &slotA, &slotB }){
            b->setColour(juce::TextButton::buttonOnColourId, accent);
            b->setRadioGroupId(0x0AB0);
        }
        undo.setTooltip("Undo the last change (Ctrl or Cmd + Z)");
        redo.setTooltip("Redo what was undone (Ctrl or Cmd + Shift + Z)");
        slotA.setTooltip("Compare: two settings to flip between. This is A");
        slotB.setTooltip("Compare: two settings to flip between. B starts as a copy of A");
        undo.onClick  = [this]{ session_.undo(); refresh(); };
        redo.onClick  = [this]{ session_.redo(); refresh(); };
        slotA.onClick = [this]{ session_.selectSlot(0); refresh(); };
        slotB.onClick = [this]{ session_.selectSlot(1); refresh(); };
        copy.onClick  = [this]{ session_.copyToOther(); refresh(); };
        refresh();
        startTimerHz(10);
    }
    static constexpr int preferredWidth = 52 + 4 + 52 + 12 + 32 + 32 + 4 + 64;

    void resized() override {
        auto r = getLocalBounds();
        const int h = juce::jmin(30, r.getHeight());
        auto place = [&](juce::Button& b, int w, int gapAfter){
            b.setBounds(r.removeFromLeft(w).withSizeKeepingCentre(w, h));
            r.removeFromLeft(gapAfter);
        };
        place(undo, 52, 4); place(redo, 52, 12);
        place(slotA, 32, 0); place(slotB, 32, 4); place(copy, 64, 0);
    }

    void refresh(){
        const int a = session_.activeSlot();
        slotA.setToggleState(a == 0, juce::dontSendNotification);
        slotB.setToggleState(a == 1, juce::dontSendNotification);
        const juce::String c = a == 0 ? "A to B" : "B to A";
        if (copy.getButtonText() != c){
            copy.setButtonText(c);
            copy.setTooltip(a == 0 ? "Copy what you hear now (A) into B" : "Copy what you hear now (B) into A");
        }
        enable(undo, session_.canUndo());
        enable(redo, session_.canRedo());
    }

    juce::TextButton undo { "Undo" }, redo { "Redo" }, slotA { "A" }, slotB { "B" }, copy { "A to B" };

private:
    static void enable(juce::Button& b, bool on){
        if (b.isEnabled() == on) return;
        b.setEnabled(on);
        b.setAlpha(on ? 1.0f : 0.35f);
    }
    void timerCallback() override { refresh(); }
    Session& session_;
};

// A preset menu that reads the presets folder each time it opens, so a preset
// saved in another window, or copied in by hand, is there without a restart.
class PresetBox : public juce::ComboBox {
public:
    std::function<void()> beforePopup;
    void showPopup() override { if (beforePopup) beforePopup(); juce::ComboBox::showPopup(); }
};

// A square button with a drawn triangle, in the look's own colours. Drawn
// rather than set as text, so it does not depend on a font having the glyph.
class ArrowButton : public juce::Button {
public:
    explicit ArrowButton(bool forward) : juce::Button(forward ? "Next preset" : "Previous preset"), fwd(forward){}
    void paintButton(juce::Graphics& g, bool over, bool down) override {
        auto r = getLocalBounds().toFloat();
        const auto face = findColour(juce::TextButton::buttonColourId);
        const auto ink = findColour(juce::TextButton::textColourOffId);
        g.setColour(down || over ? face.darker(0.08f) : face);
        g.fillRect(r);
        g.setColour(ink);
        g.drawRect(r, 2.0f);
        const float s = juce::jmin(r.getWidth(), r.getHeight()) * 0.26f;
        const auto c = r.getCentre();
        juce::Path t;
        if (fwd) t.addTriangle(c.x - s * 0.8f, c.y - s, c.x - s * 0.8f, c.y + s, c.x + s, c.y);
        else     t.addTriangle(c.x + s * 0.8f, c.y - s, c.x + s * 0.8f, c.y + s, c.x - s, c.y);
        g.fillPath(t);
    }
private:
    bool fwd;
};

// The preset menu and its Save button: the factory list, then the presets you
// saved, then a way to find them on disk. The processor side is passed in as
// functions so CRATE and FRACTURE share this without sharing a base class.
class PresetMenu {
public:
    struct Hooks {
        std::function<juce::String()> userName;              // the one loaded, if any
        std::function<bool(const juce::File&)> save, load;
        std::function<juce::String(int)> category;           // a factory preset's heading, if grouped
    };
    PresetMenu(juce::AudioProcessor& p, UserPresets& u, Hooks h, juce::String product)
        : proc_(p), user_(u), hooks_(std::move(h)), product_(std::move(product)){
        refill();
        box.beforePopup = [this]{ refill(); };
        box.onChange = [this]{ chosen(box.getSelectedId()); };
        saveButton.setTooltip("Save these settings as a preset of your own, in Documents/"
                              + product_ + "/Presets");
        saveButton.onClick = [this]{ saveAs(); };
        prev.setTooltip("Previous preset");
        next.setTooltip("Next preset");
        prev.onClick = [this]{ step(-1); };
        next.onClick = [this]{ step(+1); };
    }

    PresetBox box;
    juce::TextButton saveButton { "Save" };
    ArrowButton prev { false }, next { true };

    // one preset along, in the order the menu lists them: the factory list
    // under its headings, then yours, round and round. From a state that
    // matches none (a preset loaded and then changed still counts as that
    // preset), it starts from where the menu shows
    void step(int direction){
        refill();
        std::vector<int> order = factoryOrder();
        for (int k = 0; k < files_.size(); ++k) order.push_back(userBase + k + 1);
        if (order.empty()) return;
        const int now = currentId();
        const auto it = std::find(order.begin(), order.end(), now);
        const int n = static_cast<int>(order.size());
        int at = it == order.end() ? (direction > 0 ? -1 : 0) : static_cast<int>(it - order.begin());
        at = ((at + direction) % n + n) % n;
        chosen(order[static_cast<size_t>(at)]);
        box.setSelectedId(order[static_cast<size_t>(at)], juce::dontSendNotification);
    }

    void refill(){
        box.clear(juce::dontSendNotification);
        files_ = user_.list();
        // your favourites first, each pointing at the preset it names
        favTargets_.clear();
        const auto favs = user_.favourites();
        for (const auto& key : favs){
            const int target = idForKey(key);
            if (target <= 0) continue;                          // renamed or deleted since
            if (favTargets_.empty()) box.addSectionHeading("Favourites");
            box.addItem(key.substring(2), favBase + static_cast<int>(favTargets_.size()) + 1);
            favTargets_.push_back(target);
        }
        if (!favTargets_.empty()) box.addSeparator();
        // the factory list, under its headings in the order they first appear
        juce::String heading;
        for (const int id : factoryOrder()){            // the arrows step this same order
            if (hooks_.category){
                const auto h = hooks_.category(id - 1);
                if (h != heading && h.isNotEmpty()) box.addSectionHeading(h);
                heading = h;
            }
            box.addItem(proc_.getProgramName(id - 1), id);
        }
        box.addSeparator();
        box.addSectionHeading("Your presets");
        for (int k = 0; k < files_.size(); ++k)
            box.addItem(files_[k].getFileNameWithoutExtension(), userBase + k + 1);
        if (files_.isEmpty()){
            box.addItem("(none yet: press Save)", noneId);
            box.setItemEnabled(noneId, false);
        }
        box.addSeparator();
        box.addItem(isFavourite() ? "Remove this preset from favourites" : "Add this preset to favourites", favToggleId);
        box.addItem("Show the presets folder", folderId);
        box.setSelectedId(currentId(), juce::dontSendNotification);
    }
    // the factory presets' ids in menu order: grouped, each group in list order
    std::vector<int> factoryOrder() const {
        std::vector<int> order;
        if (!hooks_.category){
            for (int i = 0; i < proc_.getNumPrograms(); ++i) order.push_back(i + 1);
            return order;
        }
        juce::StringArray headings;
        for (int i = 0; i < proc_.getNumPrograms(); ++i) headings.addIfNotAlreadyThere(hooks_.category(i));
        for (const auto& h : headings)
            for (int i = 0; i < proc_.getNumPrograms(); ++i) if (hooks_.category(i) == h) order.push_back(i + 1);
        return order;
    }
    // the preset showing now, as a favourites key, and whether it is one
    juce::String currentKey() const {
        const auto name = hooks_.userName();
        if (name.isNotEmpty()) return "u:" + name;
        return "f:" + proc_.getProgramName(proc_.getCurrentProgram());
    }
    bool isFavourite() const { return user_.favourites().contains(currentKey()); }
    void toggleFavourite(){ user_.setFavourite(currentKey(), !isFavourite()); refill(); }
    // for the tests: choose a menu item as if clicked
    void choose(int id){ chosen(id); box.setSelectedId(currentId(), juce::dontSendNotification); }
    static constexpr int favouriteId(int k){ return favBase + k + 1; }   // the k-th favourite listed
    // from the editor's timer: the host can change the program behind our back
    void sync(){
        const int want = currentId();
        if (want > 0 && box.getSelectedId() != want) box.setSelectedId(want, juce::dontSendNotification);
    }

private:
    static constexpr int userBase = 1000, noneId = 1999, folderId = 2000, favToggleId = 2001, favBase = 3000;
    std::vector<int> favTargets_;
    int idForKey(const juce::String& key) const {
        const auto name = key.substring(2);
        if (key.startsWith("f:")){
            for (int i = 0; i < proc_.getNumPrograms(); ++i) if (proc_.getProgramName(i) == name) return i + 1;
        } else if (key.startsWith("u:")){
            for (int k = 0; k < files_.size(); ++k) if (files_[k].getFileNameWithoutExtension() == name) return userBase + k + 1;
        }
        return 0;
    }

    int currentId() const {
        const auto name = hooks_.userName();
        if (name.isNotEmpty()){
            for (int k = 0; k < files_.size(); ++k)
                if (files_[k].getFileNameWithoutExtension() == name) return userBase + k + 1;
            return 0;                                           // saved, but not listed yet
        }
        return proc_.getCurrentProgram() + 1;
    }
    void chosen(int id){
        if (id > favBase && id - favBase - 1 < static_cast<int>(favTargets_.size())){
            chosen(favTargets_[static_cast<size_t>(id - favBase - 1)]);
            refill();
            return;
        }
        if (id == favToggleId){ toggleFavourite(); return; }
        if (id == folderId){
            user_.folder().createDirectory();
            user_.folder().startAsProcess();
            refill();                                           // puts the selection back
        }
        else if (id > userBase && id - userBase - 1 < files_.size()) hooks_.load(files_[id - userBase - 1]);
        else if (id > 0 && id <= proc_.getNumPrograms()) proc_.setCurrentProgram(id - 1);
    }
    void saveAs(){
        const auto dir = user_.folder();
        dir.createDirectory();
        const auto name = hooks_.userName();
        const auto suggested = name.isNotEmpty() ? name : proc_.getProgramName(proc_.getCurrentProgram());
        chooser_ = std::make_unique<juce::FileChooser>("Save a " + product_ + " preset",
            dir.getChildFile(UserPresets::safeName(suggested) + ".json"), "*.json");
        chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
            [this](const juce::FileChooser& fc){
                const auto f = fc.getResult();
                if (f == juce::File()) return;
                if (!hooks_.save(f.withFileExtension(".json")))
                    juce::NativeMessageBox::showAsync(
                        juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::WarningIcon)
                            .withTitle(product_).withMessage("The preset could not be saved there."), nullptr);
                refill();
            });
    }

    juce::AudioProcessor& proc_;
    UserPresets& user_;
    Hooks hooks_;
    juce::String product_;
    juce::Array<juce::File> files_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

// Ctrl or Cmd + Z, and Shift or Y to redo, for an editor's keyPressed
inline bool undoKeys(const juce::KeyPress& key, Session& s){
    const auto m = key.getModifiers();
    if (!m.isCommandDown()) return false;
    const auto c = juce::CharacterFunctions::toUpperCase(static_cast<juce::juce_wchar>(key.getKeyCode()));
    if (c == 'Z' && !m.isShiftDown()){ s.undo(); return true; }
    if ((c == 'Z' && m.isShiftDown()) || c == 'Y'){ s.redo(); return true; }
    return false;
}

// ---------------------------------------------------------------- typed values
// What someone typed into a knob's value, read in the units the panel shows:
// "2.2k" (or "2200", "2.2 kHz"), "+3", "-6 dB", "50%", "12 ms", "/4" (the
// downsample readout), "off" (zero) and, where noteNames is set, "A3", "C#2"
// or "A3 +12c" as MIDI note numbers. Anything else after the number (a unit)
// is ignored. False when there is no number to be found.
inline bool parseTyped(const juce::String& typed, bool noteNames, double& out){
    juce::String t = typed.trim().toLowerCase();
    if (t.isEmpty()) return false;
    if (t == "off"){ out = 0.0; return true; }
    if (noteNames && t[0] >= 'a' && t[0] <= 'g'){
        static const int semis[] = { 9, 11, 0, 2, 4, 5, 7 };      // a b c d e f g
        int k = semis[t[0] - 'a'], i = 1;
        if (i < t.length() && t[i] == '#'){ ++k; ++i; }
        else if (i < t.length() && t[i] == 'b'){ --k; ++i; }
        const juce::String rest = t.substring(i).trim();
        if (rest.isEmpty() || !(juce::CharacterFunctions::isDigit(rest[0]) || rest[0] == '-')) return false;
        const int octave = rest.getIntValue();
        double cents = 0.0;
        const int sign = rest.indexOfAnyOf("+-", 1);
        if (sign > 0) cents = rest.substring(sign).getDoubleValue();
        out = (octave + 1) * 12 + k + cents / 100.0;
        return true;
    }
    if (t.startsWithChar('/')) t = t.substring(1).trim();
    if (t.startsWithChar('+')) t = t.substring(1).trim();
    const juce::String number = t.initialSectionContainingOnly("-0123456789.");
    if (number.isEmpty() || !number.containsAnyOf("0123456789")) return false;
    out = number.getDoubleValue();
    if (t.substring(number.length()).trim().startsWithChar('k')) out *= 1000.0;
    return true;
}

// The box a value is typed into: shown over the readout, Enter applies,
// Escape or clicking elsewhere puts it away. The caller does the applying
// (inside a gesture, so it is one undo step) and supplies the colours.
class ValueEntry : public juce::TextEditor {
public:
    ValueEntry(){
        setJustification(juce::Justification::centred);
        setSelectAllWhenFocused(true);
        setTitle("Type a value");
        onReturnKey = [this]{ finish(true); };
        onEscapeKey = [this]{ finish(false); };
        onFocusLost = [this]{ finish(false); };
        setVisible(false);
    }
    // apply returns false for text it could not use, which leaves the box open
    void begin(const juce::String& text, juce::Rectangle<int> where, const juce::Font& font,
               juce::Colour ink, juce::Colour face, std::function<bool(const juce::String&)> apply){
        apply_ = std::move(apply);
        setColour(juce::TextEditor::backgroundColourId, face);
        setColour(juce::TextEditor::textColourId, ink);
        setColour(juce::TextEditor::outlineColourId, ink);
        setColour(juce::TextEditor::focusedOutlineColourId, ink);
        setColour(juce::TextEditor::highlightColourId, ink.withAlpha(0.2f));
        setFont(font);
        setBounds(where);
        setText(text, false);
        setVisible(true);
        toFront(true);
        grabKeyboardFocus();
        selectAll();
    }
    bool isEditing() const { return isVisible(); }
private:
    void finish(bool keep){
        if (!isVisible()) return;
        if (keep && apply_ && !apply_(getText())){ selectAll(); return; }
        setVisible(false);
        if (auto* p = getParentComponent()) p->repaint();
    }
    std::function<bool(const juce::String&)> apply_;
};

} // namespace session
