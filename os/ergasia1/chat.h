#ifndef CHAT_H
#define CHAT_H
#include <sys/types.h>
#define MAX_DIALOGS 5
#define MAX_MESSAGES 20 
#define MAX_PROC 10
#define MAX_PAYLOAD 256
#define KEY_FILE "/tmp/chat_key"

typedef struct {
    int dialog_id;
    int sender_pid;
    char payload[MAX_PAYLOAD];
    int read_count;
    int readers[MAX_PROC];
    int is_valid;
}Message;

typedef struct {
    int dialog_id;
    int participant_count;
    int participants[MAX_PROC];
    int is_active;
    int should_terminate;
}Dialog;

typedef struct {
    Dialog dialogs[MAX_DIALOGS];
    Message messages[MAX_MESSAGES];
    int dialog_count;
    int message_count;
}SharedData;

// FUNCTION DECLARATIONS

// Αρχικοποίηση συστήματος
void init_system();
// Διαχείριση διαλόγων
void join_dialog(int dialog_id, int my_pid);
void leave_dialog(int dialog_id, int my_pid);
int get_participant_count(int dialog_id);
// Τερματισμός
int should_terminate(int dialog_id);
void set_terminate_flag(int dialog_id);
void cleanup(int dialog_id, int my_pid);
// Μηνύματα
void send_msg(int dialog_id, int my_pid, char *text);
int receive_msgs(int dialog_id, int my_pid);
// Συγχρονισμός
void lock();
void unlock();

#endif
