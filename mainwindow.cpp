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

static const QStringList k_searchDirs = QStringList()
        << "C:/Program Files (x86)/Atmel/Studio/7.0/atbackend/" // Prioritize atbackend from Atmel Studio
        << "C:/Program Files (x86)/RuggedScience/atprogram/atbackend/";

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
    ui->targetComboBox->installEventFilter(this);

    // Hide user signatures group since it hasn't been implemented yet
    ui->userSigsGroup->setHidden(true);
    // Hide lock bits check box since it hasn't been implemented yet
    ui->pfileLockBits_cb->setHidden(true);

    connect(ui->targetComboBox, QOverload<const QString &>::of(&QComboBox::activated), this, &MainWindow::handleTargetChanged);

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
        QString packsDir = atprogram.canonicalPath() + "/../packs";
        m_packManager.setPath(packsDir);
        if (m_packManager.isValid())
        {
            QStringList targetList = m_packManager.getAllTargets();
            ui->targetComboBox->addItems(targetList);
        QCompleter *completer = new QCompleter(targetList, this);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        ui->targetComboBox->setCompleter(completer);
        }

        m_process->setProgram(atprogram.canonicalFilePath());
        m_process->setWorkingDirectory(atprogram.canonicalPath());
        m_process->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_process, &QProcess::readyRead, this, &MainWindow::on_readyRead);
        connect(m_process, &QProcess::errorOccurred, this, &MainWindow::on_error);
        connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &MainWindow::on_processFinished);
    }

    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       "RuggedScience", "atprogram-gui");
    m_lastDialogState = settings.value("lastDialogState").toByteArray();
    m_showPfileWarning = settings.value("showPfileWarning", true).toBool();
    ui->showDebug->setChecked(settings.value("showDebug", true).toBool());
    ui->programmerComboBox->setCurrentText(settings.value("programmer").toString());
    ui->interfaceComboBox->setCurrentText(settings.value("interface").toString());
    ui->targetComboBox->setCurrentText(settings.value("target").toString());
    m_geometry = settings.value("geometry").toByteArray();
    m_debugGeometry = settings.value("debugGeometry").toByteArray();

    ui->commandOutput->setVisible(ui->showDebug->isChecked());
    ui->commandOutput->append(QString("Using program %1").arg(m_process->program()));
    ui->commandOutput->append(QString("Using working directory %1").arg(m_process->workingDirectory()));

    // Save restoreGeometry for last. Bug causes geometry to revert back to default after adjusting the above UI components.
    if (ui->showDebug->isChecked())
        this->restoreGeometry(m_debugGeometry);
    else
        this->restoreGeometry(m_geometry);

    // If there was no target in the settings
    // just set target to the first one found.
    if (ui->targetComboBox->currentText().isEmpty())
        ui->targetComboBox->setCurrentIndex(0);
    // Force update of fuse info
    handleTargetChanged(ui->targetComboBox->currentText());
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent *)
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       "RuggedScience", "atprogram-gui");
    settings.setValue("geometry", m_geometry);
    settings.setValue("debugGeometry", m_debugGeometry);
    settings.setValue("lastDialogState", m_lastDialogState);
    settings.setValue("showPfileWarning", m_showPfileWarning);
    settings.setValue("showDebug", ui->showDebug->isChecked());
    settings.setValue("programmer", ui->programmerComboBox->currentText());
    settings.setValue("interface", ui->interfaceComboBox->currentText());
    settings.setValue("target", ui->targetComboBox->currentText());
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (HexSpinBox *sb = qobject_cast<HexSpinBox *>(obj))
    {
        if (m_fuseEditList.contains(sb))
        {
            if (event->type() == QEvent::FocusIn)
                QTimer::singleShot(0, sb, SLOT(selectAll()));
        }
    }
    else if (obj == ui->targetComboBox)
    {
        if (event->type() == QEvent::FocusOut)
            handleTargetChanged(ui->targetComboBox->currentText());
    }

    return QObject::eventFilter(obj, event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    bool accept = false;
    const QMimeData *mimeData = event->mimeData();
    if (mimeData->hasUrls())
    {
        foreach (const QUrl& url, mimeData->urls())
        {
            QString path = url.path().mid(1);
            QFileInfo info(path);
            QString suffix = info.suffix();
            // If any of the files are elf, hex or eep we will accept the event.
            // Only the supported files will be handled Under MainWindow::dropEvent.
            if (suffix == "elf" || suffix == "hex" || suffix == "eep")
            {
                accept = true;
                break;
}
        }
    }

    if (accept) event->acceptProposedAction();
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
            if (ui->tabWidget->currentWidget() == ui->memTab)
            {
                if (suffix == "elf" || suffix == "hex")
                {
                    ui->flashGroup->setChecked(true);
                    ui->flash_le->setText(path);
                }
                else if (suffix == "eep")
                {
                    ui->eepromGroup->setChecked(true);
                    ui->eeprom_le->setText(path);
                }
                else if (suffix == "usersignatures")
                {
                    ui->userSigsGroup->setChecked(true);
                    ui->userSigs_le->setText(path);
                }
            }
            else if (ui->tabWidget->currentWidget() == ui->pfileTab)
            {
                if (suffix == "elf")
                {
                    ui->pfile_le->setText(path);
                    on_pfile_le_editingFinished();
                }
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

void MainWindow::on_processFinished(int exitCode, QProcess::ExitStatus)
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

void MainWindow::on_flashBrowse_btn_clicked()
{
    QFileDialog dlg(this);
    dlg.setFileMode(QFileDialog::ExistingFile);
    dlg.setNameFilter("HEX (*.hex)");
    dlg.restoreState(m_lastDialogState);
    if (dlg.exec())
    {
        QString fileName = dlg.selectedFiles().value(0);
    if (!fileName.isEmpty())
        ui->flash_le->setText(fileName);
}
    m_lastDialogState = dlg.saveState();
}

void MainWindow::on_eepromBrowse_btn_clicked()
{
    QFileDialog dlg(this);
    dlg.setFileMode(QFileDialog::ExistingFile);
    dlg.setNameFilter("EEPROM (*.eep)");
    dlg.restoreState(m_lastDialogState);
    if (dlg.exec())
    {
        QString fileName = dlg.selectedFiles().value(0);
        if (!fileName.isEmpty())
            ui->eeprom_le->setText(fileName);
    }
    m_lastDialogState = dlg.saveState();
}
void MainWindow::on_userSigsBrowse_btn_clicked()
{
    QFileDialog dlg(this);
    dlg.setFileMode(QFileDialog::ExistingFile);
    dlg.setNameFilter("User Signatures (*.usersignatures)");
    dlg.restoreState(m_lastDialogState);
    if (dlg.exec())
    {
        QString fileName = dlg.selectedFiles().value(0);
        if (!fileName.isEmpty())
            ui->userSigs_le->setText(fileName);
    }
    m_lastDialogState = dlg.saveState();
}

void MainWindow::on_pfileBrowse_clicked()
{
    QFileDialog dlg(this);
    dlg.setFileMode(QFileDialog::ExistingFile);
    dlg.setNameFilter("ELF (*.elf)");
    dlg.restoreState(m_lastDialogState);
    if (dlg.exec())
    {
        QString fileName = dlg.selectedFiles().value(0);
    if (!fileName.isEmpty())
    {
        ui->pfile_le->setText(fileName);
        on_pfile_le_editingFinished();
    }
}
    m_lastDialogState = dlg.saveState();
}

void MainWindow::on_pfile_le_editingFinished()
{
    if (QFileInfo(ui->pfile_le->text()).isFile())
    {
        QString fileName = ui->pfile_le->text();

        bool contains;
        QStringList sections;
        getElfSections(fileName, sections);

        if (ui->pfileFuses_cb->isVisible())
        {
            contains = sections.contains(".fuse");
            ui->pfileFuses_cb->setChecked(contains);
            ui->pfileFuses_cb->setEnabled(contains);
        }

        if (ui->pfileFlash_cb->isVisible())
        {
            contains = sections.contains(".text");
            ui->pfileFlash_cb->setChecked(contains);
            ui->pfileFlash_cb->setEnabled(contains);
        }

        if (ui->pfileEeprom_cb->isVisible())
        {
            contains = sections.contains(".eeprom");
            ui->pfileEeprom_cb->setChecked(contains);
            ui->pfileEeprom_cb->setEnabled(contains);
        }

        if (ui->pfileLockBits_cb->isVisible())
        {
            contains = sections.contains(".lockbits");
            ui->pfileLockBits_cb->setChecked(contains);
            ui->pfileLockBits_cb->setEnabled(contains);
        }

        if (ui->pfileUserSigs_cb->isVisible())
        {
            contains = sections.contains(".user_signatures");
            ui->pfileUserSigs_cb->setChecked(contains);
            ui->pfileUserSigs_cb->setEnabled(contains);
        }

        ui->statusBar->showMessage(QString("CRC32: %1").arg(getCRC32(fileName)));
    }
}

void MainWindow::on_memProgram_btn_clicked()
{
    if (m_running) return;

    bool error = false;
    m_commandQueue.clear();
    const QString programmer = ui->programmerComboBox->currentText();
    const QString interface = ui->interfaceComboBox->currentText();
    const QString target = ui->targetComboBox->currentText().toLower();

    if (ui->flashGroup->isChecked())
    {
        QFileInfo file(ui->flash_le->text());
        if (file.exists() && file.isFile())
        {
            QStringList args;
            args << "-v"
                 << "-t" << programmer
                 << "-i" << interface
                 << "-d" << target
                 << "program";

            if (ui->flashErase_cb->isChecked())
                args << "-e";

            if (ui->flashVerify_cb->isChecked())
                args << "--verify";

           args << "-c" << "-fl" << "-f"
                << ui->flash_le->text();

            m_commandQueue.enqueue(args);
            ui->flash_le->setStyleSheet("");
        }
        else
        {
            error = true;
            ui->flash_le->setStyleSheet("border: 1px solid red;");
            ui->statusBar->showMessage("Flash file does not exist!");
        }
    }

    if (ui->eepromGroup->isChecked())
    {
        QFileInfo file(ui->eeprom_le->text());
        if (file.exists() && file.isFile())
        {
            QStringList args;
            args << "-v"
                 << "-t" << programmer
                 << "-i" << interface
                 << "-d" << target
                 << "program";

            if (ui->eepromVerify_cb->isChecked())
                args << "--verify";

            args << "-c" << "--format" << "hex"
                 << "-ee" << "-f" << ui->eeprom_le->text();

            m_commandQueue.enqueue(args);
            ui->eeprom_le->setStyleSheet("");
        }
        else
        {
            error = true;
            ui->eeprom_le->setStyleSheet("border: 1px solid red;");
            ui->statusBar->showMessage("EEPROM file does not exist!");
        }
    }

    // TODO: Implement user signature functionality
    if (ui->userSigsGroup->isChecked())
    {

    }

    if (!error && !m_commandQueue.isEmpty())
    {
        setRunning(true);
        startProcess(m_commandQueue.dequeue());
    }
}

void MainWindow::on_fuseProgram_btn_clicked()
{
    if (m_running) return;

    m_commandQueue.clear();
    const QString programmer = ui->programmerComboBox->currentText();
    const QString interface = ui->interfaceComboBox->currentText();
    const QString target = ui->targetComboBox->currentText().toLower();

    QString fuses;
    foreach (HexSpinBox *sb, m_fuseEditList)
    {
        int val = sb->value();
        fuses.append(QString::number(val, 16));
    }

    if (!fuses.isEmpty())
    {
        QStringList args;
        args << "-v"
             << "-t" << programmer
             << "-i" << interface
             << "-d" << target
             << "write"
             << "-fs" << "--values" << fuses;
        setRunning(true);
        m_commandQueue.enqueue(args);
        startProcess(m_commandQueue.dequeue());
    }
}

void MainWindow::on_pfileProgram_btn_clicked()
{
    if (m_running) return;

    m_commandQueue.clear();
    const QFileInfo fileInfo(ui->pfile_le->text());
    const QString programmer = ui->programmerComboBox->currentText();
    const QString interface = ui->interfaceComboBox->currentText();
    const QString target = ui->targetComboBox->currentText().toLower();
    if (fileInfo.exists() && fileInfo.isFile())
    {
        ui->pfile_le->setStyleSheet("");

        bool warn = true;
        QStringList args;
        args << "-v"
             << "-t" << programmer
             << "-i" << interface
             << "-d" << target
             << "program" << "-c"
             << "-f" << ui->pfile_le->text();

        if (ui->pfileErase_cb->isChecked())
            args << "-e";

        if (ui->pfileVerify_cb->isChecked())
            args << "--verify";

        if (ui->pfileFuses_cb->isChecked())
        {
            args << "-fs";
            warn = false;
        }

        if (ui->pfileFlash_cb->isChecked())
        {
            args << "-fl";
            warn = false;
        }

        if (ui->pfileEeprom_cb->isChecked())
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
        {
            setRunning(true);
            m_commandQueue.enqueue(args);
            startProcess(m_commandQueue.dequeue());
        }
    }
    else
    {
        setRunning(false);
        m_commandQueue.clear();
        ui->pfile_le->setStyleSheet("border: 1px solid red;");
        ui->statusBar->showMessage("Production file does not exist!");
    }
}

void MainWindow::on_showDebug_toggled(bool checked)
{
    ui->commandOutput->setVisible(checked);

    if (checked)
    {
        m_geometry = this->saveGeometry();
        this->restoreGeometry(m_debugGeometry);
    }
    else
    {
        m_debugGeometry = this->saveGeometry();
        this->restoreGeometry(m_geometry);
    }
}

void MainWindow::setRunning(bool running)
{
    m_running = running;
    ui->tabWidget->setDisabled(m_running);

    if (running)
    {
        ui->commandOutput->clear();
        ui->statusBar->clearMessage();
        ui->progressBar->setMaximum(0);
        ui->progressBar->setFormat("Loading...");
    }
    else
    {
        ui->progressBar->setMaximum(1);
    }
}

void MainWindow::startProcess(const QStringList& args)
{
    ui->commandOutput->append(k_programName + " " + args.join(" ") + "\n");
    QScrollBar *sb = ui->commandOutput->verticalScrollBar();
    sb->setValue(sb->maximum());
    m_process->setArguments(args);
    m_process->start();
}

// https://en.wikipedia.org/wiki/Executable_and_Linkable_Format#File_header
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

void MainWindow::handleTargetChanged(const QString &newTarget)
{
    static QString currentTarget;
    if (newTarget == currentTarget) return;

    // Remove previous fuses and interfaces
    qDeleteAll(m_fuseEditList);
    m_fuseEditList.clear();
    qDeleteAll(m_fuseLabelList);
    m_fuseLabelList.clear();
    ui->interfaceComboBox->clear();

    if (ui->targetComboBox->findText(newTarget) == -1)
    {
        ui->targetComboBox->setStyleSheet("border: 1px solid red;");
    }
    else
    {
        currentTarget = newTarget;
        ui->targetComboBox->setStyleSheet("");

        if (m_packManager.isValid())
        {
            QStringList interfaces = m_packManager.getInterfaces(newTarget);
            ui->interfaceComboBox->addItems(interfaces);

            bool exists;
            QStringList memories = m_packManager.getMemories(newTarget);

            // Flash
            exists = memories.contains("prog");
            ui->flashGroup->setVisible(exists);
            ui->pfileFlash_cb->setVisible(exists);
            ui->flash_le->clear();

            // EEPROM
            exists = memories.contains("eeprom");
            ui->eepromGroup->setVisible(exists);
            ui->pfileEeprom_cb->setVisible(exists);
            ui->eeprom_le->clear();

            /* TODO: Implement user signatures functionality
            // User Signatures
            exists = memories.contains("user_signatures");
            ui->userSigsGroup->setVisible(exists);
            ui->pfileUserSigs_cb->setVisible(exists);
            ui->userSigs_le->clear();
            */

            /* TODO: Implement lock bit functionality
            // Lock bits
            exists = memories.contains("lockbits");
            ui->pfileLockBits_cb->setVisible(exists);
            */

            QWidget *first = ui->commandOutput; // When we update the tab order, start right after fuseGroup.
            QList<AtPackManager::FuseInfo> fuseInfoList = m_packManager.getFuseInfo(newTarget);
            for (int i = 0; i < fuseInfoList.size(); ++i)
            {
                const AtPackManager::FuseInfo &info = fuseInfoList.at(i);

                QLabel *label = new QLabel(info.name);
                label->setToolTip(info.description);
                m_fuseLabelList.append(label);

                HexSpinBox *sb = new HexSpinBox();
                sb->setToolTip(info.description);
                sb->installEventFilter(this);
                m_fuseEditList.append(sb);

                ui->gridLayout->addWidget(label, i, 0);
                ui->gridLayout->addWidget(sb, i , 1);

                this->setTabOrder(first, sb);
                first = sb;
            }
        }
    }
}
