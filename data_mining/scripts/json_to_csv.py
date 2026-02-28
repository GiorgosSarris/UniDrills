import json
import pandas as pd

def read_jsonl(path, max_rows=None):
    data = []
    with open(path, 'r', encoding='utf-8') as f:
        for i, line in enumerate(f):
            if max_rows and i >= max_rows:
                break
            data.append(json.loads(line))
    return pd.DataFrame(data)

def convert_jsonl_to_csv(review_path, meta_path, output_path, max_rows=5000):
    print(f"  - Reading reviews from {review_path}...")
    df_reviews = read_jsonl(review_path, max_rows)

    print(f"  - Reading meta data from {meta_path}...")
    df_meta = read_jsonl(meta_path)

    print(f"  → Reviews: {len(df_reviews)}, Meta: {len(df_meta)}")

    if 'asin' not in df_reviews.columns or 'parent_asin' not in df_meta.columns:
        print(f"[!] Skipping: missing 'asin' or 'parent_asin'")
        return

    # Βαζουμε το parent_asin ως asin στο meta για να μπορει να γινει merge
    df_meta['asin'] = df_meta['parent_asin']

    df_merged = pd.merge(
        df_reviews,
        df_meta,
        on='asin',
        how='left'
    )

    if df_merged.empty:
        print(f"[!] Skipping: empty merged dataframe")
        return

    df_merged.to_csv(output_path, index=False)
    print(f"[✔] Saved {len(df_merged)} merged rows to {output_path}")
