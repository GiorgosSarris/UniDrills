# scripts/clean_data.py
import pandas as pd

def clean_dataframe(df):
    # Εντοπισμος review text
    for col in ['text', 'reviewText', 'review_text']:
        if col in df.columns:
            df['review_text'] = df[col]
            break
    else:
        print("[!] Missing review text")
        return None

    # Εντοπισμος τιτλου
    for col in ['title', 'summary', 'title_x', 'Title', 'review_title']:
        if col in df.columns:
            df['review_title'] = df[col]
            break
    else:
        print(f"[!] Missing review title: {set(df.columns)}")
        return None

    # Εντοπισμος βαθμολογιας
    for col in ['rating', 'overall', 'review_rating']:
        if col in df.columns:
            df['review_rating'] = df[col]
            break
    else:
        print("[!] Missing rating")
        return None

    # Μονο χρησιμα πεδια
    df_clean = df[['review_rating', 'review_title', 'review_text']].copy()

    # Καθαρισμα
    df_clean = df_clean.dropna(subset=['review_rating', 'review_title', 'review_text'])
    df_clean['review_text'] = df_clean['review_text'].astype(str).str.lower()
    df_clean['review_title'] = df_clean['review_title'].astype(str).str.lower()

    return df_clean
