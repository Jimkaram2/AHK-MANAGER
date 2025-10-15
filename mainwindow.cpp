#include "mainwindow.h"
#include <QVBoxLayout>
#include <QFileInfo>
#include <QBrush>
#include <QFont>
#include <QDirIterator>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QtConcurrent/QtConcurrentRun>
#include <QStandardPaths>
#include <QFile>
#include <QHeaderView>
#include <QDesktopServices>
#include <QUrl>
#include <QMenu>
#include <QStyle>

// Helper to find the script item recursively
static QTreeWidgetItem* findScriptItem(QTreeWidget* tree, const QString& path) {
    std::function<QTreeWidgetItem*(QTreeWidgetItem*)> recurse = [&](QTreeWidgetItem* parent) -> QTreeWidgetItem* {
        for (int i = 0; i < parent->childCount(); ++i) {
            QTreeWidgetItem* child = parent->child(i);
            if (child->data(0, Qt::UserRole).toString() == path)
                return child;
            if (child->childCount() > 0) {
                QTreeWidgetItem* found = recurse(child);
                if (found) return found;
            }
        }
        return nullptr;
    };
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* top = tree->topLevelItem(i);
        QTreeWidgetItem* found = recurse(top);
        if (found) return found;
    }
    return nullptr;
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    QWidget* central = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(central);

    QLabel* title = new QLabel("AHK Script Manager", this);
    QFont titleFont("Segoe UI", 20, QFont::Bold);
    title->setFont(titleFont);
    title->setStyleSheet("color: #90caf9; margin: 12px;");
    layout->addWidget(title);

    // Filter bar
    filterEdit = new QLineEdit(this);
    filterEdit->setPlaceholderText("Filter scripts...");
    filterEdit->setStyleSheet("margin:6px; padding:4px; background-color:#222b3a; color:#e3eaf2; border-radius:6px;");
    layout->addWidget(filterEdit);
    connect(filterEdit, &QLineEdit::textChanged, this, &MainWindow::filterTree);

    // Scan button
    scanButton = new QPushButton("Scan C:/ for AHK Scripts", this);
    scanButton->setStyleSheet("margin: 6px;");
    layout->addWidget(scanButton);
    connect(scanButton, &QPushButton::clicked, this, &MainWindow::startScan);

    // Tree widget
    scriptTree = new QTreeWidget(this);
    scriptTree->setHeaderLabels({"Script", "Status", "Run", "Stop"});
    scriptTree->header()->setStretchLastSection(true);
    scriptTree->setDragDropMode(QAbstractItemView::InternalMove);
    scriptTree->setStyleSheet(
        // keep text color constant even when selected, and a subtle selected background
        "QTreeWidget { background-color: #23272e; color: #e3eaf2; border-radius: 10px; font: 14px 'Segoe UI'; }"
        "QHeaderView::section { background-color: #23272e; color: #90caf9; font-weight: bold; border: none; }"
        "QTreeWidget::item:selected { background-color: #2c313c; color: #e3eaf2; }"
        "QTreeWidget::item:selected:active { background-color: #2c313c; color: #e3eaf2; }"
        "QTreeWidget::item:selected:!active { background-color: #2c313c; color: #e3eaf2; }"
        );
    layout->addWidget(scriptTree);

    // Context menu
    scriptTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(scriptTree, &QTreeWidget::customContextMenuRequested, this, &MainWindow::showContextMenu);

    // Status label + progress bar
    statusLabel = new QLabel("Ready.", this);
    layout->addWidget(statusLabel);

    progressBar = new QProgressBar(this);
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setTextVisible(true);
    progressBar->setStyleSheet(
        "QProgressBar {"
        "background-color: #222b3a; color: #e3eaf2; border-radius: 8px; text-align: center; height: 18px;}"
        "QProgressBar::chunk {background-color: #90caf9; border-radius: 8px;}"
        );
    progressBar->hide();
    layout->addWidget(progressBar);

    setCentralWidget(central);

    setWindowTitle("AHK Script Manager");
    resize(900, 650);
    setStyleSheet(
        "QWidget { background-color: #181a20; color: #e3eaf2; font: 14px 'Segoe UI'; }"
        "QPushButton { background-color: #222b3a; color: #90caf9; border: none; border-radius: 8px; padding: 6px 18px; font: 13px 'Segoe UI'; }"
        "QPushButton:hover { background-color: #263245; color: #ffffff; }"
        "QPushButton:pressed { background-color: #1b222c; }"
        );

    watcher = new QFutureWatcher<void>(this);
    connect(this, &MainWindow::scriptFound, this, &MainWindow::handleScriptFound);
    connect(this, &MainWindow::progressUpdate, this, &MainWindow::updateProgress);
    connect(watcher, &QFutureWatcher<void>::finished, this, &MainWindow::scanFinished);

    // Icons
    greenIcon = style()->standardIcon(QStyle::SP_DialogApplyButton);
    redIcon = style()->standardIcon(QStyle::SP_DialogCancelButton);

    cacheFilePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/scripts_cache.json";
    QDir().mkpath(QFileInfo(cacheFilePath).absolutePath());

    loadCache();
}

MainWindow::~MainWindow() {
    saveCache();
}

void MainWindow::filterTree(const QString& text) {
    for (int i = 0; i < scriptTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* folderItem = scriptTree->topLevelItem(i);
        bool folderVisible = folderItem->text(0).contains(text, Qt::CaseInsensitive);
        for (int j = 0; j < folderItem->childCount(); ++j) {
            QTreeWidgetItem* child = folderItem->child(j);
            bool match = child->text(0).contains(text, Qt::CaseInsensitive);
            child->setHidden(!match);
            if (match) folderVisible = true;
        }
        folderItem->setHidden(!folderVisible);
    }
}

void MainWindow::showContextMenu(const QPoint& pos) {
    QTreeWidgetItem* item = scriptTree->itemAt(pos);
    if (!item) return;
    QString path = item->data(0, Qt::UserRole).toString();
    bool isScript = !path.isEmpty();

    QMenu menu(this);
    if (isScript) {
        QAction* runAct = menu.addAction("Run");
        QAction* stopAct = menu.addAction("Stop");
        QAction* openAct = menu.addAction("Open in Explorer");
        QAction* editAct = menu.addAction("Edit");
        QAction* delAct = menu.addAction("Delete from List");
        QAction* chosen = menu.exec(scriptTree->viewport()->mapToGlobal(pos));
        if (!chosen) return;
        if (chosen == runAct) runScriptForPath(path);
        else if (chosen == stopAct) stopScriptForPath(path);
        else if (chosen == openAct) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
        else if (chosen == editAct) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        else if (chosen == delAct) {
            delete item;
        }
    }
}

void MainWindow::runScriptForPath(const QString& path) {
    QTreeWidgetItem* item = findScriptItem(scriptTree, path);
    if (!item) return;
    if (runningScripts.contains(path)) return;

    auto* proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [=](int, QProcess::ExitStatus) {
                setItemStopped(item);
                runningScripts.remove(path);
                proc->deleteLater();
            });
    proc->start("C:/Program Files/AutoHotkey/AutoHotkey.exe", {path});
    if (!proc->waitForStarted(1000)) {
        item->setText(1, "Failed");
        item->setIcon(0, redIcon);
        proc->deleteLater();
        return;
    }
    setItemRunning(item);
    runningScripts[path] = proc;
}

void MainWindow::stopScriptForPath(const QString& path) {
    if (!runningScripts.contains(path)) return;
    QTreeWidgetItem* item = findScriptItem(scriptTree, path);
    if (item) setItemStopped(item);
    runningScripts[path]->kill();
    runningScripts[path]->deleteLater();
    runningScripts.remove(path);
}

void MainWindow::loadCache() {
    QFile f(cacheFilePath);
    if (!f.exists()) return;
    if (f.open(QIODevice::ReadOnly)) {
        QByteArray data = f.readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isArray()) {
            scriptTree->clear();
            QJsonArray arr = doc.array();
            for (auto val : arr) {
                QJsonObject obj = val.toObject();
                handleScriptFound(obj["folder"].toString(), obj["path"].toString());
            }
            scriptTree->expandAll();
            for (int i = 0; i < scriptTree->columnCount(); ++i)
                scriptTree->resizeColumnToContents(i);

            statusLabel->setText("Loaded cached scripts. Click Scan to refresh.");
        }
    }
}

void MainWindow::saveCache() {
    QJsonArray arr;
    for (int i = 0; i < scriptTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* folderItem = scriptTree->topLevelItem(i);
        for (int j = 0; j < folderItem->childCount(); ++j) {
            QTreeWidgetItem* scriptItem = folderItem->child(j);
            QString path = scriptItem->data(0, Qt::UserRole).toString();
            QJsonObject obj;
            obj["folder"] = folderItem->text(0);
            obj["path"] = path;
            arr.append(obj);
        }
    }
    QJsonDocument doc(arr);
    QFile f(cacheFilePath);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(doc.toJson());
    }
}

void MainWindow::startScan() {
    existingPaths.clear();
    for (int i = 0; i < scriptTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* folderItem = scriptTree->topLevelItem(i);
        for (int j = 0; j < folderItem->childCount(); ++j) {
            existingPaths.insert(folderItem->child(j)->data(0, Qt::UserRole).toString());
        }
    }

    statusLabel->setText("Scanning C:/ (this may take a while)...");
    scanButton->setEnabled(false);
    progressBar->setValue(0);
    progressBar->show();

    QFuture<void> future = QtConcurrent::run([this](){
        scanForScripts();
    });
    watcher->setFuture(future);
}

void MainWindow::scanForScripts() {
    QStringList skipFolders = {
        "C:/Windows",
        "C:/Program Files",
        "C:/Program Files (x86)",
        "C:/ProgramData",
        "C:/$Recycle.Bin",
        "C:/Users/All Users",
        "C:/System Volume Information"
    };

    QStringList allDirs;
    QDirIterator dirIter("C:/", QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (dirIter.hasNext()) {
        QString dir = dirIter.next();
        bool skip = false;
        for (const QString& folder : skipFolders) {
            if (dir.startsWith(folder, Qt::CaseInsensitive)) {
                skip = true;
                break;
            }
        }
        if (!skip) allDirs.append(dir);
    }
    int totalDirs = allDirs.size();
    int currentDir = 0;

    for (const QString& dirPath : allDirs) {
        QDir dir(dirPath);
        QStringList files = dir.entryList(QStringList() << "*.ahk", QDir::Files);
        for (const QString& file : files) {
            QString path = dirPath + "/" + file;
            if (existingPaths.contains(path)) continue;
            QString folderName = QFileInfo(dirPath).fileName();
            if (folderName.isEmpty()) folderName = dirPath;
            emit scriptFound(folderName, path);
        }
        currentDir++;
        if (totalDirs > 0)
            emit progressUpdate(currentDir, totalDirs);
    }
}

void MainWindow::updateProgress(int current, int total) {
    int pct = (int)((current * 100.0) / total);
    progressBar->setValue(pct);
}

void MainWindow::setItemRunning(QTreeWidgetItem* item) {
    if (!item) return;
    item->setText(1, "Running");
    item->setIcon(0, greenIcon);
    // Use subtle background colors on both first two columns to be visible
    item->setBackground(0, QBrush(QColor("#1f3d2b"))); // subtle green
    item->setBackground(1, QBrush(QColor("#1f3d2b")));
}

void MainWindow::setItemStopped(QTreeWidgetItem* item) {
    if (!item) return;
    item->setText(1, "Not Running");
    item->setIcon(0, redIcon);
    item->setBackground(0, QBrush(QColor("#3d1f1f"))); // subtle red
    item->setBackground(1, QBrush(QColor("#3d1f1f")));
}

void MainWindow::handleScriptFound(const QString& folder, const QString& scriptPath) {
    QTreeWidgetItem* folderItem = nullptr;
    for (int i=0; i<scriptTree->topLevelItemCount(); ++i) {
        if (scriptTree->topLevelItem(i)->text(0)==folder) {
            folderItem=scriptTree->topLevelItem(i);
            break;
        }
    }
    if (!folderItem) {
        folderItem = new QTreeWidgetItem(scriptTree, QStringList() << folder);
        folderItem->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
        scriptTree->expandItem(folderItem);
    }
    scriptTree->expandItem(folderItem);

    QFileInfo fi(scriptPath);
    auto* scriptItem=new QTreeWidgetItem(folderItem, QStringList()<<fi.fileName()<<"Not Running");
    scriptItem->setData(0,Qt::UserRole,scriptPath);
    scriptItem->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
    scriptItem->setToolTip(0,scriptPath);
    setItemStopped(scriptItem); // initial state stopped

    auto* runBtn=new QPushButton("Run");
    auto* stopBtn=new QPushButton("Stop");
    runBtn->setProperty("scriptPath",scriptPath);
    stopBtn->setProperty("scriptPath",scriptPath);
    scriptTree->setItemWidget(scriptItem,2,runBtn);
    scriptTree->setItemWidget(scriptItem,3,stopBtn);
    connect(runBtn,&QPushButton::clicked,this,&MainWindow::runScript);
    connect(stopBtn,&QPushButton::clicked,this,&MainWindow::stopScript);
}

void MainWindow::scanFinished() {
    scriptTree->expandAll();
    for (int i = 0; i < scriptTree->columnCount(); ++i) {
        scriptTree->resizeColumnToContents(i);
    }
    scanButton->setEnabled(true);
    progressBar->hide();
    statusLabel->setText("Scan finished. Found " + QString::number(scriptTree->topLevelItemCount()) + " folders.");
    saveCache();
}

void MainWindow::runScript() {
    auto* btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    QString path = btn->property("scriptPath").toString();

    QTreeWidgetItem* item = findScriptItem(scriptTree, path);
    if (!item || runningScripts.contains(path)) return;

    auto* proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [=](int, QProcess::ExitStatus) {
                setItemStopped(item);
                runningScripts.remove(path);
                proc->deleteLater();
            });
    proc->start("C:/Program Files/AutoHotkey/AutoHotkey.exe", {path});
    if (!proc->waitForStarted(1000)) {
        item->setText(1, "Failed");
        item->setIcon(0, redIcon);
        proc->deleteLater();
        return;
    }
    setItemRunning(item);
    runningScripts[path] = proc;
}

void MainWindow::stopScript() {
    auto* btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    QString path = btn->property("scriptPath").toString();

    QTreeWidgetItem* item = findScriptItem(scriptTree, path);
    if (!item) return;

    if (runningScripts.contains(path)) {
        runningScripts[path]->kill();
        runningScripts[path]->deleteLater();
        runningScripts.remove(path);
        setItemStopped(item);
    }
}
