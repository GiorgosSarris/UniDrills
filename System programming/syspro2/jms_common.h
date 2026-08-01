#ifndef JMS_COMMON_H
#define JMS_COMMON_H
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#define JMS_LINE 1024

// Γράφει όλα τα δεδομένα στο socket. Αποφεύγει crash αν κλείσει ο client (MSG_NOSIGNAL)
static inline int write_all(int fd, const char *buf, size_t n) {
    while (n > 0) {
        ssize_t w = send(fd, buf, n, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        buf += (size_t)w;
        n   -= (size_t)w;
    }
    return 0;
}

// Στέλνει ένα string μαζί με χαρακτήρα αλλαγής γραμμής
static inline int write_line(int fd, const char *s) {
    if (write_all(fd, s, strlen(s)) != 0) return -1;
    return write_all(fd, "\n", 1);
}

// Διαβάζει από το socket μέχρι να βρει αλλαγή γραμμής
static inline ssize_t read_line(int fd, char *buf, size_t size) {
    size_t i = 0;
    if (size == 0) return -1;
    while (i + 1 < size) {
        char c;
        ssize_t r = read(fd, &c, 1);
        if (r == 0) break;      // Έκλεισε η σύνδεση
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (c == '\n') break;   // Τέλος γραμμής
        buf[i++] = c;
    }
    buf[i] = '\0';
    return (ssize_t)i;
}

#endif /* JMS_COMMON_H */