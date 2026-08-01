#ifndef JMS_COMMON_H
#define JMS_COMMON_H
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define JMS_PIPE_IN "jms_in"
#define JMS_PIPE_OUT "jms_out"
#define MAX_LINE 4096
#define MAX_TEXT 65536
#define WAIT_MS 3000
// Job states
enum { JOB_ACTIVE, JOB_FINISHED, JOB_SUSPENDED };

//διασφαλίζει ότι όλα τα δεδομένα έχουν γραφτεί στο fd, διαχειρίζοντας διακοπές και μερικές εγγραφές
static inline int write_all(int fd, const void *buf, size_t n) {
    const char *p = buf;
    while (n > 0) {
        ssize_t k = write(fd, p, n);
        if (k < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += k;
        n -= (size_t) k;
    }
    return 0;
}

//βοηθητική συνάρτηση για την προσθήκη κειμένου σε buffer 
static inline int append_text(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    size_t len= strlen(buf);
    int n;
    if (len >= size) return -1;
    va_start(ap, fmt);
    n= vsnprintf(buf+len, size-len, fmt, ap);
    va_end(ap);
    if (n < 0 || len+ (size_t) n >= size) return -1;
    return 0;
}

//αφαίρεση χαρακτήρα νέας γραμμής από το τέλος string
static inline void trim_newline(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = '\0';
}

//καθαρισμός αρχικών και τελικών κενών χαρακτήρων από string
static inline char *skip_spaces(char *s) {
    while (*s && isspace((unsigned char) *s)) s++;
    return s;
}
static inline void trim_spaces(char *s) {
    size_t n;
    char *p = skip_spaces(s);
    if (p != s) memmove(s, p, strlen(p) + 1);
    n = strlen(s);
    while (n > 0 && isspace((unsigned char) s[n - 1])) s[--n] = '\0';
}

//ασφαλή μετατροπή string σε ακέραιο
static inline int parse_int(const char *s, int *value) {
    char *end;
    long x;
    errno = 0;
    x = strtol(s, &end, 10);
    if (errno || end == s || *end != '\0' || x < INT_MIN || x > INT_MAX) return -1;
    *value = (int) x;
    return 0;
}
static inline void sleep_ms(long ms) {
    // sleep για ms χιλιοστά του δευτερολέπτου, για διακοπές
    struct timespec ts;
    if (ms <= 0) return;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) < 0 && errno == EINTR) {
    }
}

//δημιουργεί αναδρομικά directories για το path
static inline int mkdir_p(const char *path) {
    char tmp[PATH_MAX];
    char *p;
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof(tmp)) return -1;
    snprintf(tmp, sizeof(tmp), "%s", path);
    if (n > 1 && tmp[n - 1]== '/') tmp[n - 1] = '\0';

    for (p= tmp + 1; *p; p++) {
        if (*p!= '/') continue;
        *p= '\0';
        if (mkdir(tmp, 0777) < 0 && errno != EEXIST) return -1;
        *p= '/';
    }
    if (mkdir(tmp, 0777) < 0 && errno != EEXIST) return -1;
    return 0;
}

//δημιουργεί timestamp για ημερομηνία και ώρα
static inline void make_stamp(char *date, size_t dsz, char *hour, size_t hsz) {
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(date, dsz, "%Y%m%d", &tm_now);
    strftime(hour, hsz, "%H%M%S", &tm_now);
}
// δομές για pool και job, διαβιβάζονται γραμμες από file descriptors με poll και διαχειρίζονται με δυναμικούς πίνακες αποτρέποντας διακοπές και μερικές εγγραφές
static inline ssize_t read_line_fd(int fd, char *buf, size_t size, int timeout_ms) {
    size_t n = 0;

    if (size == 0) {
        errno = EINVAL;
        return -1;
    }
    buf[0] = '\0';
    while (n+ 1 < size){
        struct pollfd pfd;
        char c;
        ssize_t r;
        int pr;

        pfd.fd=fd;
        pfd.events= POLLIN| POLLHUP;
        pfd.revents=0;
        pr= poll(&pfd, 1, timeout_ms);
        if(pr== 0) {
            errno=ETIMEDOUT;
            return -1;
        }
        if (pr<0){
            if(errno== EINTR) continue;
            return -1;
        }
        r=read(fd, &c, 1);
        if(r== 0){
            buf[n]= '\0';
            return (ssize_t) n;
        }
        if(r< 0){
            if(errno == EINTR || errno == EAGAIN) continue;
            return -1;
        }
        buf[n++]= c;
        if (c == '\n') break;
    }
    buf[n]= '\0';
    return (ssize_t) n;
}

// αποστέλλει κείμενο σε fd, προσθέτοντας νέα γραμμή αν δεν υπάρχει και τελειώνει με "END\n", για να υποδεικνύει το τέλος του μπλοκ κειμένου
static inline int send_block(int fd, const char *text) {
    if (text && *text) {
        size_t len = strlen(text);
        if (write_all(fd, text, len) < 0) return -1;
        if (text[len - 1] != '\n' && write_all(fd, "\n", 1) < 0) return -1;
    }
    return write_all(fd, "END\n", 4);
}

// διαβάζει γραμμές από fd μέχρι να συναντήσει "END\n", προσθέτοντας τις σε out
static inline int read_block(int fd, char *out, size_t size, int timeout_ms) {
    char line[MAX_LINE];
    out[0] = '\0';

    for (;;) {
        ssize_t n = read_line_fd(fd, line, sizeof(line), timeout_ms);
        if (n <= 0) return -1;
        trim_newline(line);
        if (strcmp(line, "END") == 0) return 0;
        if (append_text(out, size, "%s\n", line) < 0) return -1;
    }
}

#endif
