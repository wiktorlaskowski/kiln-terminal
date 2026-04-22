#include "window.hpp"

#include "config.hpp"

#include <algorithm>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

namespace kiln {

namespace {

// Resolve cwd of a pid by reading /proc/<pid>/cwd. Best-effort; returns empty
// if the pid is invalid, the symlink can't be read, or the target no longer
// exists / isn't a directory we can actually chdir into.
std::string cwd_of_pid(GPid pid) {
    if (pid <= 0) return {};
    std::string link = "/proc/" + std::to_string(pid) + "/cwd";
    std::error_code ec;
    auto target = fs::read_symlink(link, ec);
    if (ec) return {};
    auto path = target.string();
    // Guard against unusual paths like "/foo (deleted)" that /proc emits for
    // processes whose cwd was rmdir'd.
    if (path.find(" (deleted)") != std::string::npos) return {};
    if (!fs::is_directory(target, ec) || ec) return {};
    return path;
}

// Reject shell paths that aren't plausibly safe to exec. This is defense in
// depth: the action is only wired to menu items we built from detect_shells(),
// but a crafted D-Bus activation could still hand us arbitrary strings.
bool is_plausible_shell_path(const std::string& p) {
    if (p.empty()) return false;
    if (p.size() > 4096) return false;
    if (p.front() != '/') return false;                  // must be absolute
    if (p.find('\n') != std::string::npos) return false; // no control chars
    if (p.find('\0') != std::string::npos) return false;
    std::error_code ec;
    auto canon = fs::canonical(p, ec);
    if (ec) return false;
    if (!fs::is_regular_file(canon, ec) || ec) return false;
    return ::access(canon.c_str(), X_OK) == 0;
}

} // namespace

Window::Window(AdwApplication* app) : app_(app) {
    build_ui();
    wire_actions();
}

Window::~Window() = default;

void Window::build_ui() {
    window_ = ADW_APPLICATION_WINDOW(
        adw_application_window_new(GTK_APPLICATION(app_)));
    gtk_window_set_title(GTK_WINDOW(window_), "Terminal");
    gtk_window_set_icon_name(GTK_WINDOW(window_), "utilities-terminal");
    gtk_window_set_default_size(GTK_WINDOW(window_), 960, 620);
    gtk_widget_add_css_class(GTK_WIDGET(window_), "devel-off");

    auto* toolbar = adw_toolbar_view_new();

    // --- Header bar ---
    auto* header = adw_header_bar_new();

    // Split button: primary click opens default-shell tab; dropdown arrow
    // shows a menu of detected shells.
    auto* split = adw_split_button_new();
    adw_split_button_set_icon_name(ADW_SPLIT_BUTTON(split), "tab-new-symbolic");
    gtk_widget_set_tooltip_text(split, "New tab (click) · Choose shell (arrow)");
    adw_split_button_set_menu_model(ADW_SPLIT_BUTTON(split),
        G_MENU_MODEL(build_shell_menu()));
    g_signal_connect(split, "clicked",
        G_CALLBACK(+[](GtkWidget*, gpointer u) {
            static_cast<Window*>(u)->new_default_tab();
        }), this);
    new_tab_button_ = split;
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), split);

    // Primary menu
    auto* menu_btn = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_btn), "open-menu-symbolic");
    gtk_widget_set_tooltip_text(menu_btn, "Main menu");
    auto* primary = g_menu_new();
    auto* edit_section = g_menu_new();
    g_menu_append(edit_section, "Copy",       "win.copy");
    g_menu_append(edit_section, "Paste",      "win.paste");
    g_menu_append(edit_section, "Select All", "win.select-all");
    g_menu_append_section(primary, nullptr, G_MENU_MODEL(edit_section));
    g_object_unref(edit_section);

    auto* split_section = g_menu_new();
    g_menu_append(split_section, "Split Right", "win.split-h");
    g_menu_append(split_section, "Split Down",  "win.split-v");
    g_menu_append_section(primary, nullptr, G_MENU_MODEL(split_section));
    g_object_unref(split_section);

    auto* view_section = g_menu_new();
    g_menu_append(view_section, "Zoom In",    "win.zoom-in");
    g_menu_append(view_section, "Zoom Out",   "win.zoom-out");
    g_menu_append(view_section, "Reset Zoom", "win.zoom-reset");
    g_menu_append_section(primary, nullptr, G_MENU_MODEL(view_section));
    g_object_unref(view_section);

    auto* tab_section = g_menu_new();
    g_menu_append(tab_section, "New Tab",       "win.new-default-tab");
    g_menu_append(tab_section, "Close Tab",     "win.close-tab");
    g_menu_append_section(primary, nullptr, G_MENU_MODEL(tab_section));
    g_object_unref(tab_section);

    auto* about_section = g_menu_new();
    g_menu_append(about_section, "About Kiln Terminal", "app.about");
    g_menu_append_section(primary, nullptr, G_MENU_MODEL(about_section));
    g_object_unref(about_section);

    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_btn),
        G_MENU_MODEL(primary));
    g_object_unref(primary);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), menu_btn);

    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);

    // --- Tab bar ---
    tab_view_ = ADW_TAB_VIEW(adw_tab_view_new());
    tab_bar_  = GTK_WIDGET(adw_tab_bar_new());
    adw_tab_bar_set_view(ADW_TAB_BAR(tab_bar_), tab_view_);
    adw_tab_bar_set_autohide(ADW_TAB_BAR(tab_bar_), TRUE);
    adw_tab_bar_set_expand_tabs(ADW_TAB_BAR(tab_bar_), TRUE);
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), tab_bar_);

    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar),
        GTK_WIDGET(tab_view_));

    adw_application_window_set_content(window_, toolbar);

    g_signal_connect(tab_view_, "close-page",
        G_CALLBACK(&Window::on_close_page), this);
    g_signal_connect(tab_view_, "page-attached",
        G_CALLBACK(&Window::on_page_attached), this);
    g_signal_connect(tab_view_, "notify::selected-page",
        G_CALLBACK(&Window::on_selected_page_changed), this);

    // Watch system dark preference and update color schemes live.
    auto* style_mgr = adw_style_manager_get_default();
    g_signal_connect(style_mgr, "notify::dark",
        G_CALLBACK(&Window::on_dark_changed), this);
}

void Window::wire_actions() {
    using ActionFn = void(*)(Window*);
    using Binding  = std::pair<Window*, ActionFn>;
    struct ActionEntry {
        const char* name;
        ActionFn fn;
    };

    static const ActionEntry entries[] = {
        {"copy",            [](Window* w){ if (auto* t = w->focused_tab()) t->copy_clipboard(); }},
        {"paste",           [](Window* w){ if (auto* t = w->focused_tab()) t->paste_clipboard(); }},
        {"select-all",      [](Window* w){ if (auto* t = w->focused_tab()) vte_terminal_select_all(t->vte()); }},
        {"zoom-in",         [](Window* w){ if (auto* t = w->focused_tab()) t->zoom_in(); }},
        {"zoom-out",        [](Window* w){ if (auto* t = w->focused_tab()) t->zoom_out(); }},
        {"zoom-reset",      [](Window* w){ if (auto* t = w->focused_tab()) t->zoom_reset(); }},
        {"new-default-tab", [](Window* w){ w->new_default_tab(); }},
        {"close-tab",       [](Window* w){ w->close_current_tab(); }},
        {"split-h",         [](Window* w){ w->split_current(GTK_ORIENTATION_HORIZONTAL); }},
        {"split-v",         [](Window* w){ w->split_current(GTK_ORIENTATION_VERTICAL); }},
    };

    for (const auto& e : entries) {
        auto* act = g_simple_action_new(e.name, nullptr);
        auto* binding = new Binding(this, e.fn);
        g_signal_connect_data(act, "activate",
            G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer u){
                auto* b = static_cast<Binding*>(u);
                b->second(b->first);
            }),
            binding,
            +[](gpointer data, GClosure*) {
                delete static_cast<Binding*>(data);
            },
            GConnectFlags(0));
        g_action_map_add_action(G_ACTION_MAP(window_), G_ACTION(act));
        g_object_unref(act);
    }

    // Parameterized action: spawn a tab with a specific shell path.
    auto* spawn_act = g_simple_action_new("new-tab-shell",
        G_VARIANT_TYPE_STRING);
    g_signal_connect(spawn_act, "activate",
        G_CALLBACK(+[](GSimpleAction*, GVariant* param, gpointer u){
            auto* w = static_cast<Window*>(u);
            gsize len = 0;
            const char* raw = g_variant_get_string(param, &len);
            if (!raw || len == 0 || len > 4096) { w->new_default_tab(); return; }
            std::string path(raw, len);
            if (!is_plausible_shell_path(path)) { w->new_default_tab(); return; }
            // Only allow shells we actually detected, so a rogue activation
            // can't use this action to exec arbitrary binaries.
            ShellInfo chosen;
            for (const auto& s : detect_shells()) {
                if (s.path == path) { chosen = s; break; }
            }
            if (chosen.path.empty()) { w->new_default_tab(); return; }
            w->new_tab(chosen);
        }), this);
    g_action_map_add_action(G_ACTION_MAP(window_), G_ACTION(spawn_act));
    g_object_unref(spawn_act);

    // Keyboard shortcuts
    auto add_accel = [&](const char* detailed, const char* const* accels) {
        gtk_application_set_accels_for_action(GTK_APPLICATION(app_),
            detailed, accels);
    };
    const char* a_new[]    = {"<Primary><Shift>t", nullptr};
    const char* a_close[]  = {"<Primary><Shift>w", nullptr};
    const char* a_copy[]   = {"<Primary><Shift>c", nullptr};
    const char* a_paste[]  = {"<Primary><Shift>v", nullptr};
    const char* a_sel[]    = {"<Primary><Shift>a", nullptr};
    const char* a_zin[]    = {"<Primary>plus", "<Primary>equal", nullptr};
    const char* a_zout[]   = {"<Primary>minus", nullptr};
    const char* a_zres[]   = {"<Primary>0", nullptr};
    const char* a_sph[]    = {"<Primary><Shift>d", nullptr};
    const char* a_spv[]    = {"<Primary><Shift>e", nullptr};

    add_accel("win.new-default-tab", a_new);
    add_accel("win.close-tab",       a_close);
    add_accel("win.copy",            a_copy);
    add_accel("win.paste",           a_paste);
    add_accel("win.select-all",      a_sel);
    add_accel("win.zoom-in",         a_zin);
    add_accel("win.zoom-out",        a_zout);
    add_accel("win.zoom-reset",      a_zres);
    add_accel("win.split-h",         a_sph);
    add_accel("win.split-v",         a_spv);
}

GMenu* Window::build_shell_menu() {
    auto* menu = g_menu_new();
    auto* sect = g_menu_new();

    auto shells = detect_shells();
    if (shells.empty()) shells.push_back(default_shell());

    for (const auto& s : shells) {
        std::string label = s.display;
        if (!s.path.empty()) label += "  —  " + s.path;
        auto* item = g_menu_item_new(label.c_str(), nullptr);
        g_menu_item_set_action_and_target_value(item,
            "win.new-tab-shell",
            g_variant_new_string(s.path.c_str()));
        // Theme icon for the shell (e.g. utilities-terminal).
        auto* icon = g_themed_icon_new_with_default_fallbacks(
            (s.icon + "-symbolic").c_str());
        g_menu_item_set_icon(item, G_ICON(icon));
        g_object_unref(icon);
        g_menu_append_item(sect, item);
        g_object_unref(item);
    }

    g_menu_append_section(menu, "Open new tab with…", G_MENU_MODEL(sect));
    g_object_unref(sect);
    return menu;
}

void Window::refresh_shell_menu() {
    if (new_tab_button_) {
        adw_split_button_set_menu_model(ADW_SPLIT_BUTTON(new_tab_button_),
            G_MENU_MODEL(build_shell_menu()));
    }
}

void Window::present() {
    gtk_window_present(GTK_WINDOW(window_));
}

std::string Window::cwd_of(const TerminalTab* t) const {
    if (!t) return {};
    auto* pty = vte_terminal_get_pty(const_cast<VteTerminal*>(t->vte()));
    if (!pty) return {};
    int fd = vte_pty_get_fd(pty);
    if (fd < 0) return {};
    GPid fg_pid = tcgetpgrp(fd);
    if (fg_pid <= 0) return {};
    return cwd_of_pid(fg_pid);
}

TerminalTab* Window::tab_on_page(AdwTabPage* page) const {
    if (!page) return nullptr;
    for (const auto& t : tabs_) {
        if (t->page() == page) return t.get();
    }
    return nullptr;
}

TerminalTab* Window::focused_tab() const {
    // Walk up from the current keyboard-focus widget to see if it (or one of
    // its ancestors) is the VteTerminal belonging to a known tab.
    auto* focus = gtk_window_get_focus(GTK_WINDOW(window_));
    while (focus) {
        for (const auto& t : tabs_) {
            if (t->vte_widget() == focus) return t.get();
        }
        focus = gtk_widget_get_parent(focus);
    }
    // Fallback: first tab on the currently selected page.
    return tab_on_page(adw_tab_view_get_selected_page(tab_view_));
}

void Window::erase_tab(TerminalTab* t) {
    for (auto it = tabs_.begin(); it != tabs_.end(); ++it) {
        if (it->get() == t) { tabs_.erase(it); return; }
    }
}

void Window::new_default_tab() {
    new_tab(default_shell());
}

void Window::new_tab(const ShellInfo& shell_in) {
    // Re-validate: the shell binary could have been removed between the menu
    // being built and the user clicking it (e.g. during a package upgrade).
    ShellInfo shell = shell_in;
    if (!is_plausible_shell_path(shell.path)) {
        shell = default_shell();
        if (!is_plausible_shell_path(shell.path)) {
            GtkWidget* dlg = adw_message_dialog_new(GTK_WINDOW(window_),
                "Shell not available",
                "The selected shell is not installed or not executable.");
            adw_message_dialog_add_response(ADW_MESSAGE_DIALOG(dlg),
                "ok", "_OK");
            gtk_window_present(GTK_WINDOW(dlg));
            return;
        }
    }

    auto cwd = cwd_of(focused_tab());
    auto tab = std::make_unique<TerminalTab>(shell, cwd);

    // Wrap in a box so we can later replace the single child with a GtkPaned
    // when the user splits. AdwTabPage doesn't allow swapping its child.
    auto* wrapper = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(wrapper, TRUE);
    gtk_widget_set_vexpand(wrapper, TRUE);
    gtk_box_append(GTK_BOX(wrapper), tab->widget());

    auto* page = adw_tab_view_append(tab_view_, wrapper);

    // Title + icon on the tab.
    adw_tab_page_set_title(page, shell.display.c_str());
    auto* icon = g_themed_icon_new_with_default_fallbacks(
        (shell.icon + "-symbolic").c_str());
    adw_tab_page_set_icon(page, G_ICON(icon));
    g_object_unref(icon);

    tab->set_page(page);
    tab->set_close_handler(&Window::tab_close_handler, this);
    tab->spawn();

    adw_tab_view_set_selected_page(tab_view_, page);
    tab->focus_terminal();

    tabs_.push_back(std::move(tab));
}

void Window::split_current(GtkOrientation orient) {
    auto* focused = focused_tab();
    if (!focused) return;
    auto* page = focused->page();
    if (!page) return;

    auto def = default_shell();
    if (!is_plausible_shell_path(def.path)) return;

    auto cwd = cwd_of(focused);
    auto new_tab_ptr = std::make_unique<TerminalTab>(def, cwd);
    new_tab_ptr->set_page(page);
    new_tab_ptr->set_close_handler(&Window::tab_close_handler, this);

    GtkWidget* old_widget = focused->widget();
    GtkWidget* parent = gtk_widget_get_parent(old_widget);
    if (!parent) return;

    // Remember where in the parent old_widget sat, then detach it.
    bool parent_is_paned = GTK_IS_PANED(parent);
    bool was_start_slot  = false;
    g_object_ref(old_widget);
    if (parent_is_paned) {
        auto* p = GTK_PANED(parent);
        was_start_slot = (gtk_paned_get_start_child(p) == old_widget);
        if (was_start_slot) gtk_paned_set_start_child(p, nullptr);
        else                gtk_paned_set_end_child(p, nullptr);
    } else if (GTK_IS_BOX(parent)) {
        gtk_box_remove(GTK_BOX(parent), old_widget);
    } else {
        g_object_unref(old_widget);
        return;
    }

    // Build the GtkPaned holding the old terminal and the new one.
    auto* paned = gtk_paned_new(orient);
    gtk_paned_set_resize_start_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_resize_end_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_start_child(GTK_PANED(paned), old_widget);
    gtk_paned_set_end_child(GTK_PANED(paned), new_tab_ptr->widget());
    g_object_unref(old_widget);

    // Re-attach the paned to wherever old_widget used to live.
    if (parent_is_paned) {
        auto* p = GTK_PANED(parent);
        if (was_start_slot) gtk_paned_set_start_child(p, paned);
        else                gtk_paned_set_end_child(p, paned);
    } else {
        gtk_box_append(GTK_BOX(parent), paned);
    }

    new_tab_ptr->spawn();
    new_tab_ptr->focus_terminal();
    tabs_.push_back(std::move(new_tab_ptr));
}

void Window::close_current_tab() {
    auto* page = adw_tab_view_get_selected_page(tab_view_);
    if (page) adw_tab_view_close_page(tab_view_, page);
}

gboolean Window::on_close_page(AdwTabView* view, AdwTabPage* page, gpointer user) {
    auto* self = static_cast<Window*>(user);
    // Remove every tab that belongs to this page (there can be several after a
    // split). The GtkBox/GtkPaned tree under the page is destroyed by GTK when
    // the page is finalized, which detaches the VteTerminals for us.
    self->tabs_.erase(
        std::remove_if(self->tabs_.begin(), self->tabs_.end(),
            [page](const std::unique_ptr<TerminalTab>& t) {
                return t->page() == page;
            }),
        self->tabs_.end());

    adw_tab_view_close_page_finish(view, page, TRUE);

    if (adw_tab_view_get_n_pages(view) == 0) {
        gtk_window_close(GTK_WINDOW(self->window_));
    }
    return GDK_EVENT_STOP;
}

void Window::on_page_attached(AdwTabView*, AdwTabPage* page, int, gpointer) {
    adw_tab_page_set_needs_attention(page, FALSE);
}

void Window::on_selected_page_changed(GObject*, GParamSpec*, gpointer user) {
    auto* self = static_cast<Window*>(user);
    auto* page = adw_tab_view_get_selected_page(self->tab_view_);
    if (!page) return;
    adw_tab_page_set_needs_attention(page, FALSE);
    if (auto* t = self->tab_on_page(page)) t->focus_terminal();
}

void Window::on_dark_changed(GObject*, GParamSpec*, gpointer user) {
    static_cast<Window*>(user)->update_color_scheme();
}

void Window::update_color_scheme() {
    auto* style_mgr = adw_style_manager_get_default();
    bool dark = adw_style_manager_get_dark(style_mgr);
    for (auto& t : tabs_) t->apply_color_scheme(dark);
}

void Window::tab_close_handler(TerminalTab* tab, void* user) {
    auto* self = static_cast<Window*>(user);
    if (!tab || !tab->page()) return;

    GtkWidget* w = tab->widget();
    GtkWidget* parent = gtk_widget_get_parent(w);

    // If this terminal lives inside a GtkPaned, collapse the paned: remove
    // the dying terminal and promote its sibling to the paned's parent slot.
    if (parent && GTK_IS_PANED(parent)) {
        auto* paned = GTK_PANED(parent);
        GtkWidget* start = gtk_paned_get_start_child(paned);
        GtkWidget* end   = gtk_paned_get_end_child(paned);
        GtkWidget* sibling = (start == w) ? end : start;
        if (sibling) {
            g_object_ref(sibling);
            // Detach both children so we can reparent the sibling.
            gtk_paned_set_start_child(paned, nullptr);
            gtk_paned_set_end_child(paned, nullptr);

            GtkWidget* gp = gtk_widget_get_parent(GTK_WIDGET(paned));
            if (gp && GTK_IS_PANED(gp)) {
                auto* gpp = GTK_PANED(gp);
                if (gtk_paned_get_start_child(gpp) == GTK_WIDGET(paned)) {
                    gtk_paned_set_start_child(gpp, sibling);
                } else {
                    gtk_paned_set_end_child(gpp, sibling);
                }
            } else if (gp && GTK_IS_BOX(gp)) {
                gtk_box_remove(GTK_BOX(gp), GTK_WIDGET(paned));
                gtk_box_append(GTK_BOX(gp), sibling);
            }
            g_object_unref(sibling);
        }
        // The dying tab's widget is now orphaned; drop it.
        self->erase_tab(tab);
        return;
    }

    // Otherwise this was the only terminal on the page — close the page.
    adw_tab_view_close_page(self->tab_view_, tab->page());
}

} // namespace kiln
