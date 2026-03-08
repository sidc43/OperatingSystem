/*
  shell.cpp - command interpreter for the terminal pane
  supports: ls, cat, echo, touch, rm, mkdir, cd, pwd, clear, sync, exit, help
  maintains a cwd and uses vfs to access files
*/
#include "kernel/shell/shell.hpp"
#include "kernel/fs/vfs.hpp"
#include "kernel/fs/ramfs.hpp"
#include "kernel/fs/blkfs.hpp"
#include "kernel/wm/wm.hpp"
#include "kernel/apps/editor.hpp"
#include "kernel/core/print.hpp"
#include <string.h>
#include <stdint.h>

static constexpr int kMaxTestWins = 8;
static wm::Window* s_test_stack[kMaxTestWins] = {};
static int         s_test_top   = 0;
static int         s_test_count = 0;

namespace {

static void out(const char* s) {
    wm::term_puts(s);
    print(s);
}

static char* to_dec(char* buf, unsigned v) {
    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return buf; }
    char tmp[12]; int len = 0;
    while (v) { tmp[len++] = (char)('0' + v % 10); v /= 10; }

    for (int i = 0; i < len; ++i) buf[i] = tmp[len - 1 - i];
    buf[len] = '\0';
    return buf;
}

static const char* skip_ws(const char* p) {
    while (*p == ' ' || *p == '\t') ++p;
    return p;
}

static char g_cwd[64] = "";

static const char* resolve(const char* path) {
    static char r[128];
    if (!path || !path[0]) { r[0] = '\0'; return r; }
    if (path[0] == '/') {
        size_t i = 0;
        while (path[i+1] && i < sizeof(r)-1) { r[i] = path[i+1]; ++i; }
        r[i] = '\0';
        return r;
    }
    size_t cl = strlen(g_cwd), pl = strlen(path);
    if (cl == 0) {
        size_t i = 0;
        while (i < pl && i < sizeof(r)-1) { r[i] = path[i]; ++i; }
        r[i] = '\0';
    } else {
        if (cl + 1 + pl >= sizeof(r)) { r[0] = '\0'; return r; }
        for (size_t i = 0; i < cl;  ++i) r[i]         = g_cwd[i];
        r[cl] = '/';
        for (size_t i = 0; i <= pl; ++i) r[cl + 1 + i] = path[i];
    }
    return r;
}

static const char* basename_of(const char* name) {
    const char* last = name;
    for (const char* p = name; *p; ++p)
        if (*p == '/') last = p + 1;
    return last;
}

static void cmd_help() {
    out("Commands:\n");
    out("  ls              list directory contents\n");
    out("  cat <file>      print file contents\n");
    out("  echo <text>     echo text to terminal\n");
    out("  touch <file>    create empty file\n");
    out("  rm <file>       delete a file\n");
    out("  mkdir <dir>     create a directory\n");
    out("  cd [dir]        change directory (cd .. = up, cd = root)\n");
    out("  pwd             print working directory\n");
    out("  clear           clear the terminal\n");
    out("  sync            save filesystem to disk\n");
    out("  edit <file>     open / create file in the text editor\n");
    out("  wintest         open a floating test window\n");
    out("  winclose [N]    close test window N (or newest if omitted)\n");
    out("  exit / quit     shut down the machine\n");
    out("  help            show this help\n");
}

static char* rjust(char* buf, unsigned v, int width) {
    char tmp[17]; int len = 0;
    if (v == 0) { tmp[len++] = '0'; }
    else { unsigned x = v; while (x) { tmp[len++] = (char)('0' + x % 10); x /= 10; } }

    int pad = width - len; if (pad < 0) pad = 0;
    int i = 0;
    for (; i < pad; ++i) buf[i] = ' ';
    for (int j = len - 1; j >= 0; --j) buf[i++] = tmp[j];
    buf[i] = '\0';
    return buf;
}

static void fmt_size(char* out9, size_t size) {
    char num[8];
    if (size < 1024) {
        rjust(num, (unsigned)size, 6);

        for (int i = 0; i < 6; ++i) out9[i] = num[i];
        out9[6] = ' '; out9[7] = 'B'; out9[8] = '\0';
    } else {
        unsigned kb10 = (unsigned)((size * 10) / 1024);
        rjust(num, kb10 / 10, 4);
        for (int i = 0; i < 4; ++i) out9[i] = num[i];
        out9[4] = '.';
        out9[5] = (char)('0' + kb10 % 10);
        out9[6] = ' '; out9[7] = 'K'; out9[8] = 'B'; out9[9] = '\0';
    }
}

static char     g_ls_filter[64];
static unsigned g_ls_file_count = 0;
static unsigned g_ls_dir_count  = 0;
static size_t   g_ls_total      = 0;

static void ls_cb(const char* name, size_t size, bool is_dir) {

    size_t flen = strlen(g_ls_filter);
    if (flen == 0) {
        for (const char* p = name; *p; ++p) if (*p == '/') return;
    } else {
        if (strncmp(name, g_ls_filter, flen) != 0 || name[flen] != '/') return;
        const char* rest = name + flen + 1;
        for (const char* p = rest; *p; ++p) if (*p == '/') return;
    }

    const char* base = basename_of(name);
    out("  ");
    if (is_dir) {
        ++g_ls_dir_count;
        out(base); out("/");
        size_t nlen = strlen(base) + 1;
        for (size_t i = nlen; i < 24; ++i) out(" ");
        out("       <DIR>\n");
    } else {
        ++g_ls_file_count;
        g_ls_total += size;
        out(base);
        size_t nlen = strlen(base);
        for (size_t i = nlen; i < 24; ++i) out(" ");
        out("  ");
        char sbuf[12]; fmt_size(sbuf, size); out(sbuf);
        out("\n");
    }
}

static void cmd_ls() {
    out("  Path: /"); out(g_cwd); out("\n");
    out("  Name                      Size\n");
    out("  ------------------------  --------\n");

    size_t cl = strlen(g_cwd);
    for (size_t i = 0; i <= cl && i < sizeof(g_ls_filter); ++i)
        g_ls_filter[i] = g_cwd[i];
    g_ls_file_count = 0;
    g_ls_dir_count  = 0;
    g_ls_total      = 0;
    vfs::ls(ls_cb);

    out("  ------------------------  --------\n");
    char nbuf[8]; char sbuf[12];
    out("  ");
    to_dec(nbuf, g_ls_dir_count);  out(nbuf);
    out(g_ls_dir_count  == 1 ? " dir,  " : " dirs,  ");
    to_dec(nbuf, g_ls_file_count); out(nbuf);
    out(g_ls_file_count == 1 ? " file" : " files");
    out("  |  total: ");
    fmt_size(sbuf, g_ls_total); out(sbuf);
    out("\n\n");
}

static char s_cat_buf[4096 + 1];

static void cmd_cat(const char* args) {
    args = skip_ws(args);
    if (args[0] == '\0') {
        out("cat: missing filename\n");
        return;
    }
    const char* path = resolve(args);
    if (!vfs::exists(path)) {
        out("cat: '"); out(args); out("': not found\n");
        return;
    }
    int n = vfs::read(path, s_cat_buf, sizeof(s_cat_buf) - 1);
    if (n < 0) {
        out("cat: read error\n");
        return;
    }
    s_cat_buf[n] = '\0';
    out(s_cat_buf);
    if (n > 0 && s_cat_buf[n - 1] != '\n')
        out("\n");
}

static void cmd_echo(const char* args) {
    args = skip_ws(args);
    out(args);
    out("\n");
}

static void cmd_clear() {
    wm::term_clear();
}

static void cmd_sync() {
    if (!blkfs::ready()) { out("sync: no disk device\n"); return; }
    if (blkfs::flush()) out("sync: OK\n");
    else out("sync: failed\n");
}

static void cmd_exit() {
    out("Shutting down...\n");
    if (blkfs::ready()) blkfs::flush();
    wm::render_dirty();

    asm volatile(
        "mov w0, #0x0008\n"
        "movk w0, #0x8400, lsl #16\n"
        "hvc #0\n"
        ::: "w0"
    );

    for (;;) asm volatile("wfi");
}

static void cmd_touch(const char* args) {
    args = skip_ws(args);
    if (!args[0]) { out("touch: missing filename\n"); return; }
    const char* path = resolve(args);
    if (!path[0])        { out("touch: invalid path\n"); return; }
    if (ramfs::exists_dir(path)) { out("touch: '"); out(args); out("': is a directory\n"); return; }
    if (!vfs::exists(path)) {
        if (vfs::write(path, nullptr, 0) < 0)
            { out("touch: failed to create '"); out(args); out("'\n"); return; }
    }

    if (blkfs::ready()) blkfs::flush();
}

static void cmd_rm(const char* args) {
    args = skip_ws(args);
    if (!args[0]) { out("rm: missing filename\n"); return; }
    const char* path = resolve(args);
    if (ramfs::exists_dir(path)) { out("rm: '"); out(args); out("': is a directory\n"); return; }
    if (!ramfs::remove(path))
        { out("rm: '"); out(args); out("': not found\n"); return; }
    if (blkfs::ready()) blkfs::flush();
}

static void cmd_mkdir(const char* args) {
    args = skip_ws(args);
    if (!args[0]) { out("mkdir: missing directory name\n"); return; }
    const char* path = resolve(args);
    if (!path[0]) { out("mkdir: invalid path\n"); return; }
    if (!vfs::mkdir(path))
        { out("mkdir: '"); out(args); out("': already exists or table full\n"); return; }
    if (blkfs::ready()) blkfs::flush();
}

static void win_fill(wm::Window* w,
                     uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh,
                     uint32_t col) {
    uint32_t* fb = w->client_fb;
    uint32_t  wd = w->w;
    uint32_t  ht = w->client_h;
    for (uint32_t y = ry; y < ry + rh && y < ht; ++y)
        for (uint32_t x = rx; x < rx + rw && x < wd; ++x)
            fb[y * wd + x] = col;
}

static void win_paint_test(wm::Window* w, int n) {
    uint32_t  wd = w->w;
    uint32_t  ht = w->client_h;
    uint32_t* fb = w->client_fb;

    for (uint32_t y = 0; y < ht; ++y)
        for (uint32_t x = 0; x < wd; ++x)
            fb[y * wd + x] = (((x >> 4) ^ (y >> 4)) & 1) ? 0x00303050u : 0x00202040u;

    win_fill(w, 4, 4, wd - 8, 8, 0x00b6599bu);

    uint32_t shift = (uint32_t)(n * 30);
    win_fill(w,  8, 20, 60, 50, 0x00c03030u + (shift & 0x1f) * 0x010000u);
    win_fill(w, 76, 20, 60, 50, 0x0030a030u + (shift & 0x1f) * 0x000100u);
    win_fill(w, 144, 20, 60, 50, 0x003030c0u + (shift & 0x1f) * 0x000001u);

    for (uint32_t x = 0; x < wd; ++x) {
        fb[0       * wd + x] = 0x00607070u;
        if (ht > 1) fb[(ht-1) * wd + x] = 0x00607070u;
    }

    for (uint32_t y = 0; y < ht; ++y) {
        fb[y * wd + 0]      = 0x00607070u;
        if (wd > 1) fb[y * wd + wd - 1] = 0x00607070u;
    }
}

static void cmd_wintest() {

    int32_t  ox = 60 + (s_test_count % 4) * 40;
    int32_t  oy = 60 + (s_test_count % 4) * 30;
    char     title[32] = "Window 0";
    title[7] = (char)('1' + (s_test_count % 9));

    wm::Window* w = wm::win_create(ox, oy, 280, 160, title);
    if (!w) {
        out("wintest: window table full (max 8)\n");
        return;
    }
    if (s_test_top >= kMaxTestWins) {
        wm::win_destroy(w);
        out("wintest: test-window stack full (winclose some first)\n");
        return;
    }
    win_paint_test(w, s_test_count);
    wm::win_mark_dirty(w);
    s_test_stack[s_test_top++] = w;
    ++s_test_count;
    out("wintest: opened '"); out(title); out("'\n");
    out("  click title bar to drag / bring to front\n");
    out("  type 'winclose' to close it\n");
}

static void cmd_winclose(const char* args) {
    if (s_test_top == 0) {
        out("winclose: no test windows open\n");
        return;
    }

    args = skip_ws(args);

    if (!args || !args[0]) {
        wm::Window* w = s_test_stack[--s_test_top];
        s_test_stack[s_test_top] = nullptr;
        wm::win_destroy(w);
        if (s_test_top == 0) s_test_count = 0;
        out("winclose: closed\n");
        return;
    }

    char target[32] = {};
    if (args[0] >= '1' && args[0] <= '9' && args[1] == '\0') {
        target[0] = 'W'; target[1] = 'i'; target[2] = 'n'; target[3] = 'd';
        target[4] = 'o'; target[5] = 'w'; target[6] = ' '; target[7] = args[0];
    } else {

        int i = 0;
        while (args[i] && i < 31) { target[i] = args[i]; ++i; }
    }

    for (int i = s_test_top - 1; i >= 0; --i) {
        if (strcmp(s_test_stack[i]->title, target) == 0) {
            wm::win_destroy(s_test_stack[i]);

            for (int j = i; j < s_test_top - 1; ++j)
                s_test_stack[j] = s_test_stack[j + 1];
            s_test_stack[--s_test_top] = nullptr;
            if (s_test_top == 0) s_test_count = 0;
            out("winclose: closed '"); out(target); out("'\n");
            return;
        }
    }

    out("winclose: no window named '"); out(target); out("'\n");
    out("  (open windows:");
    for (int i = 0; i < s_test_top; ++i) {
        out(" '"); out(s_test_stack[i]->title); out("'");
    }
    out(")\n");
}

static void cmd_edit(const char* args) {
    args = skip_ws(args);
    if (!args[0]) { out("usage: edit <file>\n"); return; }
    if (editor::active()) { out("edit: editor is already open (Ctrl+Q to close)\n"); return; }
    const char* path = resolve(args);
    if (!editor::open(path))
        out("edit: failed to open editor\n");
}

static void cmd_cd(const char* args) {
    args = skip_ws(args);

    if (!args[0] || (args[0] == '/' && args[1] == '\0')) {
        g_cwd[0] = '\0';
        return;
    }

    if (args[0] == '.' && args[1] == '.' && args[2] == '\0') {
        int last = -1;
        for (int i = 0; g_cwd[i]; ++i)
            if (g_cwd[i] == '/') last = i;
        if (last < 0) g_cwd[0] = '\0';
        else          g_cwd[last] = '\0';
        return;
    }

    const char* path = resolve(args);
    if (!ramfs::exists_dir(path))
        { out("cd: '"); out(args); out("': no such directory\n"); return; }
    size_t pl = strlen(path);
    if (pl >= sizeof(g_cwd))
        { out("cd: path too long\n"); return; }
    for (size_t i = 0; i <= pl; ++i) g_cwd[i] = path[i];
}

static void cmd_pwd() {
    out("/");
    out(g_cwd);
    out("\n");
}

}

namespace shell {

void execute(const char* line) {
    line = skip_ws(line);

    if (line[0] == '\0') return;

    char cmd[32];
    size_t ci = 0;
    while (ci < sizeof(cmd) - 1 && line[ci] && line[ci] != ' ' && line[ci] != '\t') {
        cmd[ci] = line[ci]; ++ci;
    }
    cmd[ci] = '\0';

    const char* args = skip_ws(line + ci);
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        cmd_help();
    } else if (strcmp(cmd, "ls") == 0) {
        cmd_ls();
    } else if (strcmp(cmd, "cat") == 0) {
        cmd_cat(args);
    } else if (strcmp(cmd, "echo") == 0) {
        cmd_echo(args);
    } else if (strcmp(cmd, "touch") == 0) {
        cmd_touch(args);
    } else if (strcmp(cmd, "rm") == 0) {
        cmd_rm(args);
    } else if (strcmp(cmd, "mkdir") == 0) {
        cmd_mkdir(args);
    } else if (strcmp(cmd, "cd") == 0) {
        cmd_cd(args);
    } else if (strcmp(cmd, "pwd") == 0) {
        cmd_pwd();
    } else if (strcmp(cmd, "clear") == 0) {
        cmd_clear();
    } else if (strcmp(cmd, "sync") == 0) {
        cmd_sync();
    } else if (strcmp(cmd, "edit") == 0) {
        cmd_edit(args);
    } else if (strcmp(cmd, "wintest") == 0) {
        cmd_wintest();
    } else if (strcmp(cmd, "winclose") == 0) {
        cmd_winclose(args);
    } else if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
        cmd_exit();
    } else {
        out("unknown command '");
        out(cmd);
        out("' - type 'help' to list commands\n");
    }
}

}

namespace shell {

const char* cwd() { return g_cwd; }

}
