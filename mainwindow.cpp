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

static const QString k_programName = "atprogram.exe";

static const QStringList k_searchDirs = QStringList()
        << "./atbackend/"
        << "C:/Program Files (x86)/RuggedScience/atprogram/atbackend/"
        << "C:/Program Files (x86)/Atmel/Studio/7.0/atbackend/";

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
    m_process(new QProcess(this))
{
    ui->setupUi(this);
    ui->programmerComboBox->addItems(k_programmers);
    ui->interfaceComboBox->addItems(k_interfaces);

    //Used to "selectAll" on focus in event
    ui->lowFuseEdit->installEventFilter(this);
    ui->highFuseEdit->installEventFilter(this);
    ui->extFuseEdit->installEventFilter(this);

    bool found = false;
    QFileInfo atprogram;
    foreach (const QString dir, k_searchDirs)
    {
        atprogram = QFileInfo(dir + k_programName);
        if (atprogram.exists() && atprogram.isFile())
        {
            ui->commandOutput->append(QString("Using atbackend from %1").arg(atprogram.canonicalPath()));
            found = true;
            break;
        }
    }

    if (found)
    {

        QStringList targetList;
        QString packsDir = atprogram.canonicalPath() + "/../packs";
        QDirIterator it(packsDir, QStringList() << "package.content", QDir::NoFilter, QDirIterator::Subdirectories);
        while (it.hasNext())
        {
            // QXmlStreamReader didn't like the ASCII encoding used in the package.content files
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

        m_process->setProgram(atprogram.canonicalFilePath());
        m_process->setWorkingDirectory(atprogram.canonicalPath());
        m_process->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_process, &QProcess::readyRead, this, &MainWindow::on_readyRead);
        connect(m_process, &QProcess::errorOccurred, this, &MainWindow::on_error);
        connect(m_process, QOverload<int>::of(&QProcess::finished), this, &MainWindow::on_processFinished);
    }

    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       "RuggedScience", "atprogram-gui");
    this->restoreGeometry(settings.value("geometry").toByteArray());
    m_showPfileWarning = settings.value("showPfileWarning", true).toBool();
    ui->showDebug->setChecked(settings.value("showDebug", true).toBool());
    ui->programmerComboBox->setCurrentText(settings.value("programmer", "atmelice").toString());
    ui->interfaceComboBox->setCurrentText(settings.value("interface", "ISP").toString());
    ui->targetComboBox->setCurrentText(settings.value("target", "Atmega32U4").toString());

    ui->commandOutput->setVisible(ui->showDebug->isChecked());

    ui->commandOutput->append(QString("Using program %1").arg(m_process->program()));
    ui->commandOutput->append(QString("Using working directory %1").arg(m_process->workingDirectory()));
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

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == ui->lowFuseEdit || obj == ui->highFuseEdit || obj == ui->extFuseEdit)
    {
        if (event->type() == QEvent::FocusIn)
        {
            QLineEdit *le = qobject_cast<QLineEdit *>(obj);
            QTimer::singleShot(0, le, SLOT(selectAll()));
        }
    }

    return QObject::eventFilter(obj, event);
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
    QString fileName = QFileDialog::getOpenFileName(this, "Open File",
                                                    QString(), "HEX (*.hex)");
    if (!fileName.isEmpty())
        ui->flashEdit->setText(fileName);
}

void MainWindow::on_eepromBrowse_clicked()
{
    QString fileName = QFileDialog::getOpenFileName(this, "Open File",
                                                    QString(), "EEPROM (*.eep)");
    if (!fileName.isEmpty())
        ui->eepromEdit->setText(fileName);
}

void MainWindow::on_pfileBrowse_clicked()
{
    QString fileName = QFileDialog::getOpenFileName(this, "Open File",
                                                    QString(), "ELF (*.elf)");
    if (!fileName.isEmpty())
    {
        ui->pfileEdit->setText(fileName);
        on_pfileEdit_editingFinished();
    }
}

void MainWindow::on_pfileEdit_editingFinished()
{
    QStringList sections;
    getElfSections(ui->pfileEdit->text(), sections);
    ui->pfileFuses->setChecked(sections.contains(".fuse"));
    ui->pfileFuses->setEnabled(sections.contains(".fuse"));
    ui->pfileFlash->setChecked(sections.contains(".text"));
    ui->pfileFlash->setEnabled(sections.contains(".text"));
    ui->pfileEeprom->setChecked(sections.contains(".eeprom"));
    ui->pfileEeprom->setEnabled(sections.contains(".eeprom"));
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
                     << "program" << "--verify" << "-c"
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
        QFileInfo fileInfo(ui->pfileEdit->text());
        if (fileInfo.exists() && fileInfo.isFile())
        {
            bool warn = true;
            QStringList args;
            args << "-v"
                 << "-t" << programmer
                 << "-i" << interface
                 << "-d" << target
                 << "program" << "--verify" << "-c"
                 << "-f" << ui->pfileEdit->text();

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

            bool proceed = true;
            if (warn)
            {
                if (m_showPfileWarning)
                {
                    QCheckBox *cb = new QCheckBox("Don't show again");
                    QMessageBox msg(this);
                    msg.setCheckBox(cb);
                    msg.setText("No memory sections have been selected.\n"
                                "This will flash the full contents of the production file.\n"
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
    qApp->processEvents(); //Process the hide event before we adjust size.
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
    ui->commandOutput->append(k_programName + " " + args.join(" ") + "\n");
    QScrollBar *sb = ui->commandOutput->verticalScrollBar();
    sb->setValue(sb->maximum());
    m_process->setArguments(args);
    m_process->start();
}

bool MainWindow::getElfSections(const QString &fileName, QStringList &sections)
{
    QFile file(fileName);
    if (!file.open(QFile::ReadOnly))
    {
        qDebug()<<file.errorString();
        return false;
    }

    QByteArray ba;
    ba.resize(0x3E);
    qint64 len = file.read(ba.data(), 0x34);
    if (len < 0)
    {
        qDebug()<<file.errorString();
        return false;
    }

    quint8 numOfSections = static_cast<quint8>(ba.at(0x30));
    quint8 sizeOfSection = static_cast<quint8>(ba.at(0x2E));
    quint8 sectionTableOffset = static_cast<quint8>(ba.at(0x20));
    int sectionTableSize = numOfSections * sizeOfSection;

    ba.clear();
    ba.resize(sectionTableSize);
    file.seek(sectionTableOffset);
    if (file.read(ba.data(), sectionTableSize) < 0)
    {
        qDebug()<<file.errorString();
        return false;
    }

    int stringTableSize = 0;
    int stringTableOffset = 0;
    for (int offset = 0; offset < sectionTableSize; offset += sizeOfSection)
    {
        if (ba.at(offset + 0x4) == 0x03)
        {
            stringTableOffset = static_cast<quint8>(ba.at(offset + 0x10));
            stringTableOffset |= static_cast<quint32>(ba.at(offset + 0x11) << 8);
            stringTableOffset |= static_cast<quint32>(ba.at(offset + 0x12) << 16);

            stringTableSize = static_cast<quint8>(ba.at(offset + 0x14));
            stringTableSize |= static_cast<quint32>(ba.at(offset + 0x15) << 8);
            stringTableSize |= static_cast<quint32>(ba.at(offset + 0x16) << 16);
            break;
        }
    }

    ba.clear();
    ba.resize(stringTableSize);
    file.seek(stringTableOffset + 1);
    if (file.read(ba.data(), stringTableSize) < 0)
    {
        qDebug()<<file.errorString();
        return false;
    }

    foreach (const QByteArray &ba, ba.split(0))
    {
        sections.append(QString(ba));
    }

    return true;
}
