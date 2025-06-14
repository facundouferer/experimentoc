#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ncurses.h>
#include <time.h>
#include <mach/mach.h>
#include <mach/vm_statistics.h>
#include <mach/mach_types.h>
#include <mach/mach_init.h>
#include <mach/mach_host.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <mach/processor_info.h>
#include <mach/mach_host.h>
#include <sys/time.h>

// Macros para MIN y MAX
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

// Estructura para almacenar información de memoria
typedef struct {
    unsigned long long total_ram;
    unsigned long long free_ram;
    unsigned long long used_ram;
    unsigned long long inactive_ram;
    unsigned long long wired_ram;
    unsigned long long compressed_ram;
    double ram_percentage;
    unsigned long long swap_used;
    unsigned long long swap_total;
    double swap_percentage;
} MemoryInfo;

// Función para obtener información de memoria en macOS
int get_memory_info(MemoryInfo *mem_info) {
    mach_port_t mach_port = mach_host_self();
    vm_size_t page_size;
    vm_statistics64_data_t vm_stat;
    mach_msg_type_number_t host_size = sizeof(vm_statistics64_data_t) / sizeof(natural_t);
    
    // Obtener el tamaño de página
    if (host_page_size(mach_port, &page_size) != KERN_SUCCESS) {
        return -1;
    }
    
    // Obtener estadísticas de memoria virtual
    if (host_statistics64(mach_port, HOST_VM_INFO64, (host_info64_t)&vm_stat, &host_size) != KERN_SUCCESS) {
        return -1;
    }
    
    // Obtener memoria total del sistema
    int mib[2];
    size_t length;
    uint64_t total_memory;
    
    mib[0] = CTL_HW;
    mib[1] = HW_MEMSIZE;
    length = sizeof(uint64_t);
    
    if (sysctl(mib, 2, &total_memory, &length, NULL, 0) != 0) {
        return -1;
    }
    
    mem_info->total_ram = total_memory;
    mem_info->free_ram = (unsigned long long)vm_stat.free_count * page_size;
    mem_info->inactive_ram = (unsigned long long)vm_stat.inactive_count * page_size;
    mem_info->wired_ram = (unsigned long long)vm_stat.wire_count * page_size;
    mem_info->compressed_ram = (unsigned long long)vm_stat.compressor_page_count * page_size;
    
    // Calcular memoria usada (activa + wired + compressed)
    mem_info->used_ram = ((unsigned long long)vm_stat.active_count + 
                         (unsigned long long)vm_stat.wire_count + 
                         (unsigned long long)vm_stat.compressor_page_count) * page_size;
    
    mem_info->ram_percentage = (double)mem_info->used_ram / mem_info->total_ram * 100.0;
    
    // Obtener información de swap (más complejo en macOS)
    struct xsw_usage vmusage;
    size_t size = sizeof(vmusage);
    if (sysctlbyname("vm.swapusage", &vmusage, &size, NULL, 0) == 0) {
        mem_info->swap_total = vmusage.xsu_total;
        mem_info->swap_used = vmusage.xsu_used;
        mem_info->swap_percentage = mem_info->swap_total > 0 ? 
                                   (double)mem_info->swap_used / mem_info->swap_total * 100.0 : 0.0;
    } else {
        mem_info->swap_total = 0;
        mem_info->swap_used = 0;
        mem_info->swap_percentage = 0.0;
    }
    
    return 0;
}

// Función para convertir bytes a formato legible
void format_bytes(unsigned long long bytes, char *buffer) {
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double size = bytes;
    
    while (size >= 1024 && unit < 4) {
        size /= 1024;
        unit++;
    }
    
    sprintf(buffer, "%.2f %s", size, units[unit]);
}

// Función para dibujar una barra de progreso
void draw_progress_bar(int y, int x, int width, double percentage, const char *label) {
    int filled = (int)(percentage * width / 100);
    
    mvprintw(y, x, "%s: [", label);

    // Determinar color basado en el porcentaje
    int active_color_pair = 1; // Verde por defecto
    if (has_colors()) {
        if (percentage > 80) {
            active_color_pair = 3; // Rojo para uso alto
        } else if (percentage > 60) {
            active_color_pair = 2; // Amarillo para uso medio
        }
        attron(COLOR_PAIR(active_color_pair));
    }

    // Dibujar la parte llena de la barra
    for (int i = 0; i < filled; i++) {
        mvaddch(y, x + strlen(label) + 3 + i, ACS_BLOCK); // Usar ACS_BLOCK para un relleno más sólido
    }
    
    if (has_colors()) {
        attroff(COLOR_PAIR(active_color_pair));
    }
    
    for (int i = filled; i < width; i++) {
        mvaddch(y, x + strlen(label) + 3 + i, '-');
    }
    
    printw("] %.1f%%", percentage); // printw continúa desde la posición actual del cursor
}

// Función para dibujar un gráfico de uso de memoria tipo Ecualizador
void draw_memory_graph(int start_y, int start_x, MemoryInfo *history_data, int capacity, int oldest_buffer_idx, int current_data_count) {
    int graph_height = 10;
    int graph_on_screen_width = COLS - start_x - 2; // Ancho disponible, -1 para eje Y, -1 para margen derecho
    if (graph_on_screen_width < 1) graph_on_screen_width = 1;

    int effective_num_points_to_draw = MIN(current_data_count, graph_on_screen_width);
    if (effective_num_points_to_draw <= 0) return;

    mvprintw(start_y - 1, start_x, "Historial RAM (Ecualizador - %d segs):", effective_num_points_to_draw);

    // Dibujar Eje Y
    for (int y_offset = 0; y_offset < graph_height; ++y_offset) {
        mvaddch(start_y + y_offset, start_x - 1, ACS_VLINE);
    }
    // Dibujar Eje X
    for (int x_offset = 0; x_offset < effective_num_points_to_draw; ++x_offset) {
        mvaddch(start_y + graph_height, start_x + x_offset, ACS_HLINE);
    }
    mvaddch(start_y + graph_height, start_x - 1, ACS_LLCORNER); // Esquina

    double max_ram_percentage = 100.0;

    // Determinar el índice del primer dato a leer del buffer para el gráfico
    int first_buffer_idx_for_render = (oldest_buffer_idx + (current_data_count - effective_num_points_to_draw) + capacity) % capacity;

    for (int screen_col_idx = 0; screen_col_idx < effective_num_points_to_draw; ++screen_col_idx) {
        int actual_history_buffer_idx = (first_buffer_idx_for_render + screen_col_idx) % capacity;
        double current_percentage = history_data[actual_history_buffer_idx].ram_percentage;

        if (history_data[actual_history_buffer_idx].total_ram == 0 && current_percentage == 0.0) {
            current_percentage = 0.0; // Dato no inicializado, tratar como 0%
        }

        int plot_x = start_x + screen_col_idx;
        int bar_pixel_height = (int)(current_percentage / max_ram_percentage * graph_height);
        bar_pixel_height = MAX(0, MIN(graph_height, bar_pixel_height)); // Asegurar que esté dentro de los límites 0..graph_height

        // Dibujar la barra vertical para la columna actual
        for (int h_row_in_graph = 0; h_row_in_graph < graph_height; ++h_row_in_graph) {
            // h_row_in_graph = 0 es la fila superior del área del gráfico
            // h_row_in_graph = graph_height - 1 es la fila inferior del área del gráfico
            int current_screen_y = start_y + h_row_in_graph;

            // La barra se llena desde abajo hacia arriba.
            // Si h_row_in_graph es mayor o igual a (graph_height - bar_pixel_height), está dentro de la barra.
            if (h_row_in_graph >= (graph_height - bar_pixel_height)) {
                 mvaddch(current_screen_y, plot_x, ACS_BLOCK); // Carácter para la barra
            } else {
                 mvaddch(current_screen_y, plot_x, ' '); // Espacio vacío encima de la barra
            }
        }
    }
}

// Función para obtener el número de CPUs
int get_cpu_count() {
    int cpu_count;
    size_t size = sizeof(cpu_count);
    if (sysctlbyname("hw.ncpu", &cpu_count, &size, NULL, 0) == 0) {
        return cpu_count;
    }
    return 1;
}

// Función para obtener el uptime del sistema
double get_uptime() {
    struct timeval boottime;
    size_t size = sizeof(boottime);
    if (sysctlbyname("kern.boottime", &boottime, &size, NULL, 0) == 0) {
        time_t now;
        time(&now);
        return (double)(now - boottime.tv_sec) / 3600.0; // En horas
    }
    return 0.0;
}

#define HISTORY_CAPACITY 60
#define CPU_HISTORY_CAPACITY 60
#define CPU_HEATMAP_WIDTH 12

// Función para obtener el uso de CPU (promedio de todos los núcleos)
double get_cpu_usage() {
    static uint64_t last_total = 0, last_idle = 0;
    host_cpu_load_info_data_t cpuinfo;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, (host_info_t)&cpuinfo, &count) != KERN_SUCCESS) {
        return 0.0;
    }
    uint64_t total = 0;
    for (int i = 0; i < CPU_STATE_MAX; i++) total += cpuinfo.cpu_ticks[i];
    uint64_t idle = cpuinfo.cpu_ticks[CPU_STATE_IDLE];

    double usage = 0.0;
    if (last_total != 0) {
        uint64_t diff_total = total - last_total;
        uint64_t diff_idle = idle - last_idle;
        if (diff_total > 0) {
            usage = 100.0 * (diff_total - diff_idle) / diff_total;
        }
    }
    last_total = total;
    last_idle = idle;
    return usage;
}

// Dibuja histograma de memoria con barras ▓ rojas y coordenadas verdes
void draw_memory_histogram(int start_y, int start_x, MemoryInfo *history_data, int capacity, int oldest_buffer_idx, int current_data_count) {
    int graph_height = 10;
    int graph_width = MIN(current_data_count, COLS - start_x - 10);
    if (graph_width <= 0) return;

    mvprintw(start_y - 1, start_x, "Histograma RAM (%%):");

    // Eje Y y coordenadas verdes
    for (int y = 0; y < graph_height; ++y) {
        if (has_colors()) attron(COLOR_PAIR(5));
        mvprintw(start_y + y, start_x - 5, "%3d%%", (graph_height - y) * 10);
        if (has_colors()) attroff(COLOR_PAIR(5));
    }

    // Barras ▓ rojas
    for (int i = 0; i < graph_width; ++i) {
        int idx = (oldest_buffer_idx + (current_data_count - graph_width) + i + capacity) % capacity;
        double perc = history_data[idx].ram_percentage;
        int bar_height = (int)(perc / 100.0 * graph_height);
        for (int y = 0; y < graph_height; ++y) {
            if (y >= graph_height - bar_height) {
                if (has_colors()) attron(COLOR_PAIR(3));
                mvaddch(start_y + y, start_x + i, 'X');
                if (has_colors()) attroff(COLOR_PAIR(3));
            } else {
                mvaddch(start_y + y, start_x + i, ' ');
            }
        }
    }
}

// Dibuja mapa de calor de CPU
void draw_cpu_heatmap(int y, int x, double *cpu_history, int count) {
    mvprintw(y - 1, x, "CPU Heatmap (últimos %d segs):", count);
    for (int i = 0; i < count; ++i) {
        double usage = cpu_history[i];
        char symbol = ' ';
        int color = 1;
        if (usage < 40) {
            symbol = ' ';
            color = 1; // Verde
        } else if (usage < 75) {
            symbol = '#';
            color = 2; // Amarillo
        } else {
            symbol = '@';
            color = 3; // Rojo
        }
        if (has_colors()) attron(COLOR_PAIR(color));
        printw("[%c]", symbol);
        if (has_colors()) attroff(COLOR_PAIR(color));
    }
}

// --- AGREGADOS PARA TEMPERATURA Y RED ---
// Obtener temperatura del CPU usando powermetrics (requiere sudo)
double get_cpu_temperature() {
    FILE *fp = popen("sudo powermetrics --samplers smc | grep -m1 'CPU die temperature' | awk '{print $4}'", "r");
    if (!fp) return -1;
    double temp = -1;
    fscanf(fp, "%lf", &temp);
    pclose(fp);
    return temp;
}

// Obtener temperatura del GPU usando powermetrics (requiere sudo, puede no estar disponible)
double get_gpu_temperature() {
    FILE *fp = popen("sudo powermetrics --samplers smc | grep -m1 'GPU die temperature' | awk '{print $4}'", "r");
    if (!fp) return -1;
    double temp = -1;
    fscanf(fp, "%lf", &temp);
    pclose(fp);
    return temp;
}

// Estructura para almacenar datos de red
typedef struct {
    unsigned long long rx_bytes;
    unsigned long long tx_bytes;
} NetStats;

// Obtener bytes de red usando netstat
void get_net_stats(NetStats *stats) {
    FILE *fp = popen("netstat -ib | awk 'NR>1 && $1!=\"lo0\" {rx+=$7; tx+=$10} END {print rx, tx}'", "r");
    if (!fp) {
        stats->rx_bytes = 0;
        stats->tx_bytes = 0;
        return;
    }
    fscanf(fp, "%llu %llu", &stats->rx_bytes, &stats->tx_bytes);
    pclose(fp);
}

// Dibuja barra de red
void draw_network_bar(int y, int x, double value, double max_value, const char *label, int color_pair) {
    int width = 30;
    int filled = (int)((value / max_value) * width);
    if (filled > width) filled = width;
    mvprintw(y, x, "%s: ", label);
    if (has_colors()) attron(COLOR_PAIR(color_pair));
    for (int i = 0; i < filled; ++i) addch('#');
    if (has_colors()) attroff(COLOR_PAIR(color_pair));
    for (int i = filled; i < width; ++i) addch(' ');
}

double cached_cpu_temp = -1;
time_t last_temp_check = 0;

double get_cpu_temperature_cached() {
    time_t now = time(NULL);
    if (now - last_temp_check > 30 || cached_cpu_temp < 0) { // Actualiza cada 30 segundos
        FILE *fp = popen("sudo powermetrics --samplers smc -n1 2>/dev/null | grep -m1 'CPU die temperature' | awk '{print $4}'", "r");
        if (fp) {
            double temp = -1;
            fscanf(fp, "%lf", &temp);
            pclose(fp);
            if (temp > 0) cached_cpu_temp = temp;
        }
        last_temp_check = now;
    }
    return cached_cpu_temp;
}

// --- PROCESOS ACTIVOS ---

typedef struct {
    int total;
    int system;
    int user;
    int background;
} ProcessStats;

// Obtiene el número de procesos activos y los clasifica
void get_process_stats(ProcessStats *stats) {
    FILE *fp = popen("ps -axo user,stat | awk 'NR>1 {if($2 ~ /S/) bg++; else if($1==\"root\") sys++; else usr++;} END {print sys,usr,bg,sys+usr+bg}'", "r");
    if (!fp) {
        stats->system = stats->user = stats->background = stats->total = 0;
        return;
    }
    fscanf(fp, "%d %d %d %d", &stats->system, &stats->user, &stats->background, &stats->total);
    pclose(fp);
}

// Dibuja barras para procesos
void draw_process_bars(int y, int x, ProcessStats *stats) {
    int maxval = stats->system;
    if (stats->user > maxval) maxval = stats->user;
    if (stats->background > maxval) maxval = stats->background;
    int width = 20;
    #define PROC_BAR(val) (maxval>0 ? (val)*width/maxval : 0)
    mvprintw(y, x,   "Procesos:");
    mvprintw(y+1, x, "Sistema: ");
    for (int i=0; i<PROC_BAR(stats->system); ++i) addch(ACS_CKBOARD);
    printw("  %d", stats->system);
    mvprintw(y+2, x, "Usuario: ");
    for (int i=0; i<PROC_BAR(stats->user); ++i) addch(ACS_CKBOARD);
    printw("  %d", stats->user);
    mvprintw(y+3, x, "Fondo:   ");
    for (int i=0; i<PROC_BAR(stats->background); ++i) addch(ACS_CKBOARD);
    printw("  %d", stats->background);
    mvprintw(y+4, x, "Total:   %d", stats->total);
}

// --- ESPACIO DE DISCO ---

typedef struct {
    unsigned long long total;
    unsigned long long used;
    unsigned long long free;
    double percent_used;
} DiskStats;

// Obtiene espacio de disco para "/"
void get_disk_stats(DiskStats *stats) {
    struct statfs sfs;
    if (statfs("/", &sfs) == 0) {
        stats->total = (unsigned long long)sfs.f_blocks * sfs.f_bsize;
        stats->free  = (unsigned long long)sfs.f_bfree  * sfs.f_bsize;
        stats->used  = stats->total - stats->free;
        stats->percent_used = stats->total > 0 ? (double)stats->used / stats->total * 100.0 : 0.0;
    } else {
        stats->total = stats->used = stats->free = stats->percent_used = 0;
    }
}

// Barra horizontal de disco
void draw_disk_bar(int y, int x, DiskStats *stats) {
    char used_str[32], total_str[32];
    format_bytes(stats->used, used_str);
    format_bytes(stats->total, total_str);
    int width = 32;
    int filled = (int)(stats->percent_used * width / 100.0);
    mvprintw(y, x, "Disco /: [");
    if (has_colors()) attron(COLOR_PAIR(3));
    for (int i=0; i<filled; ++i) addch(ACS_CKBOARD);
    if (has_colors()) attroff(COLOR_PAIR(3));
    for (int i=filled; i<width; ++i) addch(' ');
    printw("] %.0f%% usado (%s de %s)", stats->percent_used, used_str, total_str);
}

int main() {
    // Inicializar ncurses
    initscr();
    cbreak();
    noecho();
    nodelay(stdscr, TRUE);
    curs_set(0);

    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_GREEN, COLOR_BLACK);   // Verde para uso bajo
        init_pair(2, COLOR_YELLOW, COLOR_BLACK);  // Amarillo para uso medio
        init_pair(3, COLOR_RED, COLOR_BLACK);     // Rojo para uso alto
        init_pair(4, COLOR_CYAN, COLOR_BLACK);    // Cian para títulos
        init_pair(5, COLOR_GREEN, COLOR_BLACK);   // Verde para coordenadas
    }

    MemoryInfo current_info;
    MemoryInfo history[HISTORY_CAPACITY] = {0};
    int history_write_idx = 0;
    int history_data_count = 0;

    double cpu_history[CPU_HISTORY_CAPACITY] = {0};
    int cpu_history_idx = 0;
    int cpu_history_count = 0;

    char total_ram_str[32], used_ram_str[32], free_ram_str[32];
    char inactive_ram_str[32], wired_ram_str[32], compressed_ram_str[32];
    char swap_total_str[32], swap_used_str[32];

    NetStats prev_stats = {0, 0}, curr_stats = {0, 0};
    struct timeval prev_time, curr_time;
    get_net_stats(&prev_stats);
    gettimeofday(&prev_time, NULL);

    double net_down = 0, net_up = 0, net_down_max = 1, net_up_max = 1;

    ProcessStats proc_stats;
    DiskStats disk_stats;

    while (1) {
        clear();

        if (get_memory_info(&current_info) != 0) {
            mvprintw(0, 0, "Error al obtener información de memoria");
            refresh();
            sleep(1);
            continue;
        }

        // Actualizar historial de memoria
        history[history_write_idx] = current_info;
        history_write_idx = (history_write_idx + 1) % HISTORY_CAPACITY;
        if (history_data_count < HISTORY_CAPACITY) history_data_count++;

        // Actualizar historial de CPU
        double cpu_usage = get_cpu_usage();
        cpu_history[cpu_history_idx] = cpu_usage;
        cpu_history_idx = (cpu_history_idx + 1) % CPU_HISTORY_CAPACITY;
        if (cpu_history_count < CPU_HISTORY_CAPACITY) cpu_history_count++;

        format_bytes(current_info.total_ram, total_ram_str);
        format_bytes(current_info.used_ram, used_ram_str);
        format_bytes(current_info.free_ram, free_ram_str);
        format_bytes(current_info.inactive_ram, inactive_ram_str);
        format_bytes(current_info.wired_ram, wired_ram_str);
        format_bytes(current_info.compressed_ram, compressed_ram_str);
        format_bytes(current_info.swap_total, swap_total_str);
        format_bytes(current_info.swap_used, swap_used_str);

        time_t now = time(NULL);
        char *time_str = ctime(&now);
        time_str[strlen(time_str) - 1] = '\0';

        // --- CABECERA ---
        if (has_colors()) attron(COLOR_PAIR(4));
        attron(A_BOLD);
        mvprintw(0, 0, "=== MONITOR DE MEMORIA macOS ===");
        mvprintw(1, 0, "Actualizado: %s", time_str);
        attroff(A_BOLD);
        if (has_colors()) attroff(COLOR_PAIR(4));

        // --- RAM ---
        mvprintw(3, 0, "MEMORIA RAM:");
        mvprintw(4, 2, "Total:      %s", total_ram_str);
        mvprintw(5, 2, "Usada:      %s", used_ram_str);
        mvprintw(6, 2, "Libre:      %s", free_ram_str);
        mvprintw(7, 2, "Inactiva:   %s", inactive_ram_str);
        mvprintw(8, 2, "Wired:      %s", wired_ram_str);
        mvprintw(9, 2, "Comprimida: %s", compressed_ram_str);

        // --- TEMPERATURA CPU ---
        double cpu_temp = get_cpu_temperature_cached();
        mvprintw(3, 40, "Temperatura CPU: %s", (cpu_temp > 0) ? "" : "N/D");
        if (cpu_temp > 0)
            mvprintw(4, 40, "%.1f°C", cpu_temp);

        // --- PROCESOS ---
        get_process_stats(&proc_stats);
        mvprintw(3, 70, "Procesos:");
        mvprintw(4, 70, "  Sistema:   %d", proc_stats.system);
        mvprintw(5, 70, "  Usuario: ");
        int bar_user = proc_stats.user > 0 ? proc_stats.user * 18 / MAX(1, MAX(proc_stats.user, MAX(proc_stats.system, proc_stats.background))) : 0;
        for (int i = 0; i < bar_user; ++i) mvaddch(5, 81 + i, ACS_CKBOARD);
        printw("  %d", proc_stats.user);
        mvprintw(6, 70, "  Fondo:   ");
        int bar_bg = proc_stats.background > 0 ? proc_stats.background * 18 / MAX(1, MAX(proc_stats.user, MAX(proc_stats.system, proc_stats.background))) : 0;
        for (int i = 0; i < bar_bg; ++i) mvaddch(6, 81 + i, ACS_CKBOARD);
        printw("  %d", proc_stats.background);
        mvprintw(7, 70, "  Total:    %d", proc_stats.total);

        // --- RED ---
        get_net_stats(&curr_stats);
        gettimeofday(&curr_time, NULL);
        double elapsed = (curr_time.tv_sec - prev_time.tv_sec) + (curr_time.tv_usec - prev_time.tv_usec) / 1e6;
        if (elapsed > 0) {
            net_down = (curr_stats.rx_bytes - prev_stats.rx_bytes) * 8.0 / (elapsed * 1024 * 1024); // Mb/s
            net_up   = (curr_stats.tx_bytes - prev_stats.tx_bytes) * 8.0 / (elapsed * 1024 * 1024); // Mb/s
            if (net_down > net_down_max) net_down_max = net_down;
            if (net_up > net_up_max) net_up_max = net_up;
        }
        prev_stats = curr_stats;
        prev_time = curr_time;

        mvprintw(11, 0, "Red:");
        mvprintw(12, 2, "+ Download: %.2f Mb/s", net_down);
        mvprintw(13, 2, "- Upload  : %.2f Mb/s", net_up);

        // --- DISCO ---
        get_disk_stats(&disk_stats);
        mvprintw(15, 0, "Disco /:");
        draw_disk_bar(16, 2, &disk_stats);

        // --- SWAP ---
        mvprintw(18, 0, "MEMORIA SWAP:");
        mvprintw(19, 2, "Total: %s", swap_total_str);
        mvprintw(20, 2, "Usada: %s", swap_used_str);
        draw_progress_bar(21, 2, 40, current_info.swap_percentage, "SWAP");

        // --- HISTOGRAMA RAM ---
        int graph_start_y = 23;
        int graph_min_lines_needed = graph_start_y + 10 + 2;
        if (LINES >= graph_min_lines_needed) {
            int oldest_idx_in_buffer = (history_data_count < HISTORY_CAPACITY) ? 0 : history_write_idx;
            draw_memory_histogram(graph_start_y, 10, history, HISTORY_CAPACITY, oldest_idx_in_buffer, history_data_count);
        }

        // --- HEATMAP CPU ---
        if (LINES > graph_min_lines_needed + 3) {
            int cpu_y = graph_start_y + 12;
            int oldest_cpu_idx = (cpu_history_count < CPU_HISTORY_CAPACITY) ? 0 : cpu_history_idx;
            double cpu_heatmap[CPU_HEATMAP_WIDTH];
            int points = MIN(cpu_history_count, CPU_HEATMAP_WIDTH);
            for (int i = 0; i < points; ++i) {
                int idx = (oldest_cpu_idx + (cpu_history_count - points) + i + CPU_HISTORY_CAPACITY) % CPU_HISTORY_CAPACITY;
                cpu_heatmap[i] = cpu_history[idx];
            }
            draw_cpu_heatmap(cpu_y, 10, cpu_heatmap, points);
        }

        // --- INFO SISTEMA ---
        mvprintw(LINES - 5, 0, "=== INFORMACIÓN DEL SISTEMA ===");
        mvprintw(LINES - 4, 0, "CPUs: %d", get_cpu_count());
        mvprintw(LINES - 3, 0, "Uptime: %.2f horas", get_uptime());

        mvprintw(LINES - 1, 0, "Presiona 'q' para salir, 'r' para reiniciar historial");

        refresh();

        int ch = getch();
        if (ch == 'q' || ch == 'Q') break;
        else if (ch == 'r' || ch == 'R') {
            memset(history, 0, sizeof(history));
            history_write_idx = 0;
            history_data_count = 0;
            memset(cpu_history, 0, sizeof(cpu_history));
            cpu_history_idx = 0;
            cpu_history_count = 0;
        }

        sleep(1);
    }

    endwin();
    printf("Monitor de memoria finalizado.\n");
    return 0;
}