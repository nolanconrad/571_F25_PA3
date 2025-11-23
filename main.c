// sched_sim.c
// One file, two schedulers: EDF or RM (select via argv[1])
// Build: gcc -O2 -std=c11 sched_sim.c -o sched_sim
// Run:   ./sched_sim edf   OR   ./sched_sim rm

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define MAX_TASKS  8
#define MAX_READY  128
#define SIM_END    100  // simulate ticks [0..SIM_END]

typedef enum { POLICY_EDF, POLICY_RM } Policy;

typedef struct {
    const char *name;
    uint32_t period;    // T_i
    uint32_t wcet;      // C_i (ticks at current "speed")
    uint32_t deadline;  // D_i (relative). Often D_i = T_i
    uint32_t phase;     // release offset
} Task;

typedef struct {
    int task_id;
    uint64_t release_time;
    uint64_t abs_deadline;
    uint32_t remaining;   // ticks left (at current "speed")
    uint64_t job_seq;     // 0,1,2,... per task
} Job;

// ---------- Ready queue as a simple array ----------
static Job RQ[MAX_READY];
static int  RQ_sz = 0;

static void rq_push(Job j){
    if (RQ_sz < MAX_READY) RQ[RQ_sz++] = j;
    else fprintf(stderr, "Ready queue full; dropping job!\n");
}
static void rq_remove_idx(int idx){
    if (idx < 0 || idx >= RQ_sz) return;
    RQ[idx] = RQ[--RQ_sz];
}

// ---------- Selection helpers ----------
static int rq_earliest_deadline_idx(void){
    if (RQ_sz == 0) return -1;
    int best = 0;
    for (int i = 1; i < RQ_sz; ++i)
        if (RQ[i].abs_deadline < RQ[best].abs_deadline) best = i;
    return best;
}

// RM: smaller period => higher priority. Tie: earlier deadline, then smaller task_id.
static int rq_highest_rm_idx(const Task *tasks){
    if (RQ_sz == 0) return -1;
    int best = 0;
    for (int i = 1; i < RQ_sz; ++i){
        int a = RQ[i].task_id, b = RQ[best].task_id;
        if (tasks[a].period < tasks[b].period) best = i;
        else if (tasks[a].period == tasks[b].period){
            if (RQ[i].abs_deadline < RQ[best].abs_deadline) best = i;
            else if (RQ[i].abs_deadline == RQ[best].abs_deadline && a < b) best = i;
        }
    }
    return best;
}
static inline bool higher_rm(const Task *tasks, int a_task, int b_task){
    if (tasks[a_task].period < tasks[b_task].period) return true;
    if (tasks[a_task].period > tasks[b_task].period) return false;
    return a_task < b_task;
}

static int pick_ready_idx(Policy pol, const Task *tasks){
    return (pol == POLICY_EDF) ? rq_earliest_deadline_idx()
                               : rq_highest_rm_idx(tasks);
}

static bool preempt_needed(Policy pol, const Task *tasks, const Job *cur){
    if (RQ_sz == 0) return false;
    if (pol == POLICY_EDF){
        int best = rq_earliest_deadline_idx();
        return (best != -1 && RQ[best].abs_deadline < cur->abs_deadline);
    } else { // RM
        int best = rq_highest_rm_idx(tasks);
        return (best != -1 && higher_rm(tasks, RQ[best].task_id, cur->task_id));
    }
}

// ---------- Demo tasks (D_i = T_i). Tweak as needed ----------
static void load_example_tasks(Task *tasks, int *N){
    Task demo[] = {
        { "T1",  5, 1, 5, 0 },
        { "T2",  8, 2, 8, 0 },
        { "T3", 12, 3, 12, 0 },
    };
    *N = (int)(sizeof(demo)/sizeof(demo[0]));
    for (int i = 0; i < *N; ++i) tasks[i] = demo[i];
}

// ---------- Main ----------
int main(int argc, char **argv){
    Policy pol = POLICY_EDF;
    if (argc >= 2){
        if (strcmp(argv[1], "edf") == 0) pol = POLICY_EDF;
        else if (strcmp(argv[1], "rm") == 0) pol = POLICY_RM;
        else {
            fprintf(stderr, "Usage: %s [edf|rm]\n", argv[0]);
            return 1;
        }
    }

    Task tasks[MAX_TASKS];
    int N = 0;
    load_example_tasks(tasks, &N);

    uint64_t t = 0, completed = 0, preemptions = 0, misses = 0;
    uint64_t next_seq[MAX_TASKS] = {0};
    bool cpu_busy = false;
    Job cur = {0};

    printf("=== %s-only (no DVFS, no energy) ===\n",
           (pol == POLICY_EDF) ? "EDF" : "RM");

    for (t = 0; t <= SIM_END; ++t){
        // 1) Releases at time t
        for (int i = 0; i < N; ++i){
            Task *ti = &tasks[i];
            if (t >= ti->phase && ((t - ti->phase) % ti->period == 0)){
                Job j;
                j.task_id = i;
                j.release_time = t;
                j.abs_deadline = t + ti->deadline;
                j.remaining = ti->wcet;
                j.job_seq = next_seq[i]++;
                rq_push(j);
            }
        }

        // 2) Deadline miss checks
        if (cpu_busy && t > cur.abs_deadline && cur.remaining > 0){
            printf("[t=%llu] MISS  %s#%llu (dl=%llu, rem=%u)\n",
                   (unsigned long long)t, tasks[cur.task_id].name,
                   (unsigned long long)cur.job_seq,
                   (unsigned long long)cur.abs_deadline, cur.remaining);
            misses++;
        }
        for (int i = 0; i < RQ_sz; ++i){
            if (t > RQ[i].abs_deadline && RQ[i].remaining > 0){
                printf("[t=%llu] MISS  %s#%llu (dl=%llu, rem=%u)\n",
                       (unsigned long long)t, tasks[RQ[i].task_id].name,
                       (unsigned long long)RQ[i].job_seq,
                       (unsigned long long)RQ[i].abs_deadline, RQ[i].remaining);
                misses++;
            }
        }

        // 3) Start or preempt according to policy
        if (!cpu_busy){
            int idx = pick_ready_idx(pol, tasks);
            if (idx != -1){
                cur = RQ[idx];
                rq_remove_idx(idx);
                cpu_busy = true;
                if (pol == POLICY_EDF){
                    printf("[t=%llu] START %s#%llu (dl=%llu, rem=%u)\n",
                           (unsigned long long)t, tasks[cur.task_id].name,
                           (unsigned long long)cur.job_seq,
                           (unsigned long long)cur.abs_deadline, cur.remaining);
                } else {
                    printf("[t=%llu] START %s#%llu (prio T=%u, dl=%llu, rem=%u)\n",
                           (unsigned long long)t, tasks[cur.task_id].name,
                           (unsigned long long)cur.job_seq,
                           tasks[cur.task_id].period,
                           (unsigned long long)cur.abs_deadline, cur.remaining);
                }
            }
        } else {
            if (preempt_needed(pol, tasks, &cur)){
                int idx = pick_ready_idx(pol, tasks); // known to exist
                rq_push(cur);
                cur = RQ[idx];
                rq_remove_idx(idx);
                preemptions++;
                if (pol == POLICY_EDF){
                    printf("[t=%llu] PREEMPT -> %s#%llu (dl=%llu, rem=%u)\n",
                           (unsigned long long)t, tasks[cur.task_id].name,
                           (unsigned long long)cur.job_seq,
                           (unsigned long long)cur.abs_deadline, cur.remaining);
                } else {
                    printf("[t=%llu] PREEMPT -> %s#%llu (prio T=%u, dl=%llu, rem=%u)\n",
                           (unsigned long long)t, tasks[cur.task_id].name,
                           (unsigned long long)cur.job_seq,
                           tasks[cur.task_id].period,
                           (unsigned long long)cur.abs_deadline, cur.remaining);
                }
            }
        }

        // 4) Execute one tick
        if (cpu_busy){
            if (cur.remaining > 0) cur.remaining--;
            if (cur.remaining == 0){
                printf("[t=%llu] FINISH %s#%llu\n",
                       (unsigned long long)(t + 1),
                       tasks[cur.task_id].name,
                       (unsigned long long)cur.job_seq);
                completed++;
                cpu_busy = false;
            }
        }
        // else idle
    }

    printf("\nSummary (%s): Completed=%llu  Preemptions=%llu  Misses=%llu\n",
           (pol == POLICY_EDF) ? "EDF" : "RM",
           (unsigned long long)completed,
           (unsigned long long)preemptions,
           (unsigned long long)misses);

    return 0;
}
