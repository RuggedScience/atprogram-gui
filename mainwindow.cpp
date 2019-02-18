#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "tinyxml2.h"

#include <QTimer>
#include <QDebug>
#include <QSettings>
#include <QScrollBar>
#include <QCompleter>
#include <QFileDialog>
#include <QMessageBox>
#include <QDirIterator>
#include <QXmlStreamReader>

static const QString k_program = "./atbackend/atprogram.exe";

static const QStringList k_programmers = QStringList()
        << "avrdragon"
        << "avrispmk2"
        << "avrone"
        << "jtagice3"
        << "jtagicemkii"
        << "qt600"
        << "stk500"
        << "stk600"
        << "samice"
        << "edbg"
        << "medbg"
        << "atmelice"
        << "powerdebugger"
        << "megadfu"
        << "flip";

static const QStringList k_interfaces = QStringList()
        << "aWire"
        << "debugWIRE"
        << "HVPP"
        << "HVSP"
        << "ISP"
        << "JTAG"
        << "PDI"
        << "UPDI"
        << "TPI"
        << "SWD";

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    m_running(false),
    m_showPfileWarning(true),
    m_process(nullptr)
{
    ui->setupUi(this);
    ui->programmerComboBox->addItems(k_programmers);
    ui->interfaceComboBox->addItems(k_interfaces);

    QStringList targetList;
    QDirIterator it("./packs", QStringList() << "package.content", QDir::NoFilter, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        using namespace tinyxml2;
        XMLDocument doc;
        std::string fileName = it.next().toStdString();
        if (doc.LoadFile(fileName.c_str()) == XML_SUCCESS)
        {
            XMLElement *e = nullptr;
            if ((e = doc.FirstChildElement("package")))
            {
                if ((e = e->FirstChildElement("content")))
                {
                    for (e = e->FirstChildElement("resources"); e; e = e->NextSiblingElement("resources"))
                    {
                        QString attr(e->Attribute("target"));
                        if (!attr.isEmpty())
                        {
                            targetList.append(attr);
                            ui->targetComboBox->addItem(attr);
                        }
                    }
                }
            }
        }
    }

    QCompleter *completer = new QCompleter(targetList, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    ui->targetComboBox->setCompleter(completer);

    m_process = new QProcess(this);
    m_process->setProgram(k_program);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyRead, this, &MainWindow::on_readyRead);
    connect(m_process, &QProcess::errorOccurred, this, &MainWindow::on_error);
    connect(m_process, QOverload<int>::of(&QProcess::finished), this, &MainWindow::on_processFinished);

    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       "RuggedScience", "atprogram-gui");
    this->restoreGeometry(settings.value("geometry").toByteArray());
    m_showPfileWarning = settings.value("showPfileWarning", true).toBool();
    ui->showDebug->setChecked(settings.value("showDebug", true).toBool());
    ui->programmerComboBox->setCurrentText(settings.value("programmer", "atmelice").toString());
    ui->interfaceComboBox->setCurrentText(settings.value("interface", "ISP").toString());
    ui->targetComboBox->setCurrentText(settings.value("target", "Atmega32U4").toString());

    ui->commandOutput->setVisible(ui->showDebug->isChecked());
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent *)
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       "RuggedScience", "atprogram-gui");
    settings.setValue("geometry", this->saveGeometry());
    settings.setValue("showPfileWarning", m_showPfileWarning);
    settings.setValue("showDebug", ui->showDebug->isChecked());
    settings.setValue("programmer", ui->programmerComboBox->currentText());
    settings.setValue("interface", ui->interfaceComboBox->currentText());
    settings.setValue("target", ui->targetComboBox->currentText());

}

void MainWindow::on_readyRead()
{
    ui->commandOutput->insertPlainText(m_process->readAll());
    QScrollBar *sb = ui->commandOutput->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void MainWindow::on_error(QProcess::ProcessError)
{
    ui->commandOutput->append(m_process->errorString());
    setRunning(false);
}

void MainWindow::on_processFinished(int exitCode)
{
    if (exitCode != 0)
    {
        ui->progressBar->setFormat("FAILED!");
        ui->progressBar->setStyleSheet("QProgressBar::chunk{ background-color: red;}");
        ui->statusBar->showMessage("Failed to flash unit! Check debug output for more info...");
        setRunning(false);
        ui->progressBar->setValue(1);
    }
    else if (m_commandQueue.isEmpty())
    {
        ui->progressBar->setFormat("Ready");
        ui->progressBar->setStyleSheet("");
        ui->statusBar->showMessage("Unit sucessfully flashed");
        setRunning(false);
        ui->progressBar->setValue(1);
    }
    else
        startProcess(m_commandQueue.dequeue());
}

void MainWindow::on_flashBrowse_clicked()
{
    QFileDialog dlg(this);
    dlg.setFileMode(QFileDialog::ExistingFile);
    dlg.setNameFilter("*.hex");
    if (dlg.exec())
        ui->flashEdit->setText(dlg.selectedFiles().at(0));
}

void MainWindow::on_eepromBrowse_clicked()
{
    QFileDialog dlg(this);
    dlg.setFileMode(QFileDialog::ExistingFile);
    dlg.setNameFilter("*.eep");
    if (dlg.exec())
        ui->eepromEdit->setText(dlg.selectedFiles().at(0));
}

void MainWindow::on_pfileBrowse_clicked()
{
    QFileDialog dlg(this);
    dlg.setFileMode(QFileDialog::ExistingFile);
    dlg.setNameFilter("*.elf");
    if (dlg.exec())
        ui->pfileEdit->setText(dlg.selectedFiles().at(0));
}

void MainWindow::on_startButton_clicked()
{
    if (m_running) return;

    m_commandQueue.clear();
    ui->commandOutput->clear();
    ui->progressBar->setFormat("Ready");
    ui->progressBar->setStyleSheet("");
    ui->startButton->setEnabled(false); //Disable start button so they don't spam click it...
    QString programmer = ui->programmerComboBox->currentText();
    QString interface = ui->interfaceComboBox->currentText();
    QString target = ui->targetComboBox->currentText().toLower();

    if (ui->tabWidget->currentWidget() == ui->memTab)
    {
        if (ui->fuseGroup->isChecked())
        {
            QString fuses;
            fuses.append(ui->lowFuseEdit->text());
            fuses.append(ui->highFuseEdit->text());
            fuses.append(ui->extFuseEdit->text());

            if (fuses.size() == 6)
            {
                QStringList args;
                args << "-v"
                     << "-t" << programmer
                     << "-i" << interface
                     << "-d" << target
                     << "write"
                     << "-fs" << "--values" << fuses;
                m_commandQueue.enqueue(args);
            }
            else
            {
                m_commandQueue.clear();
                ui->statusBar->showMessage("All fuses must be set!");
            }
        }

        if (ui->flashGroup->isChecked())
        {
            QFileInfo file(ui->flashEdit->text());
            if (file.exists() && file.isFile())
            {
                QStringList args;
                args << "-v"
                     << "-t" << programmer
                     << "-i" << interface
                     << "-d" << target
                     << "program" << "--verify" << "-c"
                     << "-fl" << "-f" << ui->flashEdit->text();
                m_commandQueue.enqueue(args);
            }
            else
            {
                m_commandQueue.clear();
                ui->statusBar->showMessage("Flash file does not exist!");
            }
        }

        if (ui->eepromGroup->isChecked())
        {
            QFileInfo file(ui->eepromEdit->text());
            if (file.exists() && file.isFile())
            {
                QStringList args;
                args << "-v"
                     << "-t" << programmer
                     << "-i" << interface
                     << "-d" << target
                     << "program" << "--verify"
                     << "--format" << "hex"
                     << "-ee" << "-f" << ui->eepromEdit->text();
                m_commandQueue.enqueue(args);
            }
            else
            {
                m_commandQueue.clear();
                ui->statusBar->showMessage("EEPROM file does not exist!");
            }
        }
    }
    else if (ui->tabWidget->currentWidget() == ui->pfileTab)
    {
        QFileInfo file(ui->pfileEdit->text());
        if (file.exists() && file.isFile())
        {
            bool warn = true;
            QStringList args;
            args << "-v"
                 << "-t" << programmer
                 << "-i" << interface
                 << "-d" << target
                 << "program" << "--verify";
            if (ui->pfileFuses->isChecked())
            {
                args << "-fs";
                warn = false;
            }

            if (ui->pfileFlash->isChecked())
            {
                args << "-fl";
                warn = false;
            }

            if (ui->pfileEeprom->isChecked())
            {
                args << "-ee";
                warn = false;
            }

            args << "-f" << ui->pfileEdit->text();

            bool proceed = true;
            if (warn)
            {
                if (m_showPfileWarning)
                {
                    QCheckBox *cb = new QCheckBox("Don't show again");
                    QMessageBox msg(this);
                    msg.setCheckBox(cb);
                    msg.setText("Leaving all boxes unchecked will flash the full contents of the production file.\n"
                                "Do you want to continue?");
                    msg.setIcon(QMessageBox::Question);
                    msg.addButton(QMessageBox::Yes);
                    msg.addButton(QMessageBox::No);
                    msg.setDefaultButton(QMessageBox::No);

                    if (msg.exec() != QMessageBox::Yes)
                        proceed = false;

                    m_showPfileWarning = !cb->isChecked();
                }
            }

            if (proceed)
                m_commandQueue.enqueue(args);
        }
        else
        {
            m_commandQueue.clear();
            ui->statusBar->showMessage("Production file does not exist!");
        }
    }

    if (!m_commandQueue.isEmpty())
    {
        setRunning(true);
        ui->fuseGroup->setChecked(false);
        ui->flashGroup->setChecked(false);
        ui->eepromGroup->setChecked(false);
        startProcess(m_commandQueue.dequeue());
        ui->progressBar->setFormat("Loading...");
        ui->statusBar->clearMessage();
    }
    else
        ui->startButton->setEnabled(true); // If there is nothing to start we need to re-enable this
}

void MainWindow::on_showDebug_toggled(bool checked)
{
    ui->commandOutput->setVisible(checked);
    qApp->processEvents();
    this->adjustSize();
}

void MainWindow::setRunning(bool running)
{
    m_running = running;
    ui->fuseGroup->setDisabled(running);
    ui->flashGroup->setDisabled(running);
    ui->eepromGroup->setDisabled(running);
    ui->startButton->setDisabled(running);

    if (running) ui->progressBar->setMaximum(0);
    else ui->progressBar->setMaximum(1);
}

void MainWindow::startProcess(const QStringList& args)
{
    ui->commandOutput->append(k_program + " " + args.join(" "));
    QScrollBar *sb = ui->commandOutput->verticalScrollBar();
    sb->setValue(sb->maximum());
    m_process->setArguments(args);
    m_process->start();
}
