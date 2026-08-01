#include "jms_common.h"
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// Συνδέεται στον coordinator
static int connect_to_coord(const char *host, const char *port) {
    struct addrinfo hints, *res, *p;
    int fd = -1;

    // Βρίσκουμε πού είναι ο coordinator
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &res) != 0) return -1;

    // Δοκιμάζουμε μέχρι να πετύχει η σύνδεση
    for (p = res; p != NULL; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

// Ελέγχει αν η εντολή επιστρέφει πολλές γραμμές
static int is_multiline(const char *cmd) {
    return (strncmp(cmd, "status-all", 10) == 0 ||
            strcmp(cmd,  "show-active")    == 0 ||
            strcmp(cmd,  "show-finished")  == 0 ||
            strcmp(cmd,  "show-workers")   == 0);
}

// Στέλνει την εντολή και τυπώνει την απάντηση
static int send_command(int fd, const char *cmd) {
    char reply[JMS_LINE];

    // στέλνουμε την εντολή
    if (write_line(fd, cmd) < 0) return -1;

    if (is_multiline(cmd)) {
        // διαβάζουμε γραμμές μέχρι να φτάσει το "." sentinel
        while (read_line(fd, reply, sizeof(reply)) > 0) {
            if (strcmp(reply, ".") == 0) break;
            printf("%s\n", reply);
        }
    } else {
        // μία γραμμή απάντηση
        if (read_line(fd, reply, sizeof(reply)) <= 0) return -1;
        printf("%s\n", reply);

        // μετά το shutdown ο server στέλνει μόνο τα στατιστικά και κλείνει
        if (strcmp(cmd, "shutdown") == 0) {
            return -1;
        }
    }
    return 0;
}

// επεξεργασία εντολών από αρχείο ή stdin
static int process_stream(int fd, FILE *in) {
    char line[JMS_LINE];

    while (fgets(line, sizeof(line), in) != NULL) {
        // η fgets κρατάει newline, το βγάζουμε
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '\0') continue;  // αδειανή γραμμή

        if (send_command(fd, line) < 0) return -1;

        if (strcmp(line, "shutdown") == 0) return -1;
    }
    return 0;
}

// main
int main(int argc, char **argv) {
    char *host      = NULL;
    char *port      = NULL;
    char *ops_file  = NULL;  // προσθέσαμε -o
    int   fd;
    int   i;

    // παρσάρουμε και -o <operations_file>
    for (i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) {
            fprintf(stderr, "usage: %s -h <host> -p <port> [-o <ops_file>]\n", argv[0]);
            return 1;
        }
        if      (strcmp(argv[i], "-h") == 0) host     = argv[i + 1];
        else if (strcmp(argv[i], "-p") == 0) port     = argv[i + 1];
        else if (strcmp(argv[i], "-o") == 0) ops_file = argv[i + 1];
    }

    if (host == NULL || port == NULL) {
        fprintf(stderr, "usage: %s -h <host> -p <port> [-o <ops_file>]\n", argv[0]);
        return 1;
    }

    fd = connect_to_coord(host, port);
    if (fd < 0) {
        perror("connect");
        return 1;
    }

    // αν δόθηκε -o, τρέχουμε πρώτα τις εντολές από το αρχείο
    if (ops_file != NULL) {
        FILE *f = fopen(ops_file, "r");
        if (f == NULL) {
            perror(ops_file);
            close(fd);
            return 1;
        }
        if (process_stream(fd, f) < 0) {
            // αν ο server έκλεισε (shutdown) σταματάμε εντελώς
            fclose(f);
            close(fd);
            return 0;
        }
        fclose(f);
    }

    // μετά το αρχείο (ή αν δεν δόθηκε -o) συνεχίζουμε με stdin
    process_stream(fd, stdin);

    close(fd);
    return 0;
}