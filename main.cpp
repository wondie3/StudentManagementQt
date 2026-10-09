#include <QApplication>
#include <QMainWindow>
#include <QDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QComboBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QFrame>
#include <QStackedWidget>
#include <QLineEdit>
#include <QDoubleValidator>
#include <QIntValidator>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QCryptographicHash>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QCloseEvent>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QStatusBar>
#include <QStyle>
#include <QIcon>
#include <QDebug>

static QString appDataDirectory() {
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(path);
    return path;
}

static QString databasePath() {
    return appDataDirectory() + "/students.db";
}

static QString hashPassword(const QString &password, const QByteArray &salt) {
    // Salted SHA-256 is adequate for a small classroom demo, not a substitute for Argon2/bcrypt in production.
    return QString::fromLatin1(QCryptographicHash::hash(salt + password.toUtf8(),
                                                         QCryptographicHash::Sha256).toHex());
}

class Database {
public:
    static bool initialize(QString *error = nullptr) {
        if (QSqlDatabase::contains("student_connection")) {
            db = QSqlDatabase::database("student_connection");
        } else {
            db = QSqlDatabase::addDatabase("QSQLITE", "student_connection");
            db.setDatabaseName(databasePath());
        }
        if (!db.open()) {
            if (error) *error = db.lastError().text();
            return false;
        }

        QSqlQuery query(db);
        const QStringList statements = {
            "PRAGMA foreign_keys = ON",
            "CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT NOT NULL UNIQUE COLLATE NOCASE, salt TEXT NOT NULL, password_hash TEXT NOT NULL, role TEXT NOT NULL DEFAULT 'admin', created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)",
            "CREATE TABLE IF NOT EXISTS students (id INTEGER PRIMARY KEY, name TEXT NOT NULL, department TEXT NOT NULL, email TEXT NOT NULL DEFAULT '', gpa REAL NOT NULL CHECK(gpa >= 0 AND gpa <= 4), created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)"
        };
        for (const QString &sql : statements) {
            if (!query.exec(sql)) {
                if (error) *error = query.lastError().text();
                return false;
            }
        }

        query.prepare("SELECT COUNT(*) FROM users");
        if (!query.exec() || !query.next()) {
            if (error) *error = query.lastError().text();
            return false;
        }
        if (query.value(0).toInt() == 0) {
            const QByteArray salt = QByteArray::number(QDateTime::currentMSecsSinceEpoch()).toHex();
            query.prepare("INSERT INTO users(username,salt,password_hash,role) VALUES(?,?,?,?)");
            query.addBindValue("admin");
            query.addBindValue(QString::fromLatin1(salt));
            query.addBindValue(hashPassword("Admin@123", salt));
            query.addBindValue("admin");
            if (!query.exec()) {
                if (error) *error = query.lastError().text();
                return false;
            }
        }
        return true;
    }

    static QSqlDatabase connection() { return db; }

    static bool authenticate(const QString &username, const QString &password, QString *role = nullptr) {
        QSqlQuery q(db);
        q.prepare("SELECT salt,password_hash,role FROM users WHERE username = ?");
        q.addBindValue(username.trimmed());
        if (!q.exec() || !q.next()) return false;
        const QByteArray salt = q.value(0).toString().toLatin1();
        if (hashPassword(password, salt) != q.value(1).toString()) return false;
        if (role) *role = q.value(2).toString();
        return true;
    }

    static bool createUser(const QString &username, const QString &password, const QString &role, QString *error = nullptr) {
        if (username.trimmed().size() < 3 || password.size() < 8) {
            if (error) *error = "Username must contain at least 3 characters and password at least 8 characters.";
            return false;
        }
        const QByteArray salt = QByteArray::number(QDateTime::currentMSecsSinceEpoch()).toHex()
                                + QByteArray::number(qHash(username)).toHex();
        QSqlQuery q(db);
        q.prepare("INSERT INTO users(username,salt,password_hash,role) VALUES(?,?,?,?)");
        q.addBindValue(username.trimmed());
        q.addBindValue(QString::fromLatin1(salt));
        q.addBindValue(hashPassword(password, salt));
        q.addBindValue(role);
        if (!q.exec()) {
            if (error) *error = q.lastError().text();
            return false;
        }
        return true;
    }

    static bool hasOtherAdmin() {
        QSqlQuery q(db);
        q.prepare("SELECT COUNT(*) FROM users WHERE role='admin'");
        return q.exec() && q.next() && q.value(0).toInt() > 1;
    }

private:
    inline static QSqlDatabase db;
};

class StudentDialog : public QDialog {
public:
    StudentDialog(QWidget *parent = nullptr, bool edit = false,
                  int id = 0, QString name = {}, QString department = {},
                  QString email = {}, double gpa = 0.0)
        : QDialog(parent), editing(edit) {
        setWindowTitle(edit ? "Edit Student" : "Add Student");
        setModal(true);
        setMinimumWidth(420);

        auto *layout = new QVBoxLayout(this);
        auto *heading = new QLabel(edit ? "Update student details" : "Create a student record");
        heading->setObjectName("dialogHeading");
        layout->addWidget(heading);

        auto *form = new QFormLayout;
        idInput = new QLineEdit;
        idInput->setPlaceholderText("e.g. 10024");
        idInput->setValidator(new QIntValidator(1, 2147483647, idInput));
        nameInput = new QLineEdit;
        nameInput->setPlaceholderText("Full name");
        departmentInput = new QLineEdit;
        departmentInput->setPlaceholderText("e.g. Computer Science");
        emailInput = new QLineEdit;
        emailInput->setPlaceholderText("name@example.com (optional)");
        gpaInput = new QLineEdit;
        gpaInput->setPlaceholderText("0.00 - 4.00");
        gpaInput->setValidator(new QDoubleValidator(0.0, 4.0, 2, gpaInput));
        form->addRow("Student ID", idInput);
        form->addRow("Full name", nameInput);
        form->addRow("Department", departmentInput);
        form->addRow("Email", emailInput);
        form->addRow("GPA", gpaInput);
        layout->addLayout(form);

        auto *buttons = new QHBoxLayout;
        buttons->addStretch();
        auto *cancel = new QPushButton("Cancel");
        auto *save = new QPushButton(edit ? "Save changes" : "Add student");
        save->setObjectName("primaryButton");
        buttons->addWidget(cancel);
        buttons->addWidget(save);
        layout->addLayout(buttons);

        idInput->setText(edit ? QString::number(id) : "");
        idInput->setReadOnly(edit);
        nameInput->setText(name);
        departmentInput->setText(department);
        emailInput->setText(email);
        gpaInput->setText(edit ? QString::number(gpa, 'f', 2) : "");
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(save, &QPushButton::clicked, this, [this]() { validateAndAccept(); });
    }

    int studentId() const { return idInput->text().toInt(); }
    QString studentName() const { return nameInput->text().trimmed(); }
    QString department() const { return departmentInput->text().trimmed(); }
    QString email() const { return emailInput->text().trimmed(); }
    double gpa() const { return gpaInput->text().toDouble(); }

private:
    void validateAndAccept() {
        if (idInput->text().trimmed().isEmpty() || nameInput->text().trimmed().isEmpty()
            || departmentInput->text().trimmed().isEmpty() || gpaInput->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, "Missing information", "Student ID, name, department, and GPA are required.");
            return;
        }
        if (gpa() < 0 || gpa() > 4) {
            QMessageBox::warning(this, "Invalid GPA", "GPA must be between 0.00 and 4.00.");
            return;
        }
        if (!email().isEmpty() && !QRegularExpression(R"(^[^@\s]+@[^@\s]+\.[^@\s]+$)").match(email()).hasMatch()) {
            QMessageBox::warning(this, "Invalid email", "Enter a valid email address or leave it empty.");
            return;
        }
        accept();
    }

    bool editing;
    QLineEdit *idInput;
    QLineEdit *nameInput;
    QLineEdit *departmentInput;
    QLineEdit *emailInput;
    QLineEdit *gpaInput;
};

class LoginDialog : public QDialog {
public:
    explicit LoginDialog(QWidget *parent = nullptr) : QDialog(parent) {
        setWindowTitle("Sign in • StudentHub");
        setMinimumWidth(400);
        setModal(true);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(32, 28, 32, 28);
        layout->setSpacing(12);

        auto *logo = new QLabel("StudentHub");
        logo->setObjectName("brandTitle");
        logo->setAlignment(Qt::AlignCenter);
        auto *subtitle = new QLabel("Student Management System");
        subtitle->setAlignment(Qt::AlignCenter);
        subtitle->setObjectName("mutedText");
        layout->addWidget(logo);
        layout->addWidget(subtitle);
        layout->addSpacing(10);

        username = new QLineEdit;
        username->setPlaceholderText("Username");
        username->setText("admin");
        password = new QLineEdit;
        password->setPlaceholderText("Password");
        password->setEchoMode(QLineEdit::Password);
        password->setText("Admin@123");
        auto *showPassword = new QCheckBox("Show password");
        connect(showPassword, &QCheckBox::toggled, this, [this](bool show) {
            password->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
        });
        auto *signIn = new QPushButton("Sign in");
        signIn->setObjectName("primaryButton");
        signIn->setMinimumHeight(42);
        layout->addWidget(new QLabel("Username"));
        layout->addWidget(username);
        layout->addWidget(new QLabel("Password"));
        layout->addWidget(password);
        layout->addWidget(showPassword);
        layout->addSpacing(6);
        layout->addWidget(signIn);
        auto *hint = new QLabel("First-run demo login: admin / Admin@123\nChange the password or create another account after signing in.");
        hint->setObjectName("mutedText");
        hint->setWordWrap(true);
        layout->addWidget(hint);
        connect(signIn, &QPushButton::clicked, this, [this]() {
            QString role;
            if (Database::authenticate(username->text(), password->text(), &role)) {
                loggedUsername = username->text().trimmed();
                loggedRole = role;
                accept();
            } else {
                QMessageBox::warning(this, "Sign-in failed", "The username or password is incorrect.");
                password->selectAll();
                password->setFocus();
            }
        });
        connect(password, &QLineEdit::returnPressed, signIn, &QPushButton::click);
    }
    QString usernameValue() const { return loggedUsername; }
    QString roleValue() const { return loggedRole; }
private:
    QLineEdit *username;
    QLineEdit *password;
    QString loggedUsername;
    QString loggedRole;
};

class MainWindow : public QMainWindow {
public:
    MainWindow(QString username, QString role) : currentUser(std::move(username)), currentRole(std::move(role)) {
        setWindowTitle("StudentHub • Student Management System");
        setWindowIcon(QIcon(":/app_icon.svg"));
        resize(1180, 760);
        buildUi();
        applyTheme(false);
        refreshDepartmentFilter();
        refreshStudents();
        updateStats();

        auto *effect = new QGraphicsOpacityEffect(this);
        centralWidget()->setGraphicsEffect(effect);
        auto *animation = new QPropertyAnimation(effect, "opacity", this);
        animation->setDuration(260);
        animation->setStartValue(0.45);
        animation->setEndValue(1.0);
        animation->start(QAbstractAnimation::DeleteWhenStopped);
        statusBar()->showMessage("Signed in as " + currentUser + " • Data is stored locally in SQLite");
    }

private:
    void buildUi() {
        auto *root = new QWidget;
        auto *outer = new QHBoxLayout(root);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);

        sidebar = new QFrame;
        sidebar->setObjectName("sidebar");
        sidebar->setFixedWidth(220);
        auto *side = new QVBoxLayout(sidebar);
        side->setContentsMargins(18, 24, 18, 18);
        side->setSpacing(10);
        auto *brand = new QLabel("◆  StudentHub");
        brand->setObjectName("sidebarBrand");
        side->addWidget(brand);
        auto *tag = new QLabel("CAMPUS ADMINISTRATION");
        tag->setObjectName("sidebarCaption");
        side->addWidget(tag);
        side->addSpacing(22);

        auto *dashboardBtn = new QPushButton("  ◈   Dashboard");
        auto *studentsBtn = new QPushButton("  ▦   Students");
        auto *exportBtn = new QPushButton("  ⇩   Export CSV");
        auto *usersBtn = new QPushButton("  ♙   User accounts");
        auto *themeBtn = new QPushButton("  ◐   Toggle appearance");
        auto *aboutBtn = new QPushButton("  ⓘ   About");
        for (auto *b : {dashboardBtn, studentsBtn, exportBtn, usersBtn, themeBtn, aboutBtn}) {
            b->setObjectName("navButton");
            b->setMinimumHeight(42);
            b->setCursor(Qt::PointingHandCursor);
            side->addWidget(b);
        }
        side->addStretch();
        auto *userCard = new QLabel("SIGNED IN AS\n" + currentUser + "\nRole: " + currentRole);
        userCard->setObjectName("userCard");
        userCard->setWordWrap(true);
        side->addWidget(userCard);
        auto *logout = new QPushButton("Sign out");
        logout->setObjectName("logoutButton");
        side->addWidget(logout);

        auto *content = new QWidget;
        auto *mainLayout = new QVBoxLayout(content);
        mainLayout->setContentsMargins(28, 24, 28, 22);
        mainLayout->setSpacing(18);

        auto *top = new QHBoxLayout;
        auto *titles = new QVBoxLayout;
        auto *title = new QLabel("Student overview");
        title->setObjectName("pageTitle");
        auto *subtitle = new QLabel("Manage student records and monitor academic performance.");
        subtitle->setObjectName("mutedText");
        titles->addWidget(title);
        titles->addWidget(subtitle);
        top->addLayout(titles);
        top->addStretch();
        auto *date = new QLabel(QDate::currentDate().toString("dddd, dd MMMM yyyy"));
        date->setObjectName("dateLabel");
        top->addWidget(date, 0, Qt::AlignTop);
        mainLayout->addLayout(top);

        auto *cards = new QGridLayout;
        cards->setHorizontalSpacing(14);
        cards->setVerticalSpacing(14);
        totalValue = makeStatCard(cards, 0, "TOTAL STUDENTS", "0", "Across all departments");
        averageValue = makeStatCard(cards, 1, "AVERAGE GPA", "0.00", "Academic average");
        topGpaValue = makeStatCard(cards, 2, "HIGHEST GPA", "0.00", "Top recorded GPA");
        departmentsValue = makeStatCard(cards, 3, "DEPARTMENTS", "0", "Unique departments");
        mainLayout->addLayout(cards);

        auto *tableHeader = new QHBoxLayout;
        auto *sectionTitle = new QLabel("Student records");
        sectionTitle->setObjectName("sectionTitle");
        tableHeader->addWidget(sectionTitle);
        tableHeader->addStretch();
        searchInput = new QLineEdit;
        searchInput->setPlaceholderText("Search ID, name, department, email…");
        searchInput->setClearButtonEnabled(true);
        searchInput->setMinimumWidth(260);
        departmentFilter = new QComboBox;
        departmentFilter->setMinimumWidth(160);
        tableHeader->addWidget(searchInput);
        tableHeader->addWidget(departmentFilter);
        mainLayout->addLayout(tableHeader);

        table = new QTableWidget(0, 5);
        table->setHorizontalHeaderLabels({"Student ID", "Full name", "Department", "Email", "GPA"});
        table->setObjectName("studentTable");
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setAlternatingRowColors(true);
        table->verticalHeader()->setVisible(false);
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        table->setSortingEnabled(true);
        mainLayout->addWidget(table, 1);

        auto *actions = new QHBoxLayout;
        auto *addBtn = new QPushButton("+  Add student");
        addBtn->setObjectName("primaryButton");
        auto *editBtn = new QPushButton("Edit selected");
        auto *deleteBtn = new QPushButton("Delete selected");
        auto *refreshBtn = new QPushButton("Refresh");
        actions->addWidget(addBtn);
        actions->addWidget(editBtn);
        actions->addWidget(deleteBtn);
        actions->addStretch();
        actions->addWidget(refreshBtn);
        mainLayout->addLayout(actions);

        outer->addWidget(sidebar);
        outer->addWidget(content, 1);
        setCentralWidget(root);

        connect(addBtn, &QPushButton::clicked, this, [this]() { addStudent(); });
        connect(editBtn, &QPushButton::clicked, this, [this]() { editSelected(); });
        connect(deleteBtn, &QPushButton::clicked, this, [this]() { deleteSelected(); });
        connect(refreshBtn, &QPushButton::clicked, this, [this]() { refreshDepartmentFilter(); refreshStudents(); updateStats(); });
        connect(searchInput, &QLineEdit::textChanged, this, [this]() { refreshStudents(); });
        connect(departmentFilter, &QComboBox::currentTextChanged, this, [this]() { refreshStudents(); });
        connect(exportBtn, &QPushButton::clicked, this, [this]() { exportCsv(); });
        connect(usersBtn, &QPushButton::clicked, this, [this]() { manageUsers(); });
        connect(themeBtn, &QPushButton::clicked, this, [this]() { darkMode = !darkMode; applyTheme(darkMode); });
        connect(aboutBtn, &QPushButton::clicked, this, [this]() {
            QMessageBox::about(this, "About StudentHub",
                "StudentHub Student Management System\n\nBuilt with C++17, Qt 6 Widgets and SQLite.\nIncludes account login, student records, search, filtering, sorting, statistics, CSV export and themes.");
        });
        connect(dashboardBtn, &QPushButton::clicked, this, [this]() {
            searchInput->clear();
            departmentFilter->setCurrentIndex(0);
            refreshStudents();
            updateStats();
        });
        connect(studentsBtn, &QPushButton::clicked, this, [this]() { searchInput->setFocus(); });
        connect(logout, &QPushButton::clicked, this, [this]() {
            if (QMessageBox::question(this, "Sign out", "Sign out of StudentHub?") == QMessageBox::Yes) {
                close();
                QApplication::exit(42);
            }
        });
        if (currentRole != "admin") usersBtn->setEnabled(false);
    }

    QLabel *makeStatCard(QGridLayout *grid, int column, const QString &caption,
                         const QString &value, const QString &description) {
        auto *card = new QFrame;
        card->setObjectName("statCard");
        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(18, 16, 18, 16);
        auto *c = new QLabel(caption);
        c->setObjectName("statCaption");
        auto *v = new QLabel(value);
        v->setObjectName("statValue");
        auto *d = new QLabel(description);
        d->setObjectName("mutedText");
        layout->addWidget(c);
        layout->addWidget(v);
        layout->addWidget(d);
        grid->addWidget(card, 0, column);
        return v;
    }

    void applyTheme(bool dark) {
        darkMode = dark;
        const QString light = R"(
            QWidget { background:#f4f7fb; color:#182338; font-family:"Segoe UI"; font-size:10pt; }
            QMainWindow { background:#f4f7fb; }
            #sidebar { background:#101b32; }
            #sidebar QLabel { background:transparent; }
            #sidebarBrand { color:#ffffff; font-size:17pt; font-weight:700; padding-bottom:5px; }
            #sidebarCaption { color:#91a4c5; font-size:8pt; letter-spacing:1px; }
            #navButton { color:#dce6f8; background:transparent; text-align:left; border:0; border-radius:8px; padding:9px; }
            #navButton:hover { background:#223454; }
            #userCard { color:#c8d6ef; background:#1a2945; border-radius:9px; padding:12px; }
            #logoutButton { color:#fecaca; background:#402632; border:0; border-radius:8px; padding:10px; }
            #pageTitle { font-size:23pt; font-weight:700; color:#17233a; }
            #sectionTitle { font-size:15pt; font-weight:650; }
            #dateLabel { color:#64748b; }
            #mutedText { color:#64748b; }
            #statCard { background:#ffffff; border:1px solid #e4eaf3; border-radius:12px; }
            #statCaption { color:#64748b; font-size:8pt; font-weight:700; }
            #statValue { color:#1d4ed8; font-size:24pt; font-weight:700; }
            QLineEdit, QComboBox { background:#ffffff; border:1px solid #d6deea; border-radius:7px; padding:9px; selection-background-color:#2563eb; }
            QLineEdit:focus, QComboBox:focus { border:1px solid #3b82f6; }
            QPushButton { background:#ffffff; border:1px solid #d6deea; border-radius:7px; padding:9px 14px; }
            QPushButton:hover { background:#edf3fb; }
            #primaryButton { color:white; background:#2563eb; border:0; font-weight:600; }
            #primaryButton:hover { background:#1d4ed8; }
            #studentTable { background:#ffffff; alternate-background-color:#f7f9fc; gridline-color:#e8edf4; border:1px solid #e0e7f0; border-radius:8px; }
            QHeaderView::section { background:#edf2f8; color:#475569; padding:10px; border:0; border-bottom:1px solid #dbe3ef; font-weight:700; }
            QTableWidget::item { padding:7px; }
            #dialogHeading { font-size:16pt; font-weight:700; }
            #brandTitle { color:#2563eb; font-size:25pt; font-weight:800; }
            QStatusBar { color:#64748b; background:#f4f7fb; }
        )";
        const QString darkStyle = R"(
            QWidget { background:#111827; color:#e5e7eb; font-family:"Segoe UI"; font-size:10pt; }
            QMainWindow { background:#111827; }
            #sidebar { background:#080f1f; }
            #sidebar QLabel { background:transparent; }
            #sidebarBrand { color:#ffffff; font-size:17pt; font-weight:700; padding-bottom:5px; }
            #sidebarCaption { color:#8393b2; font-size:8pt; letter-spacing:1px; }
            #navButton { color:#dce6f8; background:transparent; text-align:left; border:0; border-radius:8px; padding:9px; }
            #navButton:hover { background:#1c2a43; }
            #userCard { color:#c8d6ef; background:#14213a; border-radius:9px; padding:12px; }
            #logoutButton { color:#fecaca; background:#48212b; border:0; border-radius:8px; padding:10px; }
            #pageTitle { font-size:23pt; font-weight:700; color:#f3f4f6; }
            #sectionTitle { font-size:15pt; font-weight:650; }
            #dateLabel { color:#9ca3af; }
            #mutedText { color:#9ca3af; }
            #statCard { background:#1f2937; border:1px solid #374151; border-radius:12px; }
            #statCaption { color:#9ca3af; font-size:8pt; font-weight:700; }
            #statValue { color:#60a5fa; font-size:24pt; font-weight:700; }
            QLineEdit, QComboBox { background:#1f2937; border:1px solid #4b5563; border-radius:7px; padding:9px; selection-background-color:#2563eb; }
            QLineEdit:focus, QComboBox:focus { border:1px solid #60a5fa; }
            QPushButton { background:#273449; border:1px solid #3f4b5f; border-radius:7px; padding:9px 14px; }
            QPushButton:hover { background:#34445d; }
            #primaryButton { color:white; background:#2563eb; border:0; font-weight:600; }
            #primaryButton:hover { background:#1d4ed8; }
            #studentTable { background:#1f2937; alternate-background-color:#202c3d; gridline-color:#374151; border:1px solid #374151; border-radius:8px; }
            QHeaderView::section { background:#273449; color:#cbd5e1; padding:10px; border:0; border-bottom:1px solid #374151; font-weight:700; }
            QTableWidget::item { padding:7px; }
            #dialogHeading { font-size:16pt; font-weight:700; }
            #brandTitle { color:#60a5fa; font-size:25pt; font-weight:800; }
            QStatusBar { color:#9ca3af; background:#111827; }
        )";
        qApp->setStyleSheet(dark ? darkStyle : light);
    }

    void refreshDepartmentFilter() {
        const QString current = departmentFilter->currentText();
        departmentFilter->blockSignals(true);
        departmentFilter->clear();
        departmentFilter->addItem("All departments");
        QSqlQuery q(Database::connection());
        if (q.exec("SELECT DISTINCT department FROM students ORDER BY department COLLATE NOCASE")) {
            while (q.next()) departmentFilter->addItem(q.value(0).toString());
        }
        int idx = departmentFilter->findText(current);
        departmentFilter->setCurrentIndex(idx >= 0 ? idx : 0);
        departmentFilter->blockSignals(false);
    }

    void refreshStudents() {
        const bool sorting = table->isSortingEnabled();
        table->setSortingEnabled(false);
        table->setRowCount(0);
        QSqlQuery q(Database::connection());
        q.prepare("SELECT id,name,department,email,gpa FROM students WHERE "
                  "(CAST(id AS TEXT) LIKE ? OR name LIKE ? OR department LIKE ? OR email LIKE ?) "
                  "AND (? = 'All departments' OR department = ?) ORDER BY id");
        const QString pattern = "%" + searchInput->text().trimmed() + "%";
        q.addBindValue(pattern); q.addBindValue(pattern); q.addBindValue(pattern); q.addBindValue(pattern);
        q.addBindValue(departmentFilter->currentText()); q.addBindValue(departmentFilter->currentText());
        if (!q.exec()) {
            statusBar()->showMessage("Could not load students: " + q.lastError().text());
            return;
        }
        while (q.next()) {
            const int row = table->rowCount();
            table->insertRow(row);
            for (int col = 0; col < 5; ++col) {
                auto *item = new QTableWidgetItem;
                if (col == 0) item->setData(Qt::DisplayRole, q.value(0));
                else if (col == 4) item->setData(Qt::DisplayRole, q.value(4).toDouble());
                else item->setText(q.value(col).toString());
                table->setItem(row, col, item);
            }
            table->item(row, 4)->setText(QString::number(q.value(4).toDouble(), 'f', 2));
        }
        table->setSortingEnabled(sorting);
        statusBar()->showMessage(QString("Showing %1 student record(s)").arg(table->rowCount()));
    }

    void updateStats() {
        QSqlQuery q(Database::connection());
        if (q.exec("SELECT COUNT(*), COALESCE(AVG(gpa),0), COALESCE(MAX(gpa),0), COUNT(DISTINCT department) FROM students") && q.next()) {
            totalValue->setText(q.value(0).toString());
            averageValue->setText(QString::number(q.value(1).toDouble(), 'f', 2));
            topGpaValue->setText(QString::number(q.value(2).toDouble(), 'f', 2));
            departmentsValue->setText(q.value(3).toString());
        }
    }

    void addStudent() {
        StudentDialog dialog(this);
        if (dialog.exec() != QDialog::Accepted) return;
        QSqlQuery q(Database::connection());
        q.prepare("INSERT INTO students(id,name,department,email,gpa) VALUES(?,?,?,?,?)");
        q.addBindValue(dialog.studentId());
        q.addBindValue(dialog.studentName());
        q.addBindValue(dialog.department());
        q.addBindValue(dialog.email());
        q.addBindValue(dialog.gpa());
        if (!q.exec()) {
            QMessageBox::warning(this, "Could not add student",
                q.lastError().text().contains("UNIQUE", Qt::CaseInsensitive)
                    ? "That student ID already exists." : q.lastError().text());
            return;
        }
        refreshDepartmentFilter(); refreshStudents(); updateStats();
        statusBar()->showMessage("Student added successfully.", 4000);
    }

    int selectedStudentId() const {
        const int row = table->currentRow();
        if (row < 0 || !table->item(row, 0)) return -1;
        return table->item(row, 0)->text().toInt();
    }

    void editSelected() {
        const int id = selectedStudentId();
        if (id < 0) {
            QMessageBox::information(this, "Select a student", "Choose a student row first.");
            return;
        }
        QSqlQuery q(Database::connection());
        q.prepare("SELECT name,department,email,gpa FROM students WHERE id=?");
        q.addBindValue(id);
        if (!q.exec() || !q.next()) {
            QMessageBox::warning(this, "Record unavailable", "This student record could not be loaded.");
            return;
        }
        StudentDialog dialog(this, true, id, q.value(0).toString(), q.value(1).toString(),
                             q.value(2).toString(), q.value(3).toDouble());
        if (dialog.exec() != QDialog::Accepted) return;
        QSqlQuery update(Database::connection());
        update.prepare("UPDATE students SET name=?,department=?,email=?,gpa=?,updated_at=CURRENT_TIMESTAMP WHERE id=?");
        update.addBindValue(dialog.studentName());
        update.addBindValue(dialog.department());
        update.addBindValue(dialog.email());
        update.addBindValue(dialog.gpa());
        update.addBindValue(id);
        if (!update.exec()) {
            QMessageBox::warning(this, "Update failed", update.lastError().text());
            return;
        }
        refreshDepartmentFilter(); refreshStudents(); updateStats();
        statusBar()->showMessage("Student updated successfully.", 4000);
    }

    void deleteSelected() {
        const int id = selectedStudentId();
        if (id < 0) {
            QMessageBox::information(this, "Select a student", "Choose a student row first.");
            return;
        }
        if (QMessageBox::question(this, "Delete student",
            QString("Permanently delete student ID %1?").arg(id),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        QSqlQuery q(Database::connection());
        q.prepare("DELETE FROM students WHERE id=?");
        q.addBindValue(id);
        if (!q.exec()) {
            QMessageBox::warning(this, "Delete failed", q.lastError().text());
            return;
        }
        refreshDepartmentFilter(); refreshStudents(); updateStats();
        statusBar()->showMessage("Student deleted.", 4000);
    }

    void exportCsv() {
        const QString path = QFileDialog::getSaveFileName(this, "Export student records",
            QDir::homePath() + "/student_records.csv", "CSV files (*.csv)");
        if (path.isEmpty()) return;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(this, "Export failed", file.errorString());
            return;
        }
        QTextStream out(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        out.setEncoding(QStringConverter::Utf8);
#endif
        out << "Student ID,Full Name,Department,Email,GPA\n";
        QSqlQuery q(Database::connection());
        q.prepare("SELECT id,name,department,email,gpa FROM students WHERE "
                  "(CAST(id AS TEXT) LIKE ? OR name LIKE ? OR department LIKE ? OR email LIKE ?) "
                  "AND (? = 'All departments' OR department = ?) ORDER BY id");
        const QString pattern = "%" + searchInput->text().trimmed() + "%";
        q.addBindValue(pattern); q.addBindValue(pattern); q.addBindValue(pattern); q.addBindValue(pattern);
        q.addBindValue(departmentFilter->currentText()); q.addBindValue(departmentFilter->currentText());
        if (!q.exec()) {
            QMessageBox::warning(this, "Export failed", q.lastError().text());
            return;
        }
        auto csv = [](QString value) {
            value.replace("\"", "\"\"");
            return "\"" + value + "\"";
        };
        while (q.next()) {
            out << q.value(0).toString() << ","
                << csv(q.value(1).toString()) << ","
                << csv(q.value(2).toString()) << ","
                << csv(q.value(3).toString()) << ","
                << QString::number(q.value(4).toDouble(), 'f', 2) << "\n";
        }
        file.close();
        QMessageBox::information(this, "Export complete", "CSV report saved successfully.\n\n" + path);
    }

    void manageUsers() {
        if (currentRole != "admin") {
            QMessageBox::warning(this, "Permission denied", "Only administrators can manage user accounts.");
            return;
        }
        QDialog dialog(this);
        dialog.setWindowTitle("Create user account");
        dialog.setMinimumWidth(400);
        auto *layout = new QVBoxLayout(&dialog);
        auto *heading = new QLabel("Add a system user");
        heading->setObjectName("dialogHeading");
        layout->addWidget(heading);
        auto *form = new QFormLayout;
        auto *username = new QLineEdit;
        username->setPlaceholderText("At least 3 characters");
        auto *password = new QLineEdit;
        password->setEchoMode(QLineEdit::Password);
        password->setPlaceholderText("At least 8 characters");
        auto *role = new QComboBox;
        role->addItems({"staff", "admin"});
        form->addRow("Username", username);
        form->addRow("Password", password);
        form->addRow("Role", role);
        layout->addLayout(form);
        auto *buttons = new QHBoxLayout;
        buttons->addStretch();
        auto *cancel = new QPushButton("Cancel");
        auto *create = new QPushButton("Create account");
        create->setObjectName("primaryButton");
        buttons->addWidget(cancel); buttons->addWidget(create);
        layout->addLayout(buttons);
        connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
        connect(create, &QPushButton::clicked, &dialog, [&]() {
            QString error;
            if (!Database::createUser(username->text(), password->text(), role->currentText(), &error)) {
                QMessageBox::warning(&dialog, "Could not create account", error);
                return;
            }
            QMessageBox::information(&dialog, "Account created", "The user account was created successfully.");
            dialog.accept();
        });
        dialog.exec();
    }

    QString currentUser;
    QString currentRole;
    bool darkMode = false;
    QFrame *sidebar = nullptr;
    QLineEdit *searchInput = nullptr;
    QComboBox *departmentFilter = nullptr;
    QTableWidget *table = nullptr;
    QLabel *totalValue = nullptr;
    QLabel *averageValue = nullptr;
    QLabel *topGpaValue = nullptr;
    QLabel *departmentsValue = nullptr;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("StudentHub");
    QApplication::setOrganizationName("StudentHub");
    QApplication::setWindowIcon(QIcon(":/app_icon.svg"));

    QString error;
    if (!Database::initialize(&error)) {
        QMessageBox::critical(nullptr, "Database error",
            "StudentHub could not initialize its SQLite database:\n\n" + error +
            "\n\nMake sure the Qt SQLite driver is installed.");
        return 1;
    }

    LoginDialog login;
    if (login.exec() != QDialog::Accepted) return 0;

    MainWindow window(login.usernameValue(), login.roleValue());
    window.show();
    return app.exec();
}
