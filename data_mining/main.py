# main.py
import os
import pandas as pd
from scripts.json_to_csv import convert_jsonl_to_csv
from scripts.clean import clean_dataframe
from scripts.sentiment import compute_sentiment_score
from scripts.visualize import plot_ratings_dist

RAW_DIR = "data/raw_json"
CSV_DIR = "data/csv"

# Λίστα με τα αρχεία που υπάρχουν στον φάκελο
review_files = [
    "Digital_Music.jsonl",
    "Gift_Cards.jsonl",
    "Health_and_Personal_Care.jsonl",
    "Magazine_Subscriptions.jsonl",
    "Subscription_Boxes.jsonl"
]

for review_file in review_files:
    category = review_file.replace(".jsonl", "")
    review_path = os.path.join(RAW_DIR, review_file)
    meta_path = os.path.join(RAW_DIR, f"meta_{category}.jsonl")
    csv_path = os.path.join(CSV_DIR, f"{category}.csv")
    cleaned_path = os.path.join(CSV_DIR, f"{category}_cleaned.csv")

    print(f"[→] Processing category: {category}")

    convert_jsonl_to_csv(review_path, meta_path, csv_path, max_rows=5000)

    if not os.path.exists(csv_path):
        continue

    df = pd.read_csv(csv_path)
    df_cleaned = clean_dataframe(df)

    if df_cleaned is None:
        print(f"[!] Skipping {category}: clean failed")
        continue

    # Προσθήκη sentiment score
    df_cleaned['sentiment_score'] = df_cleaned.apply(
        lambda row: compute_sentiment_score(row['review_text'], row['review_rating']), axis=1
    )

    df_cleaned.to_csv(cleaned_path, index=False)
    print(f"[✔] Saved cleaned data with sentiment to {cleaned_path}")

    # Οπτικοποίηση
    plot_ratings_dist(df_cleaned, category)
