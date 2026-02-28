import nltk
from nltk.sentiment.vader import SentimentIntensityAnalyzer

# Αρχικοποιούμε το εργαλείο
vader = SentimentIntensityAnalyzer()

def compute_sentiment_score(text: str, rating: float, w1=0.6, w2=0.4) -> float:
    """
    Υπολογίζει τον τελικό sentiment score από το κείμενο και τη βαθμολογία (rating)
    w1: βάρος για το sentiment από το κείμενο
    w2: βάρος για τη βαθμολογία (rating)
    """
    if not isinstance(text, str):
        text = ""

    # VADER score: compound ∈ [-1, 1]
    sentiment = vader.polarity_scores(text)["compound"]

    # Κανονικοποίηση βαθμολογίας (1–5 → 0–1)
    normalized_rating = (rating - 1) / 4

    # Κανονικοποίηση VADER score σε [0, 1]
    normalized_sentiment = (sentiment + 1) / 2

    # Τελικό σκορ με βάρη
    final_score = w1 * normalized_sentiment + w2 * normalized_rating

    return round(final_score, 4)
