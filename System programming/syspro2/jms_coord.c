#include "jms_common.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef enum JobState { JOB_QUEUED, JOB_ACTIVE, JOB_FINISHED } JobState;

// κρατάμε τα βασικά για κάθε job
typedef struct Job {
    int      id;                 // το JobID που δίνουμε πίσω
    pid_t    pid;                
    JobState state;              // queued/active/finished
    char     command[JMS_LINE];  // η εντολή μετά το submit
    time_t   submit_time;        
    time_t   start_time;         
} Job;

#define MAX_JOBS 1024

// κοινά δεδομένα για coord και workers
static Job   jobs[MAX_JOBS];
static int   job_count   = 0;
static int   job_queue[MAX_JOBS];
static int   queue_start = 0, queue_count = 0;
static int   shutting_down = 0;

static int   shutdown_running = 0, shutdown_queued = 0, total_served = 0;

static pthread_t *workers = NULL, *worker_tids = NULL;
static int       *worker_ids = NULL, *worker_job = NULL, *worker_served = NULL;
static int        worker_count = 0;

static char base_path[PATH_MAX];

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  queue_not_empty = PTHREAD_COND_INITIALIZER;


static void queue_push(int job_index) {
    int pos = (queue_start + queue_count) % MAX_JOBS;
    // στην ουρά βάζουμε μόνο τη θέση του job στον πίνακα
    job_queue[pos] = job_index;
    queue_count++;
}

static int queue_pop(void) {
    int job_index = job_queue[queue_start];
    // παίρνουμε το πιο παλιό queued job
    queue_start = (queue_start + 1) % MAX_JOBS;
    queue_count--;
    return job_index;
}

static int open_server(int port) {
    struct sockaddr_in addr;
    int fd, yes = 1;

    // Socket του coordinator, εδώ θα συνδέεται η κονσόλα.
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) return -1;
    
    // Για να μη μας κρατάει κολλημένη τη θύρα μετά από restart.
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    // Λέμε σε ποια θύρα θα ακούει.
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);

    // Δένουμε το socket στη θύρα.
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(fd, 5) < 0) {
        close(fd);
        return -1;
    }
    // Από εδώ και μετά μπορεί να κάνει accept clients.
    return fd;
}

static void make_stamp(char *date, size_t dsz, char *hour, size_t hsz) {
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(date, dsz, "%Y%m%d", &tm_now);
    strftime(hour, hsz, "%H%M%S", &tm_now);
}

static void run_child(Job *job) {
    char cmd[JMS_LINE], *argv[64], *save, *tok;
    int argc = 0, out_fd, err_fd, null_fd;
    char date[16], hour[16], dir[PATH_MAX], out_name[PATH_MAX], err_name[PATH_MAX];

    snprintf(cmd, sizeof(cmd), "%s", job->command);
    tok = strtok_r(cmd, " \t", &save);
    while (tok && argc < 63) {
        argv[argc++] = tok;
        tok = strtok_r(NULL, " \t", &save);
    }
    argv[argc] = NULL;
    if (argc == 0) _exit(127);

    if (strlen(base_path) + 100 >= sizeof(dir)) _exit(127);
    make_stamp(date, sizeof(date), hour, sizeof(hour));
    snprintf(dir, sizeof(dir), "%s/outputs_%d_%d_%s_%s", base_path, job->id, (int)getpid(), date, hour);
    if (mkdir(dir, 0777) < 0 && errno != EEXIST) _exit(127);

    snprintf(out_name, sizeof(out_name), "%s/stdout_%d", dir, job->id);
    snprintf(err_name, sizeof(err_name), "%s/stderr_%d", dir, job->id);
    
    out_fd  = open(out_name, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    err_fd  = open(err_name, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    null_fd = open("/dev/null", O_RDONLY);
    
    if (out_fd < 0 || err_fd < 0 || null_fd < 0) _exit(127);

    dup2(null_fd, STDIN_FILENO);
    dup2(out_fd,  STDOUT_FILENO);
    dup2(err_fd,  STDERR_FILENO);
    close(null_fd); close(out_fd); close(err_fd);

    execvp(argv[0], argv);
    _exit(127);
}

static void *worker_main(void *arg) {
    int wid = *(int *)arg;

    pthread_mutex_lock(&mtx);
    worker_tids[wid] = pthread_self();
    pthread_mutex_unlock(&mtx);

    // κάθε worker περιμένει jobs μέχρι να γίνει shutdown
    for (;;) {
        int job_index;
        pid_t pid;

        pthread_mutex_lock(&mtx);
        // αν δεν υπάρχει job, κοιμάται εδώ χωρίς busy waiting
        while (queue_count == 0 && !shutting_down) {
            pthread_cond_wait(&queue_not_empty, &mtx);
        }

        // στο shutdown ξυπνάει και βγαίνει
        if (shutting_down) {
            pthread_mutex_unlock(&mtx);
            break;
        }

        // παίρνει το πρώτο job από την ουρά
        job_index = queue_pop();
        jobs[job_index].state = JOB_ACTIVE;
        jobs[job_index].start_time = time(NULL);
        worker_job[wid] = jobs[job_index].id;
        pthread_mutex_unlock(&mtx);

        if ((pid = fork()) == 0) {
            run_child(&jobs[job_index]);
        }

        pthread_mutex_lock(&mtx);
        jobs[job_index].pid = pid;
        pthread_mutex_unlock(&mtx);

        if (pid > 0) {
            while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {}
        }

        pthread_mutex_lock(&mtx);
        // μόλις τελειώσει, ενημερώνει τα κοινά στοιχεία
        jobs[job_index].state = JOB_FINISHED;
        worker_job[wid] = 0;
        worker_served[wid]++;
        total_served++;
        pthread_mutex_unlock(&mtx);
    }
    return NULL;
}

static void send_status(int client_fd, int id) {
    char reply[JMS_LINE];

    pthread_mutex_lock(&mtx);
    // αν δεν υπάρχει τέτοιο id, το λέμε απλά
    if (id <= 0 || id > job_count) {
        snprintf(reply, sizeof(reply), "JobID %d not found", id);
    } else {
        // αλλιώς γυρνάμε την κατάσταση που έχουμε κρατήσει
        Job *j = &jobs[id - 1];
        if (j->state == JOB_ACTIVE) {
            snprintf(reply, sizeof(reply), "JobID %d Status: Active (running for %ld seconds)", j->id, (long)(time(NULL) - j->start_time));
        } else if (j->state == JOB_QUEUED) {
            snprintf(reply, sizeof(reply), "JobID %d Status: Queued (waiting in job queue)", j->id);
        } else {
            snprintf(reply, sizeof(reply), "JobID %d Status: Finished", j->id);
        }
    }
    pthread_mutex_unlock(&mtx);
    write_line(client_fd, reply);
}

static void send_status_all(int client_fd, int since_secs) {
    char line[JMS_LINE];
    int i, found = 0;
    time_t cutoff = (since_secs > 0) ? (time(NULL) - since_secs) : 0;

    pthread_mutex_lock(&mtx);
    if (job_count == 0) {
        pthread_mutex_unlock(&mtx);
        write_line(client_fd, "No jobs\n.");
        return;
    }

    for (i = 0; i < job_count; i++) {
        Job *j = &jobs[i];
        if (since_secs > 0 && j->submit_time < cutoff) continue;
        found = 1;

        if (j->state == JOB_ACTIVE) {
            snprintf(line, sizeof(line), "JobID %d Status: Active (running for %ld sec)", j->id, (long)(time(NULL) - j->start_time));
        } else if (j->state == JOB_QUEUED) {
            snprintf(line, sizeof(line), "JobID %d Status: Queued", j->id);
        } else {
            snprintf(line, sizeof(line), "JobID %d Status: Finished", j->id);
        }
        
        pthread_mutex_unlock(&mtx);
        write_line(client_fd, line);
        pthread_mutex_lock(&mtx);
    }
    pthread_mutex_unlock(&mtx);

    if (!found) write_line(client_fd, "No jobs");
    write_line(client_fd, ".");
}

static void send_jobs_by_state(int client_fd, JobState state, const char *title) {
    char line[JMS_LINE];
    int i;

    // ίδια λογική για show-active και show-finished
    write_line(client_fd, title);

    pthread_mutex_lock(&mtx);
    for (i = 0; i < job_count; i++) {
        if (jobs[i].state == state) {
            snprintf(line, sizeof(line), "JobID %d", jobs[i].id);
            pthread_mutex_unlock(&mtx);
            write_line(client_fd, line);
            pthread_mutex_lock(&mtx);
        }
    }
    pthread_mutex_unlock(&mtx);
    write_line(client_fd, ".");
}

static void send_workers(int client_fd) {
    char line[JMS_LINE];
    int i;

    write_line(client_fd, "Worker TID, State, Served:");

    pthread_mutex_lock(&mtx);
    for (i = 0; i < worker_count; i++) {
        // κρατάμε απλό output για να φαίνεται αν ο worker είναι idle ή τρέχει job
        if (worker_job[i] == 0) {
            snprintf(line, sizeof(line), "0x%lx idle served %d", (unsigned long)worker_tids[i], worker_served[i]);
        } else {
            snprintf(line, sizeof(line), "0x%lx running JobID %d served %d", (unsigned long)worker_tids[i], worker_job[i], worker_served[i]);
        }
        pthread_mutex_unlock(&mtx);
        write_line(client_fd, line);
        pthread_mutex_lock(&mtx);
    }
    pthread_mutex_unlock(&mtx);
    write_line(client_fd, ".");
}

static void *handler_main(void *arg) {
    int client_fd = *(int *)arg;
    char line[JMS_LINE];

    free(arg);

    while (read_line(client_fd, line, sizeof(line)) > 0) {
        // submit: κρατάμε το job και το βάζουμε στην ουρά
        if (strncmp(line, "submit ", 7) == 0) {
            char reply[JMS_LINE];
            Job *job;

            pthread_mutex_lock(&mtx);
            if (shutting_down) {
                pthread_mutex_unlock(&mtx);
                write_line(client_fd, "server is shutting down");
                continue;
            }
            // μη βγούμε έξω από τον πίνακα
            if (job_count >= MAX_JOBS) {
                pthread_mutex_unlock(&mtx);
                write_line(client_fd, "too many jobs");
                continue;
            }
            
            // νέο JobID και αρχικά queued
            job = &jobs[job_count];
            job->id = job_count + 1;
            job->pid = -1;
            job->state = JOB_QUEUED;
            job->submit_time = time(NULL);
            job->start_time = 0;
            snprintf(job->command, sizeof(job->command), "%s", line + 7);
            queue_push(job_count);
            job_count++;
            // ξυπνάμε έναν worker για να πάρει το καινούριο job
            pthread_cond_signal(&queue_not_empty);
            snprintf(reply, sizeof(reply), "JobID: %d", job->id);
            pthread_mutex_unlock(&mtx);
            write_line(client_fd, reply);
        } else if (strncmp(line, "status ", 7) == 0) {
            // status για ένα job
            send_status(client_fd, atoi(line + 7));
        } else if (strncmp(line, "status-all", 10) == 0) {
            // status για όλα τα jobs
            int n = (line[10] == ' ') ? atoi(line + 11) : 0;
            send_status_all(client_fd, n);
        } else if (strcmp(line, "show-active") == 0) {
            // όσα jobs τρέχουν τώρα
            send_jobs_by_state(client_fd, JOB_ACTIVE, "Active jobs:");
        } else if (strcmp(line, "show-finished") == 0) {
            // όσα jobs έχουν τελειώσει
            send_jobs_by_state(client_fd, JOB_FINISHED, "Finished jobs:");
        } else if (strcmp(line, "show-workers") == 0) {
            // τι κάνει κάθε worker
            send_workers(client_fd);
        } else if (strcmp(line, "shutdown") == 0) {
            // κλείσιμο server και workers
            int i, running_now = 0, queued_now = 0;
            char reply[JMS_LINE];

            pthread_mutex_lock(&mtx);
            shutting_down = 1;
            
            for (i = 0; i < job_count; i++) {
                if (jobs[i].state == JOB_ACTIVE) running_now++;
                if (jobs[i].state == JOB_QUEUED) queued_now++;
            }
            shutdown_running = running_now;
            shutdown_queued = queued_now;
            
            // ξυπνάμε όσους workers κοιμούνται στην ουρά
            pthread_cond_broadcast(&queue_not_empty);
            pthread_mutex_unlock(&mtx);

            // περιμένουμε να φύγουν πριν κλείσει το πρόγραμμα
            for (i = 0; i < worker_count; i++) {
                pthread_join(workers[i], NULL);
            }

            snprintf(reply, sizeof(reply), "Served %d jobs, %d were running, %d were still queued",
                     total_served, shutdown_running, shutdown_queued);
            write_line(client_fd, reply);

            close(client_fd);
            free(workers); free(worker_ids); free(worker_job);
            free(worker_served); free(worker_tids);
            exit(0);

        } else {
            write_line(client_fd, "unknown command");
        }
    }

    close(client_fd);
    return NULL;
}

int main(int argc, char **argv) {
    int port = 0, listen_fd, i;
    base_path[0] = '\0';

    // παίρνουμε port και πόσους workers θέλουμε
    for (i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) return 1;
        if (strcmp(argv[i], "-p") == 0) port = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "-n") == 0) worker_count = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "-l") == 0) snprintf(base_path, sizeof(base_path), "%s", argv[i + 1]);
    }

    if (port <= 0 || worker_count <= 0 || base_path[0] == '\0') return 1;

    if (mkdir(base_path, 0777) < 0 && errno != EEXIST) return 1;

    if ((listen_fd = open_server(port)) < 0) return 1;

    workers       = calloc(worker_count, sizeof(*workers));
    worker_ids    = calloc(worker_count, sizeof(*worker_ids));
    worker_job    = calloc(worker_count, sizeof(*worker_job));
    worker_served = calloc(worker_count, sizeof(*worker_served));
    worker_tids   = calloc(worker_count, sizeof(*worker_tids));
    
    if (!workers || !worker_ids || !worker_job || !worker_served || !worker_tids) return 1;

    // φτιάχνουμε το σταθερό pool από workers μία φορά στην αρχή
    for (i = 0; i < worker_count; i++) {
        worker_ids[i] = i;
        if (pthread_create(&workers[i], NULL, worker_main, &worker_ids[i]) != 0) return 1;
    }

    // δέχεται client και διαβάζει εντολές
    for (;;) {
        int *client_fd_ptr;
        pthread_t htid;
        int client_fd = accept(listen_fd, NULL, NULL);
        
        if (client_fd < 0) continue;

        if ((client_fd_ptr = malloc(sizeof(int))) == NULL) {
            close(client_fd);
            continue;
        }
        *client_fd_ptr = client_fd;

        if (pthread_create(&htid, NULL, handler_main, client_fd_ptr) != 0) {
            free(client_fd_ptr);
            close(client_fd);
            continue;
        }
        pthread_detach(htid);
    }
}