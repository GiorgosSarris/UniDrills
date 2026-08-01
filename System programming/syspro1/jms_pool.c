#include "jms_common.h"

//δομή αναπαράστασης κάθε job στο pool
typedef struct {
    int id;
    pid_t pid;
    int state;
    time_t started;
} Job;

static Job *jobs;
static int job_count;
static int job_limit;
static int cmd_fd;
static int resp_fd;
static char base_path[PATH_MAX];
static volatile sig_atomic_t stop_now;

//χειριστής για σήμα τερματισμού (SIGTERM) από τον coordinator
static void on_term(int sig) {
    (void) sig;
    stop_now = 1;
}

//αναζητά job με βάση το id
static Job *find_job(int id) {
    int i;
    for (i = 0; i < job_count; i++) if (jobs[i].id == id) return &jobs[i];
    return NULL;
}

//ελέγχει την κατάσταση των παιδιών χωρίς να μπλοκάρει. ενημερώνει για κάποιο job την κατάσταση του
static void reap_jobs(void) {
    int status;
    pid_t pid;
    int i;

    //WNOHANG: επιστροφή αν δεν υπάρχει αλλαγή
    //WUNTRACED |WCONTINUED: Πιάνει τα SIGSTOP και SIGCONT
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        for (i = 0; i < job_count; i++) {
            if (jobs[i].pid != pid) continue;
            if (WIFEXITED(status) || WIFSIGNALED(status)) jobs[i].state = JOB_FINISHED;
            else if (WIFSTOPPED(status)) jobs[i].state = JOB_SUSPENDED;
            else if (WIFCONTINUED(status)) jobs[i].state = JOB_ACTIVE;
            break;
        }
    }
}

//ελέγχει αν έχουν τερματίσει όλα
static int all_finished(void) {
    int i;
    for (i = 0; i < job_count; i++) if (jobs[i].state != JOB_FINISHED) return 0;
    return 1;
}

//τρέχει την εντολή (εκτελείται από το child)
static void run_child(int id, const char *cmd) {
    char tmp[MAX_LINE];
    char *argv[128];
    char *tok;
    char *save;
    int argc = 0;
    char date[16], hour[16];
    char dir[PATH_MAX] = "", out1[PATH_MAX] = "", out2[PATH_MAX] = "";
    int fd1, fd2, fd0;

    // Parsing της εντολής και των arguments
    snprintf(tmp, sizeof(tmp), "%s", cmd);
    tok = strtok_r(tmp, " \t", &save);
    while (tok && argc < 127) {
        argv[argc++] = tok;
        tok = strtok_r(NULL, " \t", &save);
    }
    argv[argc] = NULL;
    if (argc == 0) _exit(127);

    // δημιουργία καταλόγων για τα outputs με timestamps σε folder name
    if (mkdir_p(base_path)< 0) _exit(127);
    make_stamp(date, sizeof(date), hour, sizeof(hour));
    append_text(dir, sizeof(dir), "%s/outputs_%d_%d_%s_%s", base_path, id, (int) getpid(), date, hour);
    if (mkdir(dir, 0777)< 0 && errno != EEXIST) _exit(127);
    append_text(out1, sizeof(out1), "%s/stdout_%d", dir, id);
    append_text(out2, sizeof(out2), "%s/stderr_%d", dir, id);

    //ανοιγμα αρχείων και ανακατεύθυνση stdin, stdout, stderr 
    fd1=open(out1, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    fd2=open(out2, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    fd0=open("/dev/null", O_RDONLY);
    if (fd1 < 0 || fd2 < 0 || fd0 < 0) _exit(127);

    //ανακατεύθυνση: stdin στο /dev/null, stdout/stderr στα αντίστοιχα αρχεία
    dup2(fd0, 0);
    dup2(fd1, 1);
    dup2(fd2, 2);
    close(fd0);
    close(fd1);
    close(fd2);
    
    // εκτέλεση της εντολής
    execvp(argv[0], argv);
    _exit(127); //Αν φτάσει εδώ, η execvp απέτυχε
}

//χειρίζεται αίτημα νέου job: κάνει fork, τρέχει την εντολή και απαντά στον coordinator
static void answer_submit(int id, const char *cmd) {
    char out[MAX_TEXT] = "";
    pid_t pid;

    if (job_count >= job_limit) {
        append_text(out, sizeof(out), "ERR\n");
        send_block(resp_fd, out);
        return;
    }
    pid = fork();
    if (pid < 0) {
        append_text(out, sizeof(out), "ERR\n");
        send_block(resp_fd, out);
        return;
    }
    if (pid== 0) {
        // επαναφορά των signal handlers στο state πριν την execvp
        signal(SIGTERM, SIG_DFL);
        signal(SIGPIPE, SIG_DFL);
        run_child(id, cmd);
    }
    // καταγραφή του νέου από τον parent
    jobs[job_count].id= id;
    jobs[job_count].pid= pid;
    jobs[job_count].state= JOB_ACTIVE;
    jobs[job_count].started= time(NULL);
    job_count++;
    append_text(out, sizeof(out), "OK %d\n", (int) pid);
    send_block(resp_fd, out);
}

//χειρίζεται αιτήματα suspend/resume για ενα job
static void answer_signal(int id, int sig) {
    char out[MAX_TEXT] = "";
    Job *j;
    reap_jobs();
    j = find_job(id);
    if (!j || j->state == JOB_FINISHED || kill(j->pid, sig) < 0) {
        append_text(out, sizeof(out), "ERR\n");
    } else {
        j->state = (sig == SIGSTOP) ? JOB_SUSPENDED : JOB_ACTIVE;
        append_text(out, sizeof(out), "OK\n");
    }
    send_block(resp_fd, out);
}

// στέλνει snapshot όλων των jobs πίσω στον coordinator
static void answer_snapshot(void) {
    char out[MAX_TEXT] = "";
    time_t now= time(NULL);
    int i, active= 0;
    reap_jobs();
    for (i = 0; i < job_count; i++) {
        long sec = 0;
        if (jobs[i].state == JOB_ACTIVE) {
            active++;
            sec = (long) (now - jobs[i].started);
        }
        append_text(out, sizeof(out), "JOB %d %d %d %ld\n",
                    jobs[i].id, (int) jobs[i].pid, jobs[i].state, sec);
    }
    // τελικό block με συνολικά στατιστικά του pool
    append_text(out, sizeof(out), "POOL %d %d\n", active, job_count);
    send_block(resp_fd, out);
}

//τερματισμός όλων των jobs του pool
static void kill_jobs(void) {
    int i, loops;
    //στέλνουμε SIGTERM για ομαλό τερματισμό
    for (i = 0; i < job_count; i++) if (jobs[i].state != JOB_FINISHED) kill(jobs[i].pid, SIGTERM);
    
    //περιμένουμε μέχρι 3 δευτερόλεπτα
    for (loops = 0; loops < 30 && !all_finished(); loops++) {
        reap_jobs();
        sleep_ms(100);
    }
    //αν κάποια jobs δεν έκλεισαν, στέλνουμε SIGKILL
    if (!all_finished()) {
        for (i = 0; i < job_count; i++) if (jobs[i].state != JOB_FINISHED) kill(jobs[i].pid, SIGKILL);
        for (loops = 0; loops < 10 && !all_finished(); loops++) {
            reap_jobs();
            sleep_ms(100);
        }
    }
}

int main(int argc, char *argv[]) {
    int i, id_dummy = 0;
    struct sigaction sa;
    signal(SIGPIPE, SIG_IGN);
    
    // setup του handler για το SIGTERM
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_term;
    sigaction(SIGTERM, &sa, NULL);

    // parsing παραμέτρων που δόθηκαν από την exec() του coordinator
    for (i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) return 1;
        if (strcmp(argv[i], "-i") == 0) parse_int(argv[i + 1], &id_dummy);
        else if (strcmp(argv[i], "-n") == 0) parse_int(argv[i + 1], &job_limit);
        else if (strcmp(argv[i], "-l") == 0) snprintf(base_path, sizeof(base_path), "%s", argv[i + 1]);
        else if (strcmp(argv[i], "-c") == 0) cmd_fd = open(argv[i + 1], O_RDONLY);
        else if (strcmp(argv[i], "-r") == 0) resp_fd = open(argv[i + 1], O_WRONLY);
        else return 1;
    }

    if (job_limit <= 0 || cmd_fd < 0 || resp_fd < 0 || base_path[0] == '\0') return 1;
    jobs = calloc((size_t) job_limit, sizeof(Job));
    if (!jobs) return 1;

    // κύρια λούπα αναμονής εντολών από τον coordinator
    while (!stop_now) {
        struct pollfd pfd;
        char line[MAX_LINE];
        char out[MAX_TEXT] = "";
        ssize_t n;
        reap_jobs();
        
        //τερματισμός του pool αν έχει φτάσει το όριο και όλα τα jobs έχουν τελειώσει
        if (job_count == job_limit && all_finished()) break;

        //Polling στο file descriptor εισόδου
        pfd.fd = cmd_fd;
        pfd.events = POLLIN | POLLHUP;
        pfd.revents = 0;
        if (poll(&pfd, 1, 200) <= 0) continue;
        n= read_line_fd(cmd_fd, line, sizeof(line), 100);
        if (n <= 0) continue;
        trim_newline(line);

        //διαχωρισμός και εκτέλεση της εντολής
        if (strncmp(line, "submit ", 7) == 0) {
            char *p = line + 7;
            char *q = strchr(p, ' ');
            int id;
            if (q) {
                *q = '\0'; //σπάσιμο string σε id και cmd
                if (parse_int(p, &id) == 0) answer_submit(id, q + 1);
                else {
                    append_text(out, sizeof(out), "ERR\n");
                    send_block(resp_fd, out);
                }
            } else {
                append_text(out, sizeof(out), "ERR\n");
                send_block(resp_fd, out);
            }
        } else if (strncmp(line, "suspend ", 8) == 0) {
            int id;
            if (parse_int(line + 8, &id) == 0) answer_signal(id, SIGSTOP);
            else {
                append_text(out, sizeof(out), "ERR\n");
                send_block(resp_fd, out);
            }
        } else if (strncmp(line, "resume ", 7) == 0) {
            int id;
            if (parse_int(line + 7, &id) == 0) answer_signal(id, SIGCONT);
            else {
                append_text(out, sizeof(out), "ERR\n");
                send_block(resp_fd, out);
            }
        } else if (strcmp(line, "snapshot") == 0) {
            answer_snapshot();
        } else {
            append_text(out, sizeof(out), "ERR\n");
            send_block(resp_fd, out);
        }
    }

    kill_jobs();
    close(cmd_fd);
    close(resp_fd);
    free(jobs);
    return 0;
}