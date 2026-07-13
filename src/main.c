#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/hidraw.h>
#include <gtk/gtk.h>

#define CORSAIR_VID 0x1b1c
#define CONFIG_FILE "macros.conf"

typedef struct {
    int fd;
    char name[256];
    char path[512];
    int interface;
    struct hidraw_devinfo info;
} DeviceInfo;

typedef struct {
    GtkWidget *window;
    GtkWidget *text_view;
    GtkTextBuffer *buffer;
    GtkWidget *entries[6];
    int verbose;
    unsigned char last_report[64];
} AppContext;

static void save_config(AppContext *ctx) {
    FILE *f = fopen(CONFIG_FILE, "w");
    if (!f) return;
    for (int i = 0; i < 6; i++) {
        const char *text = gtk_entry_get_text(GTK_ENTRY(ctx->entries[i]));
        fprintf(f, "G%d=%s\n", i + 1, text);
    }
    fclose(f);
}

static void load_config(AppContext *ctx) {
    FILE *f = fopen(CONFIG_FILE, "r");
    if (!f) return;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        if (strncmp(line, "G", 1) == 0 && line[2] == '=') {
            int idx = line[1] - '1';
            if (idx >= 0 && idx < 6) {
                gtk_entry_set_text(GTK_ENTRY(ctx->entries[idx]), line + 3);
            }
        }
    }
    fclose(f);
}

static void log_to_gui(AppContext *ctx, const char *format, ...) {
    char buf[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(ctx->buffer, &iter);
    gtk_text_buffer_insert(ctx->buffer, &iter, buf, -1);
    GtkTextMark *mark = gtk_text_buffer_get_insert(ctx->buffer);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(ctx->text_view), mark);
    printf("%s", buf);
}

static void execute_macro(AppContext *ctx, int idx) {
    const char *path = gtk_entry_get_text(GTK_ENTRY(ctx->entries[idx]));
    if (path && strlen(path) > 0) {
        log_to_gui(ctx, "G%d pressionado. Executando: %s\n", idx + 1, path);
        char cmd[1024];
        
        struct stat st;
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode) && (st.st_mode & S_IXUSR)) {
            // Se for um arquivo regular e executável, executa diretamente
            snprintf(cmd, sizeof(cmd), "\"%s\" &", path);
        } else {
            // Caso contrário (pasta ou arquivo não executável), usa xdg-open
            snprintf(cmd, sizeof(cmd), "xdg-open \"%s\" &", path);
        }
        
        if (system(cmd) == -1) {
            log_to_gui(ctx, "Erro ao tentar executar macro G%d\n", idx + 1);
        }
    } else {
        log_to_gui(ctx, "G%d pressionado\n", idx + 1);
    }
}

void process_report(unsigned char *report, int len, AppContext *ctx) {
    if (len > 64) len = 64;
    int changed = 0;
    for (int i = 0; i < len; i++) {
        if (report[i] != ctx->last_report[i]) {
            changed = 1;
            break;
        }
    }

    if (changed) {
        if (len == 64 && report[0] == 0x03) {
            unsigned char diff = report[16] ^ ctx->last_report[16];
            for (int i = 0; i < 6; i++) {
                if ((diff & (1 << i)) && (report[16] & (1 << i))) execute_macro(ctx, i);
            }
        } else if (len == 64 && report[0] == 0x01) {
            unsigned char diff = report[1] ^ ctx->last_report[1];
            for (int i = 0; i < 6; i++) {
                if ((diff & (1 << i)) && (report[1] & (1 << i))) execute_macro(ctx, i);
            }
        }
        memcpy(ctx->last_report, report, len);
    }
}

static gboolean on_hid_data(GIOChannel *source, GIOCondition condition, gpointer data) {
    AppContext *ctx = (AppContext *)data;
    unsigned char buf[64];
    int fd = g_io_channel_unix_get_fd(source);
    if (condition & G_IO_IN) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0) process_report(buf, (int)n, ctx);
    }
    return TRUE;
}

static void on_entry_changed(GtkEditable *editable, gpointer data) {
    save_config((AppContext *)data);
}

static void on_browse_clicked(GtkButton *button, gpointer data) {
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "idx"));
    AppContext *ctx = (AppContext *)data;
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Selecionar Arquivo ou Pasta",
                                                  GTK_WINDOW(ctx->window),
                                                  GTK_FILE_CHOOSER_ACTION_OPEN,
                                                  "Cancelar", GTK_RESPONSE_CANCEL,
                                                  "Abrir", GTK_RESPONSE_ACCEPT,
                                                  NULL);
    // Permitir selecionar pastas também
    GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
    gtk_file_chooser_set_select_multiple(chooser, FALSE);
    
    // Adicionar opção para selecionar pastas
    GtkWidget *folder_button = gtk_check_button_new_with_label("Selecionar Pasta");
    gtk_file_chooser_set_extra_widget(chooser, folder_button);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(chooser);
        gtk_entry_set_text(GTK_ENTRY(ctx->entries[idx]), filename);
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

// Helper: Interface detection similar to previous steps
static int get_interface_number(const char *hidraw_path) {
    char sys_path[1024];
    char buf[32];
    const char *name = strrchr(hidraw_path, '/');
    if (!name) return -1;
    name++;
    snprintf(sys_path, sizeof(sys_path), "/sys/class/hidraw/%s/device/../bInterfaceNumber", name);
    int fd = open(sys_path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';
    return (int)strtol(buf, NULL, 16);
}

static int find_corsair_devices(DeviceInfo *devices, int max) {
    DIR *dir = opendir("/dev");
    if (!dir) return -1;
    struct dirent *entry;
    char filepath[512];
    int count = 0;
    while ((entry = readdir(dir)) != NULL && count < max) {
        if (strncmp(entry->d_name, "hidraw", 6) != 0) continue;
        snprintf(filepath, sizeof(filepath), "/dev/%s", entry->d_name);
        int fd = open(filepath, O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;
        struct hidraw_devinfo info;
        if (ioctl(fd, HIDIOCGRAWINFO, &info) < 0) { close(fd); continue; }
        if (info.vendor == CORSAIR_VID) {
            devices[count].fd = fd;
            devices[count].info = info;
            strncpy(devices[count].path, filepath, sizeof(devices[count].path) - 1);
            devices[count].interface = get_interface_number(filepath);
            ioctl(fd, HIDIOCGRAWNAME(sizeof(devices[count].name)), devices[count].name);
            count++;
        } else close(fd);
    }
    closedir(dir);
    return count;
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    AppContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    
    const char *device_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) device_path = argv[++i];
    }

    DeviceInfo devices[32];
    int dev_count = find_corsair_devices(devices, 32);
    int fd = -1;
    if (device_path) fd = open(device_path, O_RDWR | O_NONBLOCK);
    else if (dev_count > 0) {
        int best_idx = 0;
        for (int i = 0; i < dev_count; i++) if (devices[i].interface == 0) { best_idx = i; break; }
        fd = devices[best_idx].fd;
        for (int i = 0; i < dev_count; i++) if (i != best_idx) close(devices[i].fd);
    }

    if (fd < 0) return 1;

    ctx.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ctx.window), "Corsair Macro Mapper");
    gtk_window_set_default_size(GTK_WINDOW(ctx.window), 700, 500);
    gtk_window_set_resizable(GTK_WINDOW(ctx.window), FALSE);
    g_signal_connect(ctx.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(ctx.window), main_vbox);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(main_vbox), scrolled, TRUE, TRUE, 0);
    ctx.text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(ctx.text_view), FALSE);
    ctx.buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ctx.text_view));
    gtk_container_add(GTK_CONTAINER(scrolled), ctx.text_view);

    // Terminal styling (Black background, Green text)
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        "textview text {\n"
        "  background-color: black;\n"
        "  color: #00FF00;\n"
        "  font-family: monospace;\n"
        "}\n", -1, NULL);
    GtkStyleContext *context = gtk_widget_get_style_context(ctx.text_view);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_box_pack_start(GTK_BOX(main_vbox), grid, FALSE, FALSE, 10);

    for (int i = 0; i < 6; i++) {
        char label_text[8];
        snprintf(label_text, sizeof(label_text), "G%d:", i + 1);
        GtkWidget *label = gtk_label_new(label_text);
        gtk_grid_attach(GTK_GRID(grid), label, 0, i, 1, 1);

        ctx.entries[i] = gtk_entry_new();
        gtk_widget_set_hexpand(ctx.entries[i], TRUE);
        gtk_grid_attach(GTK_GRID(grid), ctx.entries[i], 1, i, 1, 1);
        g_signal_connect(ctx.entries[i], "changed", G_CALLBACK(on_entry_changed), &ctx);

        GtkWidget *btn = gtk_button_new_with_label("...");
        g_object_set_data(G_OBJECT(btn), "idx", GINT_TO_POINTER(i));
        g_signal_connect(btn, "clicked", G_CALLBACK(on_browse_clicked), &ctx);
        gtk_grid_attach(GTK_GRID(grid), btn, 2, i, 1, 1);
    }

    load_config(&ctx);

    GIOChannel *channel = g_io_channel_unix_new(fd);
    g_io_channel_set_encoding(channel, NULL, NULL);
    g_io_channel_set_buffered(channel, FALSE);
    g_io_add_watch(channel, G_IO_IN, on_hid_data, &ctx);

    gtk_widget_show_all(ctx.window);
    gtk_main();
    close(fd);
    return 0;
}
