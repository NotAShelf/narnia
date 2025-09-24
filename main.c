#include "include/clipboard.h"
#include <curl/curl.h>
#include <getopt.h>
#include <limits.h>
#include <locale.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_RESULTS 16
#define HASH_START 11
#define HASH_LEN 32

typedef struct {
  char url[256];
  char name[128];
  char resolved_path[PATH_MAX];
  char hash[HASH_LEN + 1];
  char *narinfo;
  int narinfo_lines;
  char **narinfo_view;
} NarinfoResult;

typedef struct {
  char *key;
  char *desc;
} StatusItem;

struct string {
  char *ptr;
  size_t len;
};

static char *cache_urls[MAX_RESULTS];
static int cache_count = 0;

void init_string(struct string *s) {
  s->len = 0;
  s->ptr = malloc(1);
  if (s->ptr)
    s->ptr[0] = '\0';
}

size_t writefunc(void *ptr, size_t size, size_t nmemb, struct string *s) {
  size_t new_len = s->len + size * nmemb;
  char *p = realloc(s->ptr, new_len + 1);
  if (!p)
    return 0;
  s->ptr = p;
  memcpy(s->ptr + s->len, ptr, size * nmemb);
  s->ptr[new_len] = '\0';
  s->len = new_len;
  return size * nmemb;
}

int find_executable(const char *prog, char *out, size_t outlen) {
  if (strchr(prog, '/')) {
    if (access(prog, X_OK) == 0) {
      if (realpath(prog, out) == NULL)
        return -1;
      return 0;
    }
    return -1;
  }

  char *path = getenv("PATH");
  if (!path)
    return -1;

  char *paths = strdup(path);
  if (!paths)
    return -1;

  char *tok = strtok(paths, ":");
  while (tok) {
    snprintf(out, outlen, "%s/%s", tok, prog);
    if (access(out, X_OK) == 0) {
      char real[PATH_MAX];
      if (realpath(out, real)) {
        strncpy(out, real, outlen - 1);
        out[outlen - 1] = '\0';
      }
      free(paths);
      return 0;
    }
    tok = strtok(NULL, ":");
  }
  free(paths);
  return -1;
}

int extract_hash(const char *real, char *out, size_t outlen) {
  size_t len = strlen(real);
  if (len < HASH_START + HASH_LEN)
    return -1;
  strncpy(out, real + HASH_START, HASH_LEN);
  out[HASH_LEN] = '\0';
  return 0;
}

int fetch_narinfo(const char *base_url, const char *hash,
                  struct string *response, char *errbuf, size_t errlen) {
  CURL *curl = curl_easy_init();
  if (!curl)
    return -1;

  char url[512];
  snprintf(url, sizeof(url), "%s/%s.narinfo", base_url, hash);

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "narnia/1.0");
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

  CURLcode res = curl_easy_perform(curl);
  curl_easy_cleanup(curl);
  return (res == CURLE_OK) ? 0 : -1;
}

int split_lines(char *buf, char ***view) {
  int lines = 0;
  for (char *p = buf; *p; ++p) {
    if (*p == '\n')
      ++lines;
  }

  char **arr = malloc(sizeof(char *) * (lines + 2));
  if (!arr)
    return -1;

  int l = 0;
  char *saveptr, *tok = strtok_r(buf, "\n", &saveptr);
  while (tok) {
    arr[l++] = tok;
    tok = strtok_r(NULL, "\n", &saveptr);
  }
  arr[l] = NULL;
  *view = arr;
  return l;
}

void draw_centered_bordered_win(WINDOW **outwin, int height, int width,
                                int maxy, int maxx) {
  int starty = (maxy - height) / 2;
  int startx = (maxx - width) / 2;
  *outwin = newwin(height, width, starty, startx);
  box(*outwin, 0, 0);
  wrefresh(*outwin);
}

void show_centered_inputbox(const char *prompt, char *out, size_t outlen) {
  int maxy, maxx;
  getmaxyx(stdscr, maxy, maxx);
  int width = strlen(prompt) + 32;
  if (width < 40)
    width = 40;
  int height = 5;

  WINDOW *win;
  draw_centered_bordered_win(&win, height, width, maxy, maxx);
  mvwprintw(win, 1, (width - strlen(prompt)) / 2, "%s", prompt);

  int input_width = width - 6;
  int input_x = 3;
  wmove(win, 3, input_x);

  echo();
  wgetnstr(win, out, outlen - 1);
  noecho();

  delwin(win);
  clear();
  refresh();
}

void show_status_structured(StatusItem *items, int item_count) {
  int maxy, maxx;
  getmaxyx(stdscr, maxy, maxx);

  char status_line[1024];
  int pos = 0;
  memset(status_line, 0, sizeof(status_line));

  for (int i = 0; i < item_count && pos < 800; i++) {
    if (i > 0) {
      pos += snprintf(status_line + pos, sizeof(status_line) - pos, "  ");
    }
    pos += snprintf(status_line + pos, sizeof(status_line) - pos, "%s: %s",
                    items[i].key, items[i].desc);
  }

  char padded_line[1024];
  snprintf(padded_line, sizeof(padded_line), "%-*s", maxx - 2, status_line);
  padded_line[maxx - 2] = '\0';

  move(maxy - 2, 1);
  attron(A_REVERSE);
  addstr(padded_line);
  attroff(A_REVERSE);
}

void show_status(const char *msg) {
  StatusItem item = {"Status", (char *)msg};
  show_status_structured(&item, 1);
}

void show_main_borders(void) {
  int maxy, maxx;
  getmaxyx(stdscr, maxy, maxx);
  box(stdscr, 0, 0);
  mvhline(2, 1, 0, maxx - 2);
  mvhline(maxy - 3, 1, 0, maxx - 2);
}

void free_narinfo(NarinfoResult *res) {
  if (res->narinfo_view)
    free(res->narinfo_view);
  if (res->narinfo)
    free(res->narinfo);
  res->narinfo = NULL;
  res->narinfo_view = NULL;
}

void clipboard_copy(const char *str) {
  if (clipboard_set_text(str) != 0) {
    show_status("Failed to copy to clipboard");
    napms(1000);
  }
}

int process_executable(const char *input, NarinfoResult results[]) {
  int loaded = 0;

  for (int repo = 0; repo < cache_count; ++repo) {
    NarinfoResult *res = &results[repo];
    memset(res, 0, sizeof(*res));
    strncpy(res->url, cache_urls[repo], sizeof(res->url) - 1);
    strncpy(res->name, input, sizeof(res->name) - 1);

    show_status("Locating executable...");
    if (find_executable(input, res->resolved_path,
                        sizeof(res->resolved_path)) != 0) {
      show_status("Executable not found or not executable. Press any key.");
      getch();
      break;
    }

    show_status("Extracting hash...");
    if (extract_hash(res->resolved_path, res->hash, sizeof(res->hash)) != 0) {
      show_status("Could not extract hash from path. Press any key.");
      getch();
      break;
    }

    show_status("Fetching narinfo...");
    struct string response;
    init_string(&response);
    char errbuf[CURL_ERROR_SIZE] = {0};

    if (fetch_narinfo(res->url, res->hash, &response, errbuf, sizeof(errbuf)) !=
        0) {
      char prompt[256];
      snprintf(prompt, sizeof(prompt),
               "Failed to fetch narinfo from %s. Press any key.", res->url);
      show_status(prompt);
      free(response.ptr);
      loaded = repo;
      getch();
      break;
    }

    res->narinfo = response.ptr;
    res->narinfo_lines = split_lines(res->narinfo, &res->narinfo_view);
    loaded = repo + 1;
  }

  return loaded;
}

void show_narinfo_viewer(NarinfoResult results[], int loaded) {
  int current = 0;
  int selected_line = 0;

  nodelay(stdscr, 0);
  keypad(stdscr, 1);

  while (1) {
    clear();
    int maxy, maxx;
    getmaxyx(stdscr, maxy, maxx);
    show_main_borders();

    NarinfoResult *res = &results[current];

    char exec_name[64];
    char path_short[128];
    char hash_short[16];
    char url_short[64];

    snprintf(exec_name, sizeof(exec_name), "%.60s", res->name);
    snprintf(path_short, sizeof(path_short), "%.120s", res->resolved_path);
    snprintf(hash_short, sizeof(hash_short), "%.12s", res->hash);
    snprintf(url_short, sizeof(url_short), "%.60s", res->url);

    mvprintw(1, 1, "Exec: %s Path: %s Hash: %s Src: %s [%d/%d]", exec_name,
             path_short, hash_short, url_short, current + 1, loaded);

    int lines_avail = maxy - 6;
    int top = selected_line - lines_avail / 2;
    if (top < 0)
      top = 0;
    if (top > res->narinfo_lines - lines_avail)
      top = res->narinfo_lines - lines_avail;
    if (top < 0)
      top = 0;

    for (int l = 0; l < lines_avail && (l + top) < res->narinfo_lines; ++l) {
      if (l + top == selected_line) {
        attron(A_REVERSE);
        mvprintw(3 + l, 2, "%.*s", maxx - 4, res->narinfo_view[l + top]);
        attroff(A_REVERSE);
      } else {
        mvprintw(3 + l, 2, "%.*s", maxx - 4, res->narinfo_view[l + top]);
      }
    }

    StatusItem status_items[] = {{"Tab/Shift-Tab", "switch result"},
                                 {"Up/Down", "move cursor"},
                                 {"Enter", "copy"},
                                 {"PgUp/PgDn", "jump"},
                                 {"r", "retry"},
                                 {"q", "quit"}};
    show_status_structured(status_items, 6);

    refresh();

    int ch = getch();
    if (ch == '\t' || ch == KEY_RIGHT) {
      current = (current + 1) % loaded;
      selected_line = 0;
    } else if (ch == KEY_BTAB || ch == KEY_LEFT) {
      current = (current + loaded - 1) % loaded;
      selected_line = 0;
    } else if (ch == KEY_NPAGE) {
      selected_line += lines_avail;
      if (selected_line > res->narinfo_lines - 1)
        selected_line = res->narinfo_lines - 1;
    } else if (ch == KEY_PPAGE) {
      selected_line -= lines_avail;
      if (selected_line < 0)
        selected_line = 0;
    } else if (ch == KEY_DOWN) {
      if (selected_line < res->narinfo_lines - 1)
        selected_line++;
    } else if (ch == KEY_UP) {
      if (selected_line > 0)
        selected_line--;
    } else if (ch == '\n' || ch == KEY_ENTER) {
      clipboard_copy(res->narinfo_view[selected_line]);
      show_status("Copied to clipboard!");
      refresh();
      napms(500);
    } else if (ch == 'q') {
      return;
    } else if (ch == 'r') {
      break;
    }
  }
}

void tui_main(const char *initial_input) {
  NarinfoResult results[MAX_RESULTS] = {0};
  char prompt[128] = "Executable name or path ('q' to quit): ";
  char input[256] = {0};

  if (initial_input) {
    strncpy(input, initial_input, sizeof(input) - 1);
    int loaded = process_executable(input, results);
    if (loaded > 0) {
      show_narinfo_viewer(results, loaded);
    }
    for (int i = 0; i < loaded; ++i)
      free_narinfo(&results[i]);
    return;
  }

  while (1) {
    clear();
    show_main_borders();
    show_centered_inputbox(prompt, input, sizeof(input));

    if (strcmp(input, "q") == 0)
      break;

    int loaded = process_executable(input, results);
    if (loaded > 0) {
      show_narinfo_viewer(results, loaded);
    }

    for (int i = 0; i < loaded; ++i)
      free_narinfo(&results[i]);
    strcpy(prompt, "Executable name or path ('q' to quit): ");
  }
}

void print_usage(const char *progname) {
  printf("Usage: %s [OPTIONS] [EXECUTABLE]\n", progname);
  printf("Options:\n");
  printf("  -c, --cache URL    Add cache URL (can be used multiple times)\n");
  printf("  -h, --help         Show this help message\n");
  printf("\nArguments:\n");
  printf("  EXECUTABLE         Skip prompt and look up this executable "
         "directly\n");
  printf("\nDefault cache: https://cache.nixos.org\n");
}

int main(int argc, char *argv[]) {
  cache_urls[0] = strdup("https://cache.nixos.org");
  cache_count = 1;

  static struct option long_options[] = {{"cache", required_argument, 0, 'c'},
                                         {"help", no_argument, 0, 'h'},
                                         {0, 0, 0, 0}};

  int c;
  while ((c = getopt_long(argc, argv, "c:h", long_options, NULL)) != -1) {
    switch (c) {
    case 'c':
      if (cache_count < MAX_RESULTS) {
        cache_urls[cache_count] = strdup(optarg);
        cache_count++;
      } else {
        fprintf(stderr, "Maximum number of caches (%d) exceeded\n",
                MAX_RESULTS);
        return 1;
      }
      break;
    case 'h':
      print_usage(argv[0]);
      return 0;
    default:
      print_usage(argv[0]);
      return 1;
    }
  }

  const char *initial_input = NULL;
  if (optind < argc) {
    initial_input = argv[optind];
  }

  setlocale(LC_ALL, "");

  if (clipboard_init() != 0) {
    fprintf(stderr, "Warning: Could not initialize clipboard functionality\n");
  }

  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  curl_global_init(CURL_GLOBAL_DEFAULT);

  tui_main(initial_input);

  curl_global_cleanup();
  endwin();
  clipboard_cleanup();

  for (int i = 0; i < cache_count; i++) {
    free(cache_urls[i]);
  }

  return 0;
}
