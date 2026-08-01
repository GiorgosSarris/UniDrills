#include "jms_common.h"

static int write_fd;
static int read_fd;

//επιχειρεί να ανοίξει το pipe γραφής, με επαναλήψεις αν ο coord δεν είναι ακόμα έτοιμος
static int open_write_retry(const char *name) {
    int tries = WAIT_MS / 100;

    while (tries-- >= 0) {
        int fd = open(name, O_WRONLY | O_NONBLOCK);
        if (fd >= 0) {
            //επαναφορά σε blocking mode για κανονική ροή εγγραφής
            int flags = fcntl(fd, F_GETFL);
            if (flags >= 0) fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
            return fd;
        }
        if (errno != ENOENT && errno != ENXIO) return -1;
        sleep_ms(100);
    }

    errno = ETIMEDOUT;
    return -1;
}

//παρόμοια λειτουργία για το άνοιγμα του pipe ανάγνωσης
static int open_read_retry(const char *name) {
    int tries= WAIT_MS / 100;
    while (tries-- >= 0) {
        int fd = open(name, O_RDONLY | O_NONBLOCK);
        if (fd>= 0){
            int flags= fcntl(fd, F_GETFL);
            if (flags >= 0) fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
            return fd;
        }
        if (errno != ENOENT) return -1;
        sleep_ms(100);
    }
    errno= ETIMEDOUT;
    return -1;
}

//διαχειρίζεται τη ροή των εντολών: ανάγνωση, αποστολή στο coord και λήψη απάντησης
static int run_stream(FILE *fp, int *done) {
    char *line = NULL;
    size_t n = 0;
    char ans[MAX_TEXT];

    while (getline(&line, &n, fp) != -1) {
        trim_newline(line);
        trim_spaces(line);
        if (line[0] == '\0') continue;
        // αποστολή της εντολής μέσω του pipe εισόδου
        if (write_all(write_fd, line, strlen(line)) < 0 ||
            write_all(write_fd, "\n", 1) < 0) {
            free(line);
            return -1;
        }
        //αναμονή για το μπλοκ απάντησης από τον coordinator
        for (;;) {
            if (read_block(read_fd, ans, sizeof(ans), WAIT_MS) == 0) break;
            if (errno != ETIMEDOUT) {
                free(line);
                return -1;
            }
        }
        //εκτύπωση της απάντησης στο stdout
        if (*ans && write_all(STDOUT_FILENO, ans, strlen(ans)) < 0) {
            free(line);
            return -1;
        }
        //ελεγχος αν η εντολή ήταν τερματισμού
        if (strcmp(line, "shutdown") == 0) {
            *done= 1;
            break;
        }
    }

    free(line);
    return 0;
}

int main(int argc, char *argv[]) {
    char in_name[PATH_MAX]= "";
    char out_name[PATH_MAX]= "";
    char ops_name[PATH_MAX]= "";
    int i, done = 0;

    //επεξεργασία των ορισμάτων
    for (i= 1; i< argc; i+= 2) {
        if (i+1>= argc) return 1;
        if (strcmp(argv[i], "-w") == 0) snprintf(in_name, sizeof(in_name), "%s", argv[i + 1]);
        else if (strcmp(argv[i], "-r") == 0) snprintf(out_name, sizeof(out_name), "%s", argv[i + 1]);
        else if (strcmp(argv[i], "-o") == 0) snprintf(ops_name, sizeof(ops_name), "%s", argv[i + 1]);
        else return 1;
    }
    if (in_name[0] == '\0' || out_name[0] == '\0') return 1;
    
    //αρχικοποίηση των συνδέσεων με τα named pipes
    write_fd= open_write_retry(in_name);
    read_fd= open_read_retry(out_name);
    if (write_fd < 0 || read_fd < 0) return 1;

    //αν έχει δοθεί αρχείο εντολών, προηγείται η εκτέλεσή του
    if (ops_name[0]) {
        FILE *fp = fopen(ops_name, "r");
        if (!fp) {
            close(write_fd);
            close(read_fd);
            return 1;
        }
        if (run_stream(fp, &done) < 0) {
            fclose(fp);
            close(write_fd);
            close(read_fd);
            return 1;
        }
        fclose(fp);
    }

    //αν η κονσόλα παραμένει ενεργή, συνεχίζει με λήψη δεδομένων από το stdin
    if (!done && run_stream(stdin, &done) < 0) {
        close(write_fd);
        close(read_fd);
        return 1;
    }
    
    close(write_fd);
    close(read_fd);
    return 0;
}