#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>

#define MAX_TARGETS 5
#define HISTORY_SIZE 120
#define DEFAULT_INTERVAL_SEC 3
#define PING_TIMEOUT_SEC 1

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

static volatile sig_atomic_t g_running = 1;

static void handle_signal(int signum) {
    (void)signum;
    g_running = 0;
}

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

    if (hostbuf[0] == '[') {
        size_t n = strlen(hostbuf);
        if (n > 2 && hostbuf[n - 1] == ']') {
            memmove(hostbuf, hostbuf + 1, n - 2);
            hostbuf[n - 2] = '\0';
        }
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

    FILE *pipe = popen(cmd, "r");
    if (pipe == NULL) {
        *has_latency = false;
        return false;
    }

    char line[1024];
    bool found_latency = false;
    double parsed_latency = 0.0;

    while (fgets(line, sizeof(line), pipe) != NULL) {
        char *token = strstr(line, "time=");
        if (token == NULL) {
            token = strstr(line, "time<");
            if (token != NULL) {
                token += 5;
            }
        } else {
            token += 5;
        }

        if (token != NULL) {
            char *endptr = NULL;
            double value = strtod(token, &endptr);
            if (endptr != token) {
                parsed_latency = value;
                found_latency = true;
            }
        }
    }

    int status = pclose(pipe);
    *has_latency = found_latency;
    if (found_latency) {
        *latency_ms = parsed_latency;
    }

    if (status == -1) {
        return false;
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return true;
    }

    return false;
}

static void print_cycle(Target *targets, int target_count, int cycle_num) {
    int healthy = 0;
    int critical = 0;

    printf("\n--- NetPulse C Monitor | Cycle %d ---\n", cycle_num);
    printf("%-30s %-7s %-10s %-10s %-10s\n", "Target", "Status", "Latency", "Avg60s", "Uptime60s");
    printf("--------------------------------------------------------------------------------\n");

    for (int i = 0; i < target_count; ++i) {
        Target *target = &targets[i];
        if (strcmp(target->status, "GREEN") == 0) {
            healthy++;
        }
        if (strcmp(target->status, "AMBER") == 0 || strcmp(target->status, "RED") == 0) {
            critical++;
        }

        char latency_text[32];
        char avg_text[32];
        char uptime_text[32];
        compute_stats(target, latency_text, sizeof(latency_text), avg_text, sizeof(avg_text), uptime_text,
                      sizeof(uptime_text));

        printf("%-30s %-7s %-10s %-10s %-10s\n", target->display, target->status, latency_text, avg_text, uptime_text);
    }

    printf("--------------------------------------------------------------------------------\n");
    printf("Targets: %d | Healthy: %d | Critical: %d\n", target_count, healthy, critical);
    fflush(stdout);
}

static int load_targets_from_file(const char *path, Target *targets, int *target_count) {
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return -1;
    }

    char line[512];
    while (fgets(line, sizeof(line), f) != NULL && *target_count < MAX_TARGETS) {
        trim(line);
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        char display[256];
        char host[256];
        if (!normalize_target(line, display, sizeof(display), host, sizeof(host))) {
            continue;
        }

        bool duplicate = false;
        for (int i = 0; i < *target_count; ++i) {
            if (strcasecmp(targets[i].host, host) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }

        Target *t = &targets[*target_count];
        memset(t, 0, sizeof(*t));
        snprintf(t->display, sizeof(t->display), "%s", display);
        snprintf(t->host, sizeof(t->host), "%s", host);
        snprintf(t->status, sizeof(t->status), "OFF");
        (*target_count)++;
    }

    fclose(f);
    return 0;
}

static int save_targets_to_file(const char *path, const Target *targets, int target_count) {
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        return -1;
    }

    for (int i = 0; i < target_count; ++i) {
        fprintf(f, "%s\n", targets[i].display);
    }

    fclose(f);
    return 0;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [options] [target1 target2 ...]\n"
            "Options:\n"
            "  -i <seconds>    Ping interval in seconds (default: %d)\n"
            "  -f <file>       Load targets (one per line) from file\n"
            "  -s <file>       Save normalized targets to file and exit\n"
            "  -h              Show this help\n"
            "\n"
            "Examples:\n"
            "  %s github.com 1.1.1.1\n"
            "  %s -i 5 -f targets.txt\n",
            prog, DEFAULT_INTERVAL_SEC, prog, prog);
}

int main(int argc, char **argv) {
    Target targets[MAX_TARGETS];
    memset(targets, 0, sizeof(targets));
    int target_count = 0;
    int interval = DEFAULT_INTERVAL_SEC;

    const char *input_file = NULL;
    const char *save_file = NULL;

    int opt;
    while ((opt = getopt(argc, argv, "hi:f:s:")) != -1) {
        switch (opt) {
        case 'h':
            print_usage(argv[0]);
            return 0;
        case 'i':
            interval = atoi(optarg);
            if (interval <= 0) {
                fprintf(stderr, "Invalid interval: %s\n", optarg);
                return 1;
            }
            break;
        case 'f':
            input_file = optarg;
            break;
        case 's':
            save_file = optarg;
            break;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    if (input_file != NULL) {
        if (load_targets_from_file(input_file, targets, &target_count) != 0) {
            fprintf(stderr, "Failed to load targets from %s\n", input_file);
            return 1;
        }
    }

    for (int i = optind; i < argc && target_count < MAX_TARGETS; ++i) {
        char display[256];
        char host[256];
        if (!normalize_target(argv[i], display, sizeof(display), host, sizeof(host))) {
            fprintf(stderr, "Skipping invalid target: %s\n", argv[i]);
            continue;
        }

        bool duplicate = false;
        for (int j = 0; j < target_count; ++j) {
            if (strcasecmp(targets[j].host, host) == 0) {
                duplicate = true;
                break;
            }
        }

        if (duplicate) {
            fprintf(stderr, "Skipping duplicate target: %s\n", argv[i]);
            continue;
        }

        Target *t = &targets[target_count];
        memset(t, 0, sizeof(*t));
        snprintf(t->display, sizeof(t->display), "%s", display);
        snprintf(t->host, sizeof(t->host), "%s", host);
        snprintf(t->status, sizeof(t->status), "OFF");
        target_count++;
    }

    if (target_count == 0) {
        fprintf(stderr, "No targets to monitor.\n\n");
        print_usage(argv[0]);
        return 1;
    }

    if (save_file != NULL) {
        if (save_targets_to_file(save_file, targets, target_count) != 0) {
            fprintf(stderr, "Failed to save targets to %s\n", save_file);
            return 1;
        }
        printf("Saved %d targets to %s\n", target_count, save_file);
        return 0;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    printf("NetPulse Pro | C Edition\n");
    printf("Monitoring %d target(s) every %d second(s). Press Ctrl+C to stop.\n", target_count, interval);

    int cycle = 1;
    while (g_running) {
        for (int i = 0; i < target_count; ++i) {
            double latency = 0.0;
            bool has_latency = false;
            bool success = run_ping(targets[i].host, &latency, &has_latency);
            add_history(&targets[i], success, latency, has_latency);
            compute_status(&targets[i]);
        }

        print_cycle(targets, target_count, cycle++);

        for (int i = 0; i < interval && g_running; ++i) {
            sleep(1);
        }
    }

    printf("\nMonitoring stopped.\n");
    return 0;
}
