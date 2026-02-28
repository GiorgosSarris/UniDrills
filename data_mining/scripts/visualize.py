import matplotlib.pyplot as plt
import seaborn as sns

def plot_ratings_dist(df, category):
    plt.figure(figsize=(6, 4))
    sns.histplot(df['review_rating'], bins=5, kde=True)
    plt.title(f"{category} - Rating Distribution")
    plt.xlabel("Rating")
    plt.ylabel("Count")
    plt.tight_layout()
    plt.show()
