#include "jms_common.h"

//διαχειριση κάθε pool process και των αντίστοιχων job
typedef struct {
    int id;
    pid_t pid;
    int in_fd;
    int out_fd;
    int total_jobs;
    int active_jobs;
    int alive;
    char in_name[PATH_MAX];
    char out_name[PATH_MAX];
} Pool;
typedef struct {
    int id;
    pid_t pid;
    int pool_id;
    int state;
    time_t submitted;
    long sec;
} Job;
static Pool *pools;
static Job *jobs;
static int pool_count, pool_cap;
static int job_count, job_cap;
static int jobs_per_pool;
static int console_in, console_out;
static char base_path[PATH_MAX];

// ανοίγει τα named pipes για επικοινωνία με τα pool processes, με επαναλήψεις αν δεν είναι ακόμα έτοιμα και απενεργοποιεί το O_NONBLOCK 
static int open_pool_read(const char *name) {
    int fd = open(name, O_RDONLY | O_NONBLOCK);
    if (fd >= 0) {
        int flags = fcntl(fd, F_GETFL);
        if (flags >= 0) fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    }
    return fd;
}

//Ανοίγει το pipe εγγραφής προς το pool. Κάνει μερικές προσπάθειες αν δεν έχει δημιουργηθεί ακόμα
static int open_pool_write(const char *name) {
    int tries = WAIT_MS/ 100;

    while (tries-- >= 0) {
        int fd = open(name, O_WRONLY | O_NONBLOCK);
        if (fd >= 0) {
            int flags = fcntl(fd, F_GETFL);
            if (flags >= 0) fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
            return fd;
        }
        if (errno != ENXIO && errno != ENOENT) return -1;
        sleep_ms(100);
    }

    errno = ETIMEDOUT;
    return -1;
}

//διαβάζει την απάντηση του pool με περιθώριο κάποιων επαναλήψεων (timeout)
static int read_pool_reply(Pool *p, char *buf, size_t size) {
    int tries= 3;
    while (tries-- > 0) {
        if (read_block(p->out_fd, buf, size, WAIT_MS) == 0) return 0;
        if (errno != ETIMEDOUT) return -1;
    }

    return -1;
}

//επέκταση του δυναμικού πίνακα pools
static int grow_pools(void) {
    int ncap = pool_cap ? 2 * pool_cap : 4;
    Pool *tmp = realloc(pools, (size_t) ncap * sizeof(Pool));
    if (!tmp) return -1;
    pools = tmp;
    pool_cap = ncap;
    return 0;
}

//αντίστοιχα για τον πίνακα jobs
static int grow_jobs(void) {
    int ncap = job_cap ? 2 * job_cap : 16;
    Job *tmp = realloc(jobs, (size_t) ncap * sizeof(Job));
    if (!tmp) return -1;
    jobs = tmp;
    job_cap = ncap;
    return 0;
}

//επιστρέφει δείκτη σε job με βάση το id
static Job *get_job(int id) {
    if (id < 1 || id > job_count) return NULL;
    return &jobs[id - 1];
}

//αντίστοιχα για pool με βάση το id
static Pool *get_pool(int id) {
    int i;
    for (i = 0; i < pool_count; i++) if (pools[i].id == id) return &pools[i];
    return NULL;
}

//σηματοδοτεί ένα pool ως νεκρό, κλείνει fd's, διαγράφει pipes και ενημερώνει τα jobs του
static void mark_pool_dead(Pool *p) {
    int i;
    if (!p || !p->alive) return;
    p->alive = 0;
    p->active_jobs = 0;
    if (p->in_fd >= 0) close(p->in_fd);
    if (p->out_fd >= 0) close(p->out_fd);
    p->in_fd = p->out_fd = -1;
    unlink(p->in_name);
    unlink(p->out_name);
    // οσα jobs έτρεχαν σε αυτό το pool θεωρούνται πλέον finished
    for (i = 0; i < job_count; i++) if (jobs[i].pool_id == p->id && jobs[i].state != JOB_FINISHED) jobs[i].state = JOB_FINISHED;
}

//μαζεύει τα processes των pools που έχουν τερματίσει (αποφυγή zombies)
static void reap_pools(void) {
    int i, status;
    pid_t pid;
    while ((pid= waitpid(-1, &status, WNOHANG)) > 0) {
        for (i = 0; i < pool_count; i++) if (pools[i].pid == pid) mark_pool_dead(&pools[i]);
    }
}

//ζητάει ενημέρωση από το pool για να συγχρονίσει την κατάσταση των jobs του
static void refresh_pool(Pool *p) {
    char msg[MAX_TEXT], *line, *save;
    if (!p || !p->alive) return;
    //αν δεν απαντήσει, θεωρείται νεκρό
    if (write_all(p->in_fd, "snapshot\n", 9) < 0 || read_pool_reply(p, msg, sizeof(msg)) < 0) {
        mark_pool_dead(p);
        return;
    }

    p->active_jobs = 0;
    line = strtok_r(msg, "\n", &save);
    while (line) {
        int id, pid, st, a, t;
        long sec;
        //Κάνουμε parse τα δεδομένα που μας έστειλε το pool
        if (sscanf(line, "JOB %d %d %d %ld", &id, &pid, &st, &sec) == 4) {
            Job *j = get_job(id);
            if (j) {
                j->pid = pid;
                j->state = st;
                j->sec = sec;
            }
        } else if (sscanf(line, "POOL %d %d", &a, &t) == 2) {
            p->active_jobs = a;
            p->total_jobs = t;
        }
        line = strtok_r(NULL, "\n", &save);
    }
}

// κάνει refresh σε όλα τα ενεργά pools και ταυτόχρονα μαζεύει τυχόν zombies
static void refresh_all(void) {
    int i;
    reap_pools();
    for (i = 0; i < pool_count; i++) refresh_pool(&pools[i]);
    reap_pools();
}

// δημιουργεί νέο pool process, με τα απαραίτητα pipes και ενημερώνει τον πίνακα pools
static int new_pool(void) {
    Pool p;
    char id[32], lim[32];
    pid_t pid;

    if (pool_count == pool_cap && grow_pools() < 0) return -1;

    memset(&p, 0, sizeof(p));
    p.id = pool_count + 1;
    p.in_fd = p.out_fd = -1;
    p.alive = 1;
    p.in_name[0] = '\0';
    p.out_name[0] = '\0';
    append_text(p.in_name, sizeof(p.in_name), "%s/pool_%d_in", base_path, p.id);
    append_text(p.out_name, sizeof(p.out_name), "%s/pool_%d_out", base_path, p.id);
    // δημιουργία των named pipes για το νέο pool
    unlink(p.in_name);
    unlink(p.out_name);
    if (mkfifo(p.in_name, 0666) < 0) return -1;
    if (mkfifo(p.out_name, 0666) < 0) {
        unlink(p.in_name);
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        //το child process δεν δημιουργήθηκε, καθαρισμός
        unlink(p.in_name);
        unlink(p.out_name);
        return -1;
    }
    if (pid == 0) {
        //στο child process, εκτέλεση του jms_pool με τα κατάλληλα ορίσματα
        snprintf(id, sizeof(id), "%d", p.id);
        snprintf(lim, sizeof(lim), "%d", jobs_per_pool);
        execl("./jms_pool", "jms_pool",
              "-i", id, "-n", lim, "-l", base_path, "-c", p.in_name, "-r", p.out_name,
              (char *) NULL);
        perror("execl");
        _exit(1);
    }

    //στο parent process, συνδέεται στα fifo του pool
    p.pid= pid;
    p.out_fd= open_pool_read(p.out_name);
    p.in_fd= open_pool_write(p.in_name);
    if (p.in_fd < 0 || p.out_fd < 0) {
        if (p.in_fd >= 0) close(p.in_fd);
        if (p.out_fd >= 0) close(p.out_fd);
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        unlink(p.in_name);
        unlink(p.out_name);
        return -1;
    }
    pools[pool_count++]= p;
    return 0;
}

//βρίσκει pool με διαθέσιμο χώρο για νέο job, αν δεν υπάρχει δημιουργεί νέο pool
static Pool *submit_pool(void) {
    int i;
    for (i = pool_count - 1; i >= 0; i--) if (pools[i].alive && pools[i].total_jobs < jobs_per_pool) return &pools[i];
    if (new_pool() < 0) return NULL;
    return &pools[pool_count - 1];
}

//προσθέτει την κατάσταση ενός job σε out, με βάση το state και τον χρόνο που τρέχει
static void add_status(char *out, Job *j) {
    if (j->state == JOB_ACTIVE) append_text(out, MAX_TEXT, "JobID %d Status: Active (running for %ld seconds)\n", j->id, j->sec);
    else if (j->state == JOB_SUSPENDED) append_text(out, MAX_TEXT, "JobID %d Status: Suspended\n", j->id);
    else append_text(out, MAX_TEXT, "JobID %d Status: Finished\n", j->id);
}

//submit: βρίσκει ή δημιουργεί pool, στέλνει την εντολή και ενημερώνει τον πίνακα jobs
static void do_submit(const char *cmd, char *out) {
    Pool *p;
    char req[MAX_LINE+128], ans[MAX_TEXT];
    int pid;

    refresh_all();
    if (job_count == job_cap && grow_jobs() < 0) return;
    p = submit_pool();
    if (!p) return;

    snprintf(req, sizeof(req), "submit %d %s\n", job_count + 1, cmd);
    if (write_all(p->in_fd, req, strlen(req)) < 0 || read_pool_reply(p, ans, sizeof(ans)) < 0) {
        mark_pool_dead(p);
        return;
    }

    trim_newline(ans);
    if (strncmp(ans, "OK ", 3) != 0 || parse_int(ans + 3, &pid) < 0) return;
    //καταχώρηση του job στα εσωτερικά structs
    jobs[job_count].id= job_count+ 1;
    jobs[job_count].pid= pid;
    jobs[job_count].pool_id= p->id;
    jobs[job_count].state= JOB_ACTIVE;
    jobs[job_count].submitted= time(NULL);
    jobs[job_count].sec= 0;
    job_count++;
    p->total_jobs++;
    append_text(out, MAX_TEXT, "JobID: %d, PID: %d\n", job_count, pid);
}

//στέλνει σήμα suspend ή resume σε ένα job, αν είναι ενεργό, και ενημερώνει την κατάσταση του στο pool
static void do_signal(const char *name, int sig, int id, char *out) {
    Job *j = get_job(id);
    Pool *p;
    char req[MAX_LINE], ans[MAX_TEXT];

    if (!j) return;
    p = get_pool(j->pool_id);
    if (!p || !p->alive || j->state == JOB_FINISHED) return;

    snprintf(req, sizeof(req), "%s %d\n", sig == SIGSTOP ? "suspend" : "resume", id);
    if (write_all(p->in_fd, req, strlen(req)) < 0 || read_pool_reply(p, ans, sizeof(ans)) < 0) {
        mark_pool_dead(p);
        return;
    }

    trim_newline(ans);
    if (strcmp(ans, "OK") != 0) return;
    j->state = (sig == SIGSTOP) ? JOB_SUSPENDED : JOB_ACTIVE;
    append_text(out, MAX_TEXT, "Sent %s signal to JobID %d\n", name, id);
}

//τερματισμός: στέλνει SIGTERM στα pools και περιμένει να κλείσουν, ενημερώνει τα jobs που τρέχουν και εκτυπώνει συνοπτική αναφορά
static void shutdown_all(char *out) {
    int i, unfinished= 0;
    refresh_all();
    for (i = 0; i < job_count; i++) if (jobs[i].state != JOB_FINISHED) unfinished++;
    for (i = 0; i < pool_count; i++) if (pools[i].alive) kill(pools[i].pid, SIGTERM);
    for (i = 0; i < pool_count; i++) if (pools[i].alive) waitpid(pools[i].pid, NULL, 0);
    for (i = 0; i < pool_count; i++) mark_pool_dead(&pools[i]);
    append_text(out, MAX_TEXT, "Served %d jobs, %d were still in progress\n", job_count, unfinished);
}

int main(int argc, char *argv[]) {
    int i, running = 1;
    //απενεργοποίηση του SIGPIPE για να μην τερματιστεί ο coordinator αν ο console κλείσει απρόσμενα
    signal(SIGPIPE, SIG_IGN);
    for (i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) return 1;
        if (strcmp(argv[i], "-l") == 0) snprintf(base_path, sizeof(base_path), "%s", argv[i + 1]);
        else if (strcmp(argv[i], "-n") == 0) parse_int(argv[i + 1], &jobs_per_pool);
        else return 1;
    }
    if (base_path[0] == '\0' || jobs_per_pool <= 0) return 1;
    if (mkdir_p(base_path) < 0) return 1;
    //δημιουργία των named pipes για την επικοινωνία με το jms_console
    unlink(JMS_PIPE_IN);
    unlink(JMS_PIPE_OUT);
    if (mkfifo(JMS_PIPE_IN, 0666) < 0) return 1;
    if (mkfifo(JMS_PIPE_OUT, 0666) < 0) {
        unlink(JMS_PIPE_IN);
        return 1;
    }
    console_in= open(JMS_PIPE_IN, O_RDWR);
    console_out= open(JMS_PIPE_OUT, O_RDWR);
    if (console_in < 0 || console_out < 0) return 1;
    //κύρια λούπα, διαχείριση εντολών από το console, μέχρι να ληφθεί εντολή shutdown
    while (running) {
        struct pollfd pfd;
        char line[MAX_LINE], out[MAX_TEXT] = "";
        ssize_t n;

        reap_pools();
        //poll στο pipe εισόδου για να δούμε αν υπάρχει νέα εντολή από το console
        pfd.fd=console_in;
        pfd.events= POLLIN | POLLHUP;
        pfd.revents=0;
        if (poll(&pfd, 1, 200) <= 0) continue;

        n = read_line_fd(console_in, line, sizeof(line), 100);
        if (n <= 0) continue;
        trim_newline(line);
        trim_spaces(line);
        if (line[0] == '\0') continue;
        //ανάλογα με την εντολή, καλούμε την αντίστοιχη συνάρτηση διαχείρισης
        if (strncmp(line, "submit ", 7) == 0) {
            do_submit(line + 7, out);
        } else if (strncmp(line, "status-all", 10) == 0) {
            int sec, have = 0, bad = 0;
            time_t now = time(NULL), cut = 0;
            char *p = line + 10;
            trim_spaces(p);
            if (*p) {
                if (parse_int(p, &sec) == 0 && sec >= 0) {
                    have = 1;
                    cut = now - sec;
                } else {
                    bad = 1;
                }
            }
            if (bad) {
                append_text(out, sizeof(out), "Invalid command\n");
            } else {
                refresh_all();
                for (i = 0; i < job_count; i++) if (!have || jobs[i].submitted >= cut) add_status(out, &jobs[i]);
            }
        } else if (strncmp(line, "status ", 7) == 0) {
            int id;
            if (parse_int(line + 7, &id) == 0 && get_job(id)) {
                Pool *p = get_pool(get_job(id)->pool_id);
                if (p) refresh_pool(p);
                add_status(out, get_job(id));
            }
        } else if (strcmp(line, "show-active") == 0) {
            refresh_all();
            append_text(out, sizeof(out), "Active jobs:\n");
            for (i = 0; i < job_count; i++) if (jobs[i].state == JOB_ACTIVE) append_text(out, sizeof(out), "JobID %d\n", jobs[i].id);
        } else if (strcmp(line, "show-finished") == 0) {
            refresh_all();
            append_text(out, sizeof(out), "Finished jobs:\n");
            for (i = 0; i < job_count; i++) if (jobs[i].state == JOB_FINISHED) append_text(out, sizeof(out), "JobID %d\n", jobs[i].id);
        } else if (strcmp(line, "show-pools") == 0) {
            refresh_all();
            append_text(out, sizeof(out), "Pool & NumOfJobs:\n");
            for (i = 0; i < pool_count; i++) if (pools[i].alive) append_text(out, sizeof(out), "%d %d\n", (int) pools[i].pid, pools[i].active_jobs);
        } else if (strncmp(line, "suspend ", 8) == 0) {
            int id;
            if (parse_int(line + 8, &id) == 0) do_signal("suspend", SIGSTOP, id, out);
        } else if (strncmp(line, "resume ", 7) == 0) {
            int id;
            if (parse_int(line + 7, &id) == 0) do_signal("resume", SIGCONT, id, out);
        } else if (strcmp(line, "shutdown") == 0) {
            shutdown_all(out);
            running = 0;
        } else {
            append_text(out, sizeof(out), "Invalid command\n");
        }
        //αποστολή της απάντησης στο console
        send_block(console_out, out);
    }
    //cleanup
    close(console_in);
    close(console_out);
    unlink(JMS_PIPE_IN);
    unlink(JMS_PIPE_OUT);
    free(pools);
    free(jobs);
    return 0;
}
