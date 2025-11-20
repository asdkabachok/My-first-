#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

#define MAX_JOBS 100
#define MAX_CMD 512
#define JOBS_FILE "jobs.txt"

typedef struct {
    int id;
    char cmd[MAX_CMD];
    time_t run_time;
    int executed;
} Job;

Job jobs[MAX_JOBS];
int job_count = 0;

/* Простой парсер YYYY-MM-DD HH:MM:SS -> time_t.
   Возвращает 0 при ошибке (в т.ч. если input == NULL или несоответствие формата). */
time_t parse_time_manual(const char *s) {
    if (!s) return (time_t)0;
    int year, mon, day, hour, min, sec;
    if (sscanf(s, "%d-%d-%d %d:%d:%d", &year, &mon, &day, &hour, &min, &sec) != 6)
        return (time_t)0;
    struct tm tm = {0};
    tm.tm_year = year - 1900;
    tm.tm_mon = mon - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = sec;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

void load_jobs() {
    FILE *f = fopen(JOBS_FILE, "r");
    if (!f) {
        job_count = 0;
        return;
    }

    job_count = 0;
    while (job_count < MAX_JOBS) {
        int id;
        long run_time;
        int executed;
        char cmd_buf[MAX_CMD];

        int res = fscanf(f, "%d %ld %d %511[^\n]", &id, &run_time, &executed, cmd_buf);
        if (res != 4) break;
        jobs[job_count].id = id;
        /* безопасно копируем строку */
        snprintf(jobs[job_count].cmd, sizeof jobs[job_count].cmd, "%s", cmd_buf);
        jobs[job_count].run_time = (time_t)run_time;
        jobs[job_count].executed = executed;
        job_count++;
    }
    fclose(f);
}

void save_jobs() {
    FILE *f = fopen(JOBS_FILE, "w");
    if (!f) {
        perror("save_jobs: fopen");
        return;
    }

    for (int i = 0; i < job_count; i++) {
        fprintf(f, "%d %ld %d %s\n",
                jobs[i].id,
                (long)jobs[i].run_time,
                jobs[i].executed,
                jobs[i].cmd);
    }
    fclose(f);
}

void cmd_add(char *cmd_str, char *time_str) {
    load_jobs();

    if (job_count >= MAX_JOBS) {
        printf("too many jobs\n");
        return;
    }

    time_t run_time = 0;
    if (time_str) {
        run_time = parse_time_manual(time_str);
        if (run_time == (time_t)0) {
            fprintf(stderr, "bad time format, expected: YYYY-MM-DD HH:MM:SS\n");
            return;
        }
    }

    int max_id = 0;
    for (int i = 0; i < job_count; i++) if (jobs[i].id > max_id) max_id = jobs[i].id;
    int new_id = max_id + 1;

    jobs[job_count].id = new_id;
    /* безопасно копируем команду */
    snprintf(jobs[job_count].cmd, sizeof jobs[job_count].cmd, "%s", cmd_str);
    jobs[job_count].run_time = run_time;
    jobs[job_count].executed = 0;
    job_count++;

    save_jobs();
    printf("added job %d\n", new_id);
}

void cmd_list() {
    load_jobs();
    if (job_count == 0) {
        printf("no jobs\n");
        return;
    }

    for (int i = 0; i < job_count; i++) {
        printf("[%d] cmd: %s", jobs[i].id, jobs[i].cmd);
        if (jobs[i].run_time > 0) {
            char buf[32];
            struct tm *tm = localtime(&jobs[i].run_time);
            if (tm) {
                strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
                printf(" at: %s", buf);
            }
        }
        printf(" %s\n", jobs[i].executed ? "(done)" : "(pending)");
    }
}

void cmd_run() {
    printf("daemon: starting task runner\n");

    while (1) {
        load_jobs();
        time_t now = time(NULL);

        for (int i = 0; i < job_count; i++) {
            if (jobs[i].run_time > 0 && jobs[i].run_time <= now && !jobs[i].executed) {
                printf("[%d] running: %s\n", jobs[i].id, jobs[i].cmd);

                pid_t pid = fork();
                if (pid == 0) {
                    /* child */
                    execl("/bin/sh", "sh", "-c", jobs[i].cmd, (char *)NULL);
                    _exit(127);
                } else if (pid > 0) {
                    int status;
                    waitpid(pid, &status, 0);
                } else {
                    perror("fork");
                }

                jobs[i].executed = 1;
                save_jobs();
            }
        }

        sleep(10); /* проверка каждые 10 секунд */
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("usage: taskd add <cmd> <time> | list | run\n");
        printf("example: taskd add \"echo hello\" \"2025-11-21 12:00:00\"\n");
        return 1;
    }

    if (strcmp(argv[1], "add") == 0 && argc >= 3) {
        cmd_add(argv[2], argc > 3 ? argv[3] : NULL);
    } else if (strcmp(argv[1], "list") == 0) {
        cmd_list();
    } else if (strcmp(argv[1], "run") == 0) {
        cmd_run();
    } else {
        printf("unknown command\n");
        return 1;
    }

    return 0;
}