/* flow-window.c - With tabs and file explorer */

#include "config.h"
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gtksourceview/gtksource.h>
#include <string.h>
#include "flow-window.h"

typedef struct {
    GtkSourceView *text_view;
    GtkScrolledWindow *scrolled;
    GFile *file;
    gboolean modified;
} TabData;

struct _FlowWindow
{
    AdwApplicationWindow parent_instance;
    
    AdwNavigationSplitView *split_view;
    AdwTabView *tab_view;
    AdwTabBar *tab_bar;
    GtkListBox *file_list;
    GtkLabel *folder_label;
    GtkButton *open_folder_button;
    GtkLabel *status_label;
    GtkLabel *position_label;
    GtkLabel *stats_label;
    AdwWindowTitle *title_widget;
    
    GtkRevealer *search_revealer;
    GtkSearchEntry *search_entry;
    GtkEntry *replace_entry;
    GtkBox *replace_box;
    GtkButton *find_previous_button;
    GtkButton *find_next_button;
    GtkButton *close_search_button;
    GtkButton *replace_button;
    GtkButton *replace_all_button;
    
    GFile *current_folder;
    gdouble font_scale;
    GtkCssProvider *font_provider;
    gboolean dark_mode;
};

G_DEFINE_FINAL_TYPE (FlowWindow, flow_window, ADW_TYPE_APPLICATION_WINDOW)

/* Forward declarations */
static TabData* tab_data_new (void);
static void tab_data_free (TabData *data);
static TabData* get_current_tab_data (FlowWindow *self);
static GtkSourceView* get_current_text_view (FlowWindow *self);
static void create_new_tab (FlowWindow *self, const gchar *title, GFile *file);
static void setup_actions (FlowWindow *self);
static void apply_theme (FlowWindow *self);
static void load_folder (FlowWindow *self, GFile *folder);
static void on_tab_close_request (AdwTabView *view, AdwTabPage *page, FlowWindow *self);
static void on_file_row_activated (GtkListBox *box, GtkListBoxRow *row, FlowWindow *self);
static void on_page_attached (AdwTabView *view, AdwTabPage *page, gint position, FlowWindow *self);
static void on_selected_page_changed (GObject *object, GParamSpec *pspec, FlowWindow *self);
static void update_stats (FlowWindow *self);

/* Actions */
static void on_folder_dialog_response (GObject *source, GAsyncResult *result, gpointer user_data);
static void action_new_file (GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_open_file (GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_open_folder (GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_save_file (GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_close_tab (GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_toggle_theme (GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_zoom_in (GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_zoom_out (GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_zoom_reset (GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_find (GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_replace (GSimpleAction *action, GVariant *parameter, gpointer user_data);

static TabData*
tab_data_new (void)
{
    TabData *data = g_new0 (TabData, 1);
    GtkSourceBuffer *buffer;
    
    data->text_view = GTK_SOURCE_VIEW (gtk_source_view_new ());
    buffer = GTK_SOURCE_BUFFER (gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->text_view)));
    
    gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (data->text_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_top_margin (GTK_TEXT_VIEW (data->text_view), 12);
    gtk_text_view_set_bottom_margin (GTK_TEXT_VIEW (data->text_view), 12);
    gtk_text_view_set_left_margin (GTK_TEXT_VIEW (data->text_view), 12);
    gtk_text_view_set_right_margin (GTK_TEXT_VIEW (data->text_view), 12);
    gtk_source_view_set_tab_width (data->text_view, 4);
    gtk_source_view_set_insert_spaces_instead_of_tabs (data->text_view, TRUE);
    gtk_source_view_set_show_line_numbers (data->text_view, TRUE);
    gtk_source_view_set_highlight_current_line (data->text_view, TRUE);
    gtk_source_view_set_auto_indent (data->text_view, TRUE);
    
    data->scrolled = GTK_SCROLLED_WINDOW (gtk_scrolled_window_new ());
    gtk_scrolled_window_set_child (data->scrolled, GTK_WIDGET (data->text_view));
    
    data->file = NULL;
    data->modified = FALSE;
    
    return data;
}

static void
tab_data_free (TabData *data)
{
    if (data->file)
        g_object_unref (data->file);
    g_free (data);
}

static TabData*
get_current_tab_data (FlowWindow *self)
{
    AdwTabPage *page = adw_tab_view_get_selected_page (self->tab_view);
    if (!page)
        return NULL;
    return g_object_get_data (G_OBJECT (page), "tab-data");
}

static GtkSourceView*
get_current_text_view (FlowWindow *self)
{
    TabData *data = get_current_tab_data (self);
    return data ? data->text_view : NULL;
}

static void
create_new_tab (FlowWindow *self, const gchar *title, GFile *file)
{
    TabData *data;
    AdwTabPage *page;
    GtkSourceBuffer *buffer;
    
    data = tab_data_new ();
    data->file = file ? g_object_ref (file) : NULL;
    
    page = adw_tab_view_append (self->tab_view, GTK_WIDGET (data->scrolled));
    adw_tab_page_set_title (page, title);
    adw_tab_page_set_indicator_icon (page, NULL);
    
    g_object_set_data_full (G_OBJECT (page), "tab-data", data, (GDestroyNotify) tab_data_free);
    
    if (file) {
        GtkSourceLanguageManager *lm = gtk_source_language_manager_get_default ();
        GtkSourceLanguage *lang = gtk_source_language_manager_guess_language (lm, g_file_get_basename (file), NULL);
        buffer = GTK_SOURCE_BUFFER (gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->text_view)));
        if (lang)
            gtk_source_buffer_set_language (buffer, lang);
    }
    
    apply_theme (self);
    adw_tab_view_set_selected_page (self->tab_view, page);
}

static void
apply_theme (FlowWindow *self)
{
    GtkSourceStyleSchemeManager *sm;
    GtkSourceStyleScheme *scheme;
    AdwStyleManager *style_manager;
    const gchar *scheme_name;
    guint n_pages, i;
    
    style_manager = adw_style_manager_get_default ();
    
    if (self->dark_mode) {
        adw_style_manager_set_color_scheme (style_manager, ADW_COLOR_SCHEME_FORCE_DARK);
        scheme_name = "Adwaita-dark";
    } else {
        adw_style_manager_set_color_scheme (style_manager, ADW_COLOR_SCHEME_FORCE_LIGHT);
        scheme_name = "Adwaita";
    }
    
    sm = gtk_source_style_scheme_manager_get_default ();
    scheme = gtk_source_style_scheme_manager_get_scheme (sm, scheme_name);
    
    if (!scheme)
        return;
    
    n_pages = adw_tab_view_get_n_pages (self->tab_view);
    for (i = 0; i < n_pages; i++) {
        AdwTabPage *page = adw_tab_view_get_nth_page (self->tab_view, i);
        TabData *data = g_object_get_data (G_OBJECT (page), "tab-data");
        if (data) {
            GtkSourceBuffer *buffer = GTK_SOURCE_BUFFER (gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->text_view)));
            gtk_source_buffer_set_style_scheme (buffer, scheme);
        }
    }
}

static void
load_folder (FlowWindow *self, GFile *folder)
{
    GFileEnumerator *enumerator;
    GError *error = NULL;
    GFileInfo *info;
    GtkWidget *child;
    
    while ((child = gtk_widget_get_first_child (GTK_WIDGET (self->file_list))))
        gtk_list_box_remove (self->file_list, child);
    
    enumerator = g_file_enumerate_children (folder, G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE, 
                                            G_FILE_QUERY_INFO_NONE, NULL, &error);
    if (!enumerator) {
        g_warning ("Failed to enumerate folder: %s", error->message);
        g_error_free (error);
        return;
    }
    
    gtk_widget_set_visible (GTK_WIDGET (self->file_list), TRUE);
    gtk_widget_set_visible (GTK_WIDGET (self->folder_label), FALSE);
    
    gchar *path = g_file_get_path (folder);
    gtk_label_set_text (self->folder_label, path);
    g_free (path);
    
    while ((info = g_file_enumerator_next_file (enumerator, NULL, &error)) != NULL) {
        const gchar *name = g_file_info_get_name (info);
        GFileType type = g_file_info_get_file_type (info);
        
        if (type == G_FILE_TYPE_REGULAR) {
            GtkWidget *row = gtk_list_box_row_new ();
            GtkWidget *label = gtk_label_new (name);
            gtk_label_set_xalign (GTK_LABEL (label), 0);
            gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), label);
            
            GFile *file = g_file_get_child (folder, name);
            g_object_set_data_full (G_OBJECT (row), "file", file, g_object_unref);
            
            gtk_list_box_append (self->file_list, row);
        }
        g_object_unref (info);
    }
    
    if (error) {
        g_warning ("Error during enumeration: %s", error->message);
        g_error_free (error);
    }
    
    g_object_unref (enumerator);
    
    if (self->current_folder)
        g_object_unref (self->current_folder);
    self->current_folder = g_object_ref (folder);
}

static void
on_file_row_activated (GtkListBox *box, GtkListBoxRow *row, FlowWindow *self)
{
    GFile *file;
    gchar *contents;
    gsize length;
    GError *error = NULL;
    
    if (!row)
        return;
    
    file = g_object_get_data (G_OBJECT (row), "file");
    if (!file)
        return;
    
    if (g_file_load_contents (file, NULL, &contents, &length, NULL, &error)) {
        gchar *basename = g_file_get_basename (file);
        create_new_tab (self, basename, file);
        
        TabData *data = get_current_tab_data (self);
        if (data) {
            GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->text_view));
            gtk_text_buffer_set_text (buffer, contents, length);
        }
        
        g_free (basename);
        g_free (contents);
    } else {
        g_warning ("Failed to load file: %s", error->message);
        g_error_free (error);
    }
}

static void
on_tab_close_request (AdwTabView *view, AdwTabPage *page, FlowWindow *self)
{
    adw_tab_view_close_page_finish (view, page, TRUE);
}

static void
update_stats (FlowWindow *self)
{
    GtkSourceView *text_view = get_current_text_view (self);
    if (!text_view)
        return;
    
    GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (text_view));
    GtkTextIter start, end, cursor;
    gchar *text, *stats_text, *pos_text;
    gint lines, words = 0, chars;
    GtkTextMark *mark;
    gint line, col;
    
    gtk_text_buffer_get_bounds (buffer, &start, &end);
    text = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
    chars = gtk_text_iter_get_offset (&end);
    lines = gtk_text_buffer_get_line_count (buffer);
    
    /* Simple word count */
    if (text && *text) {
        gboolean in_word = FALSE;
        for (const gchar *p = text; *p; p = g_utf8_next_char (p)) {
            gunichar c = g_utf8_get_char (p);
            if (g_unichar_isspace (c)) {
                in_word = FALSE;
            } else if (!in_word) {
                in_word = TRUE;
                words++;
            }
        }
    }
    
    mark = gtk_text_buffer_get_insert (buffer);
    gtk_text_buffer_get_iter_at_mark (buffer, &cursor, mark);
    line = gtk_text_iter_get_line (&cursor) + 1;
    col = gtk_text_iter_get_line_offset (&cursor) + 1;
    
    stats_text = g_strdup_printf ("Lines: %d · Words: %d · Chars: %d", lines, words, chars);
    pos_text = g_strdup_printf ("Ln %d, Col %d", line, col);
    
    gtk_label_set_text (self->stats_label, stats_text);
    gtk_label_set_text (self->position_label, pos_text);
    
    g_free (stats_text);
    g_free (pos_text);
    g_free (text);
}

static void
on_selected_page_changed (GObject *object, GParamSpec *pspec, FlowWindow *self)
{
    TabData *data = get_current_tab_data (self);
    if (data && data->file) {
        gchar *basename = g_file_get_basename (data->file);
        adw_window_title_set_title (self->title_widget, basename);
        g_free (basename);
    } else {
        adw_window_title_set_title (self->title_widget, "Flow Editor");
    }
    update_stats (self);
}

static void
on_page_attached (AdwTabView *view, AdwTabPage *page, gint position, FlowWindow *self)
{
    TabData *data = g_object_get_data (G_OBJECT (page), "tab-data");
    if (data) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->text_view));
        g_signal_connect_swapped (buffer, "changed", G_CALLBACK (update_stats), self);
        g_signal_connect_swapped (buffer, "mark-set", G_CALLBACK (update_stats), self);
    }
}

/* Actions */
static void
action_new_file (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    FlowWindow *self = FLOW_WINDOW (user_data);
    create_new_tab (self, "Untitled", NULL);
}

static void
action_open_file (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    FlowWindow *self = FLOW_WINDOW (user_data);
    GtkFileDialog *dialog = gtk_file_dialog_new ();
    gtk_file_dialog_set_title (dialog, "Open File");
    gtk_file_dialog_open (dialog, GTK_WINDOW (self), NULL, NULL, NULL);
    g_object_unref (dialog);
}

static void
on_folder_dialog_response (GObject *source, GAsyncResult *result, gpointer user_data)
{
    FlowWindow *self = FLOW_WINDOW (user_data);
    GError *error = NULL;
    GFile *folder;
    
    folder = gtk_file_dialog_select_folder_finish (GTK_FILE_DIALOG (source), result, &error);
    if (folder) {
        load_folder (self, folder);
        g_object_unref (folder);
    } else if (error && !g_error_matches (error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)) {
        g_warning ("Failed to open folder: %s", error->message);
    }
    
    if (error)
        g_error_free (error);
}

static void
action_open_folder (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    FlowWindow *self = FLOW_WINDOW (user_data);
    GtkFileDialog *dialog = gtk_file_dialog_new ();
    
    gtk_file_dialog_set_title (dialog, "Open Folder");
    gtk_file_dialog_select_folder (dialog, GTK_WINDOW (self), NULL, 
                                   on_folder_dialog_response, self);
    g_object_unref (dialog);
}

static void
action_save_file (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    FlowWindow *self = FLOW_WINDOW (user_data);
    TabData *data = get_current_tab_data (self);
    
    if (!data)
        return;
    
    GtkTextBuffer *buffer;
    GtkTextIter start, end;
    gchar *text;
    GError *error = NULL;
    
    if (!data->file) {
        /* Save As */
        GtkFileDialog *dialog = gtk_file_dialog_new ();
        gtk_file_dialog_set_title (dialog, "Save File");
        gtk_file_dialog_save (dialog, GTK_WINDOW (self), NULL, NULL, NULL);
        g_object_unref (dialog);
        return;
    }
    
    buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->text_view));
    gtk_text_buffer_get_bounds (buffer, &start, &end);
    text = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
    if (!g_file_replace_contents (data->file, text, strlen (text), NULL, FALSE,
                                   G_FILE_CREATE_NONE, NULL, NULL, &error)) {
        g_warning ("Failed to save file: %s", error->message);
        g_error_free (error);
    }
    g_free (text);
}

static void
action_close_tab (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    FlowWindow *self = FLOW_WINDOW (user_data);
    AdwTabPage *page = adw_tab_view_get_selected_page (self->tab_view);
    if (page)
        adw_tab_view_close_page (self->tab_view, page);
}

static void
action_toggle_theme (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    FlowWindow *self = FLOW_WINDOW (user_data);
    self->dark_mode = !self->dark_mode;
    apply_theme (self);
}

static void
action_zoom_in (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    FlowWindow *self = FLOW_WINDOW (user_data);
    self->font_scale *= 1.1;
    if (self->font_scale > 5.0)
        self->font_scale = 5.0;
}

static void
action_zoom_out (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    FlowWindow *self = FLOW_WINDOW (user_data);
    self->font_scale /= 1.1;
    if (self->font_scale < 0.5)
        self->font_scale = 0.5;
}

static void
action_zoom_reset (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    FlowWindow *self = FLOW_WINDOW (user_data);
    self->font_scale = 1.0;
}

static void
action_find (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    FlowWindow *self = FLOW_WINDOW (user_data);
    gtk_widget_set_visible (GTK_WIDGET (self->replace_box), FALSE);
    gtk_revealer_set_reveal_child (self->search_revealer, TRUE);
    gtk_widget_grab_focus (GTK_WIDGET (self->search_entry));
}

static void
action_replace (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    FlowWindow *self = FLOW_WINDOW (user_data);
    gtk_widget_set_visible (GTK_WIDGET (self->replace_box), TRUE);
    gtk_revealer_set_reveal_child (self->search_revealer, TRUE);
    gtk_widget_grab_focus (GTK_WIDGET (self->search_entry));
}

static void
setup_actions (FlowWindow *self)
{
    GActionEntry entries[] = {
        { "new-file", action_new_file },
        { "open-file", action_open_file },
        { "open-folder", action_open_folder },
        { "save-file", action_save_file },
        { "close-tab", action_close_tab },
        { "toggle-theme", action_toggle_theme },
        { "zoom-in", action_zoom_in },
        { "zoom-out", action_zoom_out },
        { "zoom-reset", action_zoom_reset },
        { "find", action_find },
        { "replace", action_replace },
    };
    
    g_action_map_add_action_entries (G_ACTION_MAP (self), entries, G_N_ELEMENTS (entries), self);
}

static void
flow_window_dispose (GObject *object)
{
    FlowWindow *self = FLOW_WINDOW (object);
    
    if (self->current_folder) {
        g_object_unref (self->current_folder);
        self->current_folder = NULL;
    }
    
    if (self->font_provider) {
        g_object_unref (self->font_provider);
        self->font_provider = NULL;
    }
    
    G_OBJECT_CLASS (flow_window_parent_class)->dispose (object);
}

static void
flow_window_class_init (FlowWindowClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
    
    object_class->dispose = flow_window_dispose;
    
    gtk_widget_class_set_template_from_resource (widget_class, "/ink/coda/Flow/flow-window.ui");
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, split_view);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, tab_view);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, tab_bar);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, file_list);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, folder_label);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, open_folder_button);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, status_label);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, position_label);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, stats_label);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, title_widget);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, search_revealer);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, search_entry);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, replace_entry);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, replace_box);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, find_previous_button);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, find_next_button);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, close_search_button);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, replace_button);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, replace_all_button);
}

static void
flow_window_init (FlowWindow *self)
{
    GtkCssProvider *provider;
    GdkDisplay *display;
    const gchar *css =
        "sourceview { background-color: @card_bg_color; border-radius: 6px; padding: 4px; }"
        "sourceview text { background-color: @card_bg_color; }";
    
    gtk_widget_init_template (GTK_WIDGET (self));
    
    self->current_folder = NULL;
    self->font_scale = 1.0;
    self->dark_mode = TRUE;
    
    /* Apply CSS */
    provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_string (provider, css);
    display = gdk_display_get_default ();
    if (display) {
        gtk_style_context_add_provider_for_display (display,
                                                    GTK_STYLE_PROVIDER (provider),
                                                    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    g_object_unref (provider);
    
    /* Setup actions */
    setup_actions (self);
    
    /* Connect signals */
    g_signal_connect (self->tab_view, "close-page", G_CALLBACK (on_tab_close_request), self);
    g_signal_connect (self->tab_view, "page-attached", G_CALLBACK (on_page_attached), self);
    g_signal_connect (self->tab_view, "notify::selected-page", G_CALLBACK (on_selected_page_changed), self);
    g_signal_connect (self->file_list, "row-activated", G_CALLBACK (on_file_row_activated), self);
    g_signal_connect_swapped (self->open_folder_button, "clicked", G_CALLBACK (action_open_folder), self);
    
    /* Create initial tab */
    create_new_tab (self, "Untitled", NULL);
    
    /* Apply theme */
    apply_theme (self);
}

AdwApplicationWindow *
flow_window_new (AdwApplication *application)
{
    return g_object_new (FLOW_TYPE_WINDOW,
                         "application", application,
                         NULL);
}
