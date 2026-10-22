//
// "$Id: editor.cxx 5519 2006-10-11 03:12:15Z mike $"
//
// A simple text editor program for the Fast Light Tool Kit (FLTK).
//
// This program is described in Chapter 4 of the FLTK Programmer's Guide.
//
// Copyright 1998-2005 by Bill Spitzak and others.
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Library General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Library General Public License for more details.
//
// You should have received a copy of the GNU Library General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
// USA.
//
// Please report all bugs and problems on the following page:
//
//     http://www.fltk.org/str.php
//

// -------------------------- Change Log -------------------------- //
// Ver 0.1 GNULinuxBP  10/1/2026
// Edited by: GNULinuxBP
// URL: https://linuxbp.blogspot.com/
// Based on the 0.2 version by Rich (Tinycore Linux).
//
// Added a status bar at the bottom of the window showing the current line, the current column, and a [MOD] marker when the buffer has unsaved changes.
// The status bar is now updated from an idle handler instead of from the modify callback. This keeps typing fast on large files and also refreshes the line/column when the cursor moves without editing. An early return in update_status() short-circuits the work when neither the cursor nor the modified flag changed.
// Added a Font dialog (Edit > Font...) with the four Helvetica variants (Regular, Bold, Italic, Bold Italic) and a size selector from 6 to 64. The change is on-screen only, the buffer and the saved file are not affected. The four Helvetica variants are hardcoded because they are the ones that scale reliably on this target; system font enumeration through Fl::get_font_name() was removed after it proved unstable on some builds.
// Added Zoom In, Zoom Out and Zoom Reset, bound to Ctrl+=, Ctrl+- and Ctrl+0.
// Added Go to Line, bound to Ctrl+J.
// Added Uppercase, Lowercase and Title Case for the current selection.
// Added Insert Date/Time, bound to F5, with format YYYY-MM-DD HH:MM:SS.
// Added Select All to the Edit menu, bound to Ctrl+A. Note that this overrides the default Fl_Text_Editor binding for Ctrl+A (beginning of line); Home still works for that.
// Added a right-click context menu on the editor with Cut, Copy, Paste, Delete and Select All. The selection is preserved when the click lands inside it, like in most other editors.
// Added Fl_Preferences so that window position, window size, editor font and font size are remembered between runs. The prefs file is written to ~/.config/tinycore.editor/fltk-editor.prefs.
// Increased the filename and title buffers from 256 to 1024 bytes to avoid overflow with long paths.
// Removed a redundant assignment to 'find' in replall_cb that was left over from the original FLTK example.
// Replaced the Version submenu with an About entry that opens a small message dialog listing the application name, version and authors. The entry lives inside its own top-level "About" submenu so it matches the style of the other menus.
//
// Ver 0.2  1/21/2025
// Changed by: Rich for Tinycore Linux.
// Removed syntax highlighting.
// Disabled "drag and drop text". You can re-enable it with ~/.Xdefaults: fltk*dndTextOps: true
// Added version number to source.
// Added Version menu entry.
// Added line number and line wrap code lifted from a later version of this program.
// I added these options in preprocessor directives:
//     Uncomment #define DIS_DNDTEXT to disable drag and drop text.
//     Uncomment #define ENA_LINEWRAP to enable line wrapping in Edit menu.
//     Uncomment #define ENA_LINENUMS to enable line numbers in Edit menu.
//
// --------------------------- End Log ---------------------------- //

#define Editor_TextFont FL_HELVETICA
#define Editor_TextSize 10

#define Version "Ver 0.1 GNULinuxBP"

// Uncomment the next line to disable drag and drop text.
#define DIS_DNDTEXT

// Uncomment the next line to enable linewrap option in Edit menu.
#define ENA_LINEWRAP

// Uncomment the next line to enable linenumber option in Edit menu.
#define ENA_LINENUMS

#ifdef ENA_LINENUMS
// Width of line number display, if enabled.
const int line_num_width = 40;
#endif // ENA_LINENUMS

//
// Include necessary headers...
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>

#ifdef __MWERKS__
# define FL_DLL
#endif

#include <FL/Fl.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Double_Window.H>
#include <FL/fl_ask.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Preferences.H>


int                changed = 0;
char               filename[1024] = "";
char               title[1024];
Fl_Text_Buffer     *textbuf = 0;
// g_prefs is intentionally never deleted: the program always exits through exit(), so the OS reclaims all memory. Adding a manual delete would only be needed if we ever switch to a clean return from Fl::run().
Fl_Preferences     *g_prefs = 0;


// Font choices. Only the four Helvetica variants: they scale reliably
// on all backends we target, unlike the other built-in fonts or any
// dynamically registered XFT font.
static const struct { const char* name; Fl_Font id; } g_fonts[] = {
  { "Helvetica",             FL_HELVETICA },
  { "Helvetica Bold",        FL_HELVETICA_BOLD },
  { "Helvetica Italic",      FL_HELVETICA_ITALIC },
  { "Helvetica Bold Italic", FL_HELVETICA_BOLD_ITALIC },
};
static const int num_fonts = sizeof(g_fonts) / sizeof(g_fonts[0]);

static const int size_values[] = { 6, 7, 8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32, 40, 48, 64 };
static const int num_sizes = sizeof(size_values) / sizeof(size_values[0]);


// Editor window functions and class...
void save_cb();
void saveas_cb();
void find2_cb(Fl_Widget*, void*);
void replall_cb(Fl_Widget*, void*);
void replace2_cb(Fl_Widget*, void*);
void replcan_cb(Fl_Widget*, void*);
void font_cb(Fl_Widget*, void*);
void font_apply_cb(Fl_Widget*, void*);
void font_cancel_cb(Fl_Widget*, void*);
void zoom_in_cb(Fl_Widget*, void*);
void zoom_out_cb(Fl_Widget*, void*);
void zoom_reset_cb(Fl_Widget*, void*);
void goto_line_cb(Fl_Widget*, void*);
void upper_cb(Fl_Widget*, void*);
void lower_cb(Fl_Widget*, void*);
void titlecase_cb(Fl_Widget*, void*);
void insert_date_cb(Fl_Widget*, void*);
void idle_status_cb(void*);
void select_all_cb(Fl_Widget*, void*);
void about_cb(Fl_Widget*, void*);

void menu_cut_cb(Fl_Widget*, void*);
void menu_copy_cb(Fl_Widget*, void*);
void menu_paste_cb(Fl_Widget*, void*);
void menu_delete_cb(Fl_Widget*, void*);
void menu_select_all_cb(Fl_Widget*, void*);


class EditorText : public Fl_Text_Editor {
  public:
    Fl_Menu_Button* popup;
    EditorText(int x, int y, int w, int h) : Fl_Text_Editor(x, y, w, h) {
      popup = 0;
    }
    int handle(int e);
};

int EditorText::handle(int e) {
  if (e == FL_PUSH && Fl::event_button() == FL_RIGHT_MOUSE && popup) {
    int pos = xy_to_position(Fl::event_x(), Fl::event_y(), CHARACTER_POS);
    int start = 0, end = 0;
    buffer()->selection_position(&start, &end);
    if (start == end || pos < start || pos > end) {
      insert_position(pos);
      buffer()->unselect();
    }
    popup->popup();
    take_focus();
    return 1;
  }
  return Fl_Text_Editor::handle(e);
}

void menu_cut_cb(Fl_Widget*, void* v) {
  EditorText* e = (EditorText*)v;
  Fl_Text_Editor::kf_cut(0, e);
}
void menu_copy_cb(Fl_Widget*, void* v) {
  EditorText* e = (EditorText*)v;
  Fl_Text_Editor::kf_copy(0, e);
}
void menu_paste_cb(Fl_Widget*, void* v) {
  EditorText* e = (EditorText*)v;
  Fl_Text_Editor::kf_paste(0, e);
}
void menu_delete_cb(Fl_Widget*, void* v) {
  EditorText* e = (EditorText*)v;
  e->buffer()->remove_selection();
}
void menu_select_all_cb(Fl_Widget*, void* v) {
  EditorText* e = (EditorText*)v;
  e->buffer()->select(0, e->buffer()->length());
}


class EditorWindow : public Fl_Double_Window {
  public:
    EditorWindow(int w, int h, const char* t);
    ~EditorWindow();

    Fl_Window          *replace_dlg;
    Fl_Input           *replace_find;
    Fl_Input           *replace_with;
    Fl_Button          *replace_all;
    Fl_Return_Button   *replace_next;
    Fl_Button          *replace_cancel;

    Fl_Window          *font_dlg;
    Fl_Choice          *font_choice;
    Fl_Choice          *size_choice;

#ifdef ENA_LINEWRAP
    int			wrap_mode;
#endif // ENA_LINEWRAP

#ifdef ENA_LINENUMS
    int			line_numbers;
#endif // ENA_LINENUMS

    EditorText         *editor;
    Fl_Box             *status;
    char               search[256];

    int                last_pos;
    int                last_changed;

    void update_status();
    void apply_editor_font(Fl_Font f, int s);
    void save_font_prefs();
};

EditorWindow::EditorWindow(int w, int h, const char* t) : Fl_Double_Window(w, h, t) {
  replace_dlg = new Fl_Window(300, 105, "Replace");
    replace_find = new Fl_Input(80, 10, 210, 25, "Find:");
    replace_find->align(FL_ALIGN_LEFT);

    replace_with = new Fl_Input(80, 40, 210, 25, "Replace:");
    replace_with->align(FL_ALIGN_LEFT);

    replace_all = new Fl_Button(10, 70, 90, 25, "Replace All");
    replace_all->callback((Fl_Callback *)replall_cb, this);

    replace_next = new Fl_Return_Button(105, 70, 120, 25, "Replace Next");
    replace_next->callback((Fl_Callback *)replace2_cb, this);

    replace_cancel = new Fl_Button(230, 70, 60, 25, "Cancel");
    replace_cancel->callback((Fl_Callback *)replcan_cb, this);
  replace_dlg->end();
  replace_dlg->set_non_modal();

  font_dlg = new Fl_Window(320, 150, "Editor Font");
    font_choice = new Fl_Choice(90, 10, 210, 25, "Font:");
    font_choice->align(FL_ALIGN_LEFT);
    for (int i = 0; i < num_fonts; i++) font_choice->add(g_fonts[i].name);

    size_choice = new Fl_Choice(90, 45, 210, 25, "Size:");
    size_choice->align(FL_ALIGN_LEFT);
    for (int i = 0; i < num_sizes; i++) {
      char buf[16];
      snprintf(buf, sizeof(buf), "%d", size_values[i]);
      size_choice->add(buf);
    }

    Fl_Button* ok = new Fl_Button(90, 100, 90, 25, "Apply");
    ok->callback((Fl_Callback *)font_apply_cb, this);
    Fl_Button* cl = new Fl_Button(200, 100, 90, 25, "Close");
    cl->callback((Fl_Callback *)font_cancel_cb, this);
  font_dlg->end();
  font_dlg->set_non_modal();

  editor = 0;
  status = 0;
  last_pos = -1;
  last_changed = -1;
  *search = (char)0;

#ifdef ENA_LINEWRAP
  wrap_mode = 0;
#endif // ENA_LINEWRAP

#ifdef ENA_LINENUMS
  line_numbers = 0;
#endif // ENA_LINENUMS
}

EditorWindow::~EditorWindow() {
  delete replace_dlg;
  delete font_dlg;
}

void EditorWindow::update_status() {
  if (!editor || !status) return;
  int pos = editor->insert_position();
  if (pos == last_pos && ::changed == last_changed) return;
  last_pos = pos;
  last_changed = ::changed;

  int line = textbuf->count_lines(0, pos) + 1;
  int lineStart = textbuf->skip_lines(0, line - 1);
  if (lineStart < 0) lineStart = 0;
  int col = pos - lineStart + 1;

  char buf[80];
  snprintf(buf, sizeof(buf), " Ln %d, Col %d%s", line, col,
           ::changed ? "   [MOD]" : "");
  status->copy_label(buf);
  status->redraw();
}

void EditorWindow::apply_editor_font(Fl_Font f, int s) {
  if (!editor) return;
  editor->textfont(f);
  editor->textsize(s);
#ifdef ENA_LINENUMS
  if (line_numbers) {
    editor->linenumber_font(f);
    editor->linenumber_size(s);
  }
#endif // ENA_LINENUMS
  redraw();
}

void EditorWindow::save_font_prefs() {
  if (!g_prefs || !editor) return;
  g_prefs->set("editor_font", (int)editor->textfont());
  g_prefs->set("editor_size", editor->textsize());
}


void idle_status_cb(void* data) {
  EditorWindow* e = (EditorWindow*)data;
  if (e) e->update_status();
}


int check_save(void) {
  if (!changed) return 1;

  int r = fl_choice("The current file has not been saved.\n"
                    "Would you like to save it now?",
                    "Cancel", "Save", "Don't Save");

  if (r == 1) {
    save_cb(); // Save the file...
    return !changed;
  }

  return (r == 2) ? 1 : 0;
}

int loading = 0;
void load_file(char *newfile, int ipos) {
  loading = 1;
  int insert = (ipos != -1);
  changed = insert;
  if (!insert) strcpy(filename, "");
  int r;
  if (!insert) r = textbuf->loadfile(newfile);
  else r = textbuf->insertfile(newfile, ipos);
  if (r)
    fl_alert("Error reading from file \'%s\':\n%s.", newfile, strerror(errno));
  else
    if (!insert) strcpy(filename, newfile);
  loading = 0;
  textbuf->call_modify_callbacks();
}

void save_file(char *newfile) {
  if (textbuf->savefile(newfile))
    fl_alert("Error writing to file \'%s\':\n%s.", newfile, strerror(errno));
  else
    strcpy(filename, newfile);
  changed = 0;
  textbuf->call_modify_callbacks();
}

void copy_cb(Fl_Widget*, void* v) {
  EditorWindow* e = (EditorWindow*)v;
  Fl_Text_Editor::kf_copy(0, e->editor);
}

void cut_cb(Fl_Widget*, void* v) {
  EditorWindow* e = (EditorWindow*)v;
  Fl_Text_Editor::kf_cut(0, e->editor);
}

void delete_cb(Fl_Widget*, void*) {
  textbuf->remove_selection();
}

void select_all_cb(Fl_Widget*, void* v) {
  EditorWindow* e = (EditorWindow*)v;
  e->editor->buffer()->select(0, e->editor->buffer()->length());
}

#ifdef ENA_LINEWRAP
void wordwrap_cb(Fl_Widget *w, void* v) {
  EditorWindow* e = (EditorWindow*)v;
  Fl_Menu_Bar* m = (Fl_Menu_Bar*)w;
  const Fl_Menu_Item* i = m->mvalue();
  if ( i->value() )
    e->editor->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
  else
    e->editor->wrap_mode(Fl_Text_Display::WRAP_NONE, 0);
  e->wrap_mode = (i->value()?1:0);
  e->redraw();
}
#endif // ENA_LINEWRAP

#ifdef ENA_LINENUMS
void linenumbers_cb(Fl_Widget *w, void* v) {
  EditorWindow* e = (EditorWindow*)v;
  Fl_Menu_Bar* m = (Fl_Menu_Bar*)w;
  const Fl_Menu_Item* i = m->mvalue();
  if ( i->value() ) {
    e->editor->linenumber_width(line_num_width);	// enable
    e->editor->linenumber_font(e->editor->textfont());
    e->editor->linenumber_size(e->editor->textsize());
  } else {
    e->editor->linenumber_width(0);	// disable
  }
  e->line_numbers = (i->value()?1:0);
  e->redraw();
}
#endif // ENA_LINENUMS

void find_cb(Fl_Widget* w, void* v) {
  EditorWindow* e = (EditorWindow*)v;
  const char *val;

  val = fl_input("Search String:", e->search);
  if (val != NULL) {
    // User entered a string - go find it!
    strcpy(e->search, val);
    find2_cb(w, v);
  }
}

void find2_cb(Fl_Widget* w, void* v) {
  EditorWindow* e = (EditorWindow*)v;
  if (e->search[0] == '\0') {
    // Search string is blank; get a new one...
    find_cb(w, v);
    return;
  }

  int pos = e->editor->insert_position();
  int found = textbuf->search_forward(pos, e->search, &pos);
  if (found) {
    // Found a match; select and update the position...
    textbuf->select(pos, pos+strlen(e->search));
    e->editor->insert_position(pos+strlen(e->search));
    e->editor->show_insert_position();
  }
  else fl_alert("No occurrences of \'%s\' found!", e->search);
}

void set_title(Fl_Window* w) {
  if (filename[0] == '\0') strcpy(title, "Untitled");
  else {
    char *slash;
    slash = strrchr(filename, '/');
#ifdef WIN32
    if (slash == NULL) slash = strrchr(filename, '\\');
#endif
    if (slash != NULL) strcpy(title, slash + 1);
    else strcpy(title, filename);
  }

  if (changed) strcat(title, " (modified)");

  w->label(title);
}

void changed_cb(int, int nInserted, int nDeleted,int, const char*, void* v) {
  if ((nInserted || nDeleted) && !loading) changed = 1;
  EditorWindow *w = (EditorWindow *)v;
  set_title(w);
  if (loading) w->editor->show_insert_position();
}

void new_cb(Fl_Widget*, void*) {
  if (!check_save()) return;

  filename[0] = '\0';
  textbuf->select(0, textbuf->length());
  textbuf->remove_selection();
  changed = 0;
  textbuf->call_modify_callbacks();
}

void open_cb(Fl_Widget*, void*) {
  if (!check_save()) return;

  char *newfile = fl_file_chooser("Open File?", "*", filename);
  if (newfile != NULL) load_file(newfile, -1);
}

void insert_cb(Fl_Widget*, void *v) {
  char *newfile = fl_file_chooser("Insert File?", "*", filename);
  EditorWindow *w = (EditorWindow *)v;
  if (newfile != NULL) load_file(newfile, w->editor->insert_position());
}

void paste_cb(Fl_Widget*, void* v) {
  EditorWindow* e = (EditorWindow*)v;
  Fl_Text_Editor::kf_paste(0, e->editor);
}

int num_windows = 0;

void close_cb(Fl_Widget*, void* v) {
  Fl_Window* w = (Fl_Window*)v;
  if (num_windows == 1 && !check_save()) {
    return;
  }

  if (g_prefs) {
    g_prefs->set("win_x", w->x());
    g_prefs->set("win_y", w->y());
    g_prefs->set("win_w", w->w());
    g_prefs->set("win_h", w->h());
  }

  Fl::remove_idle((Fl_Idle_Handler)idle_status_cb, w);

  w->hide();
  textbuf->remove_modify_callback(changed_cb, w);
  Fl::delete_widget(w);
  num_windows--;
  if (!num_windows) exit(0);
}

void quit_cb(Fl_Widget*, void*) {
  if (changed && !check_save())
    return;

  if (g_prefs) {
    Fl_Window* w = Fl::first_window();
    if (w) {
      g_prefs->set("win_x", w->x());
      g_prefs->set("win_y", w->y());
      g_prefs->set("win_w", w->w());
      g_prefs->set("win_h", w->h());
    }
    g_prefs->flush();
  }
  exit(0);
}

void replace_cb(Fl_Widget*, void* v) {
  EditorWindow* e = (EditorWindow*)v;
  e->replace_dlg->show();
}

void replace2_cb(Fl_Widget*, void* v) {
  EditorWindow* e = (EditorWindow*)v;
  const char *find = e->replace_find->value();
  const char *replace = e->replace_with->value();

  if (find[0] == '\0') {
    // Search string is blank; get a new one...
    e->replace_dlg->show();
    return;
  }

  e->replace_dlg->hide();

  int pos = e->editor->insert_position();
  int found = textbuf->search_forward(pos, find, &pos);

  if (found) {
    // Found a match; update the position and replace text...
    textbuf->select(pos, pos+strlen(find));
    textbuf->remove_selection();
    textbuf->insert(pos, replace);
    textbuf->select(pos, pos+strlen(replace));
    e->editor->insert_position(pos+strlen(replace));
    e->editor->show_insert_position();
  }
  else fl_alert("No occurrences of \'%s\' found!", find);
}

void replall_cb(Fl_Widget*, void* v) {
  EditorWindow* e = (EditorWindow*)v;
  const char *find = e->replace_find->value();
  const char *replace = e->replace_with->value();

  if (find[0] == '\0') {
    // Search string is blank; get a new one...
    e->replace_dlg->show();
    return;
  }

  e->replace_dlg->hide();

  e->editor->insert_position(0);
  int times = 0;

  // Loop through the whole string
  for (int found = 1; found;) {
    int pos = e->editor->insert_position();
    found = textbuf->search_forward(pos, find, &pos);

    if (found) {
      // Found a match; update the position and replace text...
      textbuf->select(pos, pos+strlen(find));
      textbuf->remove_selection();
      textbuf->insert(pos, replace);
      e->editor->insert_position(pos+strlen(replace));
      e->editor->show_insert_position();
      times++;
    }
  }

  if (times) fl_message("Replaced %d occurrences.", times);
  else fl_alert("No occurrences of \'%s\' found!", find);
}

void replcan_cb(Fl_Widget*, void* v) {
  EditorWindow* e = (EditorWindow*)v;
  e->replace_dlg->hide();
}

void save_cb() {
  if (filename[0] == '\0') {
    // No filename - get one!
    saveas_cb();
    return;
  }
  else save_file(filename);
}

void saveas_cb() {
  char *newfile;

  newfile = fl_file_chooser("Save File As?", "*", filename);
  if (newfile != NULL) save_file(newfile);
}

Fl_Window* new_view();

void view_cb(Fl_Widget*, void*) {
  Fl_Window* w = new_view();
  w->show();
}

void zoom_in_cb(Fl_Widget*, void* v) {
  EditorWindow* e = (EditorWindow*)v;
  int s = e->editor->textsize() + 1;
  if (s > 72) s = 72;
  e->apply_editor_font(e->editor->textfont(), s);
  e->save_font_prefs();
}
void zoom_out_cb(Fl_Widget*, void* v) {
  EditorWindow* e = (EditorWindow*)v;
  int s = e->editor->textsize() - 1;
  if (s < 6) s = 6;
  e->apply_editor_font(e->editor->textfont(), s);
  e->save_font_prefs();
}
void zoom_reset_cb(Fl_Widget*, void* v) {
  EditorWindow* e = (EditorWindow*)v;
  e->apply_editor_font(e->editor->textfont(), Editor_TextSize);
  e->save_font_prefs();
}

void goto_line_cb(Fl_Widget*, void* v) {
  EditorWindow* e = (EditorWindow*)v;
  const char* s = fl_input("Go to line:", "");
  if (!s || !s[0]) return;
  int n = atoi(s);
  if (n < 1) return;
  int pos = textbuf->skip_lines(0, n - 1);
  if (pos < 0) pos = textbuf->length();
  e->editor->insert_position(pos);
  e->editor->show_insert_position();
}

static int has_selection() {
  int start = 0, end = 0;
  textbuf->selection_position(&start, &end);
  return (start != end);
}

void upper_cb(Fl_Widget*, void*) {
  if (!has_selection()) { fl_alert("Select text first."); return; }
  int start = 0, end = 0;
  textbuf->selection_position(&start, &end);
  char* sel = textbuf->text_range(start, end);
  if (!sel) return;
  for (int i = 0; sel[i]; i++) sel[i] = toupper((unsigned char)sel[i]);
  textbuf->replace(start, end, sel);
  free(sel);
}

void lower_cb(Fl_Widget*, void*) {
  if (!has_selection()) { fl_alert("Select text first."); return; }
  int start = 0, end = 0;
  textbuf->selection_position(&start, &end);
  char* sel = textbuf->text_range(start, end);
  if (!sel) return;
  for (int i = 0; sel[i]; i++) sel[i] = tolower((unsigned char)sel[i]);
  textbuf->replace(start, end, sel);
  free(sel);
}

void titlecase_cb(Fl_Widget*, void*) {
  if (!has_selection()) { fl_alert("Select text first."); return; }
  int start = 0, end = 0;
  textbuf->selection_position(&start, &end);
  char* sel = textbuf->text_range(start, end);
  if (!sel) return;
  int prev_alpha = 0;
  for (int i = 0; sel[i]; i++) {
    unsigned char c = (unsigned char)sel[i];
    if (isalpha(c)) {
      sel[i] = prev_alpha ? tolower(c) : toupper(c);
      prev_alpha = 1;
    } else {
      prev_alpha = 0;
    }
  }
  textbuf->replace(start, end, sel);
  free(sel);
}

void insert_date_cb(Fl_Widget*, void* v) {
  EditorWindow* e = (EditorWindow*)v;
  time_t t = time(0);
  struct tm* lt = localtime(&t);
  char buf[64];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", lt);
  e->editor->insert(buf);
}

void font_cb(Fl_Widget*, void* v) {
  EditorWindow* e = (EditorWindow*)v;
  Fl_Font f = e->editor->textfont();
  int     s = e->editor->textsize();
  for (int i = 0; i < num_fonts; i++) {
    if (g_fonts[i].id == f) { e->font_choice->value(i); break; }
  }
  for (int i = 0; i < num_sizes; i++) {
    if (size_values[i] == s) { e->size_choice->value(i); break; }
  }
  e->font_dlg->show();
}

void font_apply_cb(Fl_Widget*, void* v) {
  EditorWindow* e = (EditorWindow*)v;
  int fi = e->font_choice->value();
  int si = e->size_choice->value();
  if (fi < 0 || fi >= num_fonts || si < 0 || si >= num_sizes) return;
  e->apply_editor_font(g_fonts[fi].id, size_values[si]);
  e->save_font_prefs();
}

void font_cancel_cb(Fl_Widget*, void* v) {
  EditorWindow* e = (EditorWindow*)v;
  e->font_dlg->hide();
}

void about_cb(Fl_Widget*, void*) {
  fl_message_title("About");
  fl_message("FLTK Editor - %s\n"
             "\n"
             "Bill Spitzak and others\n"
             "Rich (Tinycore Linux)\n"
             "GNULinuxBP",
             Version);
}

#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
Fl_Menu_Item menuitems[] = {
    { "&File",              0, 0, 0, FL_SUBMENU },
    { "&New File",        0, (Fl_Callback *)new_cb },
    { "&Open File...",    FL_CTRL + 'o', (Fl_Callback *)open_cb },
    { "&Insert File...",  FL_CTRL + 'i', (Fl_Callback *)insert_cb, 0, FL_MENU_DIVIDER },
    { "&Save File",       FL_CTRL + 's', (Fl_Callback *)save_cb },
    { "Save File &As...", FL_CTRL + FL_SHIFT + 's', (Fl_Callback *)saveas_cb, 0, FL_MENU_DIVIDER },
    { "New &View", FL_ALT + 'v', (Fl_Callback *)view_cb, 0 },
    { "&Close View", FL_CTRL + 'w', (Fl_Callback *)close_cb, 0, FL_MENU_DIVIDER },
    { "E&xit", FL_CTRL + 'q', (Fl_Callback *)quit_cb, 0 },
    { 0 },

  { "&Edit", 0, 0, 0, FL_SUBMENU },
    { "Cu&t",        FL_CTRL + 'x', (Fl_Callback *)cut_cb },
    { "&Copy",       FL_CTRL + 'c', (Fl_Callback *)copy_cb },
    { "&Paste",      FL_CTRL + 'v', (Fl_Callback *)paste_cb },
    { "&Delete",     0, (Fl_Callback *)delete_cb },
    { "Select &All", FL_CTRL + 'a', (Fl_Callback *)select_all_cb, 0, FL_MENU_DIVIDER },

    { "&Uppercase",  0, (Fl_Callback *)upper_cb },
    { "&Lowercase",  0, (Fl_Callback *)lower_cb },
    { "&Title Case", 0, (Fl_Callback *)titlecase_cb, 0, FL_MENU_DIVIDER },

    { "Insert &Date/Time", FL_F + 5, (Fl_Callback *)insert_date_cb, 0, FL_MENU_DIVIDER },

#ifdef ENA_LINENUMS
    { "Line &Numbers ",   FL_COMMAND + 'n', (Fl_Callback *)linenumbers_cb, 0, FL_MENU_TOGGLE },
#endif // ENA_LINENUMS

#ifdef ENA_LINEWRAP
    { "Word Wrap",      FL_COMMAND + 'l', (Fl_Callback *)wordwrap_cb, 0, FL_MENU_TOGGLE },
#endif // ENA_LINEWRAP

    { "&Font...",       0, (Fl_Callback *)font_cb, 0, FL_MENU_DIVIDER },
    { "Zoom &In",       FL_CTRL + '=', (Fl_Callback *)zoom_in_cb },
    { "Zoom &Out",      FL_CTRL + '-', (Fl_Callback *)zoom_out_cb },
    { "Zoom &Reset",    FL_CTRL + '0', (Fl_Callback *)zoom_reset_cb },
    { 0 },

  { "&Search", 0, 0, 0, FL_SUBMENU },
    { "&Find...",       FL_CTRL + 'f', (Fl_Callback *)find_cb },
    { "F&ind Again",    FL_CTRL + 'g', find2_cb },
    { "&Go to Line...", FL_CTRL + 'j', (Fl_Callback *)goto_line_cb, 0, FL_MENU_DIVIDER },
    { "&Replace...",    FL_CTRL + 'r', replace_cb },
    { "Re&place Again", FL_CTRL + 't', replace2_cb },
    { 0 },

  { "&About", 0, 0, 0, FL_SUBMENU },
    { "About", 0, (Fl_Callback *)about_cb },
    { 0 },

  { 0 }
};
#pragma GCC diagnostic warning "-Wmissing-field-initializers"

Fl_Window* new_view() {
  int wx = 100, wy = 100, ww = 660, wh = 420;
  if (g_prefs) {
    g_prefs->get("win_x", wx, 100);
    g_prefs->get("win_y", wy, 100);
    g_prefs->get("win_w", ww, 660);
    g_prefs->get("win_h", wh, 420);
    if (ww < 300) ww = 300;
    if (wh < 200) wh = 200;
  }

  EditorWindow* w = new EditorWindow(ww, wh, title);
    w->begin();
    Fl_Menu_Bar* m = new Fl_Menu_Bar(0, 0, ww, 30);
    m->copy(menuitems, w);

    w->editor = new EditorText(0, 30, ww, wh - 50);
    w->editor->buffer(textbuf);

    w->editor->popup = new Fl_Menu_Button(0, 0, 1, 1);
    w->editor->popup->type(Fl_Menu_Button::POPUP3);
    w->editor->popup->add("Cu&t",   FL_CTRL + 'x', (Fl_Callback *)menu_cut_cb,        w->editor);
    w->editor->popup->add("&Copy",  FL_CTRL + 'c', (Fl_Callback *)menu_copy_cb,       w->editor);
    w->editor->popup->add("&Paste", FL_CTRL + 'v', (Fl_Callback *)menu_paste_cb,      w->editor);
    w->editor->popup->add("&Delete", 0,            (Fl_Callback *)menu_delete_cb,     w->editor);
    w->editor->popup->add("Select &All", FL_CTRL + 'a', (Fl_Callback *)menu_select_all_cb, w->editor, FL_MENU_DIVIDER);

    int  saved_font = 0;
    int  saved_size = Editor_TextSize;
    if (g_prefs) {
      g_prefs->get("editor_font", saved_font, 0);
      g_prefs->get("editor_size", saved_size, Editor_TextSize);
    }
    if (saved_font < 0 || saved_font >= num_fonts) saved_font = 0;
    if (saved_size < 6) saved_size = Editor_TextSize;
    if (saved_size > 72) saved_size = 72;
    w->editor->textfont(g_fonts[saved_font].id);
    w->editor->textsize(saved_size);

    w->status = new Fl_Box(0, wh - 20, ww, 20);
    w->status->box(FL_DOWN_BOX);
    w->status->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    w->status->labelsize(11);
    w->status->copy_label(" Ln 1, Col 1");
  w->end();
  w->resizable(w->editor);
  w->callback((Fl_Callback *)close_cb, w);
  w->position(wx, wy);

  textbuf->add_modify_callback(changed_cb, w);
  textbuf->call_modify_callbacks();
  Fl::add_idle((Fl_Idle_Handler)idle_status_cb, w);
  num_windows++;
  return w;
}

int main(int argc, char **argv) {

#ifdef DIS_DNDTEXT
  Fl::dnd_text_ops(false);
#endif // DIS_DNDTEXT

  g_prefs = new Fl_Preferences(Fl_Preferences::USER,
                               "tinycore.editor",
                               "fltk-editor");

  textbuf = new Fl_Text_Buffer;

  Fl_Window* window = new_view();

  window->show(1, argv);

  if (argc > 1) load_file(argv[1], -1);

  return Fl::run();
}

//
// End of "$Id: editor.cxx 5519 2006-10-11 03:12:15Z mike $".
//