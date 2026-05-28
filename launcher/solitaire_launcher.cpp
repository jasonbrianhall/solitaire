#include <gtk/gtk.h>
#include <cstdlib>
#include <windows.h>

struct GameButton {
    const char *label;
    const char *exe;
    const char *icon;
};

static GameButton games[] = {
    { "Solitaire",   "solitaire.exe",   "\xe2\x99\xa0" },  // ♠
    { "FreeCell",    "freecell.exe",    "\xe2\x99\xa3" },  // ♣
    { "Spider",      "spider.exe",      "\xe2\x99\xa6" },  // ♦
    { "Minesweeper", "minesweeper.exe", "\xe2\x98\x85" },  // ★
    { "Pyramid",     "pyramid.exe",     "\xe2\x99\xa5" },  // ♥
};

// Returns the directory containing this .exe, with trailing backslash
static void get_exe_dir(char *out, size_t out_size) {
    GetModuleFileNameA(NULL, out, (DWORD)out_size);
    char *last = strrchr(out, '\\');
    if (last) *(last + 1) = '\0';
    else out[0] = '\0';
}

static void launch_game(GtkWidget *widget, gpointer data) {
    const char *exe = (const char *)data;
    char dir[MAX_PATH];
    char full_path[MAX_PATH];
    get_exe_dir(dir, sizeof(dir));
    snprintf(full_path, sizeof(full_path), "%s%s", dir, exe);
    ShellExecuteA(NULL, "open", full_path, NULL, dir, SW_SHOW);
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Solitaire Essentials");
    gtk_window_set_default_size(GTK_WINDOW(window), 480, 600);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "window {"
        "  background-color: #162016;"
        "}"
        "#header-box {"
        "  background-color: #1e2e1e;"
        "  border-bottom: 2px solid #c8a84b;"
        "  padding: 28px 0 20px 0;"
        "}"
        "label#title {"
        "  font-family: 'Palatino Linotype', 'Palatino', 'Book Antiqua', serif;"
        "  font-size: 36px;"
        "  font-weight: bold;"
        "  color: #c8a84b;"
        "  letter-spacing: 6px;"
        "}"
        "label#subtitle {"
        "  font-family: 'Palatino Linotype', 'Palatino', 'Book Antiqua', serif;"
        "  font-size: 12px;"
        "  color: #7a9a7a;"
        "  letter-spacing: 8px;"
        "  margin-top: 2px;"
        "}"
        "label#suits {"
        "  font-size: 20px;"
        "  color: #3a5a3a;"
        "  letter-spacing: 10px;"
        "  margin-top: 10px;"
        "}"
        "#button-area {"
        "  padding: 24px 36px 28px 36px;"
        "}"
        ".game-btn {"
        "  font-family: 'Palatino Linotype', 'Palatino', 'Book Antiqua', serif;"
        "  font-size: 17px;"
        "  font-weight: bold;"
        "  color: #1a1a1a;"
        "  background-color: #c8b87a;"
        "  background-image: linear-gradient(180deg, #d4c688 0%, #b8a055 100%);"
        "  border: none;"
        "  border-radius: 8px;"
        "  padding: 0;"
        "  box-shadow: 0 3px 8px rgba(0,0,0,0.5), inset 0 1px 0 rgba(255,255,255,0.15);"
        "  transition: all 150ms ease;"
        "}"
        ".game-btn:hover {"
        "  background-image: linear-gradient(180deg, #e0d498 0%, #c8a84b 100%);"
        "  color: #000000;"
        "  box-shadow: 0 5px 14px rgba(0,0,0,0.6), inset 0 1px 0 rgba(255,255,255,0.2);"
        "}"
        ".game-btn:active {"
        "  background-image: linear-gradient(180deg, #a89040 0%, #c8b060 100%);"
        "  box-shadow: 0 1px 4px rgba(0,0,0,0.5);"
        "}",
        -1, NULL);

    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(header, "header-box");
    gtk_box_pack_start(GTK_BOX(root), header, FALSE, FALSE, 0);

    GtkWidget *title = gtk_label_new("SOLITAIRE");
    gtk_widget_set_name(title, "title");
    gtk_box_pack_start(GTK_BOX(header), title, FALSE, FALSE, 0);

    GtkWidget *subtitle = gtk_label_new("ESSENTIALS");
    gtk_widget_set_name(subtitle, "subtitle");
    gtk_box_pack_start(GTK_BOX(header), subtitle, FALSE, FALSE, 0);

    GtkWidget *suits = gtk_label_new("\xe2\x99\xa0  \xe2\x99\xa5  \xe2\x99\xa3  \xe2\x99\xa6");
    gtk_widget_set_name(suits, "suits");
    gtk_box_pack_start(GTK_BOX(header), suits, FALSE, FALSE, 0);

    GtkWidget *btn_area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_name(btn_area, "button-area");
    gtk_container_set_border_width(GTK_CONTAINER(btn_area), 36);
    gtk_box_pack_start(GTK_BOX(root), btn_area, TRUE, TRUE, 0);

    int n = sizeof(games) / sizeof(games[0]);
    for (int i = 0; i < n; i++) {
        char label_text[64];
        snprintf(label_text, sizeof(label_text), "%s   %s", games[i].icon, games[i].label);

        GtkWidget *btn = gtk_button_new_with_label(label_text);
        gtk_style_context_add_class(gtk_widget_get_style_context(btn), "game-btn");
        gtk_widget_set_size_request(btn, -1, 64);
        g_signal_connect(btn, "clicked", G_CALLBACK(launch_game), (gpointer)games[i].exe);
        gtk_box_pack_start(GTK_BOX(btn_area), btn, FALSE, FALSE, 0);
    }

    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
