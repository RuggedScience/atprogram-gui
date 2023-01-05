#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "tinyxml2.h"
#include "crc32.h"

#include <QTimer>
#include <QDebug>
#include <QSettings>
#include <QScrollBar>
#include <QCompleter>
#include <QFileDialog>
#include <QMessageBox>
#include <QDirIterator>
#include <QMimeData>

static const QString k_programName = "atprogram.exe";

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

    // Get relative application path
    QDir appPath(QCoreApplication::applicationDirPath());

    //TODO: Create installer https://www.youtube.com/watch?v=1pKMcwJZay4
    //TODO: Search for microchip/atmel Studio in all drivers "C:/Program Files (x86)/Atmel/Studio/7.0/atbackend/" // Prioritize atbackend from Atmel Studio

    // Search for the atprogram.exe in installation path
    QString appDir = appPath.cleanPath(appPath.absoluteFilePath("../atprogram/atbackend/" + k_programName));

    QFileInfo atprogram = QFileInfo(appDir);

    if (atprogram.isFile())
    {
        ui->commandOutput->append(QString("Using atbackend from %1").arg(atprogram.canonicalPath()));
        found = true;
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
        connect(m_process, &QProcess::finished, this, &MainWindow::on_processFinished);
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

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    event->acceptProposedAction();
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    event->acceptProposedAction();
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent *event)
{
    event->accept();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QMimeData *mimeData = event->mimeData();

    if (mimeData->hasUrls())
    {
        foreach (const QUrl& url, mimeData->urls())
        {
            QString path = url.path().mid(1);
            QFileInfo info(path);
            QString suffix = info.suffix();

            if (suffix == "elf")
            {                
                // On first drop config application code
                if (ui->pAppEdit->text().isEmpty())
                {
                    ui->pAppEdit->setText(path);
                    on_pfileEdit_editingFinished(path);
                }
                // On second drop config bootloader code
                else
                {
                    ui->pBootEdit->setText(path);
                    on_pfileEdit_editingFinished(path);
                }
            }
            else if (suffix == "hex")
            {
                ui->flashGroup->setChecked(true);
                ui->flashEdit->setText(path);
            }
            else if (suffix == "eep")
            {
                ui->eepromGroup->setChecked(true);
                ui->eepromEdit->setText(path);
            }
        }
    }
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

void MainWindow::on_pBootBrowse_clicked()
{
    QString fileName = QFileDialog::getOpenFileName(this, "Open File",
                                                    QString(), "ELF (*.elf)");
    if (!fileName.isEmpty())
    {
        ui->pBootEdit->setText(fileName);
        on_pfileEdit_editingFinished(fileName);
    }
}

void MainWindow::on_pAppBrowse_clicked()
{
    QString fileName = QFileDialog::getOpenFileName(this, "Open File",
                                                    QString(), "ELF (*.elf)");
    if (!fileName.isEmpty())
    {
        ui->pAppEdit->setText(fileName);
        on_pfileEdit_editingFinished(fileName);
    }
}

void MainWindow::on_pfileEdit_editingFinished(const QString& filePath)
{
    if (QFileInfo(filePath).isFile())
    {
        QStringList sections;
        getElfSections(filePath, sections);

        // In case bootloader or app have fuse section, enable it
        if ((ui->pfileFuses->isChecked() == false) && (sections.contains(".fuse")))
        {
            ui->pfileFuses->setChecked(true);
            ui->pfileFuses->setEnabled(true);
        }

        // In case bootloader or app have text section, enable it
        if ((ui->pfileFlash->isChecked() == false) && (sections.contains(".text")))
        {
            ui->pfileFlash->setChecked(true);
            ui->pfileFlash->setEnabled(true);
        }

        // In case bootloader or app have eeprom section, enable it
        if ((ui->pfileEeprom->isChecked() == false) && (sections.contains(".eeprom")))
        {
            ui->pfileEeprom->setChecked(true);
            ui->pfileEeprom->setEnabled(true);
        }

        // In case bootloader or app have lock section, enable it
        if ((ui->plockDevice->isChecked() == false) && (sections.contains(".lock")))
        {
            ui->plockDevice->setChecked(true);
            ui->plockDevice->setEnabled(true);
        }

        QString crc32 = getCRC32(filePath);
        ui->statusBar->showMessage(QString("CRC32: %1").arg(crc32));
    }
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
        QFileInfo appFileInfo  (ui->pAppEdit ->text());
        QFileInfo bootFileInfo (ui->pBootEdit->text());

        if ((appFileInfo.exists()  && appFileInfo.isFile()) ||
            (bootFileInfo.exists() && bootFileInfo.isFile()))
        {
            // Do not throw warning in case exist boot and app code
            bool warn    = true,
                 bootApp = (bootFileInfo.isFile() && appFileInfo.isFile()),
                 proceed = true;

            QStringList args;

            args << "-v"
                 << "-t" << programmer
                 << "-i" << interface
                 << "-d" << target
                 //<< "-c"              // this command does not work when the devie is locked
                 << "chiperase"         // to full chiperase instead
                 << "program";

            if (bootApp)
            {
                // Flash bootloader at the end since by default, the bootloader also sets the fuses and lock bits
                // program the full contents of both production files but only verify application code, since boot code will
                // lock the device and prevent verification code
                args << "--verify"
                     << "-f" << ui->pAppEdit->text()
                     << "program"
                     << "-f" << ui->pBootEdit->text();
            }
            // Otherwise only flash the given file
            else
            {
                args << "--verify"
                     << "-f" << ((bootFileInfo.isFile()) ? (ui->pBootEdit->text()) : (ui->pAppEdit->text()));

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

                if (ui->plockDevice->isChecked())
                {
                    //args << "-lb --values A33A3AA3";
                    args << "-lb";
                    warn = false;
                }
            }

            if ((warn == true) && (bootApp == false))
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

                    if (msg.exec() != QMessageBox::Yes) proceed = false;

                    m_showPfileWarning = !cb->isChecked();
                }
            }

            if (proceed) m_commandQueue.enqueue(args);
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
    //FIXME: this is not working for AVR elf files
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

    int stringTableSize   = 0;
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
