#include "application.hpp"

#include "config.hpp"

namespace kiln {

Application::Application() {
    app_ = adw_application_new(KILN_APP_ID, G_APPLICATION_DEFAULT_FLAGS);
    // Fallback app icon for the taskbar / window list on compositors that
    // read GTK's default icon name rather than the .desktop file.
    gtk_window_set_default_icon_name("utilities-terminal");
    g_signal_connect(app_, "activate", G_CALLBACK(&Application::on_activate), this);

    auto* about = g_simple_action_new("about", nullptr);
    g_signal_connect(about, "activate",
        G_CALLBACK(&Application::on_about), this);
    g_action_map_add_action(G_ACTION_MAP(app_), G_ACTION(about));
    g_object_unref(about);

    auto* quit = g_simple_action_new("quit", nullptr);
    g_signal_connect(quit, "activate",
        G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer u) {
            g_application_quit(G_APPLICATION(u));
        }), app_);
    g_action_map_add_action(G_ACTION_MAP(app_), G_ACTION(quit));
    g_object_unref(quit);

    const char* quit_accels[] = {"<Primary><Shift>q", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app_),
        "app.quit", quit_accels);
}

Application::~Application() {
    if (app_) g_object_unref(app_);
}

int Application::run(int argc, char** argv) {
    return g_application_run(G_APPLICATION(app_), argc, argv);
}

void Application::on_activate(GApplication*, gpointer user) {
    auto* self = static_cast<Application*>(user);
    if (!self->window_) {
        self->window_ = std::make_unique<Window>(self->app_);
        self->window_->new_default_tab();
    }
    self->window_->present();
}

void Application::on_about(GSimpleAction*, GVariant*, gpointer user) {
    auto* self = static_cast<Application*>(user);
    const char* developers[] = {"Kiln Terminal contributors", nullptr};

    auto* win = gtk_application_get_active_window(GTK_APPLICATION(self->app_));
    auto* about = adw_about_window_new();
    adw_about_window_set_application_name(ADW_ABOUT_WINDOW(about), KILN_APP_NAME);
    adw_about_window_set_application_icon(ADW_ABOUT_WINDOW(about),
        "utilities-terminal");
    adw_about_window_set_version(ADW_ABOUT_WINDOW(about), KILN_APP_VERSION);
    adw_about_window_set_developer_name(ADW_ABOUT_WINDOW(about),
        "Kiln Terminal");
    adw_about_window_set_developers(ADW_ABOUT_WINDOW(about), developers);
    adw_about_window_set_license_type(ADW_ABOUT_WINDOW(about),
        GTK_LICENSE_MIT_X11);
    adw_about_window_set_comments(ADW_ABOUT_WINDOW(about),
        "A modern GTK4 terminal emulator with multi-shell support.");
    gtk_window_set_transient_for(GTK_WINDOW(about), GTK_WINDOW(win));
    gtk_window_present(GTK_WINDOW(about));
}

} // namespace kiln
