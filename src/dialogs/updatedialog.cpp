#include "updatedialog.h"
#include "ui_updatedialog.h"
#include <QDesktopServices>
#include <QUrl>
#include <QPushButton>
#include <QDebug>
#include <QSettings>
#include <release.h>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QMessageBox>
#include <utils/misc.h>
#include <QDir>
#include <QTemporaryFile>

UpdateDialog::UpdateDialog(QWidget *parent, QString changesHtml,
                           QString releaseUrl, QString releaseVersionString,
                           int releaseBuildNumber) :
        MasterDialog(parent),
        ui(new Ui::UpdateDialog) {
    ui->setupUi(this);
    ui->downloadProgressBar->hide();

    ui->changeLogEdit->setHtml(changesHtml);
    ui->versionLabel->setText("Version " + releaseVersionString + " - build " +
                              QString::number(releaseBuildNumber));
    this->releaseVersionString = releaseVersionString;
    this->releaseUrl = releaseUrl;

    QPushButton *button;
    ui->buttonBox->clear();

    _updateButton = new QPushButton(tr("&Update"));

    // automatic updates are only available for Windows
#if defined(Q_OS_WIN)
    _updateButton->setProperty("ActionRole", Update);
    _updateButton->setDefault(true);
    _updateButton->setIcon(
            QIcon::fromTheme(
                    "svn-update",
                    QIcon(":/icons/breeze-qownnotes/16x16/svn-update.svg")));
    ui->buttonBox->addButton(_updateButton, QDialogButtonBox::ActionRole);
#endif

    button = new QPushButton(tr("&Download latest"));
    button->setProperty("ActionRole", Download);
    button->setDefault(true);
    button->setIcon(
            QIcon::fromTheme(
                    "edit-download",
                    QIcon(":/icons/breeze-qownnotes/16x16/edit-download.svg")));
    ui->buttonBox->addButton(button, QDialogButtonBox::ActionRole);

    button = new QPushButton(tr("&Skip version"));
    button->setProperty("ActionRole", Skip);
    button->setDefault(false);
    button->setIcon(
            QIcon::fromTheme(
                    "media-skip-forward",
                    QIcon(":/icons/breeze-qownnotes/16x16/"
                                  "media-skip-forward.svg")));
    ui->buttonBox->addButton(button, QDialogButtonBox::ActionRole);

    button = new QPushButton(tr("&Cancel"));
    button->setProperty("ActionRole", Cancel);
    button->setIcon(
            QIcon::fromTheme(
                    "dialog-cancel",
                    QIcon(":/icons/breeze-qownnotes/16x16/dialog-cancel.svg")));
    button->setDefault(false);
    ui->buttonBox->addButton(button, QDialogButtonBox::ActionRole);

    connect(this->ui->buttonBox, SIGNAL(clicked(QAbstractButton *)),
            SLOT(dialogButtonClicked(QAbstractButton *)));

    _networkManager = new QNetworkAccessManager();
    QObject::connect(_networkManager, SIGNAL(finished(QNetworkReply *)),
                     this, SLOT(slotReplyFinished(QNetworkReply *)));
}

UpdateDialog::~UpdateDialog() {
    delete ui;
}

int UpdateDialog::exec() {
    setIsUpdateDialogOpen(true);
    int result = MasterDialog::exec();
    setIsUpdateDialogOpen(false);
    return result;
}

void UpdateDialog::closeEvent(QCloseEvent *event) {
    Q_UNUSED(event);
    setIsUpdateDialogOpen(false);
}

void UpdateDialog::show() {
    setIsUpdateDialogOpen(true);
    MasterDialog::show();
}

void UpdateDialog::dialogButtonClicked(QAbstractButton *button) {
    int actionRole = button->property("ActionRole").toInt();

    switch (actionRole) {
        case Skip:
        {
            QSettings settings;
            settings.setValue("skipVersion", this->releaseVersionString);
            qDebug() << "skip version";
            break;
        }
        case Download:
        {
            QString release = qApp->property("release").toString();
            // if the release was build by the CI systems download the new
            // release
            if ((release == "Travis CI") || (release == "AppVeyor")) {
                // download the new release
                QDesktopServices::openUrl(QUrl(releaseUrl.toUtf8()));
            } else {
                // open the installation page
                QDesktopServices::openUrl(
                        QUrl("http://www.qownnotes.org/installation"));
            }
            break;
        }
        case Update:
        {
            qDebug() << __func__ << " - 'releaseUrl': " << releaseUrl;

            ui->downloadProgressBar->show();
            _updateButton->setDisabled(true);

            QUrl url(releaseUrl);
            QNetworkRequest networkRequest(url);

#if (QT_VERSION >= QT_VERSION_CHECK(5, 6, 0))
            // we really need redirects for GitHub urls!
            networkRequest.setAttribute(
                    QNetworkRequest::FollowRedirectsAttribute, true);
#endif

            QNetworkReply *reply = _networkManager->get(networkRequest);

            connect(reply, SIGNAL(downloadProgress(qint64, qint64)),
                    this, SLOT(releaseDownloadProgress(qint64, qint64)));

            break;
        }
        default:
            break;
    }

    if (actionRole != Update) {
        this->close();
    }
}

/**
 * Shows the download progress
 */
void UpdateDialog::releaseDownloadProgress(
        qint64 bytesReceived, qint64 bytesTotal) {
    ui->downloadProgressBar->setMaximum(bytesTotal);
    ui->downloadProgressBar->setValue(bytesReceived);
}

/**
 * Stores the downloaded file and starts updater script
 *
 * @param reply
 */
void UpdateDialog::slotReplyFinished(QNetworkReply *reply) {
    if (reply == NULL) {
        return;
    }

    ui->downloadProgressBar->hide();

    qDebug() << "Reply from " << reply->url().path();
    QByteArray data = reply->readAll();

//    qDebug() << __func__ << " - 'data': " << data;
     qDebug() << __func__ << " - 'data.size': " << data.size();

    if (reply->error() != QNetworkReply::NoError) {
        QMessageBox::critical(
                0, tr("Download error"),
                tr("Error while downloading:\n%1").arg(reply->errorString()));

        qWarning() << tr("network error: %1").arg(reply->errorString());
        _updateButton->setDisabled(false);
        return;
    }
#if defined(Q_OS_MAC)
    QString suffix = "dmg";
#elif defined(Q_OS_WIN)
    QString suffix = "zip";
#elif defined(Q_OS_LINUX)
    QString suffix = "deb";
#else
    QString suffix = "download";
#endif

    QTemporaryFile *tempFile = new QTemporaryFile(
            QDir::tempPath() + "/QOwnNotes-XXXXXX." + suffix);

    // we want to keep the file to be used in the update process
    tempFile->setAutoRemove(false);

    // get a temporary file
    if (!tempFile->open()) {
        QMessageBox::critical(
                    0, tr("File error"),
                    tr("Could not open temporary file:\n%1")
                    .arg(tempFile->errorString()));
        return;
    }

    // file->fileName() only holds a value after file->open()
    QString filePath = tempFile->fileName();
    tempFile->close();
    // we need to delete the object so the later written file
    // can be read again while the app is open
    tempFile->deleteLater();
    destroy(tempFile);

    // unfortunately if you write to a QTemporaryFile under Windows the data
    // only gets written when the application quits,
    // so we need a QFile to write to
    QFile file(filePath);

    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(
                0, tr("File error"),
                tr("Could not store downloaded file:\n%1")
                        .arg(file.errorString()));
        return;
    }

    // store the data in the file
    file.write(data);
    file.close();
    // we need to delete the object so the written file
    // can be read again while the app is open
    file.deleteLater();

    // this is needed so deleteLater() really deletes the object
    // and the downloaded file can be accessed
    QCoreApplication::processEvents();

    // initialize the update process
    initializeUpdateProcess(filePath);
}

/**
 * Initializes the update process
 */
bool UpdateDialog::initializeUpdateProcess(QString filePath) {
#if defined(Q_OS_MAC)
    // TODO(pbek): implement OS X updater
    Q_UNUSED(filePath);
#elif defined(Q_OS_WIN)
    return initializeWindowsUpdateProcess(filePath);
#elif defined(Q_OS_LINUX)
    Q_UNUSED(filePath);
#endif

    return true;
}

/**
 * Initializes the Windows update process
 */
bool UpdateDialog::initializeWindowsUpdateProcess(QString filePath) {
    // get the folder path from the file path
    int lastPoint = filePath.lastIndexOf(".");
    QString pathPrefix = filePath.left(lastPoint);
    QString updaterPath = pathPrefix + "-updater.bat";
    QString unzipPath = pathPrefix + "-unzip.exe";

    QFile updaterSourceFile(QCoreApplication::applicationDirPath() +
                                    "/update.bat");
    QFile unzipSourceFile(QCoreApplication::applicationDirPath() +
                                  "/unzip.exe");

    // check if updater script exists
    if (!updaterSourceFile.exists()) {
        QMessageBox::critical(
                    0, tr("Error"),
                    tr("Updater script '%1' doesn't exist!")
                    .arg(updaterSourceFile.fileName()));
        return false;
    }

    // check if unzip executable exists
    if (!unzipSourceFile.exists()) {
        QMessageBox::critical(
                    0, tr("Error"),
                    tr("Updater unzip executable '%1' doesn't exist!")
                    .arg(unzipSourceFile.fileName()));
        return false;
    }

    // copy updater script
    if (!updaterSourceFile.copy(updaterPath)) {
        QMessageBox::critical(
                    0, tr("Error"),
                    tr("Could not copy updater script '%1' to '%2'!")
                    .arg(updaterSourceFile.fileName(), unzipPath));
        return false;
    }

    // copy unzip executable script
    if (!unzipSourceFile.copy(unzipPath)) {
        QMessageBox::critical(
                    0, tr("Error"),
                    tr("Could not copy updater script '%1' to '%2'!")
                    .arg(updaterSourceFile.fileName(), unzipPath));
        return false;
    }

    // check if updater script exists in temporary folder
    QFile updaterFile(updaterPath);
    if (!updaterFile.exists()) {
        QMessageBox::critical(
                0, tr("Error"),
                tr("Couldn't find updater script: %1").arg(updaterPath));
        return false;
    }

    // check if unzip executable exists in temporary folder
    QFile unzipFile(unzipPath);
    if (!unzipFile.exists()) {
        QMessageBox::critical(
                    0, tr("Error"),
                    tr("Couldn't find unzip executable: %1").arg(unzipPath));
        return false;
    }

    if (QMessageBox::information(
            this,
            tr("Proceed with update"),
            tr("The download is now finished. Do you want to update and "
                       "restart QOwnNotes?"),
            tr("&Update and restart"), tr("&Cancel"), QString::null,
            0, 1) == 1) {
        return false;
    }

    QStringList parameters(QStringList() <<
                           QDir::toNativeSeparators(filePath) <<
                           QDir::toNativeSeparators(
                                   QCoreApplication::applicationDirPath()) <<
                           QDir::toNativeSeparators(unzipPath));

    qDebug() << __func__ << " - 'parameters': " << parameters;

    // start updater script
    Utils::Misc::startDetachedProcess(updaterPath, parameters);

    qApp->quit();
    return true;
}

/**
 * Checks if the update dialog is open
 */
bool UpdateDialog::isUpdateDialogOpen() {
    return qApp->property("isUpdateDialogOpen").value<bool>();
}

/**
 * Sets if the update dialog is open
 */
void UpdateDialog::setIsUpdateDialogOpen(bool isOpen) {
    qApp->setProperty("isUpdateDialogOpen", QVariant::fromValue<bool>(isOpen));
}
