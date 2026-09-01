#pragma once

// Editor for the msposd widget theme.
//
// Every control writes into an OsdTheme and marks the tab dirty; update() saves
// the file once the changes stop coming. msposd notices the new mtime and
// reloads on its next frame, so a slider move shows up on the video feed without
// restarting anything.
//
// Saving is debounced rather than immediate because a slider drag produces a
// change per frame, and each one would otherwise be a file write plus a full
// re-parse on msposd's side.
//
// The theme picker at the top is the exception: it replaces the theme file with
// a copy of the chosen one and reloads, so it applies at once rather than
// through the debounce. Where the themes are looked for, and why picking one is
// a reset rather than a layer, is written down in msposd's
// osd/widgets/README.md under "Picking one" - PixelPilot's gsmenu follows the
// same rule and the pilot should get the same list from either.

#include <functional>
#include <string>
#include <vector>

#include "../gui_interface.h"
#include "osd_theme_model.h"

#include <vecgui/app.h>

class OsdContainer : public vecgui::MarginContainer {
public:
    void on_ready() override;

    void update(double dt) override;

private:
    OsdTheme theme_;
    std::string theme_path_;

    bool dirty_ = false;
    double idle_since_change_ = 0.0;

    /// vecgui's TextEdit has no change signal, so the fields are polled. Cheap:
    /// a couple of dozen string compares once a frame.
    struct TextBinding {
        std::shared_ptr<vecgui::TextEdit> edit;
        std::string last;
        std::function<void(const std::string&)> apply;
    };
    std::vector<TextBinding> text_bindings_;

    /// Pushes the model back into the widgets. Only needed by "reset to
    /// defaults", which changes everything at once.
    std::vector<std::function<void()>> refreshers_;

    std::shared_ptr<vecgui::Label> status_label_;

    void mark_dirty();
    void save_now();
    void set_status(const std::string& text);

    // --- builders, all of which append one labelled row to `parent`
    std::shared_ptr<vecgui::VBoxContainer> add_section(const std::shared_ptr<vecgui::Node>& parent,
                                                       const std::string& title,
                                                       bool collapsed);

    void add_check(const std::shared_ptr<vecgui::Node>& parent,
                   const std::string& label,
                   const std::function<bool()>& get,
                   const std::function<void(bool)>& set);

    void add_slider(const std::shared_ptr<vecgui::Node>& parent,
                    const std::string& label,
                    float min_value,
                    float max_value,
                    bool integer,
                    const std::function<float()>& get,
                    const std::function<void(float)>& set);

    void add_number(const std::shared_ptr<vecgui::Node>& parent,
                    const std::string& label,
                    const std::function<float()>& get,
                    const std::function<void(float)>& set);

    void add_string(const std::shared_ptr<vecgui::Node>& parent,
                    const std::string& label,
                    const std::function<std::string()>& get,
                    const std::function<void(const std::string&)>& set);

    void add_choice(const std::shared_ptr<vecgui::Node>& parent,
                    const std::string& label,
                    const std::vector<std::string>& items,
                    const std::function<int()>& get,
                    const std::function<void(int)>& set);

    void add_color(const std::shared_ptr<vecgui::Node>& parent, const OsdColorField& field);
};
