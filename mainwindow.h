#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "atpackmanager.h"
#include "hexspinbox.h"

#include <QQueue>
#include <QProcess>
#include <QDropEvent>
#include <QMainWindow>
#include <QSpinBox>
#include <QLabel>

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
    void on_processFinished(int exitCode, QProcess::ExitStatus);
    void on_flashBrowse_btn_clicked();
    void on_eepromBrowse_btn_clicked();
    void on_pfileBrowse_clicked();
    void on_pfile_le_editingFinished();
    void on_memProgram_btn_clicked();
    void on_fuseProgram_btn_clicked();
    void on_pfileProgram_btn_clicked();
    void on_userSigsBrowse_btn_clicked();

private:
    Ui::MainWindow *ui;
    bool m_running;
    bool m_showPfileWarning;
    QByteArray m_lastDialogState;
    QProcess *m_process;
    AtPackManager m_packManager;
    QQueue<QStringList> m_commandQueue;
    QVector<HexSpinBox *> m_fuseEditList;
    QVector<QLabel *> m_fuseLabelList;

    void setRunning(bool running);
    void startProcess(const QStringList& args);
    bool getElfSections(const QString& fileName, QStringList &sections);
    void handleTargetChanged(const QString& newTarget);

};

#endif // MAINWINDOW_H
