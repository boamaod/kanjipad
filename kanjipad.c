/* KanjiPad - Japanese handwriting recognition front end
 * Copyright (C) 1997 Owen Taylor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gtk/gtk.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "kanjipad.h"

typedef struct {
  gchar d[2];
} kp_wchar;

#define WCHAR_EQ(a,b) (a.d[0] == b.d[0] && a.d[1] == b.d[1])

/* Wait for child process? */

/* user interface elements */
static GdkPixmap *kpixmap;
GtkWidget *karea;
GtkWidget *clear_button;
GtkWidget *lookup_button;
GtkItemFactory *factory;

#define MAX_GUESSES 10
kp_wchar kanjiguess[MAX_GUESSES];
int num_guesses = 0;
kp_wchar kselected;

PadArea *pad_area;

/* globals for engine communication */
static int engine_pid;
static GIOChannel *from_engine;
static GIOChannel *to_engine;

static char *data_file = NULL;
static char *progname;

static void exit_callback ();
static void copy_callback ();
static void save_callback ();
static void clear_callback ();
static void look_up_callback ();
static void annotate_callback ();

static void update_sensitivity ();

static GtkItemFactoryEntry menu_items[] =
{
  { "/_File", NULL, NULL, 0, "<Branch>" },
  { "/File/_Quit", NULL, exit_callback, 0, "<StockItem>", GTK_STOCK_QUIT },

  { "/_Edit", NULL, NULL, 0, "<Branch>" },
  { "/Edit/_Copy", NULL, copy_callback, 0, "<StockItem>", GTK_STOCK_COPY },
  
  { "/_Character", NULL, NULL, 0, "<Branch>" },
  { "/Character/_Lookup", "<control>L", look_up_callback },
  { "/Character/_Clear", "<control>X", clear_callback },
  { "/Character/_Save", "<control>S", save_callback },
  { "/Character/sep1", NULL, NULL, 0, "<Separator>" },
  
  { "/Character/_Annotate", NULL, annotate_callback, 0, "<CheckItem>" },
};

static int nmenu_items = sizeof (menu_items) / sizeof (menu_items[0]);

static void
karea_get_char_size (GtkWidget *widget,
		     int       *width,
		     int       *height)
{
  PangoLayout *layout = gtk_widget_create_pango_layout (widget, "\xe6\xb6\x88");
  pango_layout_get_pixel_size (layout, width, height);

  g_object_unref (layout);
}

static gchar *
utf8_for_char (kp_wchar ch)
{
  gchar *string_utf;
  GError *err = NULL;
  gchar str[3];

  str[0] = ch.d[0] + 0x80;
  str[1] = ch.d[1] + 0x80;
  str[2] = 0;

  string_utf = g_convert (str, -1, "UTF-8", "EUC-JP", NULL, NULL, &err);
  if (!string_utf)
    {
      g_printerr ("Cannot convert string from EUC-JP to UTF-8: %s\n",
		  err->message);
      exit (1);
    }

  return string_utf;
}

static void
karea_draw_character (GtkWidget *w,
		      int        index,
		      int        selected)
{
  PangoLayout *layout;
  gchar *string_utf;
  gint char_width, char_height;
  gint x;

  karea_get_char_size (w, &char_width, &char_height);

  if (selected >= 0)
    {
      gdk_draw_rectangle (kpixmap,
			  selected ? w->style->bg_gc[GTK_STATE_SELECTED] :
			  w->style->white_gc,
			  TRUE,
			  0, (char_height + 6) *index, w->allocation.width - 1, char_height + 5);
    }

  string_utf = utf8_for_char (kanjiguess[index]);
  layout = gtk_widget_create_pango_layout (w, string_utf);
  g_free (string_utf);

  x = (w->allocation.width - char_width) / 2;
  
  gdk_draw_layout (kpixmap, 
		   (selected > 0) ? w->style->white_gc :
		                    w->style->black_gc,
		   x, (char_height + 6) * index + 3, layout);
  g_object_unref (layout);
}


static void
karea_draw (GtkWidget *w)
{
  gint width = w->allocation.width;
  gint height = w->allocation.height;
  int i;

  gdk_draw_rectangle (kpixmap, 
		      w->style->white_gc, TRUE,
		      0, 0, width, height);
  

  for (i=0; i<num_guesses; i++)
    {
      if (WCHAR_EQ (kselected, kanjiguess[i]))
	karea_draw_character (w, i, 1);
      else
	karea_draw_character (w, i, -1);
    }

  gtk_widget_queue_draw (w);
}

static int
karea_configure_event (GtkWidget *w, GdkEventConfigure *event)
{
  if (kpixmap)
    g_object_unref (kpixmap);

  kpixmap = gdk_pixmap_new (w->window, event->width, event->height, -1);

  karea_draw (w);
  
  return TRUE;
}

static int
karea_expose_event (GtkWidget *w, GdkEventExpose *event)
{
  if (!kpixmap)
    return 0;

  gdk_draw_drawable (w->window,
		     w->style->fg_gc[GTK_STATE_NORMAL], kpixmap,
		     event->area.x, event->area.y,
		     event->area.x, event->area.y,
		     event->area.width, event->area.height);

    return 0;
}

static int
karea_erase_selection (GtkWidget *w)
{
  int i;
  if (kselected.d[0] || kselected.d[1])
    {
      for (i=0; i<num_guesses; i++)
	{
	  if (WCHAR_EQ (kselected, kanjiguess[i]))
	    {
	      karea_draw_character (w, i, 0);
	    }
	}
    }
  return TRUE;
}

static void
karea_primary_clear (GtkClipboard *clipboard,
		     gpointer      owner)
{
  GtkWidget *w = owner;
  
  karea_erase_selection (w);
  kselected.d[0] = kselected.d[1] = 0;

  update_sensitivity ();
  gtk_widget_queue_draw (w);
}

static void
karea_primary_get (GtkClipboard     *clipboard,
		   GtkSelectionData *selection_data,
		   guint             info,
		   gpointer          owner)
{
  if (kselected.d[0] || kselected.d[1])
    {
      gchar *string_utf = utf8_for_char (kselected);
      gtk_selection_data_set_text (selection_data, string_utf, -1);
      g_free (string_utf);
    }
}

static int
karea_button_press_event (GtkWidget *w, GdkEventButton *event)
{
  int j;
  gint char_height;
  GtkClipboard *clipboard = gtk_clipboard_get (GDK_SELECTION_PRIMARY);

  static const GtkTargetEntry targets[] = {
    { "STRING", 0, 0 },
    { "TEXT",   0, 0 }, 
    { "COMPOUND_TEXT", 0, 0 },
    { "UTF8_STRING", 0, 0 }
  };

  karea_erase_selection (w);

  karea_get_char_size (w, NULL, &char_height);

  j = event->y / (char_height + 6);
  if (j < num_guesses)
    {
      kselected = kanjiguess[j];
      karea_draw_character (w, j, 1);
      
      if (!gtk_clipboard_set_with_owner (clipboard, targets, G_N_ELEMENTS (targets),
					 karea_primary_get, karea_primary_clear, G_OBJECT (w)))
	karea_primary_clear (clipboard, w);
    }
  else
    {
      kselected.d[0] = 0;
      kselected.d[1] = 0;
      if (gtk_clipboard_get_owner (clipboard) == G_OBJECT (w))
	gtk_clipboard_clear (clipboard);
    }

  update_sensitivity ();
  gtk_widget_queue_draw (w);

  return TRUE;
}

static void 
exit_callback (GtkWidget *w)
{
  exit (0);
}

static void 
copy_callback (GtkWidget *w)
{
  if (kselected.d[0] || kselected.d[1])
    {
      gchar *string_utf = utf8_for_char (kselected);
      gtk_clipboard_set_text (gtk_clipboard_get (GDK_SELECTION_CLIPBOARD), string_utf, -1);
      g_free (string_utf);
    }
}

static void 
look_up_callback (GtkWidget *w)
{
  /*	     kill 'HUP',$engine_pid; */
  GList *tmp_list;
  GString *message = g_string_new (NULL);
  GError *err = NULL;
    
  tmp_list = pad_area->strokes;
  while (tmp_list)
    {
     GList *stroke_list = tmp_list->data;
     while (stroke_list)
       {
	 gint16 x = ((GdkPoint *)stroke_list->data)->x;
	 gint16 y = ((GdkPoint *)stroke_list->data)->y;
	 g_string_append_printf (message, "%d %d ", x, y);
	 stroke_list = stroke_list->next;
       }
     g_string_append (message, "\n");
     tmp_list = tmp_list->next;
    }
  g_string_append (message, "\n");
  if (g_io_channel_write_chars (to_engine,
				message->str, message->len,
				NULL, &err) != G_IO_STATUS_NORMAL)
    {
      g_printerr ("Cannot write message to engine: %s\n",
		  err->message);
      exit (1);
    }
  if (g_io_channel_flush (to_engine, &err) != G_IO_STATUS_NORMAL)
    {
      g_printerr ("Error flushing message to engine: %s\n",
		  err->message);
      exit (1);
    }

  g_string_free (message, FALSE);
}

static void 
clear_callback (GtkWidget *w)
{
  pad_area_clear (pad_area);
}

static void 
save_callback (GtkWidget *w)
{
  static int unknownID = 0;
  static FILE *samples = NULL;

  int found = FALSE;
  int i;
  GList *tmp_list;
  
  if (!samples)
    {
      if (!(samples = fopen("samples.dat", "a")))
	g_error ("Can't open 'samples.dat': %s", g_strerror(errno));
    }
  
  if (kselected.d[0] || kselected.d[1])
    {
      for (i=0; i<num_guesses; i++)
	{
	  if (WCHAR_EQ (kselected, kanjiguess[i]))
	    found = TRUE;
	}
    }
  
  if (found)
    fprintf(samples,"%2x%2x %c%c\n", kselected.d[0], kselected.d[1],
	   0x80 | kselected.d[0], 0x80 | kselected.d[1]);
  else
    {
      fprintf (samples, "0000 ??%d\n", unknownID);
      fprintf (stderr, "Unknown character saved, ID: %d\n", unknownID);
      unknownID++;
    }

  tmp_list = pad_area->strokes;
  while (tmp_list)
    {
     GList *stroke_list = tmp_list->data;
     while (stroke_list)
       {
	 gint16 x = ((GdkPoint *)stroke_list->data)->x;
	 gint16 y = ((GdkPoint *)stroke_list->data)->y;
	 fprintf(samples, "%d %d ", x, y);
	 stroke_list = stroke_list->next;
       }
     fprintf(samples, "\n");
     tmp_list = tmp_list->next;
    }
  fprintf(samples, "\n");
  fflush(samples);
}

static void
annotate_callback ()
{
  pad_area_set_annotate (pad_area, !pad_area->annotate);
}

void
pad_area_changed_callback (PadArea *area)
{
  update_sensitivity ();
}

static void
update_path_sensitive (const gchar *path,
		       gboolean     sensitive)
{
  GtkWidget *widget = gtk_item_factory_get_widget (factory, path);
  gtk_widget_set_sensitive (widget, sensitive);
}

static void
update_sensitivity ()
{
  gboolean have_selected = (kselected.d[0] || kselected.d[1]);
  gboolean have_strokes = (pad_area->strokes != NULL);

  update_path_sensitive ("/Edit/Copy", have_selected);
  update_path_sensitive ("/Character/Lookup", have_strokes);
  gtk_widget_set_sensitive (lookup_button, have_strokes);
  update_path_sensitive ("/Character/Clear", have_strokes);
  gtk_widget_set_sensitive (clear_button, have_strokes);
  update_path_sensitive ("/Character/Save", have_strokes);
}

#define BUFLEN 256

static gboolean
engine_input_handler (GIOChannel *source, GIOCondition condition, gpointer data)
{
  static gchar *p;
  static gchar *line;
  GError *err = NULL;
  GIOStatus status;
  int i;

  status = g_io_channel_read_line (from_engine, &line, NULL, NULL, &err);
  switch (status)
    {
    case G_IO_STATUS_ERROR:
      g_printerr ("Error reading from engine: %s\n", err->message);
      exit(1);
      break;
    case G_IO_STATUS_NORMAL:
      break;
    case G_IO_STATUS_EOF:
      g_printerr ("Engine no longer exists");
      exit (1);
      break;
    case G_IO_STATUS_AGAIN:
      g_assert_not_reached ();
      break;
    }

  if (line[0] == 'K')
    {
      unsigned int t1, t2;
      p = line+1;
      for (i=0; i<MAX_GUESSES; i++)
	{
	  while (*p && isspace(*p)) p++;
	  if (!*p || sscanf(p, "%2x%2x", &t1, &t2) != 2)
	    {
	      i--;
	      break;
	    }
	  kanjiguess[i].d[0] = t1;
	  kanjiguess[i].d[1] = t2;
	  while (*p && !isspace(*p)) p++;
	}
      num_guesses = i+1;
      karea_draw(karea);
    }

  g_free (line);

  return TRUE;
}

/* Open the connection to the engine */
static void 
init_engine()
{
  gchar *argv[] = { BINDIR G_DIR_SEPARATOR_S "kpengine", "--data-file", NULL, NULL };
  GError *err = NULL;
  gchar *uninstalled;
  int stdin_fd, stdout_fd;

  uninstalled = g_build_filename (".", "kpengine", NULL);
  if (g_file_test (uninstalled, G_FILE_TEST_EXISTS))
    argv[0] = uninstalled;

  if (data_file)
    argv[2] = data_file;
  else
    argv[1] = NULL;

  if (!g_spawn_async_with_pipes (NULL, /* working directory */
				 argv, NULL,	/* argv, envp */
				 0,
				 NULL, NULL,	/* child_setup */
				 &engine_pid,   /* child pid */
				 &stdin_fd, &stdout_fd, NULL,
				 &err))
    {
      GtkWidget *dialog;

      dialog = gtk_message_dialog_new (NULL, 0,
				       GTK_MESSAGE_ERROR,
				       GTK_BUTTONS_OK,
				       "Could not start engine '%s': %s",
				       argv[0], err->message);
      gtk_dialog_run (GTK_DIALOG (dialog));
      g_error_free (err);
      exit (1);
    }

  g_free (uninstalled);
  
  if (!(to_engine = g_io_channel_unix_new (stdin_fd)))
    g_error ("Couldn't create pipe to child process: %s", g_strerror(errno));
  if (!(from_engine = g_io_channel_unix_new (stdout_fd)))
    g_error ("Couldn't create pipe from child process: %s", g_strerror(errno));

  g_io_add_watch (from_engine, G_IO_IN, engine_input_handler, NULL);
}

  
/* Create Interface */

void
usage ()
{
  fprintf(stderr, "Usage: %s [-f/--data-file FILE]\n", progname);
  exit (1);
}

int 
main (int argc, char **argv)
{
  GtkWidget *window;
  GtkWidget *main_hbox;
  GtkWidget *vseparator;
  GtkWidget *button;
  GtkWidget *main_vbox;
  GtkWidget *menubar;
  GtkWidget *vbox;
  GtkWidget *label;
  
  GtkAccelGroup *accel_group;

  PangoFontDescription *font_desc;
  int i;
  char *p;

  p = progname = argv[0];
  while (*p)
    {
      if (*p == '/') progname = p+1;
      p++;
    }

  gtk_init (&argc, &argv);

  for (i=1; i<argc; i++)
    {
      if (!strcmp(argv[i], "--data-file") ||
	  !strcmp(argv[i], "-f"))
	{
	  i++;
	  if (i < argc)
	    data_file = argv[i];
	  else
	    usage();
	}
      else
	{
	  usage();
	}
    }

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_resizable (GTK_WINDOW (window), TRUE);
  gtk_window_set_default_size (GTK_WINDOW (window), 350, 350);

  g_signal_connect (window, "destroy",
		    G_CALLBACK (exit_callback), NULL);

  gtk_window_set_title (GTK_WINDOW(window), "KanjiPad");
  
  main_vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (window), main_vbox);
  gtk_widget_show (main_vbox);

  /* Menu */

  accel_group = gtk_accel_group_new ();
  factory = gtk_item_factory_new (GTK_TYPE_MENU_BAR, "<main>", accel_group);
  gtk_item_factory_create_items (factory, nmenu_items, menu_items, NULL);

  /* create a menubar */
  menubar = gtk_item_factory_get_widget (factory, "<main>");
  gtk_box_pack_start (GTK_BOX (main_vbox), menubar,
		      FALSE, TRUE, 0);
  gtk_widget_show (menubar);

  /*  Install the accelerator table in the main window  */
  gtk_window_add_accel_group (GTK_WINDOW (window), accel_group);

  main_hbox = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX(main_vbox), main_hbox, TRUE, TRUE, 0);
  gtk_widget_show (main_hbox);

  /* Area for user to draw characters in */

  pad_area = pad_area_create ();

  gtk_box_pack_start (GTK_BOX (main_hbox), pad_area->widget, TRUE, TRUE, 0);
  gtk_widget_show (pad_area->widget);

  vseparator = gtk_vseparator_new();
  gtk_box_pack_start (GTK_BOX (main_hbox), vseparator, FALSE, FALSE, 0);
  gtk_widget_show (vseparator);
  
  /* Area in which to draw guesses */

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (main_hbox), vbox, FALSE, FALSE, 0);
  gtk_widget_show (vbox);

  karea = gtk_drawing_area_new();

  g_signal_connect (karea, "configure_event",
		    G_CALLBACK (karea_configure_event), NULL);
  g_signal_connect (karea, "expose_event",
		    G_CALLBACK (karea_expose_event), NULL);
  g_signal_connect (karea, "button_press_event",
		    G_CALLBACK (karea_button_press_event), NULL);

  gtk_widget_set_events (karea, GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK);

#ifdef G_OS_WIN32
  font_desc = pango_font_description_from_string ("MS Gothic 18");
#else
  font_desc = pango_font_description_from_string ("Sans 18");
#endif  
  gtk_widget_modify_font (karea, font_desc);
  
  gtk_box_pack_start (GTK_BOX (vbox), karea, TRUE, TRUE, 0);
  gtk_widget_show (karea);

  /* Buttons */
  label = gtk_label_new ("\xe5\xbc\x95");
  /* We have to set the alignment here, since GTK+ will fail
   * to get the width of the string appropriately...
   */
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_widget_modify_font (label, font_desc);
  gtk_widget_show (label);
  
  lookup_button = button = gtk_button_new ();
  gtk_container_add (GTK_CONTAINER (button), label);
  g_signal_connect (button, "clicked",
		    G_CALLBACK (look_up_callback), NULL);

  gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, 0);
  gtk_widget_show (button);
  
  label = gtk_label_new ("\xe6\xb6\x88");
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_widget_modify_font (label, font_desc);
  gtk_widget_show (label);

  clear_button = button = gtk_button_new ();
  gtk_container_add (GTK_CONTAINER (button), label);
  g_signal_connect (button, "clicked",
		    G_CALLBACK (clear_callback), NULL);

  gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, 0);
  gtk_widget_show (button);

  gtk_widget_show(window);

  pango_font_description_free (font_desc);

  init_engine();

  gtk_main();

  return 0;
}
