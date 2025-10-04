/* flow-window.c - Minimal clean version */

#include "config.h"
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "flow-window.h"

typedef struct {
    GtkSourceView *text_view;
    GtkScrolledWindow *scrolled;
    GFile *file;
    gboolean is_welcome;
} TabData;

typedef struct {
    FlowWindow *window;
    GFile *directory;
    GtkExpander *expander;
    GtkListBox *list_box;
} FolderData;

typedef struct {
    gchar *role;
    gchar *content;
} AiMessage;

typedef struct {
    gchar *payload;
    gsize payload_len;
    guint previous_message_count;
} AiRequestJob;

typedef struct {
    guint status;
    gchar *body;
    gsize body_len;
} AiHttpResult;

struct _FlowWindow
{
    AdwApplicationWindow parent_instance;
    
    AdwOverlaySplitView *split_view;
    AdwViewSwitcher *sidebar_switcher;
    AdwViewStack *sidebar_stack;
    AdwTabView *tab_view;
    AdwTabBar *tab_bar;
    GtkBox *file_list_container;
    GtkLabel *no_folder_label;
    GtkButton *open_folder_button;
    GtkButton *toggle_sidebar_button;
    GtkButton *settings_button;
    GtkButton *command_palette_button;
    GtkLabel *status_label;
    GtkLabel *position_label;
    AdwWindowTitle *title_widget;
    GtkPopover *command_popover;
    GtkSearchEntry *command_search;
    GtkListBox *command_list;
    GtkSearchEntry *file_search;
    GtkLabel *sidebar_folder_label;
    GtkBox *ai_message_list;
    GtkEntry *ai_message_entry;
    GtkButton *ai_send_button;
    GtkSpinner *ai_spinner;
    
    GFile *current_folder;
    gboolean dark_mode;
    gchar *search_text;
    gboolean show_welcome;
    gchar *ai_model;
    gboolean ai_request_in_progress;
    GPtrArray *ai_conversation;
};

G_DEFINE_FINAL_TYPE (FlowWindow, flow_window, ADW_TYPE_APPLICATION_WINDOW)

/* Forward declarations */
static TabData* tab_data_new (void);
static void tab_data_free (TabData *data);
static TabData* get_current_tab_data (FlowWindow *self);
static void create_new_tab (FlowWindow *self, const gchar *title, GFile *file);
static void create_welcome_tab (FlowWindow *self);
static void apply_theme (FlowWindow *self);
static void load_folder (FlowWindow *self, GFile *folder);
static void update_sidebar_folder_label (FlowWindow *self, GFile *folder);
static void load_folder_tree (FlowWindow *self, GFile *folder, GtkWidget *parent, gint depth);
static void on_expander_activated (GtkExpander *expander, gpointer user_data);
static void on_file_search_changed (GtkSearchEntry *entry, FlowWindow *self);
static void update_stats (FlowWindow *self);

static void on_toggle_sidebar_clicked (GtkButton *button, FlowWindow *self);
static void on_settings_clicked (GtkButton *button, FlowWindow *self);
static void on_theme_switch_toggled (GtkSwitch *sw, GParamSpec *pspec, FlowWindow *self);
static void show_preferences_window (FlowWindow *self);
static void on_welcome_switch_toggled (GtkSwitch *sw, GParamSpec *pspec, FlowWindow *self);
static void on_command_palette_clicked (GtkButton *button, FlowWindow *self);
static void on_open_folder_clicked (GtkButton *button, FlowWindow *self);
static void on_command_activated (GtkListBox *box, GtkListBoxRow *row, FlowWindow *self);
static void on_command_search_changed (GtkSearchEntry *entry, FlowWindow *self);
static gboolean on_key_pressed (GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, FlowWindow *self);
static gboolean on_tab_close_request (AdwTabView *view, AdwTabPage *page, FlowWindow *self);
static void on_file_button_clicked (GtkButton *button, FlowWindow *self);
static void on_file_row_activated (FlowWindow *self, gpointer user_data);
static void on_page_attached (AdwTabView *view, AdwTabPage *page, gint position, FlowWindow *self);
static void on_selected_page_changed (GObject *object, GParamSpec *pspec, FlowWindow *self);
static void on_folder_dialog_response (GObject *source, GAsyncResult *result, gpointer user_data);
static void on_open_dialog_response (GObject *source, GAsyncResult *result, gpointer user_data);
static void on_save_dialog_response (GObject *source, GAsyncResult *result, gpointer user_data);
static void ai_send_request (FlowWindow *self);
static void on_ai_send_clicked (GtkButton *button, FlowWindow *self);
static void on_ai_entry_activate (GtkEntry *entry, FlowWindow *self);
static void ai_request_set_busy (FlowWindow *self, gboolean busy);
static void ai_request_worker (GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable);
static void ai_request_task_completed (GObject *source_object, GAsyncResult *res, gpointer user_data);
static void ai_append_message_widget (FlowWindow *self, const gchar *text, gboolean is_user);
static gchar *ai_build_payload (FlowWindow *self, GPtrArray *messages);
static gchar *json_escape_string (const gchar *input);
static gchar *json_extract_content (const gchar *json, gsize length);
static gboolean header_has_chunked_encoding (const gchar *headers, gsize header_len);
static gchar *decode_chunked_body (const gchar *body, gsize body_len, gsize *out_len, GError **error);
static guint ai_model_index_from_name (const gchar *name);
static void on_ai_model_selected (AdwComboRow *row, GParamSpec *pspec, FlowWindow *self);

static AiMessage *ai_message_new (const gchar *role, const gchar *content);
static void ai_message_free (AiMessage *msg);
static void ai_request_job_free (AiRequestJob *job);
static void ai_http_result_free (AiHttpResult *result);

static const gchar *AI_SYSTEM_PROMPT =
    "Ты — встроенный помощник редактора Flow. Отвечай кратко и по делу. "
    "Когда пользователь просит изменить код и предоставляет фрагмент, "
    "верни только готовый код без комментариев и Markdown. В остальных случаях "
    "отвечай текстом, соблюдай язык пользователя и избегай лишних приветствий.";
static const gchar *AI_DEFAULT_MODEL = "gpt-5-nano";
static const gchar *AI_HOST = "text.pollinations.ai";
static const gchar *AI_PATH = "/openai";
static const gchar *AI_REFERRER = "https://g4f.dev/";
static const gchar *AI_AVAILABLE_MODELS[] = {
    "gpt-5-nano",
    "gpt-5-mini",
    "o4-mini",
    "gpt-5-chat",
    NULL
};

static AiMessage *
ai_message_new (const gchar *role, const gchar *content)
{
    AiMessage *msg = g_new0 (AiMessage, 1);
    msg->role = g_strdup (role);
    msg->content = g_strdup (content ? content : "");
    return msg;
}

static void
ai_message_free (AiMessage *msg)
{
    if (!msg)
        return;
    g_free (msg->role);
    g_free (msg->content);
    g_free (msg);
}

static void
ai_request_job_free (AiRequestJob *job)
{
    if (!job)
        return;
    g_free (job->payload);
    g_free (job);
}

static void
ai_http_result_free (AiHttpResult *result)
{
    if (!result)
        return;
    g_free (result->body);
    g_free (result);
}

static TabData*
tab_data_new (void)
{
    TabData *data = g_new0 (TabData, 1);
    
    data->text_view = GTK_SOURCE_VIEW (gtk_source_view_new ());
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
    gtk_text_view_set_editable (GTK_TEXT_VIEW (data->text_view), TRUE);
    
    data->scrolled = GTK_SCROLLED_WINDOW (gtk_scrolled_window_new ());
    gtk_scrolled_window_set_child (data->scrolled, GTK_WIDGET (data->text_view));
    
    data->file = NULL;
    data->is_welcome = FALSE;
    
    return data;
}

static void
on_welcome_checkbox_toggled (GtkCheckButton *check, FlowWindow *self)
{
    self->show_welcome = !gtk_check_button_get_active (check);
}

static TabData*
tab_data_new_welcome (FlowWindow *self)
{
    TabData *data;
    GtkBox *box;
    GtkLabel *title_label;
    GtkLabel *shortcuts_label;
    GtkCheckButton *check_button;
    gchar *shortcuts_text;
    
    data = g_new0 (TabData, 1);
    
    box = GTK_BOX (gtk_box_new (GTK_ORIENTATION_VERTICAL, 24));
    gtk_widget_set_valign (GTK_WIDGET (box), GTK_ALIGN_CENTER);
    gtk_widget_set_halign (GTK_WIDGET (box), GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top (GTK_WIDGET (box), 48);
    gtk_widget_set_margin_bottom (GTK_WIDGET (box), 48);
    gtk_widget_set_margin_start (GTK_WIDGET (box), 48);
    gtk_widget_set_margin_end (GTK_WIDGET (box), 48);
    
    title_label = GTK_LABEL (gtk_label_new ("Flow"));
    gtk_label_set_markup (title_label, "<span size='xx-large' weight='bold'>Flow</span>");
    gtk_box_append (box, GTK_WIDGET (title_label));
    
    shortcuts_text = g_strdup (
        "Keyboard Shortcuts\n\n"
        "Ctrl+N       New File\n"
        "Ctrl+O       Open File\n"
        "Ctrl+S       Save File\n"
        "Ctrl+Shift+O Open Folder\n"
        "Ctrl+Shift+P Command Palette\n"
        "Ctrl+W       Close Tab\n"
        "Ctrl+T       Toggle Theme"
    );
    
    shortcuts_label = GTK_LABEL (gtk_label_new (shortcuts_text));
    gtk_label_set_justify (shortcuts_label, GTK_JUSTIFY_LEFT);
    gtk_widget_add_css_class (GTK_WIDGET (shortcuts_label), "monospace");
    gtk_box_append (box, GTK_WIDGET (shortcuts_label));
    g_free (shortcuts_text);
    
    check_button = GTK_CHECK_BUTTON (gtk_check_button_new_with_label ("Don't show this again"));
    gtk_widget_set_margin_top (GTK_WIDGET (check_button), 16);
    g_signal_connect (check_button, "toggled", G_CALLBACK (on_welcome_checkbox_toggled), self);
    gtk_box_append (box, GTK_WIDGET (check_button));
    
    data->scrolled = GTK_SCROLLED_WINDOW (gtk_scrolled_window_new ());
    gtk_scrolled_window_set_child (data->scrolled, GTK_WIDGET (box));
    
    data->text_view = NULL;
    data->file = NULL;
    data->is_welcome = TRUE;
    
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
create_welcome_tab (FlowWindow *self)
{
    TabData *data;
    AdwTabPage *page;
    
    if (!self->show_welcome)
        return;
    
    data = tab_data_new_welcome (self);
    
    page = adw_tab_view_append (self->tab_view, GTK_WIDGET (data->scrolled));
    adw_tab_page_set_title (page, "Welcome");
    
    g_object_set_data_full (G_OBJECT (page), "tab-data", data, (GDestroyNotify) tab_data_free);
    
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
        if (data && data->text_view && !data->is_welcome) {
            GtkSourceBuffer *buffer = GTK_SOURCE_BUFFER (gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->text_view)));
            if (GTK_SOURCE_IS_BUFFER (buffer))
                gtk_source_buffer_set_style_scheme (buffer, scheme);
        }
    }
}

static void
load_folder_tree (FlowWindow *self, GFile *folder, GtkWidget *parent, gint depth)
{
    GFileEnumerator *enumerator;
    GError *error = NULL;
    GFileInfo *info;
    GPtrArray *dirs;
    GPtrArray *files;
    guint i;
    const gchar *search_text;
    
    if (depth > 10)
        return;
    
    search_text = self->search_text;
    
    dirs = g_ptr_array_new ();
    files = g_ptr_array_new ();
    
    enumerator = g_file_enumerate_children (folder,
        G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE,
        G_FILE_QUERY_INFO_NONE, NULL, &error);
    
    if (!enumerator) {
        g_warning ("Failed to enumerate folder: %s", error->message);
        g_error_free (error);
        g_ptr_array_free (dirs, TRUE);
        g_ptr_array_free (files, TRUE);
        return;
    }
    
    while ((info = g_file_enumerator_next_file (enumerator, NULL, &error)) != NULL) {
        const gchar *name;
        GFileType type;
        
        name = g_file_info_get_name (info);
        type = g_file_info_get_file_type (info);
        
        if (search_text && *search_text && !strstr (name, search_text)) {
            g_object_unref (info);
            continue;
        }
        
        if (type == G_FILE_TYPE_DIRECTORY)
            g_ptr_array_add (dirs, g_object_ref (info));
        else if (type == G_FILE_TYPE_REGULAR)
            g_ptr_array_add (files, g_object_ref (info));
        else
            g_object_unref (info);
    }
    
    if (error) {
        g_warning ("Error during enumeration: %s", error->message);
        g_error_free (error);
    }
    
    g_object_unref (enumerator);
    
    for (i = 0; i < dirs->len; i++) {
        GtkWidget *expander;
        GtkWidget *box;
        GtkWidget *icon;
        GtkWidget *label;
        GtkWidget *inner_box;
        GFile *dir_file;
        const gchar *name;
        
        info = g_ptr_array_index (dirs, i);
        name = g_file_info_get_name (info);
        dir_file = g_file_get_child (folder, name);
        
        box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
        icon = gtk_image_new_from_icon_name ("folder-symbolic");
        gtk_image_set_pixel_size (GTK_IMAGE (icon), 16);
        label = gtk_label_new (name);
        gtk_label_set_xalign (GTK_LABEL (label), 0);
        gtk_widget_set_hexpand (label, TRUE);
        gtk_box_append (GTK_BOX (box), icon);
        gtk_box_append (GTK_BOX (box), label);
        
        expander = gtk_expander_new (NULL);
        gtk_expander_set_label_widget (GTK_EXPANDER (expander), box);
        gtk_widget_add_css_class (expander, "file-tree-item");
        
        inner_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_margin_start (inner_box, 12);
        gtk_expander_set_child (GTK_EXPANDER (expander), inner_box);
        
        g_object_set_data_full (G_OBJECT (expander), "folder", dir_file, g_object_unref);
        g_object_set_data (G_OBJECT (expander), "inner-box", inner_box);
        g_object_set_data (G_OBJECT (expander), "window", self);
        g_object_set_data (G_OBJECT (expander), "depth", GINT_TO_POINTER (depth));
        g_signal_connect (expander, "activate", G_CALLBACK (on_expander_activated), self);
        
        gtk_box_append (GTK_BOX (parent), expander);
        g_object_unref (info);
    }
    
    for (i = 0; i < files->len; i++) {
        GtkWidget *button;
        GtkWidget *box;
        GtkWidget *icon;
        GtkWidget *label;
        GFile *file;
        const gchar *name;
        
        info = g_ptr_array_index (files, i);
        name = g_file_info_get_name (info);
        file = g_file_get_child (folder, name);
        
        button = gtk_button_new ();
        gtk_widget_add_css_class (button, "flat");
        gtk_widget_add_css_class (button, "file-tree-item");
        
        box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
        icon = gtk_image_new_from_icon_name ("text-x-generic-symbolic");
        gtk_image_set_pixel_size (GTK_IMAGE (icon), 16);
        label = gtk_label_new (name);
        gtk_label_set_xalign (GTK_LABEL (label), 0);
        gtk_widget_set_hexpand (label, TRUE);
        gtk_box_append (GTK_BOX (box), icon);
        gtk_box_append (GTK_BOX (box), label);
        
        gtk_button_set_child (GTK_BUTTON (button), box);
        g_object_set_data_full (G_OBJECT (button), "file", file, g_object_unref);
        g_object_set_data (G_OBJECT (button), "window", self);
        
        g_signal_connect (button, "clicked", G_CALLBACK (on_file_button_clicked), self);
        
        gtk_box_append (GTK_BOX (parent), button);
        g_object_unref (info);
    }
    
    g_ptr_array_free (dirs, TRUE);
    g_ptr_array_free (files, TRUE);
}

static void
on_expander_activated (GtkExpander *expander, gpointer user_data)
{
    GFile *folder;
    GtkWidget *inner_box;
    FlowWindow *self;
    gint depth;
    gboolean expanded;
    
    folder = g_object_get_data (G_OBJECT (expander), "folder");
    inner_box = g_object_get_data (G_OBJECT (expander), "inner-box");
    self = g_object_get_data (G_OBJECT (expander), "window");
    depth = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (expander), "depth"));
    
    expanded = gtk_expander_get_expanded (expander);
    
    if (!expanded) {
        GtkWidget *child;
        while ((child = gtk_widget_get_first_child (inner_box)))
            gtk_box_remove (GTK_BOX (inner_box), child);
        
        load_folder_tree (self, folder, inner_box, depth + 1);
    }
}

static void
update_sidebar_folder_label (FlowWindow *self, GFile *folder)
{
    GFile *target;
    gchar *basename = NULL;
    gchar *path = NULL;

    if (!self->sidebar_folder_label)
        return;

    target = folder ? folder : self->current_folder;

    if (target)
        basename = g_file_get_basename (target);

    if (basename && *basename) {
        gtk_label_set_text (self->sidebar_folder_label, basename);
        gtk_widget_remove_css_class (GTK_WIDGET (self->sidebar_folder_label), "dim-label");
    } else {
        gtk_label_set_text (self->sidebar_folder_label, "No folder");
        gtk_widget_add_css_class (GTK_WIDGET (self->sidebar_folder_label), "dim-label");
    }

    if (target)
        path = g_file_get_path (target);

    gtk_widget_set_tooltip_text (GTK_WIDGET (self->sidebar_folder_label),
                                 path ? path : "No folder selected");

    g_free (basename);
    g_free (path);
}

static void
load_folder (FlowWindow *self, GFile *folder)
{
    GtkWidget *child;
    
    while ((child = gtk_widget_get_first_child (GTK_WIDGET (self->file_list_container))))
        gtk_box_remove (self->file_list_container, child);
    
    gtk_widget_set_visible (GTK_WIDGET (self->open_folder_button), FALSE);
    gtk_widget_set_visible (GTK_WIDGET (self->no_folder_label), FALSE);
    
    load_folder_tree (self, folder, GTK_WIDGET (self->file_list_container), 0);
    
    if (folder)
        g_object_ref (folder);

    if (self->current_folder)
        g_object_unref (self->current_folder);
    self->current_folder = folder;
    update_sidebar_folder_label (self, self->current_folder);
}

static void
update_stats (FlowWindow *self)
{
    TabData *data;
    GtkTextBuffer *buffer;
    GtkTextIter cursor;
    gint line, col;
    GtkTextMark *mark;
    gchar *pos_text;
    
    data = get_current_tab_data (self);
    if (!data || data->is_welcome)
        return;
    
    if (!data->text_view)
        return;
    
    buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->text_view));
    mark = gtk_text_buffer_get_insert (buffer);
    gtk_text_buffer_get_iter_at_mark (buffer, &cursor, mark);
    line = gtk_text_iter_get_line (&cursor) + 1;
    col = gtk_text_iter_get_line_offset (&cursor) + 1;
    
    if (!self->position_label)
        return;

    pos_text = g_strdup_printf ("Ln %d, Col %d", line, col);
    gtk_label_set_text (self->position_label, pos_text);
    g_free (pos_text);
}

static void
on_toggle_sidebar_clicked (GtkButton *button, FlowWindow *self)
{
    gboolean visible = adw_overlay_split_view_get_show_sidebar (self->split_view);
    adw_overlay_split_view_set_show_sidebar (self->split_view, !visible);
}

static void
on_welcome_switch_toggled (GtkSwitch *sw, GParamSpec *pspec, FlowWindow *self)
{
    self->show_welcome = gtk_switch_get_active (sw);
}

static void
on_theme_switch_toggled (GtkSwitch *sw, GParamSpec *pspec, FlowWindow *self)
{
    self->dark_mode = gtk_switch_get_active (sw);
    apply_theme (self);
}

static void
show_preferences_window (FlowWindow *self)
{
    AdwPreferencesWindow *prefs;
    AdwPreferencesPage *page;
    AdwPreferencesGroup *group;
    AdwActionRow *row;
    GtkSwitch *theme_switch;
    GtkSwitch *welcome_switch;
    AdwPreferencesGroup *ai_group;
    AdwComboRow *model_row;
    GtkStringList *model_list;
    GtkWidget *model_icon;
    const gchar *current_model;
    guint selected_index;
    
    prefs = ADW_PREFERENCES_WINDOW (adw_preferences_window_new ());
    gtk_window_set_transient_for (GTK_WINDOW (prefs), GTK_WINDOW (self));
    gtk_window_set_modal (GTK_WINDOW (prefs), TRUE);
    gtk_window_set_default_size (GTK_WINDOW (prefs), 600, 400);
    
    page = ADW_PREFERENCES_PAGE (adw_preferences_page_new ());
    adw_preferences_page_set_title (page, "General");
    adw_preferences_page_set_icon_name (page, "emblem-system-symbolic");
    
    group = ADW_PREFERENCES_GROUP (adw_preferences_group_new ());
    adw_preferences_group_set_title (group, "Appearance");
    
    row = ADW_ACTION_ROW (adw_action_row_new ());
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), "Dark Theme");
    adw_action_row_set_subtitle (row, "Use dark color scheme");
    theme_switch = GTK_SWITCH (gtk_switch_new ());
    gtk_switch_set_active (theme_switch, self->dark_mode);
    gtk_widget_set_valign (GTK_WIDGET (theme_switch), GTK_ALIGN_CENTER);
    g_signal_connect (theme_switch, "notify::active", G_CALLBACK (on_theme_switch_toggled), self);
    adw_action_row_add_suffix (row, GTK_WIDGET (theme_switch));
    adw_action_row_set_activatable_widget (row, GTK_WIDGET (theme_switch));
    adw_preferences_group_add (group, GTK_WIDGET (row));
    
    row = ADW_ACTION_ROW (adw_action_row_new ());
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), "Show Welcome Screen");
    adw_action_row_set_subtitle (row, "Show welcome tab on startup");
    welcome_switch = GTK_SWITCH (gtk_switch_new ());
    gtk_switch_set_active (welcome_switch, self->show_welcome);
    gtk_widget_set_valign (GTK_WIDGET (welcome_switch), GTK_ALIGN_CENTER);
    g_signal_connect (welcome_switch, "notify::active", G_CALLBACK (on_welcome_switch_toggled), self);
    adw_action_row_add_suffix (row, GTK_WIDGET (welcome_switch));
    adw_action_row_set_activatable_widget (row, GTK_WIDGET (welcome_switch));
    adw_preferences_group_add (group, GTK_WIDGET (row));
    
    adw_preferences_page_add (page, group);

    current_model = self->ai_model ? self->ai_model : AI_DEFAULT_MODEL;
    selected_index = ai_model_index_from_name (current_model);

    ai_group = ADW_PREFERENCES_GROUP (adw_preferences_group_new ());
    adw_preferences_group_set_title (ai_group, "AI Assistant");

    model_row = ADW_COMBO_ROW (adw_combo_row_new ());
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (model_row), "Model");
    adw_action_row_set_subtitle (ADW_ACTION_ROW (model_row), "Select the assistant backend");
    model_icon = gtk_image_new_from_icon_name ("emoji-objects-symbolic");
    gtk_widget_set_valign (model_icon, GTK_ALIGN_CENTER);
    adw_action_row_add_prefix (ADW_ACTION_ROW (model_row), model_icon);
    model_list = gtk_string_list_new (AI_AVAILABLE_MODELS);
    adw_combo_row_set_model (model_row, G_LIST_MODEL (model_list));
    adw_combo_row_set_selected (model_row, selected_index);
    g_signal_connect (model_row, "notify::selected", G_CALLBACK (on_ai_model_selected), self);
    g_object_unref (model_list);
    adw_preferences_group_add (ai_group, GTK_WIDGET (model_row));

    adw_preferences_page_add (page, ai_group);
    adw_preferences_window_add (prefs, page);
    
    gtk_window_present (GTK_WINDOW (prefs));
}

static void
on_settings_clicked (GtkButton *button, FlowWindow *self)
{
    show_preferences_window (self);
}

static void
on_command_palette_clicked (GtkButton *button, FlowWindow *self)
{
    gtk_popover_popup (self->command_popover);
    gtk_widget_grab_focus (GTK_WIDGET (self->command_search));
}

static void
execute_command (FlowWindow *self, const gchar *command)
{
    GtkFileDialog *dialog;
    TabData *data;
    GtkTextBuffer *buffer;
    GtkTextIter start, end;
    gchar *text;
    GError *error = NULL;
    
    gtk_popover_popdown (self->command_popover);
    
    if (g_strcmp0 (command, "New File") == 0) {
        create_new_tab (self, "Untitled", NULL);
    } else if (g_strcmp0 (command, "Open File") == 0) {
        dialog = gtk_file_dialog_new ();
        gtk_file_dialog_set_title (dialog, "Open File");
        gtk_file_dialog_open (dialog, GTK_WINDOW (self), NULL, on_open_dialog_response, self);
        g_object_unref (dialog);
    } else if (g_strcmp0 (command, "Save File") == 0) {
        data = get_current_tab_data (self);
        if (!data || data->is_welcome)
            return;
        
        if (!data->file) {
            dialog = gtk_file_dialog_new ();
            gtk_file_dialog_set_title (dialog, "Save File");
            gtk_file_dialog_save (dialog, GTK_WINDOW (self), NULL, on_save_dialog_response, self);
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
        } else {
            if (self->status_label)
                gtk_label_set_text (self->status_label, "Saved");
        }
        g_free (text);
    } else if (g_strcmp0 (command, "Open Folder") == 0) {
        dialog = gtk_file_dialog_new ();
        gtk_file_dialog_set_title (dialog, "Open Folder");
        gtk_file_dialog_select_folder (dialog, GTK_WINDOW (self), NULL, on_folder_dialog_response, self);
        g_object_unref (dialog);
    } else if (g_strcmp0 (command, "Toggle Theme") == 0) {
        self->dark_mode = !self->dark_mode;
        apply_theme (self);
    } else if (g_strcmp0 (command, "Close Tab") == 0) {
        AdwTabPage *page = adw_tab_view_get_selected_page (self->tab_view);
        if (page)
            adw_tab_view_close_page (self->tab_view, page);
    }
}

static void
on_command_activated (GtkListBox *box, GtkListBoxRow *row, FlowWindow *self)
{
    GtkLabel *label;
    const gchar *command;
    
    if (!row)
        return;
    
    label = GTK_LABEL (gtk_list_box_row_get_child (row));
    command = gtk_label_get_text (label);
    execute_command (self, command);
}

static void
populate_command_list (FlowWindow *self, const gchar *search_text)
{
    GtkWidget *child;
    const gchar *commands[] = {
        "New File",
        "Open File",
        "Save File",
        "Open Folder",
        "Close Tab",
        "Toggle Theme",
        NULL
    };
    gint i;
    
    while ((child = gtk_widget_get_first_child (GTK_WIDGET (self->command_list))))
        gtk_list_box_remove (self->command_list, child);
    
    for (i = 0; commands[i] != NULL; i++) {
        if (search_text && *search_text && !g_str_match_string (search_text, commands[i], TRUE))
            continue;
        
        child = gtk_list_box_row_new ();
        gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (child), gtk_label_new (commands[i]));
        gtk_label_set_xalign (GTK_LABEL (gtk_list_box_row_get_child (GTK_LIST_BOX_ROW (child))), 0);
        gtk_list_box_append (self->command_list, child);
    }
}

static void
on_command_search_changed (GtkSearchEntry *entry, FlowWindow *self)
{
    const gchar *text = gtk_editable_get_text (GTK_EDITABLE (entry));
    populate_command_list (self, text);
}

static void
ai_append_message_widget (FlowWindow *self, const gchar *text, gboolean is_user)
{
    GtkWidget *child;
    GtkWidget *label;
    GtkWidget *viewport;
    GtkWidget *scroller;
    GtkAdjustment *adj;

    if (!self->ai_message_list)
        return;

    if (is_user) {
        GtkWidget *frame = gtk_frame_new (NULL);
        gtk_widget_add_css_class (frame, "card");
        gtk_widget_set_hexpand (frame, TRUE);
        gtk_widget_set_margin_start (frame, 4);
        gtk_widget_set_margin_end (frame, 4);
        gtk_widget_set_margin_top (frame, 4);
        gtk_widget_set_margin_bottom (frame, 4);

        label = gtk_label_new (text ? text : "");
        gtk_label_set_wrap (GTK_LABEL (label), TRUE);
        gtk_label_set_xalign (GTK_LABEL (label), 0);
        gtk_widget_set_margin_start (label, 6);
        gtk_widget_set_margin_end (label, 6);
        gtk_widget_set_margin_top (label, 6);
        gtk_widget_set_margin_bottom (label, 6);
        gtk_frame_set_child (GTK_FRAME (frame), label);
        child = frame;
    } else {
        label = gtk_label_new (text ? text : "");
        gtk_label_set_wrap (GTK_LABEL (label), TRUE);
        gtk_label_set_xalign (GTK_LABEL (label), 0);
        gtk_widget_set_hexpand (label, TRUE);
        gtk_widget_add_css_class (label, "dim-label");
        gtk_widget_set_margin_start (label, 4);
        gtk_widget_set_margin_end (label, 4);
        gtk_widget_set_margin_top (label, 2);
        gtk_widget_set_margin_bottom (label, 6);
        child = label;
    }

    gtk_box_append (self->ai_message_list, child);

    viewport = gtk_widget_get_parent (GTK_WIDGET (self->ai_message_list));
    if (GTK_IS_VIEWPORT (viewport)) {
        scroller = gtk_widget_get_parent (viewport);
        if (GTK_IS_SCROLLED_WINDOW (scroller)) {
            adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (scroller));
            gtk_adjustment_set_value (adj, gtk_adjustment_get_upper (adj));
        }
    }
}

static void
ai_request_set_busy (FlowWindow *self, gboolean busy)
{
    if (!self)
        return;
    self->ai_request_in_progress = busy;
    if (self->ai_message_entry)
        gtk_widget_set_sensitive (GTK_WIDGET (self->ai_message_entry), !busy);
    if (self->ai_send_button)
        gtk_widget_set_sensitive (GTK_WIDGET (self->ai_send_button), !busy);
    if (self->ai_spinner) {
        gtk_widget_set_visible (GTK_WIDGET (self->ai_spinner), busy);
        gtk_spinner_set_spinning (self->ai_spinner, busy);
    }
}

static gchar *
json_escape_string (const gchar *input)
{
    const gchar *p;
    GString *out;

    if (!input)
        return g_strdup ("");

    out = g_string_new (NULL);
    for (p = input; *p != '\0'; p++) {
        gunichar c = (gunichar)(guchar)(*p);
        switch (c) {
            case '"':
                g_string_append (out, "\\\"");
                break;
            case '\\':
                g_string_append (out, "\\\\");
                break;
            case '\b':
                g_string_append (out, "\\b");
                break;
            case '\f':
                g_string_append (out, "\\f");
                break;
            case '\n':
                g_string_append (out, "\\n");
                break;
            case '\r':
                g_string_append (out, "\\r");
                break;
            case '\t':
                g_string_append (out, "\\t");
                break;
            default:
                if (c < 0x20) {
                    g_string_append_printf (out, "\\u%04x", c);
                } else {
                    char buffer[6];
                    gint len = g_unichar_to_utf8 (c, buffer);
                    g_string_append_len (out, buffer, len);
                }
                break;
        }
    }
    return g_string_free (out, FALSE);
}

static gchar *
ai_build_payload (FlowWindow *self, GPtrArray *messages)
{
    GString *builder;
    gchar *escaped_model;
    gchar *escaped_system;
    gchar *escaped_referrer;
    const gchar *model;
    guint i;

    if (!messages)
        return NULL;

    model = self->ai_model ? self->ai_model : AI_DEFAULT_MODEL;
    escaped_model = json_escape_string (model);
    escaped_system = json_escape_string (AI_SYSTEM_PROMPT);
    escaped_referrer = json_escape_string (AI_REFERRER);

    builder = g_string_new ("{");
    g_string_append_printf (builder, "\"model\":\"%s\",", escaped_model);
    g_string_append (builder, "\"messages\":[");
    g_string_append_printf (builder, "{\"role\":\"system\",\"content\":\"%s\"}", escaped_system);

    for (i = 0; i < messages->len; i++) {
        AiMessage *msg = g_ptr_array_index (messages, i);
        gchar *escaped_role;
        gchar *escaped_content;

        if (!msg)
            continue;
        escaped_role = json_escape_string (msg->role);
        escaped_content = json_escape_string (msg->content);
        g_string_append_printf (builder, ",{\"role\":\"%s\",\"content\":\"%s\"}", escaped_role, escaped_content);
        g_free (escaped_role);
        g_free (escaped_content);
    }

    g_string_append_printf (builder, "],\"stream\":false,\"referrer\":\"%s\"}", escaped_referrer);

    g_free (escaped_model);
    g_free (escaped_system);
    g_free (escaped_referrer);

    return g_string_free (builder, FALSE);
}

static gchar *
json_extract_content (const gchar *json, gsize length)
{
    const gchar *start;
    const gchar *end = json + length;
    GString *out;

    if (!json || length == 0)
        return NULL;

    start = g_strstr_len (json, length, "\"content\"");
    if (!start)
        return NULL;
    start = strchr (start, ':');
    if (!start)
        return NULL;
    start++;
    while (start < end && g_ascii_isspace ((guchar)*start))
        start++;
    if (start >= end || *start != '"')
        return NULL;
    start++;

    out = g_string_new (NULL);
    while (start < end) {
        gchar c = *start;
        if (c == '"')
            break;
        if (c == '\\') {
            start++;
            if (start >= end)
                break;
            c = *start;
            switch (c) {
                case '"':
                case '\\':
                case '/':
                    g_string_append_c (out, c);
                    start++;
                    break;
                case 'b':
                    g_string_append_c (out, '\b');
                    start++;
                    break;
                case 'f':
                    g_string_append_c (out, '\f');
                    start++;
                    break;
                case 'n':
                    g_string_append_c (out, '\n');
                    start++;
                    break;
                case 'r':
                    g_string_append_c (out, '\r');
                    start++;
                    break;
                case 't':
                    g_string_append_c (out, '\t');
                    start++;
                    break;
                case 'u':
                {
                    gunichar code = 0;
                    gboolean ok = TRUE;
                    gint i;
                    for (i = 0; i < 4; i++) {
                        start++;
                        if (start >= end || !g_ascii_isxdigit ((guchar)*start)) {
                            ok = FALSE;
                            break;
                        }
                        code = (code << 4) + g_ascii_xdigit_value (*start);
                    }
                    if (ok) {
                        gchar buffer[6];
                        gint len = g_unichar_to_utf8 (code, buffer);
                        g_string_append_len (out, buffer, len);
                    }
                    start++;
                    break;
                }
                default:
                    g_string_append_c (out, c);
                    start++;
                    break;
            }
            continue;
        }
        g_string_append_c (out, c);
        start++;
    }

    return g_string_free (out, FALSE);
}

static gboolean
header_has_chunked_encoding (const gchar *headers, gsize header_len)
{
    gchar *copy;
    gchar *lower;
    gboolean result;

    if (!headers)
        return FALSE;

    copy = g_strndup (headers, header_len);
    lower = g_ascii_strdown (copy, -1);
    result = strstr (lower, "transfer-encoding: chunked") != NULL;
    g_free (lower);
    g_free (copy);
    return result;
}

static gchar *
decode_chunked_body (const gchar *body, gsize body_len, gsize *out_len, GError **error)
{
    const gchar *cursor = body;
    const gchar *end = body + body_len;
    GString *result = g_string_new (NULL);

    while (cursor < end) {
        const gchar *line_end = g_strstr_len (cursor, end - cursor, "\r\n");
        gchar *size_str;
        gchar *semi;
        gchar *endptr = NULL;
        guint64 chunk_size;

        if (!line_end) {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid chunked encoding: missing CRLF");
            g_string_free (result, TRUE);
            return NULL;
        }

        size_str = g_strndup (cursor, line_end - cursor);
        semi = strchr (size_str, ';');
        if (semi)
            *semi = '\0';
        chunk_size = g_ascii_strtoull (size_str, &endptr, 16);
        g_free (size_str);
        if (endptr == NULL || endptr[0] != '\0') {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid chunk size value");
            g_string_free (result, TRUE);
            return NULL;
        }

        cursor = line_end + 2;
        if (chunk_size == 0)
            break;

        if ((gsize)(end - cursor) < chunk_size + 2) {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Chunk exceeds buffer");
            g_string_free (result, TRUE);
            return NULL;
        }

        g_string_append_len (result, cursor, (gssize)chunk_size);
        cursor += chunk_size;
        if (cursor + 2 > end || cursor[0] != '\r' || cursor[1] != '\n') {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid chunk terminator");
            g_string_free (result, TRUE);
            return NULL;
        }
        cursor += 2;
    }

    if (out_len)
        *out_len = result->len;

    return g_string_free (result, FALSE);
}

static guint
ai_model_index_from_name (const gchar *name)
{
    guint i;

    if (!name)
        return 0;

    for (i = 0; AI_AVAILABLE_MODELS[i] != NULL; i++) {
        if (g_strcmp0 (AI_AVAILABLE_MODELS[i], name) == 0)
            return i;
    }

    return 0;
}

static void
on_ai_model_selected (AdwComboRow *row, GParamSpec *pspec, FlowWindow *self)
{
    GListModel *model;
    GObject *item;
    const gchar *model_name;
    guint index;

    (void)pspec;

    if (!row || !self)
        return;

    model = adw_combo_row_get_model (row);
    if (!model)
        return;

    index = adw_combo_row_get_selected (row);
    if (index == G_MAXUINT)
        return;

    item = g_list_model_get_item (model, index);
    if (!item)
        return;

    model_name = gtk_string_object_get_string (GTK_STRING_OBJECT (item));
    if (model_name && g_strcmp0 (self->ai_model, model_name) != 0) {
        g_free (self->ai_model);
        self->ai_model = g_strdup (model_name);
    }
    g_object_unref (item);
}

static void
ai_request_worker (GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
    AiRequestJob *job = task_data;
    GSocketClient *client = NULL;
    GSocketConnection *connection = NULL;
    GOutputStream *out;
    GInputStream *in;
    GString *request;
    GByteArray *response = NULL;
    guint8 buffer[4096];
    gssize nread;
    gchar *response_str;
    gchar *header_end;
    gsize header_len;
    gchar *headers_copy = NULL;
    const gchar *body_ptr;
    gsize raw_body_len;
    gchar *body = NULL;
    gsize body_len = 0;
    guint status = 0;
    GError *error = NULL;
    AiHttpResult *result;

    (void)source_object;
    (void)cancellable;

    client = g_socket_client_new ();
    g_socket_client_set_tls (client, TRUE);
    connection = g_socket_client_connect_to_host (client, AI_HOST, 443, NULL, &error);
    if (!connection) {
        g_task_return_error (task, error);
        g_object_unref (client);
        return;
    }

    out = g_io_stream_get_output_stream (G_IO_STREAM (connection));
    in = g_io_stream_get_input_stream (G_IO_STREAM (connection));

    request = g_string_new (NULL);
    g_string_append_printf (request, "POST %s HTTP/1.1\r\n", AI_PATH);
    g_string_append_printf (request, "Host: %s\r\n", AI_HOST);
    g_string_append (request, "User-Agent: Flow/1.0\r\n");
    g_string_append (request, "Accept: application/json\r\n");
    g_string_append (request, "Content-Type: application/json\r\n");
    g_string_append_printf (request, "Content-Length: %zu\r\n", job->payload_len);
    g_string_append (request, "Connection: close\r\n\r\n");
    g_string_append_len (request, job->payload, job->payload_len);

    if (!g_output_stream_write_all (out, request->str, request->len, NULL, NULL, &error)) {
        g_string_free (request, TRUE);
        g_task_return_error (task, error);
        g_object_unref (connection);
        g_object_unref (client);
        return;
    }
    g_string_free (request, TRUE);

    response = g_byte_array_new ();
    while ((nread = g_input_stream_read (in, buffer, sizeof buffer, NULL, &error)) > 0)
        g_byte_array_append (response, buffer, (gsize)nread);
    if (nread < 0) {
        g_task_return_error (task, error);
        g_byte_array_free (response, TRUE);
        g_object_unref (connection);
        g_object_unref (client);
        return;
    }
    g_byte_array_append (response, (const guint8 *)"", 1);

    response_str = (gchar *)response->data;
    header_end = g_strstr_len (response_str, response->len - 1, "\r\n\r\n");
    if (!header_end) {
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Malformed HTTP response");
        g_byte_array_free (response, TRUE);
        g_object_unref (connection);
        g_object_unref (client);
        return;
    }
    header_len = header_end - response_str;
    headers_copy = g_strndup (response_str, header_len);
    if (sscanf (response_str, "HTTP/%*s %u", &status) != 1)
        status = 0;

    body_ptr = header_end + 4;
    raw_body_len = (response->len - 1) - (body_ptr - response_str);
    if (header_has_chunked_encoding (headers_copy, header_len)) {
        body = decode_chunked_body (body_ptr, raw_body_len, &body_len, &error);
        if (!body) {
            g_task_return_error (task, error);
            g_free (headers_copy);
            g_byte_array_free (response, TRUE);
            g_object_unref (connection);
            g_object_unref (client);
            return;
        }
    } else {
        body = g_strndup (body_ptr, raw_body_len);
        body_len = raw_body_len;
    }

    result = g_new0 (AiHttpResult, 1);
    result->status = status;
    result->body = body;
    result->body_len = body_len;

    g_task_return_pointer (task, result, (GDestroyNotify)ai_http_result_free);

    g_free (headers_copy);
    g_byte_array_free (response, TRUE);
    g_object_unref (connection);
    g_object_unref (client);
}

static void
ai_request_task_completed (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    FlowWindow *self = FLOW_WINDOW (source_object);
    GTask *task = G_TASK (res);
    AiRequestJob *job = g_task_get_task_data (task);
    GError *error = NULL;
    AiHttpResult *result;
    gchar *content_text = NULL;
    gchar *status_text = NULL;
    AiMessage *assistant;

    (void)user_data;

    result = g_task_propagate_pointer (task, &error);
    if (error) {
        if (self->ai_conversation && job && self->ai_conversation->len > job->previous_message_count)
            g_ptr_array_remove_range (self->ai_conversation, job->previous_message_count,
                                      self->ai_conversation->len - job->previous_message_count);
        ai_request_set_busy (self, FALSE);
        ai_append_message_widget (self, error->message, FALSE);
        g_error_free (error);
        return;
    }

    if (!result) {
        if (self->ai_conversation && job && self->ai_conversation->len > job->previous_message_count)
            g_ptr_array_remove_range (self->ai_conversation, job->previous_message_count,
                                      self->ai_conversation->len - job->previous_message_count);
        ai_request_set_busy (self, FALSE);
        ai_append_message_widget (self, "No response from server.", FALSE);
        return;
    }

    if (result->status < 200 || result->status >= 300) {
        if (self->ai_conversation && job && self->ai_conversation->len > job->previous_message_count)
            g_ptr_array_remove_range (self->ai_conversation, job->previous_message_count,
                                      self->ai_conversation->len - job->previous_message_count);
        if (result->body && result->body_len > 0)
            status_text = g_strdup_printf ("Status %u\n%.*s", result->status, (int)result->body_len, result->body);
        else
            status_text = g_strdup_printf ("Status %u", result->status);
        ai_request_set_busy (self, FALSE);
        ai_append_message_widget (self, status_text, FALSE);
        g_free (status_text);
        return;
    }

    if (result->body && result->body_len > 0)
        content_text = json_extract_content (result->body, result->body_len);
    if (!content_text)
        content_text = g_strdup ("(empty response)");

    if (!self->ai_conversation)
        self->ai_conversation = g_ptr_array_new_with_free_func ((GDestroyNotify)ai_message_free);

    assistant = ai_message_new ("assistant", content_text);
    g_ptr_array_add (self->ai_conversation, assistant);
    ai_append_message_widget (self, assistant->content, FALSE);

    g_free (content_text);
    ai_request_set_busy (self, FALSE);
}

static void
ai_send_request (FlowWindow *self)
{
    const gchar *prompt;
    gchar *trimmed;
    AiMessage *user_msg;
    gchar *payload;
    AiRequestJob *job;
    GTask *task;
    guint previous_len;

    if (!self || self->ai_request_in_progress)
        return;

    prompt = gtk_editable_get_text (GTK_EDITABLE (self->ai_message_entry));
    if (!prompt)
        return;

    trimmed = g_strdup (prompt);
    g_strstrip (trimmed);
    if (!*trimmed) {
        g_free (trimmed);
        return;
    }

    if (!self->ai_conversation)
        self->ai_conversation = g_ptr_array_new_with_free_func ((GDestroyNotify)ai_message_free);

    previous_len = self->ai_conversation->len;
    user_msg = ai_message_new ("user", trimmed);
    g_ptr_array_add (self->ai_conversation, user_msg);
    ai_append_message_widget (self, user_msg->content, TRUE);
    g_free (trimmed);

    payload = ai_build_payload (self, self->ai_conversation);
    if (!payload) {
        ai_append_message_widget (self, "Failed to prepare request payload.", FALSE);
        if (self->ai_conversation->len > previous_len)
            g_ptr_array_remove_range (self->ai_conversation, previous_len,
                                      self->ai_conversation->len - previous_len);
        return;
    }

    job = g_new0 (AiRequestJob, 1);
    job->payload = payload;
    job->payload_len = strlen (payload);
    job->previous_message_count = previous_len;

    task = g_task_new (self, NULL, ai_request_task_completed, NULL);
    g_task_set_task_data (task, job, (GDestroyNotify)ai_request_job_free);
    ai_request_set_busy (self, TRUE);
    gtk_editable_set_text (GTK_EDITABLE (self->ai_message_entry), "");
    g_task_run_in_thread (task, ai_request_worker);
    g_object_unref (task);
}

static void
on_ai_send_clicked (GtkButton *button, FlowWindow *self)
{
    (void)button;
    ai_send_request (self);
}

static void
on_ai_entry_activate (GtkEntry *entry, FlowWindow *self)
{
    (void)entry;
    ai_send_request (self);
}

static void
on_open_dialog_response (GObject *source, GAsyncResult *result, gpointer user_data)
{
    FlowWindow *self = FLOW_WINDOW (user_data);
    GError *error = NULL;
    GFile *file;
    gchar *contents;
    gsize length;
    
    file = gtk_file_dialog_open_finish (GTK_FILE_DIALOG (source), result, &error);
    if (file) {
        if (g_file_load_contents (file, NULL, &contents, &length, NULL, &error)) {
            gchar *basename;
            TabData *data;
            GtkTextBuffer *buffer;
            
            basename = g_file_get_basename (file);
            create_new_tab (self, basename, file);
            
            data = get_current_tab_data (self);
            if (data) {
                buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->text_view));
                gtk_text_buffer_set_text (buffer, contents, length);
            }
            
            g_free (basename);
            g_free (contents);
        } else if (error) {
            g_warning ("Failed to load file: %s", error->message);
            g_error_free (error);
        }
        g_object_unref (file);
    } else if (error && !g_error_matches (error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)) {
        g_warning ("Failed to open file: %s", error->message);
    }
    if (error && error->code != GTK_DIALOG_ERROR_DISMISSED)
        g_error_free (error);
}

static void
on_save_dialog_response (GObject *source, GAsyncResult *result, gpointer user_data)
{
    FlowWindow *self = FLOW_WINDOW (user_data);
    GError *error = NULL;
    GFile *file;
    
    file = gtk_file_dialog_save_finish (GTK_FILE_DIALOG (source), result, &error);
    if (file) {
        TabData *data = get_current_tab_data (self);
        if (data) {
            GtkTextBuffer *buffer;
            GtkTextIter start, end;
            gchar *text;
            
            if (data->file)
                g_object_unref (data->file);
            data->file = g_object_ref (file);
            
            buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->text_view));
            gtk_text_buffer_get_bounds (buffer, &start, &end);
            text = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
            
            if (g_file_replace_contents (file, text, strlen (text), NULL, FALSE,
                                        G_FILE_CREATE_NONE, NULL, NULL, &error)) {
                gchar *basename = g_file_get_basename (file);
                AdwTabPage *page = adw_tab_view_get_selected_page (self->tab_view);
                if (page)
                    adw_tab_page_set_title (page, basename);
                g_free (basename);
            }
            g_free (text);
        }
        g_object_unref (file);
    }
    if (error && error->code != GTK_DIALOG_ERROR_DISMISSED) {
        g_warning ("Failed to save file: %s", error->message);
        g_error_free (error);
    }
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
    
    if (error && error->code != GTK_DIALOG_ERROR_DISMISSED)
        g_error_free (error);
}

static void
on_open_folder_clicked (GtkButton *button, FlowWindow *self)
{
    GtkFileDialog *dialog = gtk_file_dialog_new ();
    gtk_file_dialog_set_title (dialog, "Open Folder");
    gtk_file_dialog_select_folder (dialog, GTK_WINDOW (self), NULL, on_folder_dialog_response, self);
    g_object_unref (dialog);
}

static void
on_file_search_changed (GtkSearchEntry *entry, FlowWindow *self)
{
    const gchar *text;
    
    text = gtk_editable_get_text (GTK_EDITABLE (entry));
    
    g_free (self->search_text);
    self->search_text = g_strdup (text);
    
    if (self->current_folder)
        load_folder (self, self->current_folder);
}

static void
on_file_row_activated (FlowWindow *self, gpointer user_data)
{
    GFile *file;
    gchar *contents;
    gsize length;
    GError *error = NULL;
    GtkWidget *button;
    
    button = GTK_WIDGET (user_data);
    if (!button)
        return;
    
    file = g_object_get_data (G_OBJECT (button), "file");
    
    if (!file)
        return;
    
    if (g_file_load_contents (file, NULL, &contents, &length, NULL, &error)) {
        gchar *basename;
        TabData *data;
        
        basename = g_file_get_basename (file);
        create_new_tab (self, basename, file);
        
        data = get_current_tab_data (self);
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

static gboolean
on_tab_close_request (AdwTabView *view, AdwTabPage *page, FlowWindow *self)
{
    adw_tab_view_close_page_finish (view, page, TRUE);
    return GDK_EVENT_STOP;
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
        adw_window_title_set_title (self->title_widget, "Flow");
    }
    update_stats (self);
}

static void
on_page_attached (AdwTabView *view, AdwTabPage *page, gint position, FlowWindow *self)
{
    TabData *data = g_object_get_data (G_OBJECT (page), "tab-data");
    if (data && !data->is_welcome && data->text_view) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->text_view));
        g_signal_connect_swapped (buffer, "changed", G_CALLBACK (update_stats), self);
        g_signal_connect_swapped (buffer, "mark-set", G_CALLBACK (update_stats), self);
    }
}

static gboolean
on_key_pressed (GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, FlowWindow *self)
{
    gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;
    gboolean shift = (state & GDK_SHIFT_MASK) != 0;
    
    if (ctrl && shift && keyval == GDK_KEY_P) {
        on_command_palette_clicked (NULL, self);
        return TRUE;
    } else if (ctrl && shift && keyval == GDK_KEY_O) {
        GtkFileDialog *dialog = gtk_file_dialog_new ();
        gtk_file_dialog_set_title (dialog, "Open Folder");
        gtk_file_dialog_select_folder (dialog, GTK_WINDOW (self), NULL, on_folder_dialog_response, self);
        g_object_unref (dialog);
        return TRUE;
    } else if (ctrl && !shift && keyval == GDK_KEY_n) {
        create_new_tab (self, "Untitled", NULL);
        return TRUE;
    } else if (ctrl && !shift && keyval == GDK_KEY_o) {
        GtkFileDialog *dialog = gtk_file_dialog_new ();
        gtk_file_dialog_set_title (dialog, "Open File");
        gtk_file_dialog_open (dialog, GTK_WINDOW (self), NULL, on_open_dialog_response, self);
        g_object_unref (dialog);
        return TRUE;
    } else if (ctrl && !shift && keyval == GDK_KEY_s) {
        execute_command (self, "Save File");
        return TRUE;
    } else if (ctrl && !shift && keyval == GDK_KEY_w) {
        AdwTabPage *page = adw_tab_view_get_selected_page (self->tab_view);
        if (page)
            adw_tab_view_close_page (self->tab_view, page);
        return TRUE;
    } else if (ctrl && !shift && keyval == GDK_KEY_t) {
        self->dark_mode = !self->dark_mode;
        apply_theme (self);
        return TRUE;
    }
    
    return FALSE;
}

static void
on_file_button_clicked (GtkButton *button, FlowWindow *self)
{
    on_file_row_activated (self, button);
}

static void
flow_window_dispose (GObject *object)
{
    FlowWindow *self = FLOW_WINDOW (object);
    
    if (self->current_folder) {
        g_object_unref (self->current_folder);
        self->current_folder = NULL;
    }
    
    g_free (self->search_text);
    self->search_text = NULL;

    g_free (self->ai_model);
    self->ai_model = NULL;

    if (self->ai_conversation) {
        g_ptr_array_unref (self->ai_conversation);
        self->ai_conversation = NULL;
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
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, sidebar_switcher);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, sidebar_stack);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, tab_view);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, tab_bar);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, file_list_container);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, no_folder_label);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, open_folder_button);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, toggle_sidebar_button);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, settings_button);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, command_palette_button);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, title_widget);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, command_popover);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, command_search);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, command_list);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, file_search);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, sidebar_folder_label);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, ai_message_list);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, ai_message_entry);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, ai_send_button);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, ai_spinner);
}

static void
flow_window_init (FlowWindow *self)
{
    GtkCssProvider *provider;
    GdkDisplay *display;
    GtkEventController *key_controller;
    const gchar *css = 
        "sourceview { background-color: @view_bg_color; }"
        ".file-tree-item { min-height: 28px; padding: 2px 4px; }"
        ".file-tree-item > box { min-height: 24px; }"
        ".file-tree-item expander-title-box { min-height: 24px; padding: 0; }"
        ".file-tree-item image { margin: 0 4px; }"
        ".file-tree-item label { font-size: 0.9em; }";
    
    gtk_widget_init_template (GTK_WIDGET (self));
    
    self->current_folder = NULL;
    self->dark_mode = TRUE;
    self->search_text = NULL;
    self->show_welcome = TRUE;
    
    provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_string (provider, css);
    display = gdk_display_get_default ();
    if (display) {
        gtk_style_context_add_provider_for_display (display,
                                                    GTK_STYLE_PROVIDER (provider),
                                                    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    g_object_unref (provider);
    
    key_controller = GTK_EVENT_CONTROLLER (gtk_event_controller_key_new ());
    g_signal_connect (key_controller, "key-pressed", G_CALLBACK (on_key_pressed), self);
    gtk_widget_add_controller (GTK_WIDGET (self), key_controller);
    
    g_signal_connect (self->toggle_sidebar_button, "clicked", G_CALLBACK (on_toggle_sidebar_clicked), self);
    g_signal_connect (self->settings_button, "clicked", G_CALLBACK (on_settings_clicked), self);
    g_signal_connect (self->open_folder_button, "clicked", G_CALLBACK (on_open_folder_clicked), self);
    g_signal_connect (self->tab_view, "close-page", G_CALLBACK (on_tab_close_request), self);
    g_signal_connect (self->tab_view, "page-attached", G_CALLBACK (on_page_attached), self);
    g_signal_connect (self->tab_view, "notify::selected-page", G_CALLBACK (on_selected_page_changed), self);
    g_signal_connect (self->command_search, "search-changed", G_CALLBACK (on_command_search_changed), self);
    g_signal_connect (self->command_list, "row-activated", G_CALLBACK (on_command_activated), self);
    g_signal_connect (self->file_search, "search-changed", G_CALLBACK (on_file_search_changed), self);

    self->ai_model = g_strdup (AI_DEFAULT_MODEL);
    self->ai_request_in_progress = FALSE;
    self->ai_conversation = g_ptr_array_new_with_free_func ((GDestroyNotify) ai_message_free);
    if (self->ai_spinner) {
        gtk_widget_set_visible (GTK_WIDGET (self->ai_spinner), FALSE);
        gtk_spinner_set_spinning (self->ai_spinner, FALSE);
    }
    if (self->ai_send_button)
        g_signal_connect (self->ai_send_button, "clicked", G_CALLBACK (on_ai_send_clicked), self);
    if (self->ai_message_entry)
        g_signal_connect (self->ai_message_entry, "activate", G_CALLBACK (on_ai_entry_activate), self);
    ai_request_set_busy (self, FALSE);
    update_sidebar_folder_label (self, NULL);
    
    populate_command_list (self, NULL);
    create_welcome_tab (self);
    apply_theme (self);
}

AdwApplicationWindow *
flow_window_new (AdwApplication *application)
{
    return g_object_new (FLOW_TYPE_WINDOW,
                         "application", application,
                         NULL);
}
