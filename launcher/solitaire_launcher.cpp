#include <gtk/gtk.h>
#include <cstdlib>

struct GameButton {
    const char *label;
    const char *exe;
    const char *icon;
};

static GameButton games[] = {
    { "Solitaire",   "solitaire.exe",   "♠" },
    { "FreeCell",    "freecell.exe",    "♣" },
    { "Spider",      "spider.exe",      "🕷" },
    { "Minesweeper", "minesweeper.exe", "💣" },
    { "Pyramid",     "pyramid.exe",     "🔺" },
};

static void launch_game(GtkWidget *widget, gpointer data) {
    const char *exe = (const char *)data;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "wine \"%s\" &", exe);  // use wine, or just exe if native Windows
    system(cmd);
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Solitaire Essentials");
    gtk_window_set_default_size(GTK_WINDOW(window), 320, 420);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // Apply CSS
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "window {"
        "  background-color: #1a2a1a;"
        "}"
        "label#title {"
        "  font-family: 'Georgia', serif;"
        "  font-size: 22px;"
        "  font-weight: bold;"
        "  color: #000000;"
        "  letter-spacing: 2px;"
        "}"
        "label#subtitle {"
        "  font-family: 'Georgia', serif;"
        "  font-size: 11px;"
        "  color: #6a8a6a;"
        "  letter-spacing: 4px;"
        "}"
        ".game-btn {"
        "  font-family: 'Georgia', serif;"
        "  font-size: 15px;"
        "  color: #2a2a2a;"
        "  background-color: #243024;"
        "  border: 1px solid #3a5a3a;"
        "  border-radius: 6px;"
        "  padding: 14px 10px;"
        "  transition: background-color 150ms, border-color 150ms;"
        "}"
        ".game-btn:hover {"
        "  background-color: #2e4a2e;"
        "  border-color: #c8a84b;"
        "  color: #000000;"
        "}",
        -1, NULL);

    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    // Layout
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 28);
    gtk_container_add(GTK_CONTAINER(window), outer);

    // Title
    GtkWidget *title = gtk_label_new("SOLITAIRE");
    gtk_widget_set_name(title, "title");
    gtk_box_pack_start(GTK_BOX(outer), title, FALSE, FALSE, 4);

    GtkWidget *subtitle = gtk_label_new("ESSENTIALS");
    gtk_widget_set_name(subtitle, "subtitle");
    gtk_box_pack_start(GTK_BOX(outer), subtitle, FALSE, FALSE, 0);

    // Divider
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(outer), sep, FALSE, FALSE, 20);

    // Game buttons
    int n = sizeof(games) / sizeof(games[0]);
    for (int i = 0; i < n; i++) {
        char label_text[64];
        snprintf(label_text, sizeof(label_text), "%s  %s", games[i].icon, games[i].label);

        GtkWidget *btn = gtk_button_new_with_label(label_text);
        gtk_style_context_add_class(gtk_widget_get_style_context(btn), "game-btn");
        gtk_widget_set_size_request(btn, -1, 52);
        g_signal_connect(btn, "clicked", G_CALLBACK(launch_game), (gpointer)games[i].exe);
        gtk_box_pack_start(GTK_BOX(outer), btn, FALSE, FALSE, 5);
    }

    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
