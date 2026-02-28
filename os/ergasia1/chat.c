#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/select.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include "chat.h"

//Shared memory και semaphore IDs
int shm_id, sem_id;
//δείκτης στην shared memory
SharedData *data=NULL;
int running=1;
union semun {//must ορισμός ως global για semctl
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

//σεμαφόροι
void lock(){
    struct sembuf op={0, -1, 0};
    // μειώνει σεμαφόρο για κλείδωμα
    if(semop(sem_id, &op, 1)==-1){
        perror("semop lock");
        exit(1);
    }
}

void unlock(){
    struct sembuf op={0, 1, 0};
    // αυξάνει τον σεμαφόρο
    if(semop(sem_id, &op, 1)==-1){
        perror("semop unlock");
        exit(1);
    }
}

//δημιουργία/σύνδεση shared memory και semaphore
void init_system(){
    FILE *fp=fopen(KEY_FILE, "a+");// Δημιουργία αρχείου κλειδιού αν δεν υπάρχει
    if(!fp){
        perror("fopen");
        exit(1);
    }
    fclose(fp);
    // Δημιουργία κλειδιών για shared memory και semaphore
    key_t shm_key=ftok(KEY_FILE, 'S');
    if(shm_key==-1){
        perror("ftok shm");
        exit(1);
    }

    key_t sem_key=ftok(KEY_FILE, 'M');
    if(sem_key==-1){
        perror("ftok sem");
        exit(1);
    }

    //shared memory:συνδεόμαστε ή δημιουργουμε
    shm_id=shmget(shm_key, sizeof(SharedData), 0666);
    if(shm_id==-1){
        //Δημιουργία νέου segment
        shm_id=shmget(shm_key, sizeof(SharedData), IPC_CREAT|IPC_EXCL|0666);
        //Έλεγχος αποτυχίας δημιουργίας
        if(shm_id==-1){
            //Υπάρχει ήδη segment — συνδέομαι
            if(errno==EEXIST){
                shm_id=shmget(shm_key, 0, 0);  
                //Έλεγχος αποτυχίας σύνδεσης και τερματισμός   
                if (shm_id==-1) {
                    perror("shmget existing");
                    exit(1);
                }
            }else {
                perror("shmget create");
                exit(1);
            }
        }
    }

    // Σύνδεση στη shared memory το segment
    data=shmat(shm_id, NULL, 0);
    if(data==(void*)-1){
        perror("shmat");
        exit(1);
    }

    //δημιουργία ή σύνδεση και αρχικοποίηση
    sem_id=semget(sem_key, 1, 0666);
    if(sem_id==-1){
        // Δημιουργία νέου σεμαφόρου
        sem_id=semget(sem_key, 1, IPC_CREAT|IPC_EXCL|0666);
        if(sem_id==-1){
            // Υπάρχει ήδη σεμαφόρος — συνδέομαι
            if(errno==EEXIST){
                sem_id=semget(sem_key, 1, 0666);
            }
            // Έλεγχος αποτυχίας σύνδεσης και τερματισμός
            if(sem_id==-1){
                perror("semget");
                exit(1);
            }
        }else{
            // Αρχικοποίηση σεμαφόρου
            union semun arg;
            arg.val = 1;
            //αποτυχία
            if(semctl(sem_id, 0, SETVAL, arg) == -1){
                perror("semctl");
                exit(1);
            }
        }
    }

    lock();
    // Αν είναι η πρώτη δημιουργία, μηδενίζουμε τα δεδομένα
    if(data->dialog_count==0 && data->message_count==0){
        memset(data, 0, sizeof(SharedData));
    }
    unlock();
}

// διεργασίες διαλόγου
void join_dialog(int dialog_id, int my_pid){
    // index
    int idx=-1;
    //αν υπάρχει ήδη ο διάλογος, παίρνω τον δείκτη του
    for (int i=0; i<MAX_DIALOGS; i++){
        if(data->dialogs[i].is_active && data->dialogs[i].dialog_id==dialog_id){
            idx=i;
            break;
        }
    }
    //αν δεν υπάρχει, δημιουργώ νέο διάλογο
    if(idx==-1){
        for(int i=0; i<MAX_DIALOGS; i++){
            // ελυθερωμένη θέση
            if(!data->dialogs[i].is_active){
                idx=i;
                data->dialogs[i].dialog_id=dialog_id;// ορισμός id
                data->dialogs[i].is_active=1;// σημαία ενεργού διαλόγου
                data->dialogs[i].participant_count=0;// αρχικοποίηση συμμετεχόντων
                data->dialogs[i].should_terminate=0;// αρχικοποίηση flag
                data->dialog_count++;
                break;
            }
        }
    }
    // αν δεν βρέθηκε θέση για νέο διάλογο, επιστροφή
    if(idx==-1) return;
    Dialog *d=&data->dialogs[idx];
    int found=0;
    // έλεγχος αν ο χρήστης είναι ήδη συμμετέχων
    for(int i=0; i<d->participant_count; i++){
        if(d->participants[i]==my_pid){
            found=1;
            break;
        }
    }
    // αν δεν είναι, προσθήκη στη λίστα συμμετεχόντων
    if (!found && d->participant_count<MAX_PROC){
        d->participants[d->participant_count++]=my_pid;
    }
}

// Αφαίρεση συμμετέχοντα από διάλογο
void leave_dialog(int dialog_id, int my_pid){
    // Εύρεση διαλόγου
    for (int i=0; i<MAX_DIALOGS; i++){
        // Βρίσκω τον ενεργό διάλογο με το δοσμένο id
        if(data->dialogs[i].is_active && data->dialogs[i].dialog_id==dialog_id) {
            Dialog *d=&data->dialogs[i];
            //εύρεση συμμετέχοντα
            for(int j=0; j<d->participant_count; j++){
                if(d->participants[j]==my_pid){
                    // Αφαίρεση συμμετέχοντα μετακινώντας τα υπόλοιπα
                    for (int k=j; k<d->participant_count-1; k++) {
                        d->participants[k]=d->participants[k+1];
                    }
                    d->participant_count--;
                    // αν δεν υπάρχουν συμμετέχοντες, απενεργοποίηση διαλόγου
                    if (d->participant_count==0){
                        d->is_active=0;//inactive
                        d->should_terminate=0;// reset flag
                        data->dialog_count--;
                    }
                    return;
                }
            }
        }
    }
}

// επιστροφή πλήθους συμμετεχόντων σε διάλογο
int get_participant_count(int dialog_id){
    // Εύρεση διαλόγου
    for(int i=0; i<MAX_DIALOGS; i++){
        //αν βρεθεί ο διάλογος και έχει το σωστό id
        if(data->dialogs[i].is_active && data->dialogs[i].dialog_id == dialog_id) {
            // επιστροφή πλήθους συμμετεχόντων
            return data->dialogs[i].participant_count;
        }
    }
    return 0;
}

int should_terminate(int dialog_id){
    if(data==NULL) return 1; // αν δεν υπάρχει shared mem - τερματισμός
    for(int i=0; i<MAX_DIALOGS; i++){
        // εύρεση διαλόγου με το δοσμένο id
        if(data->dialogs[i].dialog_id==dialog_id){
            // αν ο διάλογος δεν είναι ενεργός - τερματισμός
            if(!data->dialogs[i].is_active) return 1;
            return data->dialogs[i].should_terminate;
        }
    }
    return 0;
}

void set_terminate_flag(int dialog_id) {
    if(data==NULL) return;
    for (int i=0; i<MAX_DIALOGS; i++){
        // εύρεση διαλόγου με το δοσμένο id και ορισμός σημαίας τερματισμού
        if (data->dialogs[i].is_active && data->dialogs[i].dialog_id==dialog_id){
            data->dialogs[i].should_terminate=1;
            return;
        }
    }
}

// send_msg επιστρέφει void στην αρχική έκδοση. Αν ο buffer είναι γεμάτος, το μήνυμα χάνεται.
void send_msg(int dialog_id, int my_pid, char *text){
    for(int i=0; i<MAX_MESSAGES; i++){
        // εύρεση πρώτης ελεύθερης θέσης για μήνυμα
        if(!data->messages[i].is_valid){
            Message *m=&data->messages[i];
            m->dialog_id=dialog_id;
            m->sender_pid=my_pid;
            // ασφαλής αντιγραφή κειμένου
            strncpy(m->payload, text, MAX_PAYLOAD-1);
            m->payload[MAX_PAYLOAD-1]='\0'; // εξασφάλιση τερματισμού
            m->read_count=0; 
            // αρχικοποίηση πίνακα αναγνωστών
            memset(m->readers, 0, sizeof(int) * MAX_PROC);
            // υπάρχει μήνυμα
            m->is_valid=1;
            return;
        }
    }
}

// διαβάζει μηνύματα του διαλόγου και τα εμφανίζει
int receive_msgs(int dialog_id, int my_pid){
    int found_terminate=0;
    for(int i=0; i<MAX_MESSAGES; i++){
        Message *m=&data->messages[i];
        // έλεγχος εγκυρότητας και αν το μήνυμα ανήκει στον διάλογο
        if(!m->is_valid || m->dialog_id != dialog_id) continue;
        int read=0;
        // έλεγχος αν το μήνυμα έχει ήδη διαβαστεί από τον χρήστη
        for(int j=0; j< m->read_count; j++){
            if(m->readers[j]==my_pid){
                read=1;
                break;
            }
        }
        // αν δεν έχει διαβαστεί
        if(!read){
            // εμφάνιση μηνύματος αν είναι από εμάς
            if(m->sender_pid==my_pid){
                printf("Εσύ: %s\n", m->payload);
            }else{// εμφάνιση μηνύματος από άλλον χρήστη
                printf("[%d]: %s\n", m->sender_pid, m->payload);
            }
            // έλεγχος για μήνυμα τερματισμού
            if(strcmp(m->payload, "TERMINATE")==0){
                found_terminate=1;//φλαγκ
            }
            // προσθήκη του χρήστη στους αναγνώστες
            if(m->read_count<MAX_PROC){
                m->readers[m->read_count++]=my_pid;
            }
            // αν όλοι οι συμμετέχοντες έχουν διαβάσει το μήνυμα, το καθιστούμε άκυρο
            if(m->read_count>=get_participant_count(dialog_id)){
                m->is_valid=0;
            }
        }
    }
    return found_terminate;
}

// Καθαρισμός διαλόγου
void cleanup(int dialog_id, int my_pid){
    if(data==NULL) return;
    lock();// προστασία με σεμαφόρο για αποφυγή race conditions
    // αφαίρεση συμμετέχοντα από διάλογο
    leave_dialog(dialog_id, my_pid);
    // αν δεν υπάρχουν άλλοι διάλογοι
    if(data->dialog_count==0){
        unlock();
        //διαγραφή semaphore
        if(semctl(sem_id, 0, IPC_RMID)==-1){
            perror("semctl IPC_RMID");
        }
        shmdt(data);// αποσύνδεση
        // διαγραφή shared memory
        if(shmctl(shm_id, IPC_RMID, NULL)==-1){
            perror("shmctl IPC_RMID");
        }
        data=NULL;
    }else{// απλή αποσύνδεση αν υπάρχουν άλλοι διάλογοι
        unlock();
        shmdt(data);
        data=NULL;
    }
}

void handle_signal(int sig){
    //Απλο flag ώστε οι loops να τερματίσουν με ασφαλή τρόπο
    running=0;
}

//λήψη μηνυμάτων στο child process
void receiver_process(int dialog_id) {
    signal(SIGINT, handle_signal);// ctrl+c
    signal(SIGTERM, handle_signal);//terminate από parent
    while(running){
        sleep(1);
        if(data==NULL) break;//shmem δεν υπάρχει
        lock(); 
        //αν ο διάλογος πρέπει να τερματιστεί unlock για έξοδο
        if(should_terminate(dialog_id)){
            unlock();
            running=0;
            break;
        }
        //εκτυπώνει μηνύματα (1 = τερματισμός)
        int terminate=receive_msgs(dialog_id, getpid());
        if(terminate){
            // αν βρεθεί μήνυμα τερματισμού, ορισμός global σημαίας και έξοδος
            set_terminate_flag(dialog_id);
            unlock();
            running = 0;
            break;
        }
        unlock();
    }
    exit(0);
}

// αποστολή μηνυμάτων στο parent process
void sender_process(int dialog_id) {
    //παρόμοια signal handlers
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    char buffer[MAX_PAYLOAD];
    printf("\n> ");
    fflush(stdout);//empty
    //όσο δεν τερματίζουμε ή ύπαρχει shared data
    while(running) {
        if(data==NULL) break;
        // χρήση select για non-blocking input
        fd_set readfds;
        struct timeval tv;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        // timeout 1 δευτερολέπτου για να μην μπλοκάρει
        tv.tv_sec=1;
        tv.tv_usec=0;
        // κλήση select
        int ret=select(STDIN_FILENO+1, &readfds, NULL, NULL, &tv);
        if(ret==-1){
            if (errno==EINTR) continue; // αν διακοπεί από σήμα, συνεχίζουμε
            break;
        }else if(ret==0){// timeout
            lock();
            if(should_terminate(dialog_id)){//τερματισμός από άλλον
                unlock();
                printf("[Ο διάλογος τερματίστηκε]\n");
                running=0;
                break;
            }
            unlock();
            continue;
        }
        if(fgets(buffer, MAX_PAYLOAD, stdin)==NULL) break; // διάβασμα εισόδου ή EOF
        // Βρίσκω το μήκος του μηνύματος για να αφαιρέσω το newline
        int len = strlen(buffer);
        // Αν ο τελευταίος χαρακτήρας είναι enter (\n), τον αντικαθιστώ με το τέλος string (\0)
        if (len > 0 && buffer[len-1] == '\n') {
            buffer[len-1] = '\0';
        }

        if(strlen(buffer)==0){// άδειο μήνυμα
            printf("> ");// prompt ξανά
            fflush(stdout);
            continue;
        }
        lock();
        send_msg(dialog_id, getpid(), buffer);// αποστολή μηνύματος στη shared memory
        if(strcmp(buffer, "TERMINATE")==0){
            // flag τερματισμού αν στάλθηκε μήνυμα τερματισμού
            set_terminate_flag(dialog_id);
            unlock();
            running=0;
            break;
        }
        unlock();
        printf("> ");
        fflush(stdout);
    }
}

int main(){
    init_system();// αρχικοποίηση shared memory και semaphore
    int dialog_id;
    printf("Dialog ID: ");// που θέλουμε να συμμετέχουμε
    if(scanf("%d", &dialog_id)!=1){//δεν είναι έγκυρος αριθμός
        printf("Λάθος είσοδος");
        return 1;
    }
    getchar(); //τρώει το newline
    lock();
    // συμμετοχή σε διάλογο
    join_dialog(dialog_id, getpid());
    unlock();
    // ρύθμιση signal handler ctrl+c
    signal(SIGINT, handle_signal);
    // child process - λήψη μηνυμάτων
    pid_t pid=fork();
    if(pid==0){//αν είμαστε στο child
        //διαβάζω μόνο
        receiver_process(dialog_id);
    }else if(pid>0){// parent
        // γράφω μόνο
        sender_process(dialog_id);
        kill(pid, SIGTERM); // στέλνουμε SIGTERM στον child όταν ο parent τελειώσει για να κλείσει
        wait(NULL);
    }else{// σφάλμα fork - καθαρισμός και έξοδος
        perror("fork");
        cleanup(dialog_id, getpid());
        return 1;
    }
    cleanup(dialog_id, getpid());
    return 0;
}
