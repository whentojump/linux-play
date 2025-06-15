#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <sched.h>
#include <sys/wait.h>
#include <time.h>

#define die(msg) do { perror(msg); exit(EXIT_FAILURE); } while (0)

// Global state to control the victim thread's behavior
volatile bool victim_long_sleep = true;
volatile bool running = true;

void pin_to_cpu(int cpu) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
        die("pthread_setaffinity_np");
    }
}

// Victim thread: alternates its sleep pattern on the target CPU
void *victim_func(void *arg) {
    int cpu = *(int*)arg;
    pin_to_cpu(cpu);
    printf("[Victim] Pinned to CPU %d\n", cpu);

    unsigned int long_sleep_us = 100 * 1000; // 100ms
    unsigned int short_sleep_us = 1 * 1000;  // 1ms

    while (running) {
        if (victim_long_sleep) {
            usleep(long_sleep_us);
        } else {
            usleep(short_sleep_us);
        }
    }
    return NULL;
}

// Waker thread: creates many short-lived tasks
void *waker_func(void *arg) {
    int cpu = *(int*)arg;
    pin_to_cpu(cpu);
    printf("[Waker] Pinned to CPU %d. Will spawn short-lived tasks.\n", cpu);

    while (running) {
        pid_t pid = fork();
        if (pid == -1) {
            die("fork");
        } else if (pid == 0) {
            // Child process - does nothing and exits
            exit(0);
        } else {
            // Parent process
            waitpid(pid, NULL, 0);
            usleep(5 * 1000); // Create a new task every 5ms
        }
    }
    return NULL;
}

void print_observation_instructions(int victim_cpu, int waker_cpu) {
    printf("\n--- OBSERVATION INSTRUCTIONS ---\n");
    printf("This program demonstrates how avg_idle affects task placement.\n");
    printf(" - The [Victim] thread is on CPU %d, controlling its idle time.\n", victim_cpu);
    printf(" - The [Waker]  thread is on CPU %d, creating new tasks.\n", waker_cpu);
    printf("\nIn another terminal, monitor avg_idle for CPU %d:\n", victim_cpu);
    printf("  watch -n0.1 \"cat /proc/sched_debug | grep -A20 'cpu#%d'\"\n", victim_cpu);
    printf("\nAlso, monitor where the 'waker' child processes run:\n");
    printf("  ps -eLo ruser,pid,ppid,lwp,psr,args | grep %d\n", getpid());
    printf("  (Look for children of the 'waker' and check their PSR column)\n");
    printf("\nPress [Enter] to switch the victim's sleep pattern.\n");
    printf("--------------------------------\n\n");
}


int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <victim_cpu> <waker_cpu>\n", argv[0]);
        fprintf(stderr, "Ensure victim_cpu and waker_cpu are different and online.\n");
        exit(EXIT_FAILURE);
    }

    int victim_cpu = atoi(argv[1]);
    int waker_cpu = atoi(argv[2]);

    if (victim_cpu == waker_cpu) {
        fprintf(stderr, "Victim and waker CPUs must be different.\n");
        exit(EXIT_FAILURE);
    }

    // Pin main thread to a CPU that isn't victim or waker to avoid interference
    int main_cpu = 0;
    while(main_cpu == victim_cpu || main_cpu == waker_cpu) {
        main_cpu++;
    }
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(main_cpu, &cpuset);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
        die("sched_setaffinity for main thread");
    }
     printf("[Main] Pinned to CPU %d.\n", main_cpu);


    pthread_t victim_thread, waker_thread;

    if (pthread_create(&victim_thread, NULL, victim_func, &victim_cpu) != 0) {
        die("pthread_create victim");
    }
    if (pthread_create(&waker_thread, NULL, waker_func, &waker_cpu) != 0) {
        die("pthread_create waker");
    }

    print_observation_instructions(victim_cpu, waker_cpu);

    for (int i=0; i<4; i++) {
        printf("--> Victim is now in '%s' sleep mode. Press [Enter] to switch...\n",
               victim_long_sleep ? "LONG" : "SHORT");
        getchar();
        victim_long_sleep = !victim_long_sleep;
    }

    printf("Finishing...\n");
    running = false;

    pthread_join(victim_thread, NULL);
    pthread_join(waker_thread, NULL);

    return 0;
}
