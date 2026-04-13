/*

CSC360 - p2 - MTS

Cole Buchinski
V01001066
2025.10.31

*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>


#define MAX_TRAINS 1024

typedef struct {
    int id;                     // train no.
    char direction;             // 'E', 'e', 'W', 'w'
    int priority;               // 1 for high, 0 for low
    int loading_time;           // in tenths of second
    int crossing_time;          // in tenths of second
    int expected_ready_tick;
    int enqueued;
    struct timeval ready_time;  // record when loading completes
    pthread_t thread_id;        // associated thread
} Train;

Train *g_trains = NULL;
int g_count = 0;

pthread_mutex_t track_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t track_cond = PTHREAD_COND_INITIALIZER;

int track_in_use = 0;       // 0: free, 1: occupied
char last_direction = 'X';  // inital priority is west
int consec_same_dir = 0;    // how many consecutive trains in same direction

int east_ready[MAX_TRAINS];
int west_ready[MAX_TRAINS];
int east_count = 0;
int west_count = 0;

struct timeval sim_start;

Train* read_file(const char *filename, int *count) {
    FILE *file = fopen(filename, "r");

    if (!file) {
        perror("Error opening file");
        *count = 0;
        return NULL;
    }

    // count lines
    int lines = 0;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), file)) {
        if (strlen(buffer) > 1) { // skip empty lines
            lines++;
        }
    }

    // allocate array
    Train *trains = malloc(lines * sizeof(Train));
    if (!trains) {
        perror("Memory allocation failed");
        fclose(file);
        *count = 0;
        return NULL;
    }

    rewind(file);

    // fill array
    int i = 0;
    while (fgets(buffer, sizeof(buffer), file)) {

        char dir;
        int load, cross;


        if (sscanf(buffer, " %c %d %d", &dir, &load, &cross) == 3) {
            trains[i].id = i;
            trains[i].direction = dir;
            trains[i].loading_time = load;
            trains[i].crossing_time = cross;
            trains[i].priority = (dir == 'E' || dir == 'W') ? 1 : 0;
            trains[i].expected_ready_tick = load;
            trains[i].enqueued = 0;
            i++;
        }
    }

    fclose(file);
    *count = i;
    return trains;

}

int current_tick_tenths() {
    struct timeval now;
    gettimeofday(&now, NULL);
    long secs = now.tv_sec - sim_start.tv_sec;
    long usec = now.tv_usec - sim_start.tv_usec;
    if (usec < 0) {
        secs -= 1;
        usec += 1000000;
    }
    long tenths = secs * 10 + (usec / 100000);
    return (int)tenths;

}
// print elapsed timestamp in "hh:mm:ss.t"
void print_time_prefix(FILE *fp) {
    struct timeval now;
    gettimeofday(&now, NULL);
    long secs = now.tv_sec - sim_start.tv_sec;
    long usec = now.tv_usec - sim_start.tv_usec;
  
    if (usec < 0) {
        secs -= 1;
        usec += 1000000;
    }

    double elapsed = (now.tv_sec - sim_start.tv_sec) + (now.tv_usec - sim_start.tv_usec) / 1000000.0;

    long hours = (long)(elapsed / 3600);
    long minutes = (long)((elapsed - hours * 3600) / 60);
    long seconds = (long)elapsed % 60;
    long tenths = (long)((elapsed - (long)elapsed) * 10); // round to nearest tenth

    fprintf(fp, "%02ld:%02ld:%02ld.%1ld ", hours, minutes, seconds, tenths);
}

// print wrapper

FILE *output_fp = NULL;

void ts_printf(const char *fmt, ...) {
    va_list ap;
    pthread_mutex_lock(&print_mutex);

    print_time_prefix(stdout);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    printf("\n");
    fflush(stdout);
    va_end(ap);

    if (output_fp) {
        print_time_prefix(output_fp);
        va_start(ap, fmt);
        vfprintf(output_fp, fmt, ap);
        fprintf(output_fp, "\n");
        fflush(output_fp);
        va_end(ap);
    }
    pthread_mutex_unlock(&print_mutex);
}

int find_best_in_dir(int *queue, int qcount, int require_high) {
    int best_idx = -1;
    for (int i = 0; i < qcount; i++) {
        int idx = queue[i];
        // skip trains that dont match priority
        if (require_high && g_trains[idx].priority == 0) {
            continue;
        }
        if (best_idx == -1) {
            best_idx = idx;
        }
        else {
            Train *a = &g_trains[idx];
            Train *b = &g_trains[best_idx];
            if (a->expected_ready_tick < b->expected_ready_tick) {
                best_idx = idx;
            }
            else if (a->expected_ready_tick == b->expected_ready_tick && a->id < b->id) {
                best_idx = idx;
            }
        }
    }
    return best_idx;
}

// determine whether train t can go
int is_my_turn(int tidx) { 
    Train *t = &g_trains[tidx]; 
    int now_tick = current_tick_tenths(); 
    
    for (int j = 0; j < g_count; j++) { 
        if (!g_trains[j].enqueued && g_trains[j].expected_ready_tick <= now_tick) { 
            return 0; 
        } 
    } 
        
    if (track_in_use) return 0; 
    
    int found_high = 0; 
    
    if (consec_same_dir < 2) {
        for (int i = 0; i < east_count; i++) { 
            if (g_trains[east_ready[i]].priority == 1) { 
                found_high = 1; 
                break; 
            } 
        } 
    }
    
    if (!found_high) 
    { 
        for (int i = 0; i < west_count; i++) 
        { 
            if (g_trains[west_ready[i]].priority == 1) 
            { 
                found_high = 1; break; 
            } 
        } 
    } 
    
    int best_east = find_best_in_dir(east_ready, east_count, found_high); 
    int best_west = find_best_in_dir(west_ready, west_count, found_high); 
    if (best_east == -1 && best_west == -1) return 0; 
    if (best_east != -1 && best_west != -1) 
    { 
        char prefer_dir; 
        if (consec_same_dir >= 2) 
        { 
            if (last_direction == 'E' && west_count > 0) 
            { 
                prefer_dir = 'W'; 
            } 
            else if (last_direction == 'W' && east_count > 0) 
            { 
                prefer_dir = 'E'; 
            } 
            else 
            { 
                prefer_dir = last_direction; 
            } 
        } 
        else 
        { 
            if (last_direction == 'X') 
            { 
                if (west_count > 0) {
                    prefer_dir = 'W'; 
                }
                else {
                    prefer_dir = 'E';
                }
            } 
            else 
            { 
                prefer_dir = (last_direction == 'E') ? 'W' : 'E'; 
            } 
        } 
        
        int chosen = (prefer_dir == 'E') ? best_east : best_west; 
        return (tidx == chosen); 
    } 
    int chosen = (best_east != -1) ? best_east : best_west;
    return (tidx == chosen); 
}




void remove_from_queue(int *queue, int *qcount, int idx_to_remove) {
    int found = -1;
    for (int i = 0; i < *qcount; i++) {
        if (queue[i] == idx_to_remove) {
            found = i;
            break;
        }
    }

    if (found >= 0) {
        for (int j = found; j < *qcount - 1; j++) {
            queue[j] = queue[j+1];
        }
        (*qcount)--;
    }
}

void *train_thread(void *arg) {
    Train *t = (Train *) arg;
    int tidx = t->id;

    usleep(t->loading_time * 100000);

    pthread_mutex_lock(&track_mutex);
    gettimeofday(&t->ready_time, NULL);

    if (t->direction == 'E' || t->direction == 'e') {
        east_ready[east_count++] = tidx;
    }
    else {
        west_ready[west_count++] = tidx;
    }

    ts_printf("Train %2d is ready to go %4s", t->id, (t->direction=='E' || t->direction=='e') ? "East" : "West");
    t->enqueued = 1;
    pthread_cond_broadcast(&track_cond);

    while (!is_my_turn(tidx)) {
        pthread_cond_wait(&track_cond, &track_mutex);
    }

    track_in_use = 1;
    if ((t->direction=='E' || t->direction=='e') && last_direction == 'E') consec_same_dir++;
    else if ((t->direction=='W' || t->direction=='w') && last_direction == 'W') consec_same_dir++;
    else consec_same_dir = 1;
    last_direction = (t->direction=='E' || t->direction=='e') ? 'E' : 'W';

    ts_printf("Train %2d is ON the main track going %4s", t->id, (t->direction=='E' || t->direction=='e') ? "East" : "West");

    pthread_mutex_unlock(&track_mutex);

    usleep(t->crossing_time * 100000);

    pthread_mutex_lock(&track_mutex);
    ts_printf("Train %2d is OFF the main track after going %4s", t->id, (t->direction=='E' || t->direction=='e') ? "East" : "West");

    if (t->direction=='E' || t->direction=='e') remove_from_queue(east_ready, &east_count, tidx);
    else remove_from_queue(west_ready, &west_count, tidx);

    track_in_use = 0;
    pthread_cond_broadcast(&track_cond);
    pthread_mutex_unlock(&track_mutex);

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    output_fp = fopen("output.txt", "w");
    if (!output_fp) {
        perror("Cannot open output.txt");
        return EXIT_FAILURE;
    }


    g_trains = read_file(argv[1], &g_count);
    if (!g_trains) return EXIT_FAILURE;

    gettimeofday(&sim_start, NULL);

    for (int i = 0; i < g_count; i++) {
        pthread_create(&g_trains[i].thread_id, NULL, train_thread, &g_trains[i]);
    }

    for (int i = 0; i < g_count; i++) {
        pthread_join(g_trains[i].thread_id, NULL);
    }
    
    fclose(output_fp);
    pthread_mutex_destroy(&track_mutex);
    pthread_mutex_destroy(&print_mutex);
    pthread_cond_destroy(&track_cond);

    free(g_trains);
    return EXIT_SUCCESS;

    /* printf("ID | Dir | Priority | Load | Cross\n");
    printf("------------------------------------\n");

    for (int i = 0; i < count; i++) {
        printf("%2d |  %c  |    %d     |  %3d  |  %3d\n",
            trains[i].id,
            trains[i].direction,
            trains[i].priority,
            trains[i].loading_time,
            trains[i].crossing_time);
    }

    free(trains);
    return EXIT_SUCCESS; */



}