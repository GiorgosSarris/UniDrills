PRML Mini Project 1- Task C
Human Activity Recognition
Ονοματεπώνυμο:Γεώργιος Νικόλαος Σαρρής
ΑΜ:1115202300181

ΠΕΡΙΓΡΑΦΗ
Εκπαίδευση 5 κλασικών αλγορίθμων machine learning (k-NN, Naive Bayes, Logistic Regression, SVM, Decision Tree) 
για αναγνώριση ανθρώπινης δραστηριότητας (WALKING, WALKING_UPSTAIRS, WALKING_DOWNSTAIRS, SITTING, STANDING, LAYING) 
χρησιμοποιώντας το UCI HAR dataset και δικά συλλεγμένα δεδομένα από αισθητήρες κινητού.

ΕΞΑΡΤΗΣΕΙΣ:
pip install numpy
pip install pandas
pip install matplotlib
pip install seaborn
pip install scipy
pip install scikit-learn
pip install umap-learn

ΕΚΤΙΜΩΜΕΝΟΣ ΧΡΟΝΟΣ ΕΚΤΕΛΕΣΗΣ:
4-5 λεπτά

ΚΥΡΙΕΣ ΣΥΝΑΡΤΗΣΕΙΣ:
- extract_har_features() - Εξαγωγή 26 features από επιταχυνσιόμετρα
- load_ucihar_raw() - Φόρτωση UCI HAR dataset
- run_model_selection() - Grid search και εκπαίδευση 5 αλγορίθμων
- retrain_with_own_data() - Αξιολόγηση σε δικά δεδομένα
- remove_feature_and_retrain() - Ανάλυση distribution shift

ΧΑΡΑΚΤΗΡΙΣΤΙΚΑ:
Συνολικά 26 features ανά δείγμα:
- 12 βασικά (mean, std, min, max για x, y, z)
- 2 από μέγεθος (magnitude_mean, magnitude_std)
- 3 διασταυρώσεις μηδέν (zero crossing rate)
- 6 από jerk (αλλαγή επιτάχυνσης)
- 3 δυσκολότερες συχνότητες

ΑΠΟΤΕΛΕΣΜΑΤΑ:
• Καλύτερο μοντέλο: SVM (rbf kernel, C=10)
• Test Accuracy: 94.05%
• Macro-F1 Score: 94.20%
• Πλήθος δειγμάτων: 10,299 (train: 7,209 | val: 1,545 | test: 1,545)

ΣΗΜΕΙΩΣΕΙΣ:
- Τα δικά δεδομένα έχουν distribution shift λόγω διαφορετικής συσκευής
- Το μοντέλο επιτυγχάνει 94% accuracy στο Kaggle test αλλά ~15% στα δικά δεδομένα
