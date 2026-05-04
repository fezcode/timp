#include "filebrowser.h"
#include "font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define WH_SEP '\\'
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#define WH_SEP '/'
#endif

static const char* AUDIO_EXTS[] = { ".mp3", ".wav", ".flac", ".ogg", ".opus", ".m4a", ".aiff", NULL };

static bool ends_with_ci(const char* s, const char* suffix) {
    size_t ls = strlen(s), lf = strlen(suffix);
    if (ls < lf) return false;
    const char* a = s + ls - lf;
    for (size_t i = 0; i < lf; i++) {
        char c1 = (char)tolower((unsigned char)a[i]);
        char c2 = (char)tolower((unsigned char)suffix[i]);
        if (c1 != c2) return false;
    }
    return true;
}

static bool is_audio_file(const char* name) {
    for (int i = 0; AUDIO_EXTS[i]; i++) {
        if (ends_with_ci(name, AUDIO_EXTS[i])) return true;
    }
    return false;
}

static int cmp_entry(const void* a, const void* b) {
    const FbEntry* ea = (const FbEntry*)a;
    const FbEntry* eb = (const FbEntry*)b;
    if (ea->is_dir != eb->is_dir) return ea->is_dir ? -1 : 1;
#ifdef _WIN32
    return _stricmp(ea->name, eb->name);
#else
    return strcasecmp(ea->name, eb->name);
#endif
}

static void fb_push(FileBrowser* fb, const char* name, bool is_dir) {
    if (fb->n_entries >= fb->cap_entries) {
        int ncap = fb->cap_entries ? fb->cap_entries * 2 : 64;
        if (ncap > FB_MAX_ENTRIES) ncap = FB_MAX_ENTRIES;
        if (fb->n_entries >= ncap) return;
        fb->entries = (FbEntry*)realloc(fb->entries, sizeof(FbEntry) * ncap);
        fb->cap_entries = ncap;
    }
    FbEntry* e = &fb->entries[fb->n_entries++];
    snprintf(e->name, sizeof(e->name), "%s", name);
    e->is_dir = is_dir;
}

static void fb_scan(FileBrowser* fb) {
    fb->n_entries = 0;
    fb->scroll = 0;
    fb->hover = -1;

    // ".." (unless at filesystem root)
#ifdef _WIN32
    if (strlen(fb->cwd) > 3 || (strlen(fb->cwd) == 3 && fb->cwd[1] != ':')) {
        fb_push(fb, "..", true);
    }
#else
    if (strcmp(fb->cwd, "/") != 0) fb_push(fb, "..", true);
#endif

#ifdef _WIN32
    char pat[FB_PATH_MAX + 4];
    snprintf(pat, sizeof(pat), "%s\\*", fb->cwd);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) continue;
            bool is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (!is_dir && !is_audio_file(fd.cFileName)) continue;
            fb_push(fb, fd.cFileName, is_dir);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR* d = opendir(fb->cwd);
    if (d) {
        struct dirent* de;
        while ((de = readdir(d)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
            if (de->d_name[0] == '.') continue;
            char full[FB_PATH_MAX + 260];
            snprintf(full, sizeof(full), "%s/%s", fb->cwd, de->d_name);
            struct stat st;
            if (stat(full, &st) != 0) continue;
            bool is_dir = S_ISDIR(st.st_mode);
            if (!is_dir && !is_audio_file(de->d_name)) continue;
            fb_push(fb, de->d_name, is_dir);
        }
        closedir(d);
    }
#endif

    qsort(fb->entries, fb->n_entries, sizeof(FbEntry), cmp_entry);
    fb->anchor = -1;
}

static void fb_navigate(FileBrowser* fb, const char* name) {
    if (strcmp(name, "..") == 0) {
#ifdef _WIN32
        size_t n = strlen(fb->cwd);
        if (n > 3) {
            char* sep = strrchr(fb->cwd, '\\');
            if (sep && sep > fb->cwd + 2) *sep = 0;
            else fb->cwd[3] = 0;  // X:\.
        }
#else
        char* sep = strrchr(fb->cwd, '/');
        if (sep && sep != fb->cwd) *sep = 0;
        else strcpy(fb->cwd, "/");
#endif
    } else {
        char tmp[FB_PATH_MAX];
        size_t n = strlen(fb->cwd);
        bool has_trailing = n > 0 && (fb->cwd[n-1] == '/' || fb->cwd[n-1] == '\\');
        snprintf(tmp, sizeof(tmp), "%s%s%s", fb->cwd, has_trailing ? "" : (char[]){WH_SEP, 0}, name);
        snprintf(fb->cwd, sizeof(fb->cwd), "%s", tmp);
    }
    fb_scan(fb);
}

void fb_init(FileBrowser* fb) {
    memset(fb, 0, sizeof(*fb));
    fb->hover = -1;
    fb->anchor = -1;
#ifdef _WIN32
    char buf[FB_PATH_MAX];
    if (_getcwd(buf, sizeof(buf))) {
        snprintf(fb->cwd, sizeof(fb->cwd), "%s", buf);
    } else {
        snprintf(fb->cwd, sizeof(fb->cwd), "%s", "C:\\");
    }
#else
    char buf[FB_PATH_MAX];
    if (getcwd(buf, sizeof(buf))) {
        snprintf(fb->cwd, sizeof(fb->cwd), "%s", buf);
    } else {
        snprintf(fb->cwd, sizeof(fb->cwd), "%s", "/");
    }
#endif
}

void fb_destroy(FileBrowser* fb) {
    free(fb->entries);
    memset(fb, 0, sizeof(*fb));
}

void fb_show(FileBrowser* fb) {
    fb->open = true;
    fb_scan(fb);
}

void fb_close(FileBrowser* fb) { fb->open = false; }

int fb_result_count(const FileBrowser* fb) {
    return fb->result_ready ? fb->result_count : 0;
}

const char* fb_result_path(const FileBrowser* fb, int i) {
    if (!fb->result_ready || i < 0 || i >= fb->result_count) return NULL;
    return fb->result_paths[i];
}

void fb_clear_result(FileBrowser* fb) {
    fb->result_ready = false;
    fb->result_count = 0;
}

static void fb_clear_selection(FileBrowser* fb) {
    for (int i = 0; i < fb->n_entries; i++) fb->entries[i].selected = false;
}

static int fb_count_selected_files(const FileBrowser* fb) {
    int n = 0;
    for (int i = 0; i < fb->n_entries; i++) {
        if (fb->entries[i].selected && !fb->entries[i].is_dir) n++;
    }
    return n;
}

static void fb_commit_selection(FileBrowser* fb, int fallback_idx) {
    fb->result_count = 0;
    for (int i = 0; i < fb->n_entries && fb->result_count < FB_MAX_PICKS; i++) {
        if (fb->entries[i].selected && !fb->entries[i].is_dir) {
            snprintf(fb->result_paths[fb->result_count],
                     sizeof(fb->result_paths[0]), "%s%c%s",
                     fb->cwd, WH_SEP, fb->entries[i].name);
            fb->result_count++;
        }
    }
    if (fb->result_count == 0 && fallback_idx >= 0 && fallback_idx < fb->n_entries
        && !fb->entries[fallback_idx].is_dir) {
        snprintf(fb->result_paths[0], sizeof(fb->result_paths[0]), "%s%c%s",
                 fb->cwd, WH_SEP, fb->entries[fallback_idx].name);
        fb->result_count = 1;
    }
    if (fb->result_count > 0) {
        fb->result_ready = true;
        fb_close(fb);
    }
}

// ----- Rendering & input -----

#define ROW_HEIGHT 14
#define HEADER_H 22
#define FOOTER_H 22

static SDL_Rect list_rect(const Skin* skin) {
    SDL_Rect r = { 8, HEADER_H + 4, skin->window_w - 16,
                   skin->window_h - HEADER_H - FOOTER_H - 8 };
    return r;
}

static SDL_Rect cancel_rect(const Skin* skin) {
    SDL_Rect r = { skin->window_w - 64, skin->window_h - FOOTER_H + 4, 56, 14 };
    return r;
}

static SDL_Rect open_rect(const Skin* skin) {
    SDL_Rect r = { skin->window_w - 128, skin->window_h - FOOTER_H + 4, 56, 14 };
    return r;
}

static void fill(SDL_Renderer* ren, SDL_Rect r, SDL_Color c) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(ren, &r);
}
static void stroke(SDL_Renderer* ren, SDL_Rect r, SDL_Color c) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_RenderDrawRect(ren, &r);
}

void fb_handle_event(FileBrowser* fb, const SDL_Event* e, const Skin* skin) {
    if (!fb->open) return;

    SDL_Rect lr = list_rect(skin);
    fb->rows_visible = lr.h / ROW_HEIGHT;

    if (e->type == SDL_KEYDOWN) {
        switch (e->key.keysym.sym) {
            case SDLK_ESCAPE: fb_close(fb); return;
            case SDLK_DOWN:
                if (fb->hover < fb->n_entries - 1) fb->hover++;
                if (fb->hover >= 0 && fb->hover - fb->scroll >= fb->rows_visible)
                    fb->scroll = fb->hover - fb->rows_visible + 1;
                return;
            case SDLK_UP:
                if (fb->hover > 0) fb->hover--;
                if (fb->hover < fb->scroll) fb->scroll = fb->hover < 0 ? 0 : fb->hover;
                return;
            case SDLK_RETURN:
                // Enter: if hover is dir, navigate; otherwise open all selected (or hover)
                if (fb->hover >= 0 && fb->hover < fb->n_entries
                    && fb->entries[fb->hover].is_dir) {
                    fb_navigate(fb, fb->entries[fb->hover].name);
                } else {
                    fb_commit_selection(fb, fb->hover);
                }
                return;
            case SDLK_BACKSPACE:
                fb_navigate(fb, "..");
                return;
            case SDLK_a:
                if (SDL_GetModState() & KMOD_CTRL) {
                    for (int i = 0; i < fb->n_entries; i++) {
                        if (!fb->entries[i].is_dir) fb->entries[i].selected = true;
                    }
                }
                return;
            default: break;
        }
    } else if (e->type == SDL_MOUSEWHEEL) {
        fb->scroll -= e->wheel.y * 3;
        int max_scroll = fb->n_entries - fb->rows_visible;
        if (max_scroll < 0) max_scroll = 0;
        if (fb->scroll < 0) fb->scroll = 0;
        if (fb->scroll > max_scroll) fb->scroll = max_scroll;
    } else if (e->type == SDL_MOUSEMOTION) {
        int mx = e->motion.x, my = e->motion.y;
        fb->hover = -1;
        if (mx >= lr.x && mx < lr.x + lr.w && my >= lr.y && my < lr.y + lr.h) {
            int row = (my - lr.y) / ROW_HEIGHT;
            int idx = fb->scroll + row;
            if (idx >= 0 && idx < fb->n_entries) fb->hover = idx;
        }
    } else if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        int mx = e->button.x, my = e->button.y;
        SDL_Rect cr = cancel_rect(skin);
        SDL_Rect orr = open_rect(skin);
        if (mx >= cr.x && mx < cr.x + cr.w && my >= cr.y && my < cr.y + cr.h) {
            fb_close(fb);
            return;
        }
        if (mx >= orr.x && mx < orr.x + orr.w && my >= orr.y && my < orr.y + orr.h) {
            fb_commit_selection(fb, fb->hover);
            return;
        }
        if (mx >= lr.x && mx < lr.x + lr.w && my >= lr.y && my < lr.y + lr.h) {
            int row = (my - lr.y) / ROW_HEIGHT;
            int idx = fb->scroll + row;
            if (idx < 0 || idx >= fb->n_entries) return;
            FbEntry* en = &fb->entries[idx];

            // Double-click: navigate dir or open file immediately
            if (e->button.clicks >= 2) {
                if (en->is_dir) {
                    fb_navigate(fb, en->name);
                } else {
                    fb_clear_selection(fb);
                    en->selected = true;
                    fb_commit_selection(fb, idx);
                }
                return;
            }

            // Directories navigate on single click (no selection state)
            if (en->is_dir) {
                fb_navigate(fb, en->name);
                return;
            }

            // Files: select with modifier-aware behavior
            SDL_Keymod mod = SDL_GetModState();
            bool ctrl  = (mod & KMOD_CTRL)  != 0;
            bool shift = (mod & KMOD_SHIFT) != 0;

            if (shift && fb->anchor >= 0 && fb->anchor < fb->n_entries) {
                fb_clear_selection(fb);
                int lo = fb->anchor < idx ? fb->anchor : idx;
                int hi = fb->anchor < idx ? idx : fb->anchor;
                for (int i = lo; i <= hi; i++) {
                    if (!fb->entries[i].is_dir) fb->entries[i].selected = true;
                }
            } else if (ctrl) {
                en->selected = !en->selected;
                fb->anchor = idx;
            } else {
                fb_clear_selection(fb);
                en->selected = true;
                fb->anchor = idx;
            }
        }
    }
}

void fb_render(FileBrowser* fb, SDL_Renderer* ren, const Skin* skin) {
    if (!fb->open) return;

    // panel background (covers whole window)
    SDL_Rect full = { 0, 0, skin->window_w, skin->window_h };
    fill(ren, full, skin->theme_bg);

    // header
    SDL_Rect header = { 0, 0, skin->window_w, HEADER_H };
    fill(ren, header, skin->theme_panel);
    font_draw(ren, 8, 4, 1, skin->theme_accent, "OPEN AUDIO");
    char shown[200];
    snprintf(shown, sizeof(shown), "%s", fb->cwd);
    int max_chars = (skin->window_w - 16) / ((FONT_W + 1));
    int n = (int)strlen(shown);
    if (n > max_chars && max_chars > 4) {
        // ellipsize the start so the directory name is visible
        int keep = max_chars - 3;
        memmove(shown, shown + (n - keep), keep + 1);
        shown[0] = '.'; shown[1] = '.'; shown[2] = '.';
    }
    font_draw(ren, 8, 12, 1, skin->theme_text, shown);

    // list area
    SDL_Rect lr = list_rect(skin);
    fill(ren, lr, (SDL_Color){ 8, 12, 16, 255 });
    stroke(ren, lr, (SDL_Color){ skin->theme_accent.r/3, skin->theme_accent.g/3, skin->theme_accent.b/3, 255 });

    fb->rows_visible = lr.h / ROW_HEIGHT;
    int max_scroll = fb->n_entries - fb->rows_visible;
    if (max_scroll < 0) max_scroll = 0;
    if (fb->scroll < 0) fb->scroll = 0;
    if (fb->scroll > max_scroll) fb->scroll = max_scroll;

    int rows_visible = fb->rows_visible;
    for (int r = 0; r < rows_visible; r++) {
        int idx = fb->scroll + r;
        if (idx >= fb->n_entries) break;
        FbEntry* en = &fb->entries[idx];
        SDL_Rect row = { lr.x + 1, lr.y + 1 + r * ROW_HEIGHT, lr.w - 2, ROW_HEIGHT };
        if (en->selected) {
            SDL_Color bg = { (Uint8)(skin->theme_accent.r / 4 + skin->theme_panel.r / 2),
                             (Uint8)(skin->theme_accent.g / 4 + skin->theme_panel.g / 2),
                             (Uint8)(skin->theme_accent.b / 4 + skin->theme_panel.b / 2), 255 };
            fill(ren, row, bg);
        } else if (idx == fb->hover) {
            fill(ren, row, skin->theme_panel);
        }

        SDL_Color color = en->is_dir ? skin->theme_accent
                          : (en->selected ? skin->theme_accent : skin->theme_text);
        char label[260];
        if (en->is_dir)        snprintf(label, sizeof(label), "[ ] %s", en->name);
        else if (en->selected) snprintf(label, sizeof(label), " *  %s", en->name);
        else                   snprintf(label, sizeof(label), "    %s", en->name);

        // truncate to fit row width
        int max_c = (row.w - 8) / (FONT_W + 1);
        if ((int)strlen(label) > max_c && max_c > 3) {
            label[max_c - 3] = '.';
            label[max_c - 2] = '.';
            label[max_c - 1] = '.';
            label[max_c] = 0;
        }
        font_draw(ren, row.x + 4, row.y + (ROW_HEIGHT - FONT_H) / 2, 1, color, label);
    }

    // scrollbar
    if (fb->n_entries > rows_visible) {
        SDL_Rect sb = { lr.x + lr.w - 4, lr.y + 1, 3, lr.h - 2 };
        fill(ren, sb, (SDL_Color){ skin->theme_panel.r, skin->theme_panel.g, skin->theme_panel.b, 255 });
        float th_h = (float)rows_visible / (float)fb->n_entries * sb.h;
        if (th_h < 8) th_h = 8;
        float th_y = (float)fb->scroll / (float)(fb->n_entries - rows_visible) * (sb.h - th_h);
        SDL_Rect th = { sb.x, sb.y + (int)th_y, sb.w, (int)th_h };
        fill(ren, th, skin->theme_accent);
    }

    // footer + cancel
    SDL_Rect footer = { 0, skin->window_h - FOOTER_H, skin->window_w, FOOTER_H };
    fill(ren, footer, skin->theme_panel);

    SDL_Rect orr = open_rect(skin);
    int n_sel = fb_count_selected_files(fb);
    SDL_Color open_face = (n_sel > 0)
        ? skin->theme_accent
        : (SDL_Color){ skin->theme_bg.r, skin->theme_bg.g, skin->theme_bg.b, 255 };
    SDL_Color open_text = (n_sel > 0) ? skin->theme_bg : skin->theme_text;
    fill(ren, orr, open_face);
    stroke(ren, orr, skin->theme_accent);
    char ob[24];
    if (n_sel > 1) snprintf(ob, sizeof(ob), "OPEN %d", n_sel);
    else           snprintf(ob, sizeof(ob), "OPEN");
    int otw = font_text_width(1, ob);
    font_draw(ren, orr.x + (orr.w - otw) / 2, orr.y + (orr.h - FONT_H) / 2, 1, open_text, ob);

    SDL_Rect cr = cancel_rect(skin);
    fill(ren, cr, (SDL_Color){ skin->theme_bg.r, skin->theme_bg.g, skin->theme_bg.b, 255 });
    stroke(ren, cr, skin->theme_accent);
    int tw = font_text_width(1, "CANCEL");
    font_draw(ren, cr.x + (cr.w - tw) / 2, cr.y + (cr.h - FONT_H) / 2, 1, skin->theme_text, "CANCEL");

    font_draw(ren, 8, footer.y + (FOOTER_H - FONT_H) / 2, 1, skin->theme_text,
              "CTRL-CLICK MULTI  SHIFT-CLICK RANGE  CTRL-A ALL");
}
