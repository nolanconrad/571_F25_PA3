#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    const char *name;
    uint32_t period;    // T_i  (shorter T => higher priority)
    uint32_t wcet;      // C_i  (ticks)
    uint32_t deadline;  // D_i  (relative) — can be = T_i
    uint32_t phase;     // release offset
} Task;

typedef struct {
    int task_id;
    uint64_t release_time;
    uint64_t abs_deadline;
    uint32_t remaining;   // ticks left
    uint64_t job_seq;     // 0,1,2,... per task
} Job;

// Ready queue as a simple array
#define READY_QUEUE_SIZE 128
Job readyJobs[READY_QUEUE_SIZE];
int readyJobCount = 0;

void rq_push(Job j) {
    if (readyJobCount < READY_QUEUE_SIZE) {
        readyJobs[readyJobCount++] = j;
    } else {
        fprintf(stderr, "Ready queue full; dropping job!\n");
    }
}

void rq_remove_idx(int idx) {
    if (idx < 0 || idx >= readyJobCount) return;
    readyJobs[idx] = readyJobs[--readyJobCount];
}

int rq_earliest_deadline_idx() {
    if (readyJobCount == 0) return -1;
    int best = 0;
    for (int i = 1; i < readyJobCount; ++i) {
        if (readyJobs[i].abs_deadline < readyJobs[best].abs_deadline) {
            best = i;
        }
    }
    return best;
}

// Simulation parameters
#define SIMULATION_END 100
uint64_t completed = 0;    // Number of completed jobs
uint64_t preemptions = 0;  // Number of preemptions
uint64_t misses = 0;       // Number of deadline misses

bool cpu_busy = false;
Job currentJob = {0};

// Define the number of tasks
#define N 10
Task tasks[N] = {
    {"Task1", 10, 2, 10, 0},
    {"Task2", 15, 3, 15, 0},
    {"Task3", 20, 4, 20, 0},
    {"Task4", 25, 5, 25, 0},
    {"Task5", 30, 6, 30, 0},
    {"Task6", 35, 7, 35, 0},
    {"Task7", 40, 8, 40, 0},
    {"Task8", 45, 9, 45, 0},
    {"Task9", 50, 10, 50, 0},
    {"Task10", 55, 11, 55, 0}
};

uint64_t next_seq[N] = {0};

int main() {
    for (uint64_t t = 0; t <= SIMULATION_END; ++t) {
        // 1) Releases at time t
        for (int i = 0; i < N; ++i) {
            Task *ti = &tasks[i];
            if (t >= ti->phase && ((t - ti->phase) % ti->period == 0)) {
                Job j;
                j.task_id = i;
                j.release_time = t;
                j.abs_deadline = t + ti->deadline;
                j.remaining = ti->wcet;
                j.job_seq = next_seq[i]++;
                rq_push(j);
                printf("[t=%llu] RELEASE %s#%llu (dl=%llu, rem=%u)\n",
                       (unsigned long long)t, ti->name,
                       (unsigned long long)j.job_seq,
                       (unsigned long long)j.abs_deadline, j.remaining);
            }
        }

        // 2) Check for deadline misses
        if (cpu_busy && t > currentJob.abs_deadline && currentJob.remaining > 0) {
            printf("[t=%llu] MISS %s#%llu (dl=%llu, rem=%u)\n",
                   (unsigned long long)t, tasks[currentJob.task_id].name,
                   (unsigned long long)currentJob.job_seq,
                   (unsigned long long)currentJob.abs_deadline, currentJob.remaining);
            misses++;
            cpu_busy = false; // Mark CPU as idle
        }
        for (int i = 0; i < readyJobCount; ++i) {
            if (t > readyJobs[i].abs_deadline && readyJobs[i].remaining > 0) {
                printf("[t=%llu] MISS %s#%llu (dl=%llu, rem=%u)\n",
                       (unsigned long long)t, tasks[readyJobs[i].task_id].name,
                       (unsigned long long)readyJobs[i].job_seq,
                       (unsigned long long)readyJobs[i].abs_deadline, readyJobs[i].remaining);
                misses++;
                rq_remove_idx(i);
                i--; // Adjust index after removal
            }
        }

        // 3) Select job to run (EDF)
        if (!cpu_busy) {
            int idx = rq_earliest_deadline_idx();
            if (idx != -1) {
                currentJob = readyJobs[idx];
                rq_remove_idx(idx);
                cpu_busy = true;
                printf("[t=%llu] START %s#%llu (dl=%llu, rem=%u)\n",
                       (unsigned long long)t, tasks[currentJob.task_id].name,
                       (unsigned long long)currentJob.job_seq,
                       (unsigned long long)currentJob.abs_deadline, currentJob.remaining);
            }
        } else {
            int idx = rq_earliest_deadline_idx();
            if (idx != -1 && readyJobs[idx].abs_deadline < currentJob.abs_deadline) {
                rq_push(currentJob); // Preempt current job
                currentJob = readyJobs[idx];
                rq_remove_idx(idx);
                preemptions++;
                printf("[t=%llu] PREEMPT -> %s#%llu (dl=%llu, rem=%u)\n",
                       (unsigned long long)t, tasks[currentJob.task_id].name,
                       (unsigned long long)currentJob.job_seq,
                       (unsigned long long)currentJob.abs_deadline, currentJob.remaining);
            }
        }

        // 4) Execute the running job
        if (cpu_busy) {
            currentJob.remaining--;
            if (currentJob.remaining == 0) {
                printf("[t=%llu] COMPLETE %s#%llu\n",
                       (unsigned long long)t, tasks[currentJob.task_id].name,
                       (unsigned long long)currentJob.job_seq);
                completed++;
                cpu_busy = false; // Mark CPU as idle
            }
        }
    }

    // Print summary
    printf("\nSummary: Completed=%llu, Preemptions=%llu, Misses=%llu\n",
           (unsigned long long)completed, (unsigned long long)preemptions, (unsigned long long)misses);

    return 0;
}