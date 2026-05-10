#include <ctype.h>
#include <limits.h>
#include <ncurses.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define BUF_SIZE 4096
#define CMD_BUF_SIZE (BUF_SIZE * 2)
#define MENU_ITEMS 4
#define NO_CHANGES_TOKEN "__NO_CHANGES__"
#define CONFIRM_WIN_HEIGHT 7
#define CONFIRM_WIN_MARGIN_X 4
#define CONFIRM_WIN_OFFSET_Y 9
#define CONFIRM_WIN_OFFSET_X 2
#define RSYNC_OPTS "-avzh --delete --exclude='.git'"

typedef struct {
    char local_dir[PATH_MAX];
    char gh_dir[PATH_MAX];
    char cb_dir[PATH_MAX];
    char branch[64];
} config_t;

static void trim(char *s) {
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
    char *start = s;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }
}

static bool shell_quote(const char *src, char *dst, size_t dst_size) {
    size_t j = 0;
    if (dst_size < 3) {
        return false;
    }
    dst[j++] = '\'';
    for (size_t i = 0; src[i] != '\0'; i++) {
        if (src[i] == '\'') {
            if (j + 4 + 2 > dst_size) {
                return false;
            }
            dst[j++] = '\'';
            dst[j++] = '\\';
            dst[j++] = '\'';
            dst[j++] = '\'';
        } else {
            if (j + 1 + 2 > dst_size) {
                return false;
            }
            dst[j++] = src[i];
        }
    }
    if (j + 2 > dst_size) {
        return false;
    }
    dst[j++] = '\'';
    dst[j] = '\0';
    return true;
}

static void normalize_path(char *path) {
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        path[--len] = '\0';
    }
}

static void set_default_config(config_t *cfg) {
    const char *home = getenv("HOME");
    if (home == NULL) {
        home = ".";
    }

    snprintf(cfg->local_dir, sizeof(cfg->local_dir), "%s/Documents/42bangkok", home);
    snprintf(cfg->gh_dir, sizeof(cfg->gh_dir), "%s/Documents/.piscine-42-github", home);
    snprintf(cfg->cb_dir, sizeof(cfg->cb_dir), "%s/Documents/.piscine-42-codeberg", home);
    snprintf(cfg->branch, sizeof(cfg->branch), "main");
}

static void apply_config_key(config_t *cfg, const char *key, const char *value) {
    if (strcmp(key, "LOCAL_DIR") == 0) {
        snprintf(cfg->local_dir, sizeof(cfg->local_dir), "%s", value);
    } else if (strcmp(key, "GH_DIR") == 0) {
        snprintf(cfg->gh_dir, sizeof(cfg->gh_dir), "%s", value);
    } else if (strcmp(key, "CB_DIR") == 0) {
        snprintf(cfg->cb_dir, sizeof(cfg->cb_dir), "%s", value);
    } else if (strcmp(key, "BRANCH") == 0) {
        snprintf(cfg->branch, sizeof(cfg->branch), "%s", value);
    }
}

static void load_config(config_t *cfg) {
    set_default_config(cfg);

    char path[PATH_MAX];
    const char *config_env = getenv("BACKUP_TOOL_CONFIG");
    if (config_env != NULL && config_env[0] != '\0') {
        snprintf(path, sizeof(path), "%s", config_env);
    } else {
        const char *home = getenv("HOME");
        if (home == NULL) {
            home = ".";
        }
        snprintf(path, sizeof(path), "%s/.config/42-backup-tool.conf", home);
    }

    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return;
    }

    char line[BUF_SIZE];
    while (fgets(line, sizeof(line), fp) != NULL) {
        trim(line);
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        char *eq = strchr(line, '=');
        if (eq == NULL) {
            continue;
        }
        *eq = '\0';
        char *key = line;
        char *value = eq + 1;
        trim(key);
        trim(value);
        apply_config_key(cfg, key, value);
    }
    fclose(fp);
}

static bool dir_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int run_command(const char *cmd, char *out, size_t out_size) {
    if (out_size > 0) {
        out[0] = '\0';
    }
    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        return -1;
    }

    size_t used = 0;
    char line[512];
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (out_size > 1 && used < out_size - 1) {
            size_t rem = out_size - 1 - used;
            size_t take = strnlen(line, rem);
            memcpy(out + used, line, take);
            used += take;
            out[used] = '\0';
        }
    }

    int status = pclose(fp);
    if (status == -1) {
        return -1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

static int handle_backup_target(const config_t *cfg, const char *target_dir, const char *name, char *msg, size_t msg_size) {
    if (!dir_exists(target_dir)) {
        snprintf(msg, msg_size, "%s: directory not found: %s", name, target_dir);
        return 1;
    }

    char q_local[PATH_MAX * 2];
    char q_target[PATH_MAX * 2];
    char q_branch[128];
    char local_norm[PATH_MAX];
    char target_norm[PATH_MAX];
    snprintf(local_norm, sizeof(local_norm), "%s", cfg->local_dir);
    snprintf(target_norm, sizeof(target_norm), "%s", target_dir);
    normalize_path(local_norm);
    normalize_path(target_norm);
    if (!shell_quote(local_norm, q_local, sizeof(q_local)) ||
        !shell_quote(target_norm, q_target, sizeof(q_target)) ||
        !shell_quote(cfg->branch, q_branch, sizeof(q_branch))) {
        snprintf(msg, msg_size, "%s: path/config value is too long.", name);
        return 1;
    }

    char cmd[CMD_BUF_SIZE];
    char out[BUF_SIZE];

    snprintf(cmd, sizeof(cmd), "cd %s && git pull origin %s --quiet 2>&1", q_target, q_branch);
    if (run_command(cmd, out, sizeof(out)) != 0) {
        snprintf(msg, msg_size, "%s: git pull failed\n%s", name, out);
        return 1;
    }

    snprintf(cmd, sizeof(cmd), "rsync " RSYNC_OPTS " %s/ %s/ 2>&1", q_local, q_target);
    if (run_command(cmd, out, sizeof(out)) != 0) {
        snprintf(msg, msg_size, "%s: rsync failed\n%s", name, out);
        return 1;
    }

    snprintf(cmd, sizeof(cmd),
             "cd %s && if [ -n \"$(git status --porcelain)\" ]; then "
             "git add . && git commit -m \"Backup: $(date +'%%Y-%%m-%%d %%H:%%M:%%S')\" && git push origin %s; "
             "else echo '" NO_CHANGES_TOKEN "'; fi 2>&1",
             q_target, q_branch);
    if (run_command(cmd, out, sizeof(out)) != 0) {
        snprintf(msg, msg_size, "%s: commit/push failed\n%s", name, out);
        return 1;
    }

    if (strstr(out, NO_CHANGES_TOKEN) != NULL) {
        snprintf(msg, msg_size, "%s: already in sync (no push needed).", name);
    } else {
        snprintf(msg, msg_size, "%s: files pushed to cloud.", name);
    }
    return 0;
}

static int run_backup(const config_t *cfg, char *msg, size_t msg_size) {
    char one[BUF_SIZE];
    char two[BUF_SIZE];

    if (handle_backup_target(cfg, cfg->gh_dir, "GitHub", one, sizeof(one)) != 0) {
        snprintf(msg, msg_size, "%s", one);
        return 1;
    }
    if (handle_backup_target(cfg, cfg->cb_dir, "Codeberg", two, sizeof(two)) != 0) {
        snprintf(msg, msg_size, "%s", two);
        return 1;
    }
    snprintf(msg, msg_size, "Backup complete.\n- %s\n- %s", one, two);
    return 0;
}

static int run_restore(const config_t *cfg, char *msg, size_t msg_size) {
    if (!dir_exists(cfg->gh_dir)) {
        snprintf(msg, msg_size, "GitHub directory not found: %s", cfg->gh_dir);
        return 1;
    }

    char q_local[PATH_MAX * 2];
    char q_gh[PATH_MAX * 2];
    char q_branch[128];
    char local_norm[PATH_MAX];
    char gh_norm[PATH_MAX];
    snprintf(local_norm, sizeof(local_norm), "%s", cfg->local_dir);
    snprintf(gh_norm, sizeof(gh_norm), "%s", cfg->gh_dir);
    normalize_path(local_norm);
    normalize_path(gh_norm);
    if (!shell_quote(local_norm, q_local, sizeof(q_local)) ||
        !shell_quote(gh_norm, q_gh, sizeof(q_gh)) ||
        !shell_quote(cfg->branch, q_branch, sizeof(q_branch))) {
        snprintf(msg, msg_size, "Path/config value is too long.");
        return 1;
    }

    char cmd[CMD_BUF_SIZE];
    char out[BUF_SIZE];

    snprintf(cmd, sizeof(cmd), "cd %s && git pull origin %s --quiet 2>&1", q_gh, q_branch);
    if (run_command(cmd, out, sizeof(out)) != 0) {
        snprintf(msg, msg_size, "Restore failed while pulling GitHub\n%s", out);
        return 1;
    }

    snprintf(cmd, sizeof(cmd), "rsync " RSYNC_OPTS " %s/ %s/ 2>&1", q_gh, q_local);
    if (run_command(cmd, out, sizeof(out)) != 0) {
        snprintf(msg, msg_size, "Restore failed while syncing to local\n%s", out);
        return 1;
    }

    snprintf(msg, msg_size, "Restore complete. Local workspace is up to date.");
    return 0;
}

static void draw_ui(const config_t *cfg, int selected, const char *status) {
    static const char *items[] = {
        "LOCAL -> CLOUD (Backup everything)",
        "CLOUD -> LOCAL (Restore to machine)",
        "Reload config",
        "Exit",
    };
    const int count = MENU_ITEMS;

    clear();
    mvprintw(1, 2, "42 BANGKOK SYNC MANAGER (C + ncurses)");
    mvprintw(3, 2, "Config: LOCAL=%s", cfg->local_dir);
    mvprintw(4, 2, "        GH=%s", cfg->gh_dir);
    mvprintw(5, 2, "        CB=%s", cfg->cb_dir);
    mvprintw(6, 2, "        BRANCH=%s", cfg->branch);

    mvprintw(8, 2, "Use UP/DOWN and ENTER:");
    for (int i = 0; i < count; i++) {
        if (i == selected) {
            attron(A_REVERSE);
        }
        mvprintw(10 + i, 4, "%s", items[i]);
        if (i == selected) {
            attroff(A_REVERSE);
        }
    }

    mvprintw(16, 2, "Status:");
    mvprintw(17, 2, "%s", status);
    refresh();
}

static bool confirm_delete_sync(const char *mode_label) {
    int rows = 0;
    int cols = 0;
    getmaxyx(stdscr, rows, cols);
    if (rows < CONFIRM_WIN_HEIGHT + 2 || cols < 30) {
        return false;
    }

    WINDOW *win = newwin(CONFIRM_WIN_HEIGHT, cols - CONFIRM_WIN_MARGIN_X, rows - CONFIRM_WIN_OFFSET_Y, CONFIRM_WIN_OFFSET_X);
    if (win == NULL) {
        return false;
    }
    box(win, 0, 0);
    mvwprintw(win, 1, 2, "%s uses rsync --delete.", mode_label);
    mvwprintw(win, 2, 2, "This can remove files in destination directories.");
    mvwprintw(win, 4, 2, "Continue? (y/N)");
    wrefresh(win);

    int ch = wgetch(win);
    delwin(win);
    return ch == 'y' || ch == 'Y';
}

int main(void) {
    config_t cfg;
    load_config(&cfg);

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    int selected = 0;
    char status[BUF_SIZE] = "Ready.";
    bool running = true;

    while (running) {
        draw_ui(&cfg, selected, status);
        int ch = getch();
        switch (ch) {
            case KEY_UP:
                selected = (selected - 1 + MENU_ITEMS) % MENU_ITEMS;
                break;
            case KEY_DOWN:
                selected = (selected + 1) % MENU_ITEMS;
                break;
            case '\n':
            case KEY_ENTER:
                if (selected == 0) {
                    if (!confirm_delete_sync("Backup")) {
                        snprintf(status, sizeof(status), "Backup cancelled.");
                        break;
                    }
                    snprintf(status, sizeof(status), "Running backup...");
                    draw_ui(&cfg, selected, status);
                    if (run_backup(&cfg, status, sizeof(status)) == 0) {
                        beep();
                    }
                } else if (selected == 1) {
                    if (!confirm_delete_sync("Restore")) {
                        snprintf(status, sizeof(status), "Restore cancelled.");
                        break;
                    }
                    snprintf(status, sizeof(status), "Running restore...");
                    draw_ui(&cfg, selected, status);
                    if (run_restore(&cfg, status, sizeof(status)) == 0) {
                        beep();
                    }
                } else if (selected == 2) {
                    load_config(&cfg);
                    snprintf(status, sizeof(status), "Config reloaded.");
                } else {
                    running = false;
                }
                break;
            case 'q':
            case 'Q':
                running = false;
                break;
            default:
                break;
        }
    }

    endwin();
    return 0;
}
