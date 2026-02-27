#include <gtk/gtk.h>
#include <cairo/cairo.h>
#include <libxfce4panel/libxfce4panel.h>
#include <libxfce4ui/libxfce4ui.h>
#include <libxfce4util/libxfce4util.h>
#include <xfconf/xfconf.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PLUGIN_NAME "gpugraph"
#define UPDATE_INTERVAL 1000  // ms
#define HISTORY_SIZE 128
#define MAX_GPUS 2

typedef struct {
    XfcePanelPlugin *plugin;
    GtkWidget *ebox;
    GtkWidget *draw_area;
    GtkWidget *tooltip_text;
    XfconfChannel *channel;

    // History
    gfloat history[MAX_GPUS][HISTORY_SIZE];
    gint history_index;
    gint history_count;

    // Current usage
    gfloat current_usage[MAX_GPUS];

    // Timer
    guint timeout_id;
    guint update_interval;

    // Size
    guint size;
    guint width;

    // Color and enabled for each GPU
    gdouble color_r[MAX_GPUS], color_g[MAX_GPUS], color_b[MAX_GPUS];
    gboolean enabled[MAX_GPUS];
} GPUGraph;

static void get_gpu_usage(gfloat *usages, gint max_gpus) {
    FILE *fp = popen("nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>/dev/null", "r");
    if (!fp) {
        for (gint i = 0; i < max_gpus; i++) usages[i] = -1.0;
        return;
    }
    char buffer[256];
    gint gpu_count = 0;
    while (fgets(buffer, sizeof(buffer), fp) != NULL && gpu_count < max_gpus) {
        usages[gpu_count] = atoi(buffer) / 100.0;
        gpu_count++;
    }
    for (gint i = gpu_count; i < max_gpus; i++) usages[i] = -1.0;
    pclose(fp);
}

static void update_history(GPUGraph *gpugraph) {
    get_gpu_usage(gpugraph->current_usage, MAX_GPUS);
    for (gint gpu = 0; gpu < MAX_GPUS; gpu++) {
        if (gpugraph->current_usage[gpu] >= 0.0) {
            gpugraph->history[gpu][gpugraph->history_index] = gpugraph->current_usage[gpu];
        }
    }
    gpugraph->history_index = (gpugraph->history_index + 1) % HISTORY_SIZE;
    if (gpugraph->history_count < HISTORY_SIZE) {
        gpugraph->history_count++;
    }
    gtk_widget_queue_draw(gpugraph->draw_area);
}

static gboolean update_timeout(gpointer data) {
    GPUGraph *gpugraph = (GPUGraph *)data;
    update_history(gpugraph);
    return TRUE;
}

static gboolean draw_graph(GtkWidget *widget, cairo_t *cr, GPUGraph *gpugraph) {
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    gint width = allocation.width;
    gint height = allocation.height;

    // Clear background to black
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_paint(cr);

    if (gpugraph->history_count == 0) return FALSE;

    // Count enabled GPUs
    gint enabled_count = 0;
    for (gint gpu = 0; gpu < MAX_GPUS; gpu++) {
        if (gpugraph->enabled[gpu]) enabled_count++;
    }
    if (enabled_count == 0) return FALSE;

    gfloat gpu_width = (gfloat)width / enabled_count;

    gint start = gpugraph->history_index - gpugraph->history_count;
    if (start < 0) start += HISTORY_SIZE;

    gint gpu_idx = 0;
    for (gint gpu = 0; gpu < MAX_GPUS; gpu++) {
        if (gpugraph->enabled[gpu]) {
            cairo_set_source_rgb(cr, gpugraph->color_r[gpu], gpugraph->color_g[gpu], gpugraph->color_b[gpu]);
            gfloat x_offset = gpu_idx * gpu_width;
            gfloat bar_width = gpu_width / gpugraph->history_count;
            gint local_start = start;
            for (gint i = 0; i < gpugraph->history_count; i++) {
                gfloat x = x_offset + i * bar_width;
                gfloat bar_height = gpugraph->history[gpu][local_start] * height;
                cairo_rectangle(cr, x, height - bar_height, bar_width, bar_height);
                cairo_fill(cr);
                local_start = (local_start + 1) % HISTORY_SIZE;
            }
            gpu_idx++;
        }
    }

    return FALSE;
}

static gboolean query_tooltip_cb(GtkWidget *widget, gint x, gint y, gboolean keyboard_mode, GtkTooltip *tooltip, GPUGraph *gpugraph) {
    GString *text = g_string_new("");
    for (gint gpu = 0; gpu < MAX_GPUS; gpu++) {
        if (gpugraph->enabled[gpu]) {
            if (text->len > 0) g_string_append(text, "\n");
            g_string_append_printf(text, "GPU %d: %.1f%%", gpu, gpugraph->current_usage[gpu] * 100.0);
        }
    }
    gtk_tooltip_set_text(tooltip, text->str);
    g_string_free(text, TRUE);
    return TRUE;
}

static gboolean button_press_cb (GtkWidget *widget, GdkEventButton *event, GPUGraph *gpugraph) {
    if (event->button == 3) { // right-click
        // Let the panel handle the right-click menu
        return FALSE;
    }
    return FALSE;
}

static void construct_gui(GPUGraph *gpugraph) {
    gpugraph->ebox = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(gpugraph->ebox), FALSE);
    gtk_container_add(GTK_CONTAINER(gpugraph->plugin), gpugraph->ebox);

    gpugraph->draw_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(gpugraph->draw_area, gpugraph->width, gpugraph->size);
    gtk_container_add(GTK_CONTAINER(gpugraph->ebox), gpugraph->draw_area);

    g_signal_connect(gpugraph->draw_area, "draw", G_CALLBACK(draw_graph), gpugraph);

    gtk_widget_set_has_tooltip(gpugraph->ebox, TRUE);
    g_signal_connect (gpugraph->ebox, "query-tooltip", G_CALLBACK (query_tooltip_cb), gpugraph);
    g_signal_connect (gpugraph->ebox, "button-press-event", G_CALLBACK (button_press_cb), gpugraph);

    gtk_widget_show_all(gpugraph->ebox);
}

static void gpugraph_free(XfcePanelPlugin *plugin, GPUGraph *gpugraph) {
    if (gpugraph->timeout_id) {
        g_source_remove(gpugraph->timeout_id);
    }
    g_free(gpugraph);
}

static void configure_cb (XfcePanelPlugin *plugin, GPUGraph *gpugraph) {
    GtkWidget *dialog, *content_area;
    GtkWidget *vbox, *hbox;
    GtkWidget *label, *spin, *color_buttons[MAX_GPUS], *check_buttons[MAX_GPUS];
    GtkWidget *button_box, *close_button;

    dialog = gtk_dialog_new_with_buttons ("GPU Graph Properties",
                                         NULL,
                                         GTK_DIALOG_MODAL,
                                         "_Close",
                                         GTK_RESPONSE_CLOSE,
                                         NULL);

    content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
    vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_add (GTK_CONTAINER (content_area), vbox);
    gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);

    // Size
    hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

    label = gtk_label_new ("Height (pixels):");
    gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

    spin = gtk_spin_button_new_with_range (50, 500, 10);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin), gpugraph->size);
    gtk_box_pack_start (GTK_BOX (hbox), spin, FALSE, FALSE, 0);

    // Width
    hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

    label = gtk_label_new ("Width (pixels):");
    gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

    GtkWidget *width_spin = gtk_spin_button_new_with_range (50, 500, 10);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (width_spin), gpugraph->width);
    gtk_box_pack_start (GTK_BOX (hbox), width_spin, FALSE, FALSE, 0);

    // Update interval
    hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

    label = gtk_label_new ("Update interval (ms):");
    gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

    GtkWidget *interval_spin = gtk_spin_button_new_with_range (500, 10000, 500);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (interval_spin), gpugraph->update_interval);
    gtk_box_pack_start (GTK_BOX (hbox), interval_spin, FALSE, FALSE, 0);

    // GPU settings
    for (gint gpu = 0; gpu < MAX_GPUS; gpu++) {
        // Separator
        GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_box_pack_start (GTK_BOX (vbox), sep, FALSE, FALSE, 6);

        // Enable checkbox
        hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

        check_buttons[gpu] = gtk_check_button_new_with_label (g_strdup_printf("Enable GPU %d", gpu));
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_buttons[gpu]), gpugraph->enabled[gpu]);
        gtk_box_pack_start (GTK_BOX (hbox), check_buttons[gpu], FALSE, FALSE, 0);

        // Color
        hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

        label = gtk_label_new (g_strdup_printf("GPU %d color:", gpu));
        gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

        color_buttons[gpu] = gtk_color_button_new ();
        GdkRGBA color = {gpugraph->color_r[gpu], gpugraph->color_g[gpu], gpugraph->color_b[gpu], 1.0};
        gtk_color_button_set_rgba (GTK_COLOR_BUTTON (color_buttons[gpu]), &color);
        gtk_box_pack_start (GTK_BOX (hbox), color_buttons[gpu], FALSE, FALSE, 0);
    }

    gtk_widget_show_all (dialog);
    gtk_dialog_run (GTK_DIALOG (dialog));

    // Update values
    gpugraph->size = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (spin));
    gpugraph->width = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (width_spin));
    gpugraph->update_interval = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (interval_spin));
    for (gint gpu = 0; gpu < MAX_GPUS; gpu++) {
        gpugraph->enabled[gpu] = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_buttons[gpu]));
        GdkRGBA new_color;
        gtk_color_button_get_rgba (GTK_COLOR_BUTTON (color_buttons[gpu]), &new_color);
        gpugraph->color_r[gpu] = new_color.red;
        gpugraph->color_g[gpu] = new_color.green;
        gpugraph->color_b[gpu] = new_color.blue;
    }

    // Update size - removed to prevent crash
    // xfce_panel_plugin_set_size (plugin, gpugraph->size);

    gtk_widget_set_size_request(gpugraph->draw_area, gpugraph->width, gpugraph->size);

    // Restart timer with new interval
    if (gpugraph->timeout_id) {
        g_source_remove (gpugraph->timeout_id);
    }
    gpugraph->timeout_id = g_timeout_add (gpugraph->update_interval, update_timeout, gpugraph);

    // Redraw
    gtk_widget_queue_draw (gpugraph->draw_area);

    gtk_widget_destroy (dialog);
}

static void size_cb (XfcePanelPlugin *plugin, guint size, GPUGraph *gpugraph) {
    gpugraph->size = size;
    gtk_widget_set_size_request(gpugraph->draw_area, gpugraph->width, gpugraph->size);
    gtk_widget_queue_draw (gpugraph->draw_area);
}

void gpugraph_construct(XfcePanelPlugin *plugin) {
    GPUGraph *gpugraph = g_new0(GPUGraph, 1);
    gpugraph->plugin = plugin;
    for (gint i = 0; i < MAX_GPUS; i++) {
        gpugraph->current_usage[i] = -1.0;
    }
    gpugraph->history_index = 0;
    gpugraph->history_count = 0;
    gpugraph->size = 64;  // Default height
    gpugraph->width = 128;  // Default width
    gpugraph->update_interval = UPDATE_INTERVAL;
    // Default: GPU 0 enabled, green; GPU 1 disabled, red
    gpugraph->enabled[0] = TRUE;
    gpugraph->color_r[0] = 0.0;
    gpugraph->color_g[0] = 1.0;
    gpugraph->color_b[0] = 0.0;
    gpugraph->enabled[1] = FALSE;
    gpugraph->color_r[1] = 1.0;
    gpugraph->color_g[1] = 0.0;
    gpugraph->color_b[1] = 0.0;

    // Set initial size - removed to prevent crash
    // xfce_panel_plugin_set_size (plugin, gpugraph->size);

    construct_gui(gpugraph);

    // Start timer
    gpugraph->timeout_id = g_timeout_add(gpugraph->update_interval, update_timeout, gpugraph);

    // Initial update - commented out to avoid potential crash on load
    // update_history(gpugraph);

    g_object_set_data(G_OBJECT(plugin), "gpugraph", gpugraph);

    xfce_panel_plugin_menu_show_about (plugin);
    xfce_panel_plugin_menu_show_configure (plugin);

    g_signal_connect (plugin, "configure-plugin", G_CALLBACK (configure_cb), gpugraph);
    g_signal_connect (plugin, "size-changed", G_CALLBACK (size_cb), gpugraph);
}