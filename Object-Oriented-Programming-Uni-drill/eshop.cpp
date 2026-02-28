
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <fstream>
#include <sstream>
using namespace std;

//κλαση προιοντα 
class Product {
public:
    string name;
    string description;
    string category;
    string subcategory;
    double price;
    int quantity;
    int orderCount; //ποσες φορες για πιο δημοφιλεστερο προιον
    static vector<Product> products; 
    //constructor
    Product(string n, string d, string c, string sc, double pr, int quant)
        : name(n), description(d), category(c), subcategory(sc), price(pr), quantity(quant) {}

    static void addProduct(const Product& p) {
        products.push_back(p);
        saveProductsToFile();
    }//προσθετω προιον

    static Product* findProduct(const string& title) {
        for (auto& p : products) {
            if (p.name == title) {
                return &p;
            }
        }
        return nullptr;
    }//βρισκω τη διευθυνση του προιοντος και βρισκω ολο το προιον

    static void updateProduct(const string& title) {
        for (auto& product : products) {//σειριακη αναζητηση των διευθησεων των προιοντων
            if (product.name == title) {//οταν βρεθει αυτο που ψαχνω μεταβαλλω οτι χρειαζεται
                cout << "Product found. Enter new details:\n";
                cout << "New Name (current: " << product.name << "): ";
                string newName;
                getline(cin >> ws, newName);
                cout << "New Description (current: " << product.description << "): ";
                string newDescription;
                getline(cin >> ws, newDescription);
                cout << "New Category (current: " << product.category << "): ";
                string newCategory;
                getline(cin >> ws, newCategory);
                cout << "New Subcategory (current: " << product.subcategory << "): ";
                string newSubcategory;
                getline(cin >> ws, newSubcategory);
                cout << "New Price (current: " << product.price << "): ";
                double newPrice;
                cin >> newPrice;
                cout << "New Quantity (current: " << product.quantity << "): ";
                int newQuantity;
                cin >> newQuantity;

                product.name = newName;
                product.description = newDescription;
                product.category = newCategory;
                product.subcategory = newSubcategory;
                product.price = newPrice;
                product.quantity = newQuantity;

                cout << "Product updated successfully!\n";
                saveProductsToFile();
                return;
            }
        }
        cout << "Product not found!\n";// αν δεν βρεθει ενηερωνουμε
    }

    static void displayProductStatistics() {
        cout << "\nProduct Statistics:\n";
        cout << "\nUnavailable Products (Quantity = 0):\n";
        for (const auto& product : products) {//με σειριακη αναζητηση για καθε διευθυνση
            if (product.quantity == 0) {//αμα δεν εχει ποσοτητα το εμφανιζω κατω απο την κατηγορια μη διαθσιμα προιοντα
                cout << "Name: " << product.name << ", Category: " << product.category
                     << ", Subcategory: " << product.subcategory << "\n";
            }
        }

        if (!products.empty()) {
            int maxOrderCount = 0;

            // Εύρεση του μεγιστου
            for (const auto& product : products) {
                if (product.orderCount > maxOrderCount) {
                    maxOrderCount = product.orderCount;
                }
            }
            // Έλεγχος αν υπάρχει παραγγελία και εμφανιση
            if (maxOrderCount > 0) {
                cout << "\nMost Frequently Ordered Products:\n";
                for (const auto& product : products) {
                    if (product.orderCount == maxOrderCount) {
                        cout << "Name: " << product.name << ", Orders: " << product.orderCount << "\n";
                    }
                }
            } else {
                cout << "\nNo products have been ordered yet.\n";
            }
        } else {
            cout << "\nNo products in the list.\n";
        }
        
    }

    void addToOrder() {
        orderCount++;
    }
    //αμα προσθεσω προιον να το αποθηκευσω
    static void saveProductsToFile() {
        ofstream file("files/products.txt", ios::trunc);//ανοιγει  το αρχειο
        if (!file) {//εμφανιζει error αμα δεν μπορει να το ανοιξει
            cout << "Error: Could not open products.txt for writing." << endl;
            return;
        }
        for (const auto& product : products) {//για καθε προιον
            file << product.name << "@"  //τα γραφει στο αρχειο
                 << product.description << "@"
                 << product.category << "@"
                 << product.subcategory << "@"
                 << product.price << "@"
                 << product.quantity << "\n";
        }
        file.close();
    }
    //τα φορτωνεις απο το φακελο τωρα
    static void loadProductsFromFile(const string& filename) {
        ifstream file(filename);
        if (!file.is_open()) {//αμα δεν μπορει να ανοιξει εμφανιζει error
            cout << "Error opening file: " << filename << endl;
            return;
        }

        string line;// καθε γραμμη του αρχειου
        while (getline(file, line)) {//διαβαζει καθε γραμμη του αρχειου στη μεταβλητη line
            istringstream ss(line);//με αυτο επιτρεπει επεξεργασια
            string name, description, category, subcategory, Sprice, Squantity;
            if (getline(ss, name, '@') && //αποθηκευει στη μεταβλητη οτιδηποτε πριν το @
                getline(ss, description, '@') &&
                getline(ss, category, '@') &&
                getline(ss, subcategory, '@') &&
                getline(ss, Sprice, '@') &&
                getline(ss, Squantity)) {
                try {
                    double price = stod(Sprice);     // Μετατρεπει string σε αριθμητικα (int, double)
                    int quantity = stoi(Squantity); 
                    products.emplace_back(name, description, category, subcategory, price, quantity);
                    cout << "Loaded product: " << name << endl; //προσθετω στη λιστα
                } catch (const invalid_argument& e) {//εξαιρεσεις
                    cout << "Invalid number format in line: " << line << endl;
                } catch (const out_of_range& e) {
                    cout << "Number out of range in line: " << line << endl;
                }
            } else {//αν δεν καταφερει να διαβαζει ολα τα πεδια
                cout << "Error " << line << endl;
            }
        }
        file.close();
    }
};

 vector<Product> Product::products;

//κλαση του χρηστη
class User {
public:
    string username;
    string password;
    bool isAdmin;
    static vector<User> users;
    //constructor
    User(string u, string p, bool admin) : username(u), password(p), isAdmin(admin) {}
    //φορτωνω απο το αρχειο χρηστων
    static void loadFromFile(const string& filename) {
        ifstream file(filename);//ανοιγω το αρχειο
        if (!file) {//αμα δεν μπορει να το ανοιξει error
            cout << "Error: Could not open " << filename << " for reading." << endl;
            return;
        }
        string line;//μεταβλητη για τη γραμμη αρχειου
        while (getline(file, line)) {//διαβαζει καθε γραμμη του αρχειου στη μεταβλητη
            istringstream ss(line);//επιτρεπει επεξεργασια γραμμης
            string u, p;
            bool admin;
            char comma; // Για να παρακαμψουμε το κομμα 

            // Διαβαζουμε τα πεδια απο τη γραμμη, διαχωρισμενα με κομμα
            if (getline(ss, u, ',') && getline(ss, p, ',') && ss >> admin) {
                users.push_back(User(u, p, admin));
            } else {//αν δεν καταφερει να διαβασει ολα τα πεδια
                cout << "Error " << line << endl;
            }
        }

        file.close();
    }
    //τα αποθηκευει στο αρχειο χρηστη
    static void saveToFile(const string& filename) {
        ofstream file(filename, ios::trunc);//ανοιγει το αρχειο
        if (!file) {//error αν δεν μπορει να το ανοιξει
            cout << "Error: Could not open " << filename << " for writing." << endl;
            return;
        }

        for (const auto& user : users) {//για καθε χρηστη στο καταλογο
            file << user.username << "," //προσθετει κατα γραμμη τα πεδια
                << user.password << "," 
                << user.isAdmin << "\n";
        }

        file.close();
    }
 
    static void registerUser(const string& u, const string& p, bool admin) {
        for (const auto& user : users) {//για καθε χρηστη του καταλογου
            if (user.username == u) {// αν βρεθει το ονομα
                cout << "Username already exists!" << endl;//εμφανισε οτι υπαρχει ηδη
                return;
            }
        }
        users.push_back(User(u, p, admin));//αλλιως το προσθετει στο καταλογο
        saveToFile("files/users.txt");//και το αποθηκευει
        cout << "User registered successfully!" << endl;
    }

    static User* loginUser(const string& u, const string& p) {
        for (auto& user : users) {//για καθε χρηστη του καταλογου
            if (user.username == u && user.password == p) {//αν στο συγκεκριμενο πεδιο ταιριαζει κωδικος και ονομα
                cout << "Login successful!" << endl;
                return &user;//επιστρεφει το χρηστη που συνδεθηκε
            }
        }
        //αλλιως εμφανιζει καταλληλο μυνημα και δεν επιστρεφφει τιποτα
        cout << "Invalid username or password!" << endl;
        return nullptr;
    }

    void searchProducts(const string& title = "", const string& category = "", const string& subcategory = "") {
        vector<Product> results;
        //σε αυτο το σημειο πηρα βοηθεια απο chatgpt οποτε η συαρτηση searchProducts δεν ειναι εξολοκληρου δικια μου
        for (const auto& product : Product::products) {//για καθε προιον 
            bool matchesTitle = title.empty() || product.name.find(title) != string::npos; // Ταιριάζει ο τίτλος
            bool matchesCategory = category.empty() || product.category == category;      // Ταιριάζει η κατηγορία
            bool matchesSubcategory = subcategory.empty() || product.subcategory == subcategory; // Ταιριάζει η υποκατηγορία

            // Έγκυρο αν ικανοποιούνται όλα τα κριτήρια
            bool isValid = matchesTitle && matchesCategory && (subcategory.empty() || matchesSubcategory);

            if (isValid) {
                results.push_back(product); //το προσθετει στα αποτελεσματα
            }
        }
        //αμα ειναι κενο τοτε δεν βρεθηκε
        if (results.empty()) {
            cout << "No products found matching the criteria.\n";
            return;
        }

        cout << "Search Results:\n";
        for (size_t i = 0; i < results.size(); ++i) {//εμφανιζεις το αποτελεσμα
            cout << i + 1 << ". " << results[i].name << " (Category: " << results[i].category
                << ", Subcategory: " << results[i].subcategory << ")\n";
        }
        //χρηστης επιλεγει προιον
        cout << "Enter the number of the product to view details (or 0 to cancel): ";
        int choice;
        cin >> choice;
        //εμφανιζεις προιον και χαρακτηριστικα
        if (choice > 0 && static_cast<size_t>(choice) <= results.size()) {
            const auto& selectedProduct = results[choice - 1];
            cout << "Product Details:\n";
            cout << "Name: " << selectedProduct.name << "\n";
            cout << "Description: " << selectedProduct.description << "\n";
            cout << "Category: " << selectedProduct.category << "\n";
            cout << "Subcategory: " << selectedProduct.subcategory << "\n";
            cout << "Price: " << selectedProduct.price << "\n";
            cout << "Quantity: " << selectedProduct.quantity << "\n";
        } else if (choice != 0) {
            cout << "Invalid choice.\n";
        }
    }

};

vector<User> User::users;
//κλαση διαχειριστης
class Admin : public User {
public:
    Admin(string u, string p) : User(u, p, true) {}//constructor
    //εμφανιση χρηστων 
    //!αυτη τη συναρτηση την προσθεσα επειδη δεν μπορω να εμφανισω προιοντα οπως ανεφερα οποτε το εκανα με το αρχειο πανω στο οποιο μπορουσα να δουλεψω
    void displayAllUsers() const {
        cout << "All registered users:\n";
        for (const auto& user : users) {//για καθε χρηστη στο καταλογο τον εμφανιζω και αν ειναι διαχειριστης
            cout << "Username: " << user.username << " | Admin: " << (user.isAdmin ? "Yes" : "No") << endl;
        }
    }
    //ο διαχειριστης εχει αυτες τις δυνατοτητες οποτε τις καλει
    void addProduct(const Product& product) const {
        Product::addProduct(product);
    }

    void updateProduct(const string& productName) const {
        Product::updateProduct(productName);
    }
    
    void displayProductStatistics() const {
        Product::displayProductStatistics();
    }
};

//εφτιαξα κλαση καλαθι αγορων για ευκολια στην αναγνωση. δεν τρεχει λογω θεματος με products.txt
class Cart {
private:
    vector<pair<Product*, int>> items; // Προιον και ποσοτητα

public:
    void addItem(Product* product, int quantity) {
        if (product->quantity < quantity) {//αμα η ζητουμενη ποσοτητα ειναι μεγαλυτερη απο την προσφερομενη
            cout << "Not enough stock for " << product->name << ". Available: " << product->quantity << "\n";
            return;
        }
        for (auto& item : items) {//για καθε προιον
            if (item.first == product) {//αμα υπαρχει ηδη αυξανω ποσοτητα
                item.second += quantity;
                return;
            }
        }
        //ενημρωνω το προιον και την ποσοτητα
        items.push_back({product, quantity});
    }


    
    void removeItem(const string& productName) {
        auto it = remove_if(items.begin(), items.end(), [&](const pair<Product*, int>& item) {
            return item.first->name == productName;
        });
        if (it != items.end()) {//αμα δεν εφτασε στο τελος σημαινει οτι το βρηκε
            items.erase(it, items.end());// το αφαιρει
            cout << "Product removed from cart.\n";
        } else {//δεν υπαρχει στο καλαθι
            cout << "Product not found in cart.\n";
        }
    }

    void updateQuantity(const string& productName, int newQuantity) {
        for (auto& item : items) {//για καθε προιον
            if (item.first->name == productName) {// αμα ειναι το απαιτουμενο προιον
                if (item.first->quantity < newQuantity) {//αμα δεν υπαρχει αρκετη ποσοτητα error
                    cout << "Not enough stock for " << productName << ". Available: " << item.first->quantity << "\n";
                } else {//αλλιως ενημερωνω ποσοτητα
                    item.second = newQuantity;
                    cout << "Quantity updated for " << productName << ".\n";
                }
                return;
            }
        }
        cout << "Product not found in cart.\n";
    }
    
    string getCartDetails() const {
    string details = "---CART START---\n";
    double totalCost = 0;
    //εμφανιζω  καθε προιον στο καλαθι με ποσοτητα
    for (const auto& item : items) {
        details += to_string(item.second) + " " + item.first->name + "\n";
        totalCost += item.second * item.first->price;
    }

    details += "---CART END---\n";
    details += "Total Cost: " + to_string(totalCost) + "\n";

    return details;
    }


    void checkout(vector<Product>& products, vector<string>& orderHistory) {
       double totalCost = 0;
       string order = "---ORDER START---\n";
       //αντιστοιχα το εμφανιζω αλλα το προσθετω και στο ιστορικο
       for (auto& item : items) {
            if (item.first->quantity >= item.second) {
               item.first->quantity -= item.second;
               totalCost += item.second * item.first->price;
               order += to_string(item.second) + " " + item.first->name + "\n";
            } else {
               cout << "Not enough stock for " << item.first->name << ".\n";
            }
       }
       order += "---ORDER END---\n";
       order += "Total Cost: " + to_string(totalCost) + "\n";
       orderHistory.push_back(order);

       items.clear();
       cout << "Order completed successfully!\n";
    }


    bool isEmpty() const {
        return items.empty();
    }
};

//κλαση πελατης
class Customer : public User {
private:
    Cart cart;
    vector<string> orderHistory;

public:
    Customer(string u, string p) : User(u, p, false) {}//constructor
    //διαχειριση του καλαθιου(μενου)
    void manageCart() {
    int choice;
    do {
        cout << "\nCart Menu:\n";
        cout << "1. Add product to cart\n";
        cout << "2. Remove product from cart\n";
        cout << "3. Update product quantity in cart\n";
        cout << "4. View cart details\n"; 
        cout << "5. Checkout\n";
        cout << "6. Exit\n";
        cout << "Enter choice: ";
        cin >> choice;
        cin.ignore();
        //διαχειριση της κλαση καλαθιου ουσιαστικα με την καθε επιλογη να δεχεται πληροφοριες και να τρεχει τις συναρτησεις του cart
        switch (choice) {
            case 1: {
                cout << "Enter product name: ";
                string productName;
                getline(cin, productName);
                cout << "Enter quantity: ";
                int quantity;
                cin >> quantity;
                cin.ignore();
                Product* product = Product::findProduct(productName);
                if (product) {
                    cart.addItem(product, quantity);
                } else {
                    cout << "Product not found.\n";
                }
                break;
            }
            case 2: {
                cout << "Enter product name to remove: ";
                string productName;
                getline(cin, productName);
                cart.removeItem(productName);
                break;
            }
            case 3: {
                cout << "Enter product name: ";
                string productName;
                getline(cin, productName);
                cout << "Enter new quantity: ";
                int quantity;
                cin >> quantity;
                cin.ignore();
                cart.updateQuantity(productName, quantity);
                break;
            }
            case 4: {
                cout << cart.getCartDetails(); 
                break;
            }
            case 5:
                if (!cart.isEmpty()) {
                    cart.checkout(Product::products, orderHistory);
                } else {
                    cout << "Cart is empty.\n";
                }
                break;
            case 6:
                cout << "Exiting cart menu...\n";
                break;
            default:
                cout << "Invalid choice. Try again.\n";
        }
    } while (choice != 6);
    }
    //εμφανιζει το ιστορικο αλλα δεν ξερω αμα το εκανα σωστα επειδη εχω το θεμα με το products.txt
    void viewOrderHistory() const {
        if (orderHistory.empty()) {//αν δεν υπαρχει ιστορικο εμφανιζει καταλληλα 
            cout << "No orders found.\n";
            return;
        }
        cout << "\nOrder History:\n";
        for (const auto& order : orderHistory) {//εμφανιζει καθε καλαθι για το ιστορικο
            cout << order << "\n";
        }
    }

    void viewProducts() {
        cout << "Available Products:\n";
        for (const auto& product : Product::products) {//για καθε προιον το βλεπει μαζι με τα χαρακτηριστικα του
            cout << product.name << " - " << product.price << " - " << product.quantity << "\n";
        }
    }

};

//τα μενου τα εφτιαξα για να ειναι πιο ευδιακριτο και πιο ευκολο στη διορθωση των λαθων
//γιατι στην αρχη ηταν πολυ μπερδεμενος ο κωδικας
//ρωτησα το gemini να μου δωσει ιδεες και επειδη δεν προλαβαινα να τα κανω σε διαφορετικα αρχεια και 
//να φτιαξω header files το εκανα ετσι
void adminMenu(Admin* admin) {
    int choice;
    do {
        //ουσιαστικα καλει τις συναρτησεις που προερχονται απο την κλαση αντμιν σαν επιλογες
        //τι εμφανιζει στον χρηστη και τις επιλογες που παιρνει
        //οι τυχον λεπτομερειες, πχ. getline... τις εχω εξηγησει ηδη στις εκαστοτε συναρτησεις
        cout << "\nAdmin Menu:\n";
        cout << "1. Display all users\n";
        cout << "2. Add a new product\n";
        cout << "3. Update a product\n";
        cout << "4. Search products\n";
        cout << "5. View product statistics\n"; 
        cout << "6. Exit\n";
        cout << "Enter your choice: ";
        cin >> choice;
        cin.ignore();

        switch (choice) {
            case 1:
                admin->displayAllUsers();
                break;
            case 2: {
                cout << "Enter product details:\n";
                string name, description, category, subcategory;
                double price;
                int quantity;
                cout << "Name: ";
                getline(cin, name);
                cout << "Description: ";
                getline(cin, description);
                cout << "Category: ";
                getline(cin, category);
                cout << "Subcategory: ";
                getline(cin, subcategory);
                cout << "Price: ";
                cin >> price;
                cout << "Quantity: ";
                cin >> quantity;
                cin.ignore();

                admin->addProduct(Product(name, description, category, subcategory, price, quantity));
                break;
            }
            case 3: {
                cout << "Enter the name of the product to update: ";
                string productName;
                getline(cin, productName);
                admin->updateProduct(productName);
                break;
            }
            case 4: {
                cout << "Search Products:\n";
                string title, category, subcategory;
                cout << "Enter title (leave blank for any): ";
                getline(cin, title);
                cout << "Enter category (leave blank for any): ";
                getline(cin, category);
                cout << "Enter subcategory (leave blank for any): ";
                getline(cin, subcategory);
                admin->searchProducts(title, category, subcategory);
                break;
            }
            case 5:
                admin->displayProductStatistics();
                break;
            case 6:
                cout << "Exiting admin menu...\n";
                break;
            default:
                cout << "Invalid choice. Try again.\n";
        }
    } while (choice != 6);
}


void customerMenu(Customer* customer) {
    int choice;
    //αντιστοιχα διαχειριζεται την αλληλεπιδραση του χρηστη με βαση τις συναρτησεις του πελατη
    do {
        cout << "\nCustomer Menu:\n";
        cout << "1. View products\n";
        cout << "2. Search products\n";
        cout << "3. Manage cart\n";
        cout << "4. View order history\n";
        cout << "5. Logout\n";
        cout << "Enter choice: ";
        cin >> choice;
        cin.ignore();

        switch (choice) {
            case 1:
                customer->viewProducts();
                break;
            case 2: {
                cout << "Search Products:\n";
                string title;
                cout << "Enter title to search: ";
                getline(cin, title);
                customer->searchProducts(title, "", "");
                break;
            }
            case 3:
                customer->manageCart();
                break;
            case 4:
                customer->viewOrderHistory();
                break;
            case 5:
                cout << "Logging out...\n";
                break;
            default:
                cout << "Invalid choice. Try again.\n";
        }
    } while (choice != 5);
}


int main() {
    //φορτωνω τα αρχεια απο τις συναρτησεις στις κλασεις
    User::loadFromFile("files/users.txt");
    Product::loadProductsFromFile("files/products.txt");

    int choice;
    //το βασικο μενου πριν κανει login και παει στα αλλα δυο μενου πιο πανω
    do {
        cout << "\nMain Menu:\n";
        cout << "1. Register\n";
        cout << "2. Login\n";
        cout << "3. Exit\n";
        cout << "Enter choice: ";
        cin >> choice;
        cin.ignore();

        if (choice == 1) {
            string username, password;
            bool isAdmin;
            cout << "Enter username: ";
            cin >> username;
            cout << "Enter password: ";
            cin >> password;
            cout << "Is admin? (1 for yes, 0 for no): ";
            cin >> isAdmin;
            User::registerUser(username, password, isAdmin);
        } else if (choice == 2) {
            string username, password;
            cout << "Enter username: ";
            cin >> username;
            cout << "Enter password: ";
            cin >> password;

            User* loggedInUser = User::loginUser(username, password);
            if (loggedInUser) {//αναλογα την ιδιοτητα κατευθεινεται στο καταλληλο μενου
                if (loggedInUser->isAdmin) {
                    Admin admin(loggedInUser->username, loggedInUser->password);
                    adminMenu(&admin);
                } else {
                    Customer customer(loggedInUser->username, loggedInUser->password);
                    customerMenu(&customer);
                }
            }
        }
    } while (choice != 3);

    return 0;
}
