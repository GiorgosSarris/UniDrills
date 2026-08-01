#!/bin/bash

path=""
cmd=""
num=""
tmp=""
tmp_sorted=""

# διαγράφει τα προσωρινά αρχεία όταν το script τερματίσει (είτε ομαλά είτε με σήμα)
cleanup_tmp() {
    [ -n "$tmp" ] && rm -f -- "$tmp"
    [ -n "$tmp_sorted" ] && rm -f -- "$tmp_sorted"
}

# εγκαθιστά τον trap για να καλέσει τη συνάρτηση καθαρισμού όταν το script τερματίσει ή λάβει σήμα
trap cleanup_tmp EXIT INT TERM HUP

# επεξεργασία των ορισμάτων γραμμής εντολών
while [ $# -gt 0 ]; do
    case "$1" in
        -l) [ $# -lt 2 ] && exit 1
            path="$2"; shift 2 ;;
        -c) [ $# -lt 2 ] && exit 1
            cmd="$2"; shift 2
            # αν η εντολή είναι 'size', ελέγχουμε αν υπάρχει επόμενο όρισμα
            if [ "$cmd" = "size" ] && [ $# -gt 0 ]; then
                case "$1" in
                    -*) ;; # Αν είναι flag το αγνοούμε εδώ
                    *) num="$1"; shift ;; # αλλιώς είναι ο αριθμός n
                esac
            fi ;;
        *) # αν βρούμε ορφανό όρισμα και η εντολή είναι 'size', το θεωρούμε ως τον αριθμό n
           if [ "$cmd" = "size" ] && [ -z "$num" ]; then
                num="$1"; shift
           else exit 1; fi ;;
    esac
done

# ελεγχος εγκυρότητας: πρέπει να έχει δοθεί path, εντολή, και το path να είναι υπαρκτό
if [ -z "$path" ] || [ -z "$cmd" ] || [ ! -d "$path" ]; then
    exit 1
fi

# εκτυπώνει τα ονόματα των καταλόγων των εργασιών (με outputs_) ταξινομημένα
list_dirs() {
    find "$path" -maxdepth 1 -mindepth 1 -type d -name 'outputs_*' -printf '%f\n' | sort
}

# υπολογίζει το συνολικό μέγεθος αρχείων ανά κατάλογο και ταξινομεί σε αύξουσα σειρά
size_dirs() {
    tmp=$(mktemp) || exit 1
    tmp_sorted="${tmp}.sorted"

    # υπολογισμός μεγέθους για κάθε κατάλογο που βρίσκει η list_dirs
    while IFS= read -r dir; do
        [ -z "$dir" ] && continue
        full="$path/$dir"
        # βρίσκει τα αρχεία στον κατάλογο, παίρνει τα μεγέθη τους και τα αθροίζει με awk
        size=$(find "$full" -type f -printf '%s\n' | awk '{s+=$1} END {print s+0}')
        printf "%s\t%s\n" "$size" "$dir" >> "$tmp"
    done < <(list_dirs)

    # ταξινόμηση βάσει μεγέθους και αντιστροφή στηλών
    sort -n "$tmp" | awk -F '\t' '{print $2, $1}' > "$tmp_sorted"

    # αν δόθηκε n, εμφανίζουμε μόνο τα n μεγαλύτερα (tail), αλλιώς όλα
    if [ -n "$num" ]; then
        [[ ! "$num" =~ ^[0-9]+$ ]] && exit 1
        tail -n "$num" "$tmp_sorted"
    else
        cat "$tmp_sorted"
    fi
}

# διαγράφει όλους τους καταλόγους εξόδου
purge_dirs() {
    while IFS= read -r dir; do
        full="$path/$dir"
        [ -d "$full" ] && rm -rf -- "$full"
    done < <(list_dirs)
}

# διαχείριση των εντολών που μπορεί να δοθούν στο script
case "$cmd" in
    list) list_dirs ;;
    size) size_dirs ;;
    purge) purge_dirs ;;
    *) exit 1 ;; # Λάθος εντολή
esac