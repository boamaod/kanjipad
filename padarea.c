#include "kanjipad.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void
pad_area_free_stroke (GList *stroke)
{
  GList *tmp_list = stroke;
  while (tmp_list)
    {
      g_free (tmp_list->data);
      tmp_list = tmp_list->next;
    }
  g_list_free (stroke);
}


static void
pad_area_annotate_stroke (PadArea *area, GList *stroke, gint index)
{
  GdkPoint *cur, *old;

  /* Annotate the stroke with the stroke number - the algorithm
   * for placing the digit is pretty simple. The text is inscribed
   * in a circle tangent to the stroke. The circle will be above
   * and/or to the left of the line */
  if (stroke)
    {
      old = (GdkPoint *)stroke->data;
      
      do
	{
	  cur = (GdkPoint *)stroke->data;
	  stroke = stroke->next;
	}
      while (stroke && abs(cur->x - old->x) < 5 && abs (cur->y - old->y) < 5);
      
      if (stroke)
	{
	  char buffer[16];
	  PangoLayout *layout;
	  int swidth, sheight;
	  gint16 x, y;
	  double r;
	  double dx = cur->x - old->x;
	  double dy = cur->y - old->y;
	  double dl = sqrt(dx*dx+dy*dy);
	  int sign = (dy <= dx) ? 1 : -1;
	  GdkRectangle update_area;

	  sprintf (buffer, "%d", index);
	  layout = gtk_widget_create_pango_layout (area->widget, buffer);
	  pango_layout_get_pixel_size (layout, &swidth, &sheight);

	  r = sqrt(swidth*swidth + sheight*sheight);
	  
	  x = 0.5 + old->x + 0.5*r*dx/dl + sign * 0.5*r*dy/dl;
	  y = 0.5 + old->y + 0.5*r*dy/dl - sign * 0.5*r*dx/dl;
	  
	  x -= swidth/2;
	  y -= sheight/2;

	  update_area.x = x;
	  update_area.y = y;
	  update_area.width = swidth;
	  update_area.height = sheight;
	  
	  x = CLAMP (x, 0, area->widget->allocation.width - swidth);
	  y = CLAMP (y, 0, area->widget->allocation.height - sheight);

	  gdk_draw_layout (area->pixmap, 
			   area->widget->style->black_gc,
			   x, y, layout);

	  g_object_unref (layout);

	  gdk_window_invalidate_rect (area->widget->window, &update_area, FALSE);
	}
    }
}

static void 
pad_area_init (PadArea *area)
{
  GList *tmp_list;
  int index = 1;
  
  guint16 width = area->widget->allocation.width;
  guint16 height = area->widget->allocation.height;

  gdk_draw_rectangle (area->pixmap, 
		      area->widget->style->white_gc, TRUE,
		      0, 0, width, height);

  tmp_list = area->strokes;
  while (tmp_list)
    {
      GdkPoint *cur, *old;
      GList *stroke_list = tmp_list->data;

      old = NULL;

      if (area->annotate)
	pad_area_annotate_stroke (area, stroke_list, index);

      while (stroke_list)
	{
	  cur = (GdkPoint *)stroke_list->data;
	  if (old)
	    gdk_draw_line (area->pixmap, 
			   area->widget->style->black_gc,
			   old->x, old->y, cur->x, cur->y);

	  old = cur;
	  stroke_list = stroke_list->next;
	}
      
      tmp_list = tmp_list->next;
      index++;
    }

  gtk_widget_queue_draw (area->widget);
  
}

static int
pad_area_configure_event (GtkWidget *w, GdkEventConfigure *event,
			  PadArea *area)
{
  if (area->pixmap)
    g_object_unref (area->pixmap);

  area->pixmap = gdk_pixmap_new (w->window, event->width, event->height, -1);

  pad_area_init (area);
  
  return TRUE;
}

static int
pad_area_expose_event (GtkWidget *w, GdkEventExpose *event, PadArea *area)
{
  if (!area->pixmap)
    return 0;

  gdk_draw_drawable (w->window,
		     w->style->fg_gc[GTK_STATE_NORMAL], area->pixmap,
		     event->area.x, event->area.y,
		     event->area.x, event->area.y,
		     event->area.width, event->area.height);

  return TRUE;
}

static int
pad_area_button_press_event (GtkWidget *w, GdkEventButton *event, PadArea *area)
{
  if (event->button == 1)
    {
      GdkPoint *p = g_new (GdkPoint, 1);
      p->x = event->x;
      p->y = event->y;
      area->curstroke = g_list_append (area->curstroke, p);
      area->instroke = TRUE;
    }

  return TRUE;
}

static int
pad_area_button_release_event (GtkWidget *w, GdkEventButton *event, PadArea *area)
{
  if (area->annotate)
    pad_area_annotate_stroke (area, area->curstroke, g_list_length (area->strokes) + 1);

  area->strokes = g_list_append (area->strokes, area->curstroke);
  area->curstroke = NULL;
  area->instroke = FALSE;

  pad_area_changed_callback (area);

  return TRUE;
}

static int
pad_area_motion_event (GtkWidget *w, GdkEventMotion *event, PadArea *area)
{
  gint x,y;
  GdkModifierType state;

  if (event->is_hint)
    {
      gdk_window_get_pointer (w->window, &x, &y, &state);
    }
  else
    {
      x = event->x;
      y = event->y;
      state = event->state;
    }

  if (area->instroke && state & GDK_BUTTON1_MASK)
    {
      GdkRectangle rect;
      GdkPoint *p;
      int xmin, ymin, xmax, ymax;
      GdkPoint *old = (GdkPoint *)g_list_last (area->curstroke)->data;

      gdk_draw_line (area->pixmap, w->style->black_gc,
		     old->x, old->y, x, y);

      if (old->x < x) { xmin = old->x; xmax = x; }
      else            { xmin = x;      xmax = old->x; }

      if (old->y < y) { ymin = old->y; ymax = y; }
      else            { ymin = y;      ymax = old->y; }

      rect.x = xmin - 1; 
      rect.y = ymin = 1;
      rect.width  = xmax - xmin + 2;
      rect.height = ymax - ymin + 2;
      gdk_window_invalidate_rect (w->window, &rect, FALSE);

      p = g_new (GdkPoint, 1);
      p->x = x;
      p->y = y;
      area->curstroke = g_list_append (area->curstroke, p);
    }

  return TRUE;
}

PadArea *pad_area_create ()
{
  PadArea *area = g_new (PadArea, 1);
  
  area->widget = gtk_drawing_area_new();
  gtk_widget_set_size_request (area->widget, 100, 100);

  g_signal_connect (area->widget, "configure_event",
		    G_CALLBACK (pad_area_configure_event), area);
  g_signal_connect (area->widget, "expose_event",
		    G_CALLBACK (pad_area_expose_event), area);
  g_signal_connect (area->widget, "button_press_event",
		    G_CALLBACK (pad_area_button_press_event), area);
  g_signal_connect (area->widget, "button_release_event",
		    G_CALLBACK (pad_area_button_release_event), area);
  g_signal_connect (area->widget, "motion_notify_event",
		    G_CALLBACK (pad_area_motion_event), area);

  gtk_widget_set_events (area->widget, 
			 GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK 
			 | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK 
			 | GDK_POINTER_MOTION_HINT_MASK);

  area->strokes = NULL;
  area->curstroke = NULL;
  area->instroke = FALSE;
  area->annotate = FALSE;
  area->pixmap = NULL;

  return area;
}

void pad_area_clear (PadArea *area)
{
  GList *tmp_list;

  tmp_list = area->strokes;
  while (tmp_list)
    {
      pad_area_free_stroke (tmp_list->data);
      tmp_list = tmp_list->next;
    }
  g_list_free (area->strokes);
  area->strokes = NULL;

#if 0
  tmp_list = thinned;
  while (tmp_list)
    {
      pad_area_free_stroke (tmp_list->data);
      tmp_list = tmp_list->next;
    }
  g_list_free (thinned);
  thinned = NULL;
#endif

  g_list_free (area->curstroke);
  area->curstroke = NULL;

  pad_area_init (area);

  pad_area_changed_callback (area);  
}

void pad_area_set_annotate (PadArea *area, gint annotate)
{
  if (area->annotate != annotate)
    {
      area->annotate = annotate;
      pad_area_init (area);
    }
}



