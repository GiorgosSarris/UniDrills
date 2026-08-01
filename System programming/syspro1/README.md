# jms_common.h:
Περιέχει όλα τα εργαλεία που χρειάζονται τα υπόλοιπα αρχεία, τα βασικά constants όπως `JMS_PIPE_IN`, `JMS_PIPE_OUT`, τα μεγέθη των buffers και τα states των jobs.
Η write_all φροντίζει να γραφτούν όλα τα bytes σε ένα file descriptor χωρίς να χαθεί κάτι. Η append_text βοηθάει να χτίζονται οι απαντήσεις μέσα σε buffers. Οι trim_newline, skip_spaces, trim_spaces και parse_int χρησιμοποιούνται για καθάρισμα και parsing εντολών. Οι mkdir_p και make_stamp βοηθούν για output directories και timestamps. Οι read_line_fd, send_block και read_block υλοποιούν κάθε απάντηση να στέλνεται σαν block γραμμών και τελειώνει με `END`.

# jms_console.c:
Το jms_console είναι η πλευρά του χρήστη. Παίρνει εντολές είτε από πληκτρολόγιο είτε αρχείο και τις στέλνει στον coordinator. Μετά περιμένει την απάντηση και την εμφανίζει στην οθόνη. Δεν κρατάει λίστα από jobs, δεν αποφασίζει για pools και δεν έχει λογική scheduling.
Η open_write_retry και η open_read_retry ανοίγουν τα pipes με μικρά retries, ώστε να μη σταματάει αμέσως το πρόγραμμα αν ο coordinator δεν είναι έτοιμος ακόμα. Η run_stream είναι η κύρια λειτουργία του console: διαβάζει μία γραμμή, την καθαρίζει, τη στέλνει στον coordinator, περιμένει απάντηση με read_block και την τυπώνει. Αν η εντολή είναι shutdown, σταματάει. Η main κάνει parse τα ορίσματα -w, -r, -o, ανοίγει τα pipes και πρώτα εκτελεί το αρχείο εντολών αν υπάρχει και συνεχίζει κανονικά με input.

# jms_coord.c:
Το jms_coord είναι το βασικό αρχείο της εργασίας. Ο coordinator κρατάει τα πάντα: ποια pools υπάρχουν, ποια jobs έχουν δοθεί, σε ποιο pool ανήκεικάθε job και ποια είναι η τελευταία του κατάσταση
Στην αρχή ορίζονται Pool και Job. Η Pool κρατάει id, pid, file descriptors, ονόματα pipes, συνολικά jobs και active jobs και η Job κρατάει id, pid, pool_id, state και χρόνο υποβολής. Οι grow_pools και grow_jobs χρησιμοποιούνται όταν χρειάζεται να μεγαλώσουν δυναμικά οι πίνακες, ενώ οι get_job και get_pool για αναζήτηση job ή pool με βάση id.Ο coordinator πρεπει να επικοινωνεί με τα pools. Οι open_pool_read και open_pool_write ανοίγουν τα pipes κάθε pool, ενώ η read_pool_reply διαβάζει μία ολόκληρη απάντηση από pool και ξαναπροσπαθεί αν υπάρξει timeout. Η mark_pool_dead χρησιμοποιείται όταν ένα pool θεωρηθεί νεκρό ή κλείσει, ώστε να κλείσουν τα file descriptors του, να σβηστούν τα pipes και να ενημερωθεί η εσωτερική κατάσταση. Η reap_pools μαζεύει τερματισμένα pool processes για να μη μείνουν zombies.Εν συνεχεία η ενημέρωση της κατάστασης γίνεται με refresh_pool και refresh_all. Η refresh_pool στέλνει snapshot σε συγκεκριμένο pool και ενημερώνει ό,τι ξέρει ο coordinator για τα jobs του και για τα active jobs του pool. Η refresh_all κάνει το ίδιο για όλα τα ενεργά pools. έτσι, πριν κάποια εντολή όπως status, show-active ή show-pools, ο coordinator έχει πρώτα συγχρονίσει την εικόνα του σε πραγματικό χρόνο. Η δημιουργία νέου pool γίνεται με new_pool. Εκεί φτιάχνονται τα pipes του, γίνεται fork, και το child process τρέχει το ./jms_pool με execl(). Η submit_pool ψάχνει αν υπάρχει ήδη alive pool που δεν έχει φτάσει το όριο jobs_per_pool. Αν δεν βρεθεί,δημιουργείται καινούριο. Έπειτα εχουμε add_status, do_submit, do_signal και shutdown_all. Η add_status μετατρέπει την κατάσταση ενός job σε κείμενο για τον χρήστη. Η do_submit κάνει refresh, βρίσκει pool, στέλνει την εσωτερική εντολή `submit jobid command`, παίρνει πίσω το pid και αποθηκεύει το νέο job. Η do_signal υλοποιεί τα suspend και resume στέλνοντας το κατάλληλο request στο pool που έχει το συγκεκριμένο job. Η shutdown_all στέλνει SIGTERM σε όλα τα pools, περιμένει να τερματίσουν και φτιάχνει την τελική αναφορά του shutdown.

Η main δημιουργεί τα `jms_in` και `jms_out`, τα ανοίγει και μετά περιμένει συνεχώς εντολές από το console. Ανάλογα με το input, καλεί τις κατάλληλες συναρτήσεις ή επιστρέφει Invalid command

# jms_pool.c:
Το jms_pool είναι το μέρος που εκτελεί τα jobs. Κάθε pool process διαχειρίζεται μόνο τα jobs που του έχει αναθέσει ο coordinator και μπορεί να δεχτεί μέχρι ένα συγκεκριμένο όριο jobs συνολικά. Ο coordinator επικοινωνεί με το pool μέσω των δικών του pipes και το pool απαντάει είτε με OK/ERR είτε με ένα snapshot.
Η δομή Job εδώ είναι πιο απλή από του coordinator, γιατί το pool χρειάζεται μόνο πληροφορία: id, pid, state και χρόνο εκκίνησης. Η on_term είναι handler για SIGTERM και απλώς σηκώνει flag stop_now, ώστε να κλείσει ομαλά. Η find_job βρίσκει job του pool με βάση το id. Η reap_jobs χρησιμοποιεί waitpid() χωρίς blocking και ενημερώνει τα jobs όταν κάποιο child σταματήσει, συνεχίσει, τερματίσει. Η all_finished ελέγχει αν έχουν τελειώσει όλα τα jobs του pool.

*Η πιο σημαντική συνάρτηση του αρχείου είναι η run_child. Η συνάρτηση κάνει parse το command, φτιάχνει timestamp, δημιουργεί directory της μορφής outputs-jobid-pid-date_time, ανοίγει τα αρχεία stdout_jobid και stderr_jobid, ανακατευθύνει stdin, stdout, stderr και καλεί execvp(). Έτσι, κάθε job εκτελείται ξεχωριστά και το output πηγαίνει στο σωστό σημείο.

Η answer_submit είναι η απάντηση του pool σε submit request από τον coordinator. Αν δεν έχει φτάσει ακόμα το όριο jobs, κάνει fork(), καταχωρεί το νέο job στον δικό του πίνακα και στέλνει πίσω OK pid. Η answer_signal υλοποιεί τα suspend και resume με SIGSTOP και SIGCONT. Η answer_snapshot φτιάχνει μια εικόνα του pool με όλα τα jobs και στο τέλος ένα συνολικό ΡOOL active total, ώστε ο coordinator να ενημερωθεί σωστά.Η kill_jobs χρησιμοποιείται όταν το pool πρέπει να κλείσει. Στέλνει πρώτα SIGTERM στα jobs που δεν έχουν τελειώσει, περιμένει λίγο και, αν χρειαστεί, στέλνει SIGKILL. Η main κάνει parse τα ορίσματα που δίνει ο coordinator, ανοίγει τα pipes του pool, δεσμεύει τον πίνακα jobs και μετά μπαίνει σε loop όπου περιμένει εσωτερικές εντολές όπως submit, snapshot, suspend, resume. !Δεν μιλάει ποτέ απευθείας με τον χρήστη.

# jms_script.sh:
Το jms_script είναι το bash script για τα output directories που δημιουργούνται από τα jobs. Η cleanup_tmp σβήνει τα προσωρινά αρχεία του script, ενώ το trap cleanup_tmp EXIT INT TERM HUP εξασφαλίζει ότι αυτά θα καθαριστούν ακόμα και αν το script διακοπεί. Το block while [ $# -gt 0 ] κάνει parse τα arguments και αναγνωρίζει το `-l`, το `-c` και το προαιρετικό `n` για την εντολή size. Η list_dirs βρίσκει όλους τους φακέλους outputs_... και τους εμφανίζει ταξινομημένους. Η size_dirs υπολογίζει το συνολικό μέγεθος των αρχείων μέσα σε κάθε output directory, τα ταξινομεί και, αν έχει δοθεί n, εμφανίζει μόνο τα τελευταία n. Η purge_dirs διαγράφει όλα τα output directories. Στο τέλος, το case "$cmd" in επιλέγει ποια από αυτές τις λειτουργίες θα τρέξει.

# Συνολική Λειτουργία
Ο χρήστης ξεκινά πρώτα τον coordinator και μετά το console. Το console στέλνει μια εντολή στον coordinator μέσω του `jms_in`. Ο coordinator τη διαβάζει και, αν είναι submit, αποφασίζει σε ποιο pool θα πάει το νέο job ή αν πρέπει να φτιαχτεί καινούριο pool. Το pool παίρνει την εσωτερική εντολή, κάνει fork() και το child process εκτελεί το command με execvp(). Το output του job αποθηκεύεται στο δικό του outputs_... directory.Όταν ο χρήστης ζητήσει πληροφορία, όπως status, status-all, show-active κλπ., ο coordinator ρωτάει πρώτα τα pools με snapshot, ενημερώνει την εικόνα του και μετά επιστρέφει την απάντηση στο console. Στο shutdown, ο coordinator ζητά από όλα τα pools να τερματίσουν, εκείνα κλείνουν τα jobs τους, και στο τέλος επιστρέφεται μια σύντομη αναφορά για το πόσα jobs εξυπηρετήθηκαν και πόσα ήταν ακόμα σε εξέλιξη. Δηλαδή, το jms_console είναι το σημείο από όπου δίνει εντολές ο χρήστης, το jms_coord είναι αυτό που οργανώνει το σύστημα, και τα jms_pool είναι αυτά που τρέχουν το πραγματικό workload.

# Εκτέλεση με 2 terminals
Πρώτα κάνουμε build:
make clean
make

Στο πρώτο terminal τρέχουμε τον coordinator:
./jms_coord -l logs_run -n 3

Στο δεύτερο terminal τρέχουμε το console:
./jms_console -w jms_in -r jms_out

και του δίνουμε εντολές, πχ.:
submit /bin/echo hello
submit /bin/sleep 2
status-all
show-pools
show-active
show-finished
shutdown

# Εκτέλεση stress test
chmod +x stress_test.sh  //σε περίπτωση δικαιωμάτων
./stress_test.sh

`Makefile`: κάνει build τα jms_coord, jms_console, jms_pool και με make clean καθαρίζει τα βασικά generated αρχεία.
`stress_test.sh`: ξαναχτίζει το project, καθαρίζει το path logs, σηκώνει coordinator με -n 3 για ευκολία και μετά ανοίγει το console για να δοθούν εντολές.
