#include "WeeklyReportSettings.h"
#include "AppConstants.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QMap>
#include <QtCore/QSettings>
#include <QtCore/QUrl>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QVBoxLayout>

#define NOMINMAX
#include <Windows.h>
#include <wincrypt.h>

namespace
{
const QString SecretKey = QStringLiteral("weeklyReport/apiKeyDpapi");
const QString SecretServerKey = QStringLiteral("weeklyReport/apiKeyServer");
const QString AllowHttpKey = QStringLiteral("weeklyReport/allowHttp");

QByteArray protect(QByteArray input, bool encrypt, bool* ok)
{
    DATA_BLOB source{static_cast<DWORD>(input.size()),
                     reinterpret_cast<BYTE*>(input.data())};
    DATA_BLOB result{};
    *ok = (encrypt
        ? CryptProtectData(&source, L"JSWorkPartner weekly report", nullptr,
                           nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &result)
        : CryptUnprotectData(&source, nullptr, nullptr,
                             nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &result)) != FALSE;
    QByteArray output;
    if (*ok)
    {
        output = QByteArray(reinterpret_cast<const char*>(result.pbData),
                            static_cast<int>(result.cbData));
        SecureZeroMemory(result.pbData, result.cbData);
        LocalFree(result.pbData);
    }
    if (!input.isEmpty())
        SecureZeroMemory(input.data(), static_cast<SIZE_T>(input.size()));
    return output;
}
}

QString WeeklyReportSettings::normalizeServerUrl(QString value)
{
    value = value.trimmed();
    if (!value.isEmpty() && !value.contains(QStringLiteral("://")))
        value.prepend(QStringLiteral("https://"));
    while (value.endsWith(QLatin1Char('/')))
        value.chop(1);
    return value;
}

QString WeeklyReportSettings::serverError(const QString& server, bool allowHttp)
{
    const QUrl parsed(server, QUrl::StrictMode);
    if (!parsed.isValid() || parsed.host().isEmpty() ||
        (parsed.scheme() != QStringLiteral("https") && parsed.scheme() != QStringLiteral("http")) ||
        parsed.authority().contains(QLatin1Char('@')) || parsed.hasQuery() || parsed.hasFragment())
        return QStringLiteral("인증정보·쿼리·fragment 없는 HTTP 또는 HTTPS 주소를 입력하세요.");
    if (parsed.scheme() == QStringLiteral("http") && !allowHttp)
        return QStringLiteral("HTTP는 기본 차단됩니다. 평문 전송 안내를 확인하고 'HTTP 평문 전송 허용'을 선택하세요.");
    return {};
}

WeeklyReportSettings WeeklyReportSettings::load()
{
    QSettings settings;
    WeeklyReportSettings result;
    result.redmineUrl = normalizeServerUrl(settings.value(AppConstants::RedmineUrlKey).toString());
    result.allowHttp = settings.value(AllowHttpKey, false).toBool();
    result.displayName = settings.value(QStringLiteral("weeklyReport/displayName")).toString();
    const int size = settings.beginReadArray(QStringLiteral("weeklyReport/workspaces"));
    for (int i = 0; i < size; ++i)
    {
        settings.setArrayIndex(i);
        result.workspaces.append({settings.value(QStringLiteral("section")).toString(),
                                 settings.value(QStringLiteral("vcs")).toString(),
                                 settings.value(QStringLiteral("path")).toString(),
                                 settings.value(QStringLiteral("author")).toString(),
                                 settings.value(QStringLiteral("project")).toString()});
    }
    settings.endArray();
    return result;
}

QString WeeklyReportSettings::apiKey(const QString& server, QString* error)
{
    QSettings settings;
    const QByteArray encrypted = settings.value(SecretKey).toByteArray();
    if (encrypted.isEmpty())
        return {};
    if (normalizeServerUrl(settings.value(SecretServerKey).toString()) != normalizeServerUrl(server))
    {
        *error = QStringLiteral("Redmine 주소가 변경되었습니다. 보고서 설정에서 API key를 다시 입력하세요.");
        return {};
    }
    bool ok = false;
    QByteArray plain = protect(encrypted, false, &ok);
    if (!ok)
        *error = QStringLiteral("API key를 복호화하지 못했습니다. 저장한 Windows 계정에서 다시 입력하세요.");
    const QString result = ok ? QString::fromUtf8(plain) : QString();
    if (!plain.isEmpty()) SecureZeroMemory(plain.data(), static_cast<SIZE_T>(plain.size()));
    return result;
}

bool WeeklyReportSettings::edit(QWidget* parent)
{
    const auto original = load();
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("주간업무 보고서 설정"));
    dialog.resize(980, 500);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* url = new QLineEdit(original.redmineUrl, &dialog);
    auto* allowHttp = new QCheckBox(QStringLiteral("HTTP 평문 전송 허용"), &dialog);
    allowHttp->setChecked(original.allowHttp);
    auto* transportHint = new QLabel(QStringLiteral(
        "기본은 HTTPS입니다. HTTP를 허용하면 API key와 업무 내용이 암호화되지 않고 전송됩니다."), &dialog);
    transportHint->setWordWrap(true);
    auto* name = new QLineEdit(original.displayName, &dialog);
    name->setPlaceholderText(QStringLiteral("비워 두면 현재 API 계정의 이름 사용"));
    auto* key = new QLineEdit(&dialog);
    key->setEchoMode(QLineEdit::Password);
    key->setPlaceholderText(QStringLiteral("새 key 입력 / 비우면 기존 보호된 key 유지"));
    auto* removeKey = new QCheckBox(QStringLiteral("저장된 API key 삭제"), &dialog);
    form->addRow(QStringLiteral("Redmine 주소"), url);
    form->addRow(QString(), allowHttp);
    form->addRow(QString(), transportHint);
    form->addRow(QStringLiteral("보고서 이름"), name);
    form->addRow(QStringLiteral("API key (Windows DPAPI)"), key);
    form->addRow(QString(), removeKey);
    layout->addLayout(form);
    auto* hint = new QLabel(QStringLiteral(
        "Git 작성자는 이름 또는 이메일을 정확히 입력하고, SVN은 계정 ID를 입력하세요.\n"
        "같은 섹션명으로 여러 폴더를 합칠 수 있습니다. 같은 Redmine 프로젝트는 같은 섹션명을 사용하세요.\n"
        "프로젝트 ID/식별자를 비우면 그 폴더의 이력만 수집합니다. API key는 현재 Windows 사용자만 복호화할 수 있습니다."), &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto* table = new QTableWidget(0, 5, &dialog);
    table->setHorizontalHeaderLabels({QStringLiteral("보고서 섹션"), QStringLiteral("VCS"),
        QStringLiteral("Working copy 경로"), QStringLiteral("작성자"), QStringLiteral("Redmine 프로젝트 ID/식별자")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    const auto addRow = [table](const ReportWorkspace& w) {
        const int row = table->rowCount();
        table->insertRow(row);
        auto* vcs = new QComboBox(table);
        vcs->addItems({QStringLiteral("Git"), QStringLiteral("SVN")});
        vcs->setCurrentIndex(w.vcs == QStringLiteral("SVN") ? 1 : 0);
        table->setCellWidget(row, 1, vcs);
        table->setItem(row, 0, new QTableWidgetItem(w.section));
        table->setItem(row, 2, new QTableWidgetItem(w.path));
        table->setItem(row, 3, new QTableWidgetItem(w.author));
        table->setItem(row, 4, new QTableWidgetItem(w.redmineProject));
    };
    for (const auto& w : original.workspaces)
        addRow(w);
    layout->addWidget(table, 1);
    auto* rowButtons = new QHBoxLayout;
    auto* add = new QPushButton(QStringLiteral("폴더 추가"), &dialog);
    auto* browse = new QPushButton(QStringLiteral("선택 행 경로 찾기"), &dialog);
    auto* remove = new QPushButton(QStringLiteral("선택 행 삭제"), &dialog);
    rowButtons->addWidget(add);
    rowButtons->addWidget(browse);
    rowButtons->addWidget(remove);
    rowButtons->addStretch();
    layout->addLayout(rowButtons);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(add, &QPushButton::clicked, &dialog, [addRow]() { addRow({}); });
    QObject::connect(browse, &QPushButton::clicked, &dialog, [&dialog, table]() {
        const int row = table->currentRow();
        if (row < 0) return;
        const QString path = QFileDialog::getExistingDirectory(&dialog, QStringLiteral("Working copy 선택"), table->item(row, 2)->text());
        if (!path.isEmpty()) table->item(row, 2)->setText(QDir::cleanPath(path));
    });
    QObject::connect(remove, &QPushButton::clicked, &dialog, [table]() {
        if (table->currentRow() >= 0) table->removeRow(table->currentRow());
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString server = normalizeServerUrl(url->text());
        const QString addressError = serverError(server, allowHttp->isChecked());
        if (!server.isEmpty() && !addressError.isEmpty())
        {
            QMessageBox::warning(&dialog, QStringLiteral("주소 확인"), addressError);
            return;
        }
        QVector<ReportWorkspace> workspaces;
        QMap<QString, QString> projectSections;
        for (int row = 0; row < table->rowCount(); ++row)
        {
            ReportWorkspace w{table->item(row, 0)->text().trimmed(),
                static_cast<QComboBox*>(table->cellWidget(row, 1))->currentText(),
                QDir::cleanPath(table->item(row, 2)->text().trimmed()),
                table->item(row, 3)->text().trimmed(), table->item(row, 4)->text().trimmed()};
            if (w.section.isEmpty() || w.author.isEmpty() || !QDir::isAbsolutePath(w.path) || !QFileInfo(w.path).isDir())
            {
                QMessageBox::warning(&dialog, QStringLiteral("Workspace 확인"), QStringLiteral("%1행: 섹션, 존재하는 절대 폴더 경로, 작성자를 입력하세요.").arg(row + 1));
                return;
            }
            if (!w.redmineProject.isEmpty() && projectSections.contains(w.redmineProject) && projectSections.value(w.redmineProject) != w.section)
            {
                QMessageBox::warning(&dialog, QStringLiteral("섹션 확인"), QStringLiteral("같은 프로젝트에는 같은 섹션명을 지정하세요."));
                return;
            }
            projectSections.insert(w.redmineProject, w.section);
            workspaces.append(w);
        }
        QSettings settings;
        QByteArray encrypted;
        const QString newKey = key->text().trimmed();
        if (!removeKey->isChecked() && !newKey.isEmpty())
        {
            if (server.isEmpty() || newKey.contains(QLatin1Char('\r')) || newKey.contains(QLatin1Char('\n')))
            {
                QMessageBox::warning(&dialog, QStringLiteral("API key 확인"), QStringLiteral("Redmine 주소와 한 줄의 API key를 입력하세요."));
                return;
            }
            bool ok = false;
            encrypted = protect(newKey.toUtf8(), true, &ok);
            if (!ok)
            {
                QMessageBox::critical(&dialog, QStringLiteral("보안 저장 실패"), QStringLiteral("DPAPI 암호화에 실패했습니다. API key를 저장하지 않았습니다."));
                return;
            }
        }
        else if (!removeKey->isChecked() && settings.contains(SecretKey) &&
                 normalizeServerUrl(settings.value(SecretServerKey).toString()) != server)
        {
            QMessageBox::warning(&dialog, QStringLiteral("API key 확인"), QStringLiteral("서버 변경 시 새 API key를 입력하거나 저장된 key 삭제를 선택하세요."));
            return;
        }
        settings.setValue(AppConstants::RedmineUrlKey, server);
        settings.setValue(AllowHttpKey, allowHttp->isChecked());
        settings.setValue(QStringLiteral("weeklyReport/displayName"), name->text().trimmed());
        settings.beginWriteArray(QStringLiteral("weeklyReport/workspaces"), workspaces.size());
        for (int i = 0; i < workspaces.size(); ++i)
        {
            settings.setArrayIndex(i);
            const auto& w = workspaces.at(i);
            settings.setValue(QStringLiteral("section"), w.section);
            settings.setValue(QStringLiteral("vcs"), w.vcs);
            settings.setValue(QStringLiteral("path"), w.path);
            settings.setValue(QStringLiteral("author"), w.author);
            settings.setValue(QStringLiteral("project"), w.redmineProject);
        }
        settings.endArray();
        if (removeKey->isChecked())
        {
            settings.remove(SecretKey);
            settings.remove(SecretServerKey);
        }
        else if (!encrypted.isEmpty())
        {
            settings.setValue(SecretKey, encrypted);
            settings.setValue(SecretServerKey, server);
        }
        settings.sync();
        if (settings.status() != QSettings::NoError)
        {
            QMessageBox::critical(&dialog, QStringLiteral("설정 저장 실패"), QStringLiteral("설정을 저장하지 못했습니다. 저장 권한을 확인하세요."));
            return;
        }
        key->clear();
        dialog.accept();
    });
    return dialog.exec() == QDialog::Accepted;
}
