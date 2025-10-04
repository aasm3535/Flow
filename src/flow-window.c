/* flow-window.c
 *
 * Copyright 2025 Artem
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#include <gio/gio.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>

#include "flow-window.h"

struct _FlowWindow
{
    AdwApplicationWindow parent_instance;

    GtkButton *new_button;
    GtkButton *open_button;
    GtkButton *save_button;
    GtkTextView *text_view;
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

    GFile *current_file;
    gdouble font_scale;
    GtkCssProvider *font_provider;
};

G_DEFINE_FINAL_TYPE (FlowWindow, flow_window, ADW_TYPE_APPLICATION_WINDOW)

static void update_window_title (FlowWindow *self);
static void update_status (FlowWindow *self, const gchar *message);
static void update_cursor_info (FlowWindow *self);
static void update_stats_info (FlowWindow *self);
static void refresh_document_info (FlowWindow *self);
static void clear_current_file (FlowWindow *self);
static void on_new_button_clicked (GtkButton *button, FlowWindow *self);
static void on_open_button_clicked (GtkButton *button, FlowWindow *self);
static void on_save_button_clicked (GtkButton *button, FlowWindow *self);
static void open_dialog_completed (GObject *source_object, GAsyncResult *result, gpointer user_data);
static void save_dialog_completed (GObject *source_object, GAsyncResult *result, gpointer user_data);
static gboolean save_buffer_to_file (FlowWindow *self, GFile *file);
static void on_buffer_changed (GtkTextBuffer *buffer, gpointer user_data);
static void on_buffer_mark_set (GtkTextBuffer *buffer, const GtkTextIter *location, GtkTextMark *mark, gpointer user_data);
static gint count_words (const gchar *text);
static gboolean on_scroll (GtkEventControllerScroll *controller, gdouble dx, gdouble dy, gpointer user_data);
static gboolean on_key_pressed (GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data);
static void update_font_size (FlowWindow *self);
static void zoom_in (FlowWindow *self);
static void zoom_out (FlowWindow *self);
static void show_find (FlowWindow *self);
static void show_replace (FlowWindow *self);
static void hide_search (FlowWindow *self);
static void on_search_changed (GtkSearchEntry *entry, gpointer user_data);
static void on_find_next_clicked (GtkButton *button, gpointer user_data);
static void on_find_previous_clicked (GtkButton *button, gpointer user_data);
static void on_replace_clicked (GtkButton *button, gpointer user_data);
static void on_replace_all_clicked (GtkButton *button, gpointer user_data);
static void on_close_search_clicked (GtkButton *button, gpointer user_data);
static gboolean find_text (FlowWindow *self, gboolean forward);
static void flow_window_dispose (GObject *object);

static void
flow_window_class_init (FlowWindowClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->dispose = flow_window_dispose;

    gtk_widget_class_set_template_from_resource (widget_class, "/ink/coda/Flow/flow-window.ui");
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, new_button);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, open_button);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, save_button);
    gtk_widget_class_bind_template_child (widget_class, FlowWindow, text_view);
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
    GtkTextBuffer *buffer;
    GtkCssProvider *provider;
    GdkDisplay *display;
    GtkEventControllerScroll *scroll_controller;
    GtkEventControllerKey *key_controller;
    const gchar *css =
        "#flow-textview { background-color: transparent; border: none; box-shadow: none; }"
        "#status_bar { border-top: 1px solid alpha(@borders,0.6); padding-top: 2px; padding-bottom: 2px; min-height: 24px; }"
        "#status_bar GtkLabel { padding-left: 4px; padding-right: 4px; }";

    gtk_widget_init_template (GTK_WIDGET (self));

    self->current_file = NULL;
    self->font_scale = 1.0;

    buffer = gtk_text_view_get_buffer (self->text_view);
    gtk_text_buffer_set_text (buffer, "", -1);
    gtk_text_view_set_wrap_mode (self->text_view, GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_top_margin (self->text_view, 12);
    gtk_text_view_set_bottom_margin (self->text_view, 12);
    gtk_text_view_set_left_margin (self->text_view, 12);
    gtk_text_view_set_right_margin (self->text_view, 12);
    gtk_text_view_set_monospace (self->text_view, TRUE);
    gtk_widget_set_name (GTK_WIDGET (self->text_view), "flow-textview");

    provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_string (provider, css);
    display = gdk_display_get_default ();
    if (display) {
        gtk_style_context_add_provider_for_display (display,
                                                    GTK_STYLE_PROVIDER (provider),
                                                    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    g_object_unref (provider);

    scroll_controller = GTK_EVENT_CONTROLLER_SCROLL (gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_VERTICAL));
    g_signal_connect (scroll_controller, "scroll", G_CALLBACK (on_scroll), self);
    gtk_widget_add_controller (GTK_WIDGET (self->text_view), GTK_EVENT_CONTROLLER (scroll_controller));

    key_controller = GTK_EVENT_CONTROLLER_KEY (gtk_event_controller_key_new ());
    g_signal_connect (key_controller, "key-pressed", G_CALLBACK (on_key_pressed), self);
    gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (key_controller));

    self->font_provider = gtk_css_provider_new ();
    gtk_style_context_add_provider_for_display (display,
                                                GTK_STYLE_PROVIDER (self->font_provider),
                                                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    update_font_size (self);

    g_signal_connect (self->new_button, "clicked", G_CALLBACK (on_new_button_clicked), self);
    g_signal_connect (self->open_button, "clicked", G_CALLBACK (on_open_button_clicked), self);
    g_signal_connect (self->save_button, "clicked", G_CALLBACK (on_save_button_clicked), self);
    g_signal_connect (buffer, "changed", G_CALLBACK (on_buffer_changed), self);
    g_signal_connect (buffer, "mark-set", G_CALLBACK (on_buffer_mark_set), self);

    g_signal_connect (self->search_entry, "search-changed", G_CALLBACK (on_search_changed), self);
    g_signal_connect (self->search_entry, "activate", G_CALLBACK (on_find_next_clicked), self);
    g_signal_connect (self->find_next_button, "clicked", G_CALLBACK (on_find_next_clicked), self);
    g_signal_connect (self->find_previous_button, "clicked", G_CALLBACK (on_find_previous_clicked), self);
    g_signal_connect (self->close_search_button, "clicked", G_CALLBACK (on_close_search_clicked), self);
    g_signal_connect (self->replace_button, "clicked", G_CALLBACK (on_replace_clicked), self);
    g_signal_connect (self->replace_all_button, "clicked", G_CALLBACK (on_replace_all_clicked), self);

    update_window_title (self);
    update_status (self, "Ready");
    refresh_document_info (self);
}

static void
flow_window_dispose (GObject *object)
{
    FlowWindow *self = FLOW_WINDOW (object);

    clear_current_file (self);

    if (self->font_provider) {
        g_object_unref (self->font_provider);
        self->font_provider = NULL;
    }

    G_OBJECT_CLASS (flow_window_parent_class)->dispose (object);
}

static void
update_window_title (FlowWindow *self)
{
    if (self->current_file) {
        gchar *basename = g_file_get_basename (self->current_file);
        adw_window_title_set_title (self->title_widget, basename);
        adw_window_title_set_subtitle (self->title_widget, "Flow Notepad");
        g_free (basename);
    } else {
        adw_window_title_set_title (self->title_widget, "Untitled");
        adw_window_title_set_subtitle (self->title_widget, "Flow Notepad");
    }
}

static void
update_status (FlowWindow *self, const gchar *message)
{
    gtk_label_set_text (self->status_label, message ? message : "");
}

static void
update_cursor_info (FlowWindow *self)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer (self->text_view);
    GtkTextIter iter;
    GtkTextMark *insert_mark = gtk_text_buffer_get_insert (buffer);
    gchar *text;
    gint line;
    gint column;

    gtk_text_buffer_get_iter_at_mark (buffer, &iter, insert_mark);
    line = gtk_text_iter_get_line (&iter) + 1;
    column = gtk_text_iter_get_line_offset (&iter) + 1;

    text = g_strdup_printf ("Ln %d, Col %d", line, column);
    gtk_label_set_text (self->position_label, text);
    g_free (text);
}

static gint
count_words (const gchar *text)
{
    gboolean in_word = FALSE;
    gint count = 0;
    const gchar *p;

    if (!text || !*text)
        return 0;

    for (p = text; *p; p = g_utf8_next_char (p)) {
        gunichar ch = g_utf8_get_char (p);

        if (g_unichar_isspace (ch)) {
            if (in_word)
                in_word = FALSE;
        } else {
            if (!in_word) {
                in_word = TRUE;
                count++;
            }
        }
    }

    return count;
}

static void
update_stats_info (FlowWindow *self)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer (self->text_view);
    GtkTextIter start;
    GtkTextIter end;
    gchar *text;
    gint words;
    gint chars;
    gint lines;
    gchar *info;

    gtk_text_buffer_get_bounds (buffer, &start, &end);
    text = gtk_text_buffer_get_text (buffer, &start, &end, TRUE);
    words = count_words (text);
    chars = gtk_text_iter_get_offset (&end);
    lines = gtk_text_buffer_get_line_count (buffer);
    info = g_strdup_printf ("Lines: %d · Words: %d · Chars: %d", lines, words, chars);
    gtk_label_set_text (self->stats_label, info);
    g_free (info);
    g_free (text);
}

static void
refresh_document_info (FlowWindow *self)
{
    update_cursor_info (self);
    update_stats_info (self);
}

static void
clear_current_file (FlowWindow *self)
{
    if (self->current_file) {
        g_object_unref (self->current_file);
        self->current_file = NULL;
    }
    update_window_title (self);
}

static void
update_font_size (FlowWindow *self)
{
    gchar *css;
    gint font_size_int;
    gdouble font_size = 13.0 * self->font_scale;

    if (font_size < 6.0)
        font_size = 6.0;
    if (font_size > 72.0)
        font_size = 72.0;

    font_size_int = (gint)font_size;
    css = g_strdup_printf ("textview { font-size: %dpt; }", font_size_int);
    gtk_css_provider_load_from_string (self->font_provider, css);
    g_free (css);
}

static void
zoom_in (FlowWindow *self)
{
    self->font_scale *= 1.1;
    if (self->font_scale > 5.0)
        self->font_scale = 5.0;
    update_font_size (self);
}

static void
zoom_out (FlowWindow *self)
{
    self->font_scale /= 1.1;
    if (self->font_scale < 0.5)
        self->font_scale = 0.5;
    update_font_size (self);
}

static gboolean
on_key_pressed (GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data)
{
    FlowWindow *self = FLOW_WINDOW (user_data);

    if (state & GDK_CONTROL_MASK) {
        switch (keyval) {
            case GDK_KEY_n:
            case GDK_KEY_N:
                on_new_button_clicked (NULL, self);
                return TRUE;
            case GDK_KEY_o:
            case GDK_KEY_O:
                on_open_button_clicked (NULL, self);
                return TRUE;
            case GDK_KEY_s:
            case GDK_KEY_S:
                on_save_button_clicked (NULL, self);
                return TRUE;
            case GDK_KEY_f:
            case GDK_KEY_F:
                show_find (self);
                return TRUE;
            case GDK_KEY_h:
            case GDK_KEY_H:
                show_replace (self);
                return TRUE;
            case GDK_KEY_plus:
            case GDK_KEY_equal:
            case GDK_KEY_KP_Add:
                zoom_in (self);
                return TRUE;
            case GDK_KEY_minus:
            case GDK_KEY_underscore:
            case GDK_KEY_KP_Subtract:
                zoom_out (self);
                return TRUE;
            case GDK_KEY_0:
            case GDK_KEY_KP_0:
                self->font_scale = 1.0;
                update_font_size (self);
                return TRUE;
            default:
                break;
        }
    }

    return FALSE;
}

static gboolean
on_scroll (GtkEventControllerScroll *controller, gdouble dx, gdouble dy, gpointer user_data)
{
    FlowWindow *self = FLOW_WINDOW (user_data);
    GdkModifierType state;

    state = gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (controller));

    if (state & GDK_CONTROL_MASK) {
        if (dy < 0) {
            zoom_in (self);
        } else if (dy > 0) {
            zoom_out (self);
        }
        return TRUE;
    }

    return FALSE;
}

static void
show_find (FlowWindow *self)
{
    gtk_widget_set_visible (GTK_WIDGET (self->replace_box), FALSE);
    gtk_revealer_set_reveal_child (self->search_revealer, TRUE);
    gtk_widget_grab_focus (GTK_WIDGET (self->search_entry));
}

static void
show_replace (FlowWindow *self)
{
    gtk_widget_set_visible (GTK_WIDGET (self->replace_box), TRUE);
    gtk_revealer_set_reveal_child (self->search_revealer, TRUE);
    gtk_widget_grab_focus (GTK_WIDGET (self->search_entry));
}

static void
hide_search (FlowWindow *self)
{
    gtk_revealer_set_reveal_child (self->search_revealer, FALSE);
    gtk_widget_grab_focus (GTK_WIDGET (self->text_view));
}

static gboolean
find_text (FlowWindow *self, gboolean forward)
{
    GtkTextBuffer *buffer;
    GtkTextIter start;
    GtkTextIter end;
    GtkTextIter match_start;
    GtkTextIter match_end;
    const gchar *search_text;
    gboolean found;

    buffer = gtk_text_view_get_buffer (self->text_view);
    search_text = gtk_editable_get_text (GTK_EDITABLE (self->search_entry));

    if (!search_text || !*search_text) {
        return FALSE;
    }

    gtk_text_buffer_get_selection_bounds (buffer, &start, &end);
    if (gtk_text_iter_equal (&start, &end)) {
        gtk_text_buffer_get_iter_at_mark (buffer, &start, gtk_text_buffer_get_insert (buffer));
    } else {
        if (forward)
            start = end;
    }

    if (forward) {
        found = gtk_text_iter_forward_search (&start, search_text,
                                              GTK_TEXT_SEARCH_TEXT_ONLY | GTK_TEXT_SEARCH_CASE_INSENSITIVE,
                                              &match_start, &match_end, NULL);
    } else {
        found = gtk_text_iter_backward_search (&start, search_text,
                                               GTK_TEXT_SEARCH_TEXT_ONLY | GTK_TEXT_SEARCH_CASE_INSENSITIVE,
                                               &match_start, &match_end, NULL);
    }

    if (found) {
        gtk_text_buffer_select_range (buffer, &match_start, &match_end);
        gtk_text_view_scroll_to_iter (self->text_view, &match_start, 0.0, FALSE, 0.0, 0.0);
        update_status (self, "Found");
    } else {
        update_status (self, "Not found");
    }

    return found;
}

static void
on_search_changed (GtkSearchEntry *entry, gpointer user_data)
{
    FlowWindow *self = FLOW_WINDOW (user_data);
    GtkTextBuffer *buffer;
    GtkTextIter start;

    buffer = gtk_text_view_get_buffer (self->text_view);
    gtk_text_buffer_get_start_iter (buffer, &start);
    gtk_text_buffer_place_cursor (buffer, &start);
    find_text (self, TRUE);
}

static void
on_find_next_clicked (GtkButton *button, gpointer user_data)
{
    FlowWindow *self = FLOW_WINDOW (user_data);
    find_text (self, TRUE);
}

static void
on_find_previous_clicked (GtkButton *button, gpointer user_data)
{
    FlowWindow *self = FLOW_WINDOW (user_data);
    find_text (self, FALSE);
}

static void
on_replace_clicked (GtkButton *button, gpointer user_data)
{
    FlowWindow *self = FLOW_WINDOW (user_data);
    GtkTextBuffer *buffer;
    GtkTextIter start;
    GtkTextIter end;
    const gchar *replace_text;

    buffer = gtk_text_view_get_buffer (self->text_view);

    if (gtk_text_buffer_get_selection_bounds (buffer, &start, &end)) {
        replace_text = gtk_editable_get_text (GTK_EDITABLE (self->replace_entry));
        gtk_text_buffer_delete (buffer, &start, &end);
        gtk_text_buffer_insert (buffer, &start, replace_text, -1);
        update_status (self, "Replaced");
        find_text (self, TRUE);
    } else {
        update_status (self, "No selection to replace");
    }
}

static void
on_replace_all_clicked (GtkButton *button, gpointer user_data)
{
    FlowWindow *self = FLOW_WINDOW (user_data);
    GtkTextBuffer *buffer;
    GtkTextIter start;
    gint count = 0;
    gchar *message;

    buffer = gtk_text_view_get_buffer (self->text_view);
    gtk_text_buffer_get_start_iter (buffer, &start);
    gtk_text_buffer_place_cursor (buffer, &start);

    while (find_text (self, TRUE)) {
        GtkTextIter sel_start;
        GtkTextIter sel_end;
        const gchar *replace_text;

        if (gtk_text_buffer_get_selection_bounds (buffer, &sel_start, &sel_end)) {
            replace_text = gtk_editable_get_text (GTK_EDITABLE (self->replace_entry));
            gtk_text_buffer_delete (buffer, &sel_start, &sel_end);
            gtk_text_buffer_insert (buffer, &sel_start, replace_text, -1);
            count++;
        } else {
            break;
        }
    }

    message = g_strdup_printf ("Replaced %d occurrence(s)", count);
    update_status (self, message);
    g_free (message);
}

static void
on_close_search_clicked (GtkButton *button, gpointer user_data)
{
    FlowWindow *self = FLOW_WINDOW (user_data);
    hide_search (self);
}

static void
on_buffer_changed (GtkTextBuffer *buffer, gpointer user_data)
{
    FlowWindow *self = FLOW_WINDOW (user_data);
    update_stats_info (self);
    update_cursor_info (self);
}

static void
on_buffer_mark_set (GtkTextBuffer *buffer, const GtkTextIter *location, GtkTextMark *mark, gpointer user_data)
{
    FlowWindow *self = FLOW_WINDOW (user_data);

    if (mark == gtk_text_buffer_get_insert (buffer))
        update_cursor_info (self);
}

static void
on_new_button_clicked (GtkButton *button, FlowWindow *self)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer (self->text_view);
    gtk_text_buffer_set_text (buffer, "", -1);
    clear_current_file (self);
    update_status (self, "New document created.");
    refresh_document_info (self);
}

static void
on_open_button_clicked (GtkButton *button, FlowWindow *self)
{
    GtkFileDialog *dialog;

    dialog = gtk_file_dialog_new ();
    gtk_file_dialog_set_title (dialog, "Open File");
    gtk_file_dialog_open (dialog, GTK_WINDOW (self), NULL, open_dialog_completed, g_object_ref (self));

    g_object_unref (dialog);
}

static void
on_save_button_clicked (GtkButton *button, FlowWindow *self)
{
    GtkFileDialog *dialog;

    if (self->current_file) {
        save_buffer_to_file (self, self->current_file);
        return;
    }

    dialog = gtk_file_dialog_new ();

    gtk_file_dialog_set_title (dialog, "Save File");
    gtk_file_dialog_save (dialog, GTK_WINDOW (self), NULL, save_dialog_completed, g_object_ref (self));

    g_object_unref (dialog);
}

static void
open_dialog_completed (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    FlowWindow *self = FLOW_WINDOW (user_data);
    GFile *file;
    GError *error = NULL;

    file = gtk_file_dialog_open_finish (GTK_FILE_DIALOG (source_object), result, &error);

    if (error) {
        if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            update_status (self, error->message);
        g_error_free (error);
        goto out;
    }

    if (file) {
        gchar *contents = NULL;
        gsize length = 0;
        GtkTextBuffer *buffer;
        gchar *basename;
        gchar *message;

        buffer = gtk_text_view_get_buffer (self->text_view);
        if (g_file_load_contents (file, NULL, &contents, &length, NULL, &error)) {
            gtk_text_buffer_set_text (buffer, contents, -1);
            g_free (contents);

            clear_current_file (self);
            self->current_file = g_object_ref (file);
            update_window_title (self);

            basename = g_file_get_basename (file);
            message = g_strdup_printf ("Opened %s", basename);
            update_status (self, message);
            g_free (message);
            g_free (basename);
            refresh_document_info (self);
        } else {
            update_status (self, error->message);
            g_error_free (error);
            g_free (contents);
            error = NULL;
        }

        g_object_unref (file);
    }

out:
    g_object_unref (self);
}

static gboolean
save_buffer_to_file (FlowWindow *self, GFile *file)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer (self->text_view);
    GtkTextIter start;
    GtkTextIter end;
    gchar *text;
    gsize length;
    gboolean success;
    GError *error = NULL;
    gchar *basename;
    gchar *message;

    gtk_text_buffer_get_bounds (buffer, &start, &end);
    text = gtk_text_buffer_get_text (buffer, &start, &end, TRUE);
    length = strlen (text);

    success = g_file_replace_contents (file,
                                       text,
                                       length,
                                       NULL,
                                       FALSE,
                                       G_FILE_CREATE_NONE,
                                       NULL,
                                       NULL,
                                       &error);

    g_free (text);

    if (!success) {
        update_status (self, error->message);
        g_error_free (error);
        return FALSE;
    }

    basename = g_file_get_basename (file);
    message = g_strdup_printf ("Saved %s", basename);
    update_status (self, message);
    g_free (message);
    g_free (basename);

    update_window_title (self);
    refresh_document_info (self);

    return TRUE;
}

static void
save_dialog_completed (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    FlowWindow *self = FLOW_WINDOW (user_data);
    GFile *file;
    GError *error = NULL;

    file = gtk_file_dialog_save_finish (GTK_FILE_DIALOG (source_object), result, &error);

    if (error) {
        if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            update_status (self, error->message);
        g_error_free (error);
        goto out;
    }

    if (file) {
        if (save_buffer_to_file (self, file)) {
            clear_current_file (self);
            self->current_file = g_object_ref (file);
            update_window_title (self);
        }
        g_object_unref (file);
    }

out:
    g_object_unref (self);
}
