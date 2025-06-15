#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <sched.h>
#include <time.h>
#include <string.h>
#include <stdatomic.h>

#define die(msg) do { perror(msg); exit(EXIT_FAILURE); } while (0)

// --- CONFIGURATION ---
#define NUM_WORKER_THREADS 50
#define PRIMING_PHASE_SEC 15
#define MEASURE_PHASE_SEC 20
#define SLICE_MS 500
#define NUM_SLICES (MEASURE_PHASE_SEC * 1000 / SLICE_MS)

// --- GLOBAL SHARED STATE ---
int g_cpu_waker, g_cpu_victim_a, g_cpu_victim_b, g_num_cpus;
volatile bool g_victim_a_long_sleep = true;
volatile bool g_victim_b_long_sleep = false;
volatile bool g_running = true;

// Globals for the thread pool
pthread_mutex_t g_pool_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_pool_cond = PTHREAD_COND_INITIALIZER;
int g_tasks_to_do = 0;

// Pointer to the current slice's result array.
atomic_int *g_placements;

// --- UTILITY FUNCTIONS ---
void pin_to_cpu(int cpu, const char* thread_name) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
        fprintf(stderr, "Failed to pin %s thread to CPU %d\n", thread_name, cpu);
        die("pthread_setaffinity_np");
    }
}

long long get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

// Finds the next available run number to create a unique CSV filename.
int get_next_run_number() {
    int run_num = 1;
    char filename[256];
    while(1) {
        sprintf(filename, "run-%d.csv", run_num);
        if (access(filename, F_OK) == -1) {
            return run_num; // File does not exist, we can use this number
        }
        run_num++;
    }
}

// --- THREAD FUNCTIONS ---
void *victim_func(void *arg) {
    volatile bool *is_long_sleep_flag = (volatile bool *)arg;
    pin_to_cpu(*is_long_sleep_flag ? g_cpu_victim_a : g_cpu_victim_b, "Victim");
    unsigned int long_sleep_us = 100 * 1000;
    unsigned int short_sleep_us = 100;  // Make the busy CPU very busy
    while (g_running) {
        usleep(*is_long_sleep_flag ? long_sleep_us : short_sleep_us);
    }
    return NULL;
}

void *worker_func(void *arg) {
    while (g_running) {
        pthread_mutex_lock(&g_pool_mutex);
        while (g_tasks_to_do == 0 && g_running) {
            pthread_cond_wait(&g_pool_cond, &g_pool_mutex);
        }
        if (!g_running) {
            pthread_mutex_unlock(&g_pool_mutex);
            break;
        }
        g_tasks_to_do--;
        pthread_mutex_unlock(&g_pool_mutex);

        // This is the core of the experiment.
        // We are now an awakened thread. Where will the scheduler place us?

        // Do a non-trivial amount of work
        long long start_ms = get_time_ms();
        while (get_time_ms() - start_ms < 30); // 30ms spin-wait

        if (g_placements) {
            int current_cpu = sched_getcpu();
            if (current_cpu >= 0 && current_cpu < g_num_cpus) {
                atomic_fetch_add(&g_placements[current_cpu], 1);
            }
        }
    }
    return NULL;
}

void *waker_func(void *arg) {
    pin_to_cpu(g_cpu_waker, "Waker");

    FILE *csv_file = (FILE *)arg;
    fprintf(csv_file, "Time,TasksOnA,TasksOnB,TasksOnWaker,TotalTasks,PercentOnA,PercentOnB\n");

    printf("--- Phase 1: Priming Scheduler History (Victim A is Idle, B is Busy) ---\n");
    printf("Running for %d seconds...\n", PRIMING_PHASE_SEC);
    long long priming_end_time = get_time_ms() + PRIMING_PHASE_SEC * 1000;
    while(get_time_ms() < priming_end_time) {
        pthread_mutex_lock(&g_pool_mutex);
        g_tasks_to_do++;
        pthread_cond_signal(&g_pool_cond);
        pthread_mutex_unlock(&g_pool_mutex);
        usleep(35000); // Pace task creation
    }

    printf("\n--- !!! FLIPPING ROLES (A -> Busy, B -> Idle) !!! ---\n\n");
    g_victim_a_long_sleep = false;
    g_victim_b_long_sleep = true;

    printf("--- Phase 2: Measuring Scheduler Adaptation ---\n");
    printf("Observing for %d seconds, in %dms slices...\n\n", MEASURE_PHASE_SEC, SLICE_MS);

    atomic_int (*placements_table)[g_num_cpus] = calloc(NUM_SLICES, sizeof(atomic_int[g_num_cpus]));
    if (!placements_table) die("calloc for results table");

    for (int i = 0; i < NUM_SLICES; i++) {
        g_placements = placements_table[i];
        long long slice_end_time = get_time_ms() + SLICE_MS;
        while (get_time_ms() < slice_end_time) {
            pthread_mutex_lock(&g_pool_mutex);
            g_tasks_to_do++;
            pthread_cond_signal(&g_pool_cond);
            pthread_mutex_unlock(&g_pool_mutex);
            usleep(35000);
        }

        int tasks_on_a = atomic_load(&g_placements[g_cpu_victim_a]);
        int tasks_on_b = atomic_load(&g_placements[g_cpu_victim_b]);
        int tasks_on_waker = atomic_load(&g_placements[g_cpu_waker]);
        int total_in_slice = tasks_on_a + tasks_on_b + tasks_on_waker;

        float percent_a = (total_in_slice > 0) ? (float)tasks_on_a * 100.0 / total_in_slice : 0.0;
        float percent_b = (total_in_slice > 0) ? (float)tasks_on_b * 100.0 / total_in_slice : 0.0;
        float time_s = (float)((i + 1) * SLICE_MS) / 1000.0;

        fprintf(csv_file, "%.2f,%d,%d,%d,%d,%.2f,%.2f\n",
                time_s, tasks_on_a, tasks_on_b, tasks_on_waker, total_in_slice,
                percent_a, percent_b);
    }

    g_running = false;
    pthread_mutex_lock(&g_pool_mutex);
    g_tasks_to_do = NUM_WORKER_THREADS; // Ensure all workers can wake up to exit
    pthread_cond_broadcast(&g_pool_cond);
    pthread_mutex_unlock(&g_pool_mutex);

    free(placements_table);
    g_placements = NULL;
    return NULL;
}


int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <waker_cpu> <victim_a_cpu> <victim_b_cpu>\n", argv[0]);
        fprintf(stderr, "Ensure CPUs are distinct and on different physical cores.\n");
        exit(EXIT_FAILURE);
    }

    g_cpu_waker = atoi(argv[1]);
    g_cpu_victim_a = atoi(argv[2]);
    g_cpu_victim_b = atoi(argv[3]);
    g_num_cpus = sysconf(_SC_NPROCESSORS_ONLN);

    if (g_cpu_waker == g_cpu_victim_a || g_cpu_waker == g_cpu_victim_b || g_cpu_victim_a == g_cpu_victim_b) {
        fprintf(stderr, "Error: All provided CPUs must be distinct.\n");
        exit(EXIT_FAILURE);
    }

    printf("Starting benchmark: Waker on %d, Victim A on %d, Victim B on %d\n\n",
        g_cpu_waker, g_cpu_victim_a, g_cpu_victim_b);

    int run_num = get_next_run_number();
    char filename[256];
    sprintf(filename, "run-%d.csv", run_num);
    FILE *csv_file = fopen(filename, "w");
    if (csv_file == NULL) {
        die("Failed to open output CSV file");
    }
    printf("Writing output to %s\n", filename);

    pthread_t waker_thread, victim_a_thread, victim_b_thread;
    pthread_t worker_threads[NUM_WORKER_THREADS];

    cpu_set_t allowed_cpus;
    CPU_ZERO(&allowed_cpus);
    CPU_SET(g_cpu_waker, &allowed_cpus);
    CPU_SET(g_cpu_victim_a, &allowed_cpus);
    CPU_SET(g_cpu_victim_b, &allowed_cpus);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &allowed_cpus);

    for (int i = 0; i < NUM_WORKER_THREADS; i++) {
        if (pthread_create(&worker_threads[i], &attr, worker_func, NULL) != 0) {
            die("pthread_create for worker thread");
        }
    }

    if (pthread_create(&victim_a_thread, NULL, victim_func, (void *)&g_victim_a_long_sleep) != 0) die("pthread_create victim A");
    if (pthread_create(&victim_b_thread, NULL, victim_func, (void *)&g_victim_b_long_sleep) != 0) die("pthread_create victim B");

    usleep(100 * 1000);

    if (pthread_create(&waker_thread, NULL, waker_func, csv_file) != 0) die("pthread_create waker");

    pthread_join(waker_thread, NULL);
    pthread_join(victim_a_thread, NULL);
    pthread_join(victim_b_thread, NULL);
    for (int i = 0; i < NUM_WORKER_THREADS; i++) {
        pthread_join(worker_threads[i], NULL);
    }
    pthread_attr_destroy(&attr);
    fclose(csv_file);

    printf("\nBenchmark finished.\n");
    return 0;
}
