#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <getopt.h>
#include <gtk/gtk.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define MAX_TARGETS 5
#define HISTORY_SIZE 120
#define DEFAULT_INTERVAL_SEC 3
#define PING_TIMEOUT_SEC 1
#define CONFIG_PATH "netpulse_c_config.txt"

typedef struct {
    time_t timestamp;
    bool success;
    double latency_ms;
} HistoryPoint;

typedef struct {
    char display[256];
    char host[256];
    HistoryPoint history[HISTORY_SIZE];
    int history_count;
    int history_start;
    char status[8];
    double last_latency_ms;
    bool has_latency;
} Target;

typedef struct {
    GtkWidget *window;
    GtkEntry *input_entry;
    GtkListStore *store;
    GtkWidget *tree;
    GtkWidget *stats_label;
    GtkTextBuffer *log_buffer;
    GtkToggleButton *auto_start_toggle;

    Target targets[MAX_TARGETS];
    int target_count;
    int interval_sec;
    guint timer_id;
    bool monitoring;
} AppState;

static void trim(char *s) {
    if (s == NULL) {
        return;
    }

    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }

    size_t start = 0;
    while (s[start] != '\0' && isspace((unsigned char)s[start])) {
        start++;
    }

    if (start > 0) {
        memmove(s, s + start, strlen(s + start) + 1);
    }
}

static bool normalize_target(const char *raw, char *display, size_t display_size, char *host, size_t host_size) {
    if (raw == NULL || display == NULL || host == NULL) {
        return false;
    }

    char temp[256];
    snprintf(temp, sizeof(temp), "%s", raw);
    trim(temp);
    if (temp[0] == '\0') {
        return false;
    }

    snprintf(display, display_size, "%s", temp);

    const char *work = temp;
    const char *scheme = strstr(work, "://");
    if (scheme != NULL) {
        work = scheme + 3;
    }

    char hostbuf[256];
    size_t i = 0;
    while (work[i] != '\0' && work[i] != '/' && work[i] != '?' && work[i] != '#') {
        if (i + 1 >= sizeof(hostbuf)) {
            break;
        }
        hostbuf[i] = work[i];
        i++;
    }
    hostbuf[i] = '\0';

    if (hostbuf[0] == '\0') {
        return false;
    }

    char *at = strrchr(hostbuf, '@');
    char *host_start = at ? at + 1 : hostbuf;
    if (host_start[0] == '\0') {
        return false;
    }

    if (host_start[0] == '[') {
        size_t n = strlen(host_start);
        if (n > 2 && host_start[n - 1] == ']') {
            memmove(host_start, host_start + 1, n - 2);
            host_start[n - 2] = '\0';
        }
    }

    char *colon = strrchr(host_start, ':');
    if (colon != NULL && strchr(colon + 1, ':') == NULL) {
        bool numeric_port = true;
        for (char *p = colon + 1; *p != '\0'; ++p) {
            if (!isdigit((unsigned char)*p)) {
                numeric_port = false;
                break;
            }
        }
        if (numeric_port) {
            *colon = '\0';
        }
    }

    trim(host_start);
    if (host_start[0] == '\0') {
        return false;
    }

    snprintf(host, host_size, "%s", host_start);
    return true;
}

static void log_message(AppState *app, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char msg[512];
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%H:%M:%S", tm_now);

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(app->log_buffer, &end);
    char line[640];
    snprintf(line, sizeof(line), "[%s] %s\n", ts, msg);
    gtk_text_buffer_insert(app->log_buffer, &end, line, -1);
}

static void add_history(Target *target, bool success, double latency_ms, bool has_latency) {
    int index;
    if (target->history_count < HISTORY_SIZE) {
        index = (target->history_start + target->history_count) % HISTORY_SIZE;
        target->history_count++;
    } else {
        index = target->history_start;
        target->history_start = (target->history_start + 1) % HISTORY_SIZE;
    }

    target->history[index].timestamp = time(NULL);
    target->history[index].success = success;
    target->history[index].latency_ms = has_latency ? latency_ms : -1.0;
    target->has_latency = has_latency;
    target->last_latency_ms = latency_ms;
}

static void compute_status(Target *target) {
    time_t now = time(NULL);
    int drops30 = 0;
    int drops60 = 0;

    for (int i = 0; i < target->history_count; ++i) {
        int idx = (target->history_start + i) % HISTORY_SIZE;
        HistoryPoint p = target->history[idx];
        double age = difftime(now, p.timestamp);
        if (age <= 60 && !p.success) {
            drops60++;
            if (age <= 30) {
                drops30++;
            }
        }
    }

    if (drops60 > 10) {
        snprintf(target->status, sizeof(target->status), "RED");
    } else if (drops30 > 3) {
        snprintf(target->status, sizeof(target->status), "AMBER");
    } else {
        snprintf(target->status, sizeof(target->status), "GREEN");
    }
}

static void compute_stats(const Target *target, char *latency_text, size_t latency_size, char *avg_text, size_t avg_size,
                          char *uptime_text, size_t uptime_size) {
    time_t now = time(NULL);
    int total = 0;
    int successes = 0;
    double latency_sum = 0.0;

    for (int i = 0; i < target->history_count; ++i) {
        int idx = (target->history_start + i) % HISTORY_SIZE;
        HistoryPoint p = target->history[idx];
        if (difftime(now, p.timestamp) <= 60) {
            total++;
            if (p.success) {
                successes++;
                if (p.latency_ms >= 0.0) {
                    latency_sum += p.latency_ms;
                }
            }
        }
    }

    if (target->has_latency) {
        snprintf(latency_text, latency_size, "%.0f ms", target->last_latency_ms);
    } else {
        snprintf(latency_text, latency_size, "--");
    }

    if (successes > 0) {
        snprintf(avg_text, avg_size, "%.0f ms", latency_sum / successes);
    } else {
        snprintf(avg_text, avg_size, "--");
    }

    if (total > 0) {
        snprintf(uptime_text, uptime_size, "%.0f%%", (100.0 * successes) / total);
    } else {
        snprintf(uptime_text, uptime_size, "--");
    }
}

static bool run_ping(const char *host, double *latency_ms, bool *has_latency) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "ping -c 1 -W %d '%s' 2>&1", PING_TIMEOUT_SEC, host);

    gchar *stdout_data = NULL;
    gchar *stderr_data = NULL;
    gint exit_status = 1;
    GError *error = NULL;

    gboolean ok = g_spawn_command_line_sync(cmd, &stdout_data, &stderr_data, &exit_status, &error);
    if (!ok) {
        if (error != NULL) {
            g_error_free(error);
        }
        *has_latency = false;
        g_free(stdout_data);
        g_free(stderr_data);
        return false;
    }

    const char *combined = stdout_data != NULL ? stdout_data : "";
    const char *time_ptr = strstr(combined, "time=");
    if (time_ptr == NULL) {
        time_ptr = strstr(combined, "time<");
        if (time_ptr != NULL) {
            time_ptr += 5;
        }
    } else {
        time_ptr += 5;
    }

    if (time_ptr != NULL) {
        char *endptr = NULL;
        double parsed = g_ascii_strtod(time_ptr, &endptr);
        if (endptr != time_ptr) {
            *latency_ms = parsed;
            *has_latency = true;
        } else {
            *has_latency = false;
        }
    } else {
        *has_latency = false;
    }

    g_free(stdout_data);
    g_free(stderr_data);
    return exit_status == 0;
}

static void refresh_table(AppState *app) {
    gtk_list_store_clear(app->store);

    int healthy = 0;
    int critical = 0;

    for (int i = 0; i < app->target_count; ++i) {
        char latency_text[32];
        char avg_text[32];
        char uptime_text[32];
        compute_stats(&app->targets[i], latency_text, sizeof(latency_text), avg_text, sizeof(avg_text), uptime_text,
                      sizeof(uptime_text));

        if (strcmp(app->targets[i].status, "GREEN") == 0) {
            healthy++;
        }
        if (strcmp(app->targets[i].status, "AMBER") == 0 || strcmp(app->targets[i].status, "RED") == 0) {
            critical++;
        }

        GtkTreeIter iter;
        gtk_list_store_append(app->store, &iter);
        gtk_list_store_set(app->store, &iter, 0, app->targets[i].display, 1, app->targets[i].status, 2, latency_text, 3,
                           avg_text, 4, uptime_text, -1);
    }

    char summary[128];
    snprintf(summary, sizeof(summary), "Targets: %d | Healthy: %d | Critical: %d", app->target_count, healthy, critical);
    gtk_label_set_text(GTK_LABEL(app->stats_label), summary);
}

static bool save_config(AppState *app, const char *path) {
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        return false;
    }

    fprintf(f, "# auto_start=%d\n", gtk_toggle_button_get_active(app->auto_start_toggle) ? 1 : 0);
    for (int i = 0; i < app->target_count; ++i) {
        fprintf(f, "%s\n", app->targets[i].display);
    }

    fclose(f);
    return true;
}

static int append_target(AppState *app, const char *raw_target, bool log_result) {
    if (app->target_count >= MAX_TARGETS) {
        if (log_result) {
            log_message(app, "Target limit reached (%d).", MAX_TARGETS);
        }
        return -1;
    }

    char display[256];
    char host[256];
    if (!normalize_target(raw_target, display, sizeof(display), host, sizeof(host))) {
        if (log_result) {
            log_message(app, "Invalid target: %s", raw_target);
        }
        return -1;
    }

    for (int i = 0; i < app->target_count; ++i) {
        if (strcasecmp(app->targets[i].host, host) == 0) {
            if (log_result) {
                log_message(app, "Skipping duplicate target: %s", display);
            }
            return -1;
        }
    }

    Target *t = &app->targets[app->target_count++];
    memset(t, 0, sizeof(*t));
    snprintf(t->display, sizeof(t->display), "%s", display);
    snprintf(t->host, sizeof(t->host), "%s", host);
    snprintf(t->status, sizeof(t->status), "OFF");

    if (log_result) {
        log_message(app, "Added target: %s", display);
    }
    return 0;
}

static bool load_config(AppState *app, const char *path) {
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return false;
    }

    char line[512];
    while (fgets(line, sizeof(line), f) != NULL) {
        trim(line);
        if (line[0] == '\0') {
            continue;
        }
        if (line[0] == '#') {
            if (strncmp(line, "# auto_start=", 13) == 0) {
                int val = atoi(line + 13);
                gtk_toggle_button_set_active(app->auto_start_toggle, val != 0);
            }
            continue;
        }
        append_target(app, line, false);
    }

    fclose(f);
    return true;
}

static gboolean monitor_tick(gpointer user_data) {
    AppState *app = user_data;
    if (!app->monitoring) {
        return G_SOURCE_REMOVE;
    }

    for (int i = 0; i < app->target_count; ++i) {
        double latency = 0.0;
        bool has_latency = false;
        bool success = run_ping(app->targets[i].host, &latency, &has_latency);
        add_history(&app->targets[i], success, latency, has_latency);
        compute_status(&app->targets[i]);
    }

    refresh_table(app);
    return G_SOURCE_CONTINUE;
}

static void start_monitoring(AppState *app) {
    if (app->monitoring) {
        return;
    }
    if (app->target_count == 0) {
        log_message(app, "Add at least one target before starting monitor.");
        return;
    }

    app->monitoring = true;
    app->timer_id = g_timeout_add_seconds(app->interval_sec, monitor_tick, app);
    monitor_tick(app);
    log_message(app, "Monitoring started (%d second interval).", app->interval_sec);
}

static void stop_monitoring(AppState *app) {
    if (!app->monitoring) {
        return;
    }

    app->monitoring = false;
    if (app->timer_id != 0) {
        g_source_remove(app->timer_id);
        app->timer_id = 0;
    }
    log_message(app, "Monitoring stopped.");
}

static void on_add_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppState *app = user_data;
    const char *text = gtk_entry_get_text(app->input_entry);
    if (append_target(app, text, true) == 0) {
        gtk_entry_set_text(app->input_entry, "");
        refresh_table(app);
    }
}

static void remove_target_at(AppState *app, int idx) {
    if (idx < 0 || idx >= app->target_count) {
        return;
    }
    log_message(app, "Removed target: %s", app->targets[idx].display);
    for (int i = idx; i < app->target_count - 1; ++i) {
        app->targets[i] = app->targets[i + 1];
    }
    app->target_count--;
}

static void on_remove_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppState *app = user_data;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(app->tree));
    GList *rows = gtk_tree_selection_get_selected_rows(selection, NULL);
    if (rows == NULL) {
        log_message(app, "No row selected for removal.");
        return;
    }

    int count = g_list_length(rows);
    int *indices = g_new0(int, count);
    int i = 0;
    for (GList *node = rows; node != NULL; node = node->next) {
        GtkTreePath *path = node->data;
        indices[i++] = gtk_tree_path_get_indices(path)[0];
    }

    for (int j = count - 1; j >= 0; --j) {
        remove_target_at(app, indices[j]);
    }

    g_free(indices);
    g_list_free_full(rows, (GDestroyNotify)gtk_tree_path_free);
    refresh_table(app);
}

static void on_save_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppState *app = user_data;
    if (save_config(app, CONFIG_PATH)) {
        log_message(app, "Configuration saved to %s", CONFIG_PATH);
    } else {
        log_message(app, "Failed to save configuration to %s", CONFIG_PATH);
    }
}

static void on_start_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    start_monitoring((AppState *)user_data);
}

static void on_stop_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    stop_monitoring((AppState *)user_data);
}

static void on_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    AppState *app = user_data;
    stop_monitoring(app);
    gtk_main_quit();
}

static gboolean on_entry_activate(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    on_add_clicked(NULL, user_data);
    return TRUE;
}

static GtkWidget *build_ui(AppState *app) {
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "NetPulse Pro | C Edition");
    gtk_window_set_default_size(GTK_WINDOW(window), 900, 600);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(root), 10);
    gtk_container_add(GTK_CONTAINER(window), root);

    GtkWidget *input_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(root), input_row, FALSE, FALSE, 0);

    app->input_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(app->input_entry, "Hostname, IP, or URL");
    gtk_box_pack_start(GTK_BOX(input_row), GTK_WIDGET(app->input_entry), TRUE, TRUE, 0);

    GtkWidget *add_btn = gtk_button_new_with_label("Add");
    GtkWidget *remove_btn = gtk_button_new_with_label("Remove Selected");
    gtk_box_pack_start(GTK_BOX(input_row), add_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(input_row), remove_btn, FALSE, FALSE, 0);

    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(root), controls, FALSE, FALSE, 0);

    GtkWidget *start_btn = gtk_button_new_with_label("Start Monitoring");
    GtkWidget *stop_btn = gtk_button_new_with_label("Stop Monitoring");
    GtkWidget *save_btn = gtk_button_new_with_label("Save");
    app->auto_start_toggle = GTK_TOGGLE_BUTTON(gtk_check_button_new_with_label("Auto-Start"));
    gtk_box_pack_start(GTK_BOX(controls), start_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(controls), stop_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(controls), save_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(controls), GTK_WIDGET(app->auto_start_toggle), FALSE, FALSE, 0);

    app->store = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    app->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(app->store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(app->tree), TRUE);

    const char *headers[] = {"Target", "Status", "Latency", "Avg60s", "Uptime60s"};
    for (int i = 0; i < 5; ++i) {
        GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(headers[i], renderer, "text", i, NULL);
        gtk_tree_view_append_column(GTK_TREE_VIEW(app->tree), column);
    }

    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(app->tree));
    gtk_tree_selection_set_mode(sel, GTK_SELECTION_MULTIPLE);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_container_add(GTK_CONTAINER(scroll), app->tree);
    gtk_box_pack_start(GTK_BOX(root), scroll, TRUE, TRUE, 0);

    app->stats_label = gtk_label_new("Targets: 0 | Healthy: 0 | Critical: 0");
    gtk_box_pack_start(GTK_BOX(root), app->stats_label, FALSE, FALSE, 0);

    GtkWidget *log_title = gtk_label_new("Activity Log");
    gtk_widget_set_halign(log_title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(root), log_title, FALSE, FALSE, 0);

    GtkWidget *log_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(log_scroll, -1, 150);
    GtkWidget *log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(log_view), FALSE);
    app->log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view));
    gtk_container_add(GTK_CONTAINER(log_scroll), log_view);
    gtk_box_pack_start(GTK_BOX(root), log_scroll, FALSE, TRUE, 0);

    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), app);
    g_signal_connect(add_btn, "clicked", G_CALLBACK(on_add_clicked), app);
    g_signal_connect(remove_btn, "clicked", G_CALLBACK(on_remove_clicked), app);
    g_signal_connect(start_btn, "clicked", G_CALLBACK(on_start_clicked), app);
    g_signal_connect(stop_btn, "clicked", G_CALLBACK(on_stop_clicked), app);
    g_signal_connect(save_btn, "clicked", G_CALLBACK(on_save_clicked), app);
    g_signal_connect(app->input_entry, "activate", G_CALLBACK(on_entry_activate), app);

    return window;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [options] [target1 target2 ...]\n"
            "Options:\n"
            "  -i <seconds>    Ping interval in seconds (default: %d)\n"
            "  -f <file>       Load targets from file (one per line)\n"
            "  -h              Show this help\n",
            prog, DEFAULT_INTERVAL_SEC);
}

int main(int argc, char **argv) {
    AppState app;
    memset(&app, 0, sizeof(app));
    app.interval_sec = DEFAULT_INTERVAL_SEC;

    const char *input_file = NULL;
    int opt;
    while ((opt = getopt(argc, argv, "hi:f:")) != -1) {
        switch (opt) {
        case 'h':
            print_usage(argv[0]);
            return 0;
        case 'i':
            app.interval_sec = atoi(optarg);
            if (app.interval_sec <= 0) {
                fprintf(stderr, "Invalid interval: %s\n", optarg);
                return 1;
            }
            break;
        case 'f':
            input_file = optarg;
            break;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    gtk_init(&argc, &argv);
    app.window = build_ui(&app);

    for (int i = optind; i < argc; ++i) {
        append_target(&app, argv[i], false);
    }
    if (input_file != NULL) {
        load_config(&app, input_file);
        log_message(&app, "Loaded targets from %s", input_file);
    }

    if (load_config(&app, CONFIG_PATH)) {
        log_message(&app, "Loaded saved configuration from %s", CONFIG_PATH);
    }

    refresh_table(&app);

    gtk_widget_show_all(app.window);

    if (gtk_toggle_button_get_active(app.auto_start_toggle) && app.target_count > 0) {
        start_monitoring(&app);
    }

    gtk_main();
    return 0;
}
