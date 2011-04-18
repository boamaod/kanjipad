#include <gtk/gtk.h>

typedef struct _PadArea PadArea;

struct _PadArea {
  GtkWidget *widget;

  gint annotate;
  GList *strokes;

  /* Private */
  GdkPixmap *pixmap;
  GList *curstroke;
  int instroke;
};

PadArea *pad_area_create ();
void pad_area_clear (PadArea *area);
void pad_area_set_annotate (PadArea *area, gint annotate);

void pad_area_changed_callback (PadArea *area);
