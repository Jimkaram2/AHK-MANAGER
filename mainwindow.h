#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTreeWidget>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QFutureWatcher>
#include <QProcess>
#include <QLineEdit>
#include <QMenu>
#include <QSet>

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

signals:
    void scriptFound(const QString& folder, const QString& scriptPath);
    void progressUpdate(int current, int total);

private slots:
    void startScan();
    void scanFinished();
    void handleScriptFound(const QString& folder, const QString& scriptPath);
    void updateProgress(int current, int total);
    void runScript();
    void stopScript();
    void filterTree(const QString& text);
    void showContextMenu(const QPoint& pos);

private:
    void scanForScripts();
    void loadCache();
    void saveCache();
    void runScriptForPath(const QString& path);
    void stopScriptForPath(const QString& path);

    QTreeWidget* scriptTree;
    QPushButton* scanButton;
    QLabel* statusLabel;
    QProgressBar* progressBar;
    QFutureWatcher<void>* watcher;
    QLineEdit* filterEdit;

    QString cacheFilePath;

    QHash<QString, QProcess*> runningScripts;
    QSet<QString> existingPaths;

    QIcon redIcon;
    QIcon greenIcon;

    // helpers for coloring
    void setItemRunning(QTreeWidgetItem* item);
    void setItemStopped(QTreeWidgetItem* item);
};

#endif // MAINWINDOW_H
