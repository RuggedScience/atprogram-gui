#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QQueue>
#include <QProcess>
#include <QDropEvent>
#include <QMainWindow>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void on_readyRead();
    void on_error(QProcess::ProcessError error);
    void on_processFinished(int exitCode);
    void on_flashBrowse_clicked();
    void on_eepromBrowse_clicked();
    void on_pfileEdit_editingFinished(const QString& filePath);
    void on_startButton_clicked();
    void on_showDebug_toggled(bool checked);
    void on_pAppBrowse_clicked();
    void on_pBootBrowse_clicked();

private:
    Ui::MainWindow *ui;
    bool m_running;
    bool m_showPfileWarning;
    QProcess *m_process;
    QQueue<QStringList> m_commandQueue;

    void setRunning(bool running);
    void startProcess(const QStringList& args);
    bool getElfSections(const QString& fileName, QStringList &sections);
};

#endif // MAINWINDOW_H
