#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    const char *name;
    uint32_t period;    // T_i
    uint32_t wcet[3];   // WCET for different frequencies (low, medium, high)
    uint32_t deadline;  // D_i (relative, often = T_i)
    uint32_t phase;     // Release offset
} Task;

typedef struct {
    int task_id;
    uint64_t release_time;
    uint64_t abs_deadline;
    double remaining_work; // Remaining work in job-units
    uint64_t job_seq;      // Sequence number
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

int rq_highest_rm_idx(const Task *tasks) {
    if (readyJobCount == 0) return -1;
    int best = 0;
    for (int i = 1; i < readyJobCount; ++i) {
        int a = readyJobs[i].task_id, b = readyJobs[best].task_id;
        if (tasks[a].period < tasks[b].period) {
            best = i;
        } else if (tasks[a].period == tasks[b].period) {
            if (readyJobs[i].abs_deadline < readyJobs[best].abs_deadline) {
                best = i;
            } else if (readyJobs[i].abs_deadline == readyJobs[best].abs_deadline && a < b) {
                best = i;
            }
        }
    }
    return best;
}

// Simulation parameters
#define SIMULATION_END 100
uint64_t completed = 0;    // Number of completed jobs
uint64_t preemptions = 0;  // Number of preemptions
uint64_t misses = 0;       // Number of deadline misses
double energy_busy = 0.0;  // Energy consumed while busy
double energy_idle = 0.0;  // Energy consumed while idle

bool cpu_busy = false;
Job currentJob = {0};
int currentFrequency = 2; // Start with the highest frequency (0 = low, 1 = medium, 2 = high)

// Define the number of tasks
#define N 3
Task tasks[N] = {
    {"Task1", 10, {1, 2, 3}, 10, 0},
    {"Task2", 15, {2, 3, 4}, 15, 0},
    {"Task3", 20, {3, 4, 5}, 20, 0}
};

uint64_t next_seq[N] = {0};
double power_levels[3] = {1.0, 2.0, 3.0}; // Power consumption for low, medium, high frequencies
double power_idle = 0.5;                  // Idle power consumption

// Helper function to calculate the required frequency
int select_frequency(const Task *tasks, const Job *currentJob, double window) {
    double work_max = 0.0;

    // Calculate total work in max-speed time
    for (int i = 0; i < readyJobCount; ++i) {
        int task_id = readyJobs[i].task_id;
        work_max += readyJobs[i].remaining_work * tasks[task_id].wcet[2];
    }
    if (cpu_busy) {
        int task_id = currentJob->task_id;
        work_max += currentJob->remaining_work * tasks[task_id].wcet[2];
    }

    // Calculate required speed fraction
    double speed_fraction = work_max / window;

    // Select the slowest frequency that meets the required speed
    for (int f = 0; f < 3; ++f) {
        if (1.0 / tasks[0].wcet[f] >= speed_fraction) {
            return f;
        }
    }
    return 2; // Default to the highest frequency
}

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
                j.remaining_work = 1.0; // Initialize remaining work in job-units
                j.job_seq = next_seq[i]++;
                rq_push(j);
                printf("[t=%llu] RELEASE %s#%llu (dl=%llu, work=%.2f)\n",
                       (unsigned long long)t, ti->name,
                       (unsigned long long)j.job_seq,
                       (unsigned long long)j.abs_deadline, j.remaining_work);
            }
        }

        // 2) Check for deadline misses
        if (cpu_busy && t > currentJob.abs_deadline && currentJob.remaining_work > 0) {
            printf("[t=%llu] MISS %s#%llu (dl=%llu, work=%.2f)\n",
                   (unsigned long long)t, tasks[currentJob.task_id].name,
                   (unsigned long long)currentJob.job_seq,
                   (unsigned long long)currentJob.abs_deadline, currentJob.remaining_work);
            misses++;
            cpu_busy = false; // Mark CPU as idle
        }
        for (int i = 0; i < readyJobCount; ++i) {
            if (t > readyJobs[i].abs_deadline && readyJobs[i].remaining_work > 0) {
                printf("[t=%llu] MISS %s#%llu (dl=%llu, work=%.2f)\n",
                       (unsigned long long)t, tasks[readyJobs[i].task_id].name,
                       (unsigned long long)readyJobs[i].job_seq,
                       (unsigned long long)readyJobs[i].abs_deadline, readyJobs[i].remaining_work);
                misses++;
                rq_remove_idx(i);
                i--; // Adjust index after removal
            }
        }

        // 3) Select job to run (RM)
        if (!cpu_busy) {
            int idx = rq_highest_rm_idx(tasks);
            if (idx != -1) {
                currentJob = readyJobs[idx];
                rq_remove_idx(idx);
                cpu_busy = true;
                printf("[t=%llu] START %s#%llu (prio T=%u, dl=%llu, work=%.2f)\n",
                       (unsigned long long)t, tasks[currentJob.task_id].name,
                       (unsigned long long)currentJob.job_seq,
                       tasks[currentJob.task_id].period,
                       (unsigned long long)currentJob.abs_deadline, currentJob.remaining_work);
            }
        }

        // 4) Frequency selection
        double window = SIMULATION_END - t; // Default window to the end of the simulation
        if (cpu_busy) {
            window = currentJob.abs_deadline - t;
        }
        for (int i = 0; i < readyJobCount; ++i) {
            double next_release = tasks[readyJobs[i].task_id].phase +
                                  ((t - tasks[readyJobs[i].task_id].phase) / tasks[readyJobs[i].task_id].period + 1) *
                                      tasks[readyJobs[i].task_id].period;
            if (next_release - t < window) {
                window = next_release - t;
            }
        }
        currentFrequency = select_frequency(tasks, &currentJob, window);

        // 5) Execute the running job
        if (cpu_busy) {
            double rate = 1.0 / tasks[currentJob.task_id].wcet[currentFrequency];
            currentJob.remaining_work -= rate;
            energy_busy += power_levels[currentFrequency];
            if (currentJob.remaining_work <= 0) {
                printf("[t=%llu] COMPLETE %s#%llu\n",
                       (unsigned long long)t, tasks[currentJob.task_id].name,
                       (unsigned long long)currentJob.job_seq);
                completed++;
                cpu_busy = false; // Mark CPU as idle
            }
        } else {
            energy_idle += power_idle;
        }
    }

    // Print summary
    printf("\nSummary: Completed=%llu, Preemptions=%llu, Misses=%llu\n",
           (unsigned long long)completed, (unsigned long long)preemptions, (unsigned long long)misses);
    printf("Energy: Busy=%.2f, Idle=%.2f, Total=%.2f\n",
           energy_busy, energy_idle, energy_busy + energy_idle);

    return 0;
}