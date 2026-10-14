#include "TaskQueueWidget.h"

#include <functional>
#include <utility>

#include "DeadlineColorDelegate.h"
#include "AppConstants.h"
#include "NullableDateEdit.h"
#include "QueueFloatingWidget.h"
#include "WeeklyReportDialog.h"
#include "DesktopPetWidget.h"
#include "PetAssistantPanel.h"
#include "PetAssistantController.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QDate>
#include <QtCore/QDir>
#include <QtCore/QEvent>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QSettings>
#include <QtCore/QSaveFile>
#include <QtCore/QRegularExpression>
#include <QtCore/QSignalBlocker>
#include <QtCore/QStandardPaths>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtGui/QClipboard>
#include <QtGui/QCloseEvent>
#include <QtGui/QDesktopServices>
#include <QtGui/QFont>
#include <QtGui/QKeySequence>
#include <QtGui/QRegularExpressionValidator>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCalendarWidget>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QDateEdit>
#include <QtWidgets/QDialog>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QMenu>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QShortcut>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QStyle>
#include <QtWidgets/QTableView>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QSystemTrayIcon>
#include <QtWidgets/QVBoxLayout>

#include "StyleManager.h"
#include "TaskQueueViewModel.h"
#include "ExternalTaskWatcher.h"
#include "Task.h"

namespace
{
bool sameEditableTask(const Task& left, const Task& right)
{
    return left.id == right.id && left.sequence == right.sequence && left.title == right.title &&
        left.details == right.details && left.externalUrl == right.externalUrl &&
        left.importance == right.importance && left.deadline == right.deadline && left.completed == right.completed;
}

QString normalizedRedmineUrl(const QString& value)
{
    QString normalized = value.trimmed();
    if (!normalized.isEmpty() &&
        !normalized.contains(QStringLiteral("://")))
    {
        normalized.prepend(QStringLiteral("https://"));
    }
    while (normalized.endsWith(QLatin1Char('/')))
    {
        normalized.chop(1);
    }
    return normalized;
}

QString redmineReferenceHtml(
    const QString& text,
    const QString& redmineUrl)
{
    const QString normalizedUrl = normalizedRedmineUrl(redmineUrl);
    const QUrl url(normalizedUrl);
    const bool canCreateLinks =
        url.isValid() &&
        !url.host().isEmpty() &&
        (url.scheme() == QStringLiteral("http") ||
         url.scheme() == QStringLiteral("https"));
    if (!canCreateLinks)
    {
        QString escaped = text.toHtmlEscaped();
        escaped.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
        return escaped;
    }

    static const QRegularExpression issueReference(
        QStringLiteral("#(\\d+)\\b"));
    QString html;
    int previousEnd = 0;
    QRegularExpressionMatchIterator matches =
        issueReference.globalMatch(text);
    while (matches.hasNext())
    {
        const QRegularExpressionMatch match = matches.next();
        html += text.mid(
                        previousEnd,
                        match.capturedStart() - previousEnd)
                    .toHtmlEscaped();
        const QString issueId = match.captured(1);
        const QString issueUrl =
            normalizedUrl + QStringLiteral("/issues/") + issueId;
        html += QStringLiteral("<a href=\"%1\">#%2</a>")
                    .arg(issueUrl.toHtmlEscaped(), issueId);
        previousEnd = match.capturedEnd();
    }
    html += text.mid(previousEnd).toHtmlEscaped();
    html.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    return html;
}

class SettingsDialog final : public QDialog
{
public:
    enum class CloseDecision
    {
        Save,
        Discard,
        Cancel
    };

    explicit SettingsDialog(QWidget* parent = nullptr)
        : QDialog(parent)
    {
    }

    void setCloseDecisionHandler(
        std::function<CloseDecision()> handler)
    {
        closeDecisionHandler_ = std::move(handler);
    }

protected:
    void closeEvent(QCloseEvent* event) override
    {
        if (!closeDecisionHandler_)
        {
            QDialog::closeEvent(event);
            return;
        }

        const CloseDecision decision = closeDecisionHandler_();
        switch (decision)
        {
        case CloseDecision::Save:
            closeDecisionHandler_ = nullptr;
            event->ignore();
            accept();
            break;
        case CloseDecision::Discard:
            closeDecisionHandler_ = nullptr;
            event->ignore();
            reject();
            break;
        case CloseDecision::Cancel:
            event->ignore();
            break;
        }
    }

private:
    std::function<CloseDecision()> closeDecisionHandler_;
};

class PetSettingsDialog final : public QDialog
{
public:
    explicit PetSettingsDialog(PetAssistantController* assistantController, QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("로봇 설정"));
        setMinimumWidth(420);

        QSettings settings;
        const bool visible = settings.value(AppConstants::PetVisibleKey, true).toBool();
        int sizePercent = 50;
        if (settings.contains(AppConstants::PetSizePercentKey)) {
            bool ok = false;
            const int stored = settings.value(AppConstants::PetSizePercentKey).toInt(&ok);
            sizePercent = DesktopPetWidget::normalizedSizePercent(ok ? stored : 50);
        }
        const bool alwaysOnTop = settings.value(AppConstants::PetAlwaysOnTopKey, true).toBool();
        const QString petName =
            AppConstants::petNameFromSettings(settings.value(AppConstants::PetNameKey));

        auto* layout = new QVBoxLayout(this);
        auto* description = new QLabel(
            QStringLiteral("저장하면 로봇 설정에 즉시 적용됩니다. 취소하거나 닫으면 변경되지 않습니다."), this);
        description->setWordWrap(true);
        layout->addWidget(description);

        auto* robotGroup = new QGroupBox(QStringLiteral("바탕화면 로봇"), this);
        auto* robotLayout = new QFormLayout(robotGroup);
        nameInput_ = new QLineEdit(petName, robotGroup);
        nameInput_->setObjectName(QStringLiteral("desktopPetNameInput"));
        nameInput_->setMaxLength(AppConstants::PetNameMaximumLength);
        nameInput_->setValidator(new QRegularExpressionValidator(
            QRegularExpression(QStringLiteral("[^\\p{Cc}\\p{Zl}\\p{Zp}]{0,%1}")
                                   .arg(AppConstants::PetNameMaximumLength)),
            nameInput_));
        robotLayout->addRow(QStringLiteral("이름"), nameInput_);

        visibleCheck_ = new QCheckBox(QStringLiteral("바탕화면에 로봇 표시"), robotGroup);
        visibleCheck_->setObjectName(QStringLiteral("desktopPetVisibleCheck"));
        visibleCheck_->setChecked(visible);
        robotLayout->addRow(visibleCheck_);

        sizeCombo_ = new QComboBox(robotGroup);
        sizeCombo_->setObjectName(QStringLiteral("desktopPetSizeCombo"));
        for (const int value : {25, 50, 75, 100, 125, 150})
            sizeCombo_->addItem(QStringLiteral("%1%").arg(value), value);
        const int sizeIndex = sizeCombo_->findData(sizePercent);
        sizeCombo_->setCurrentIndex(sizeIndex >= 0 ? sizeIndex : sizeCombo_->findData(50));
        robotLayout->addRow(QStringLiteral("로봇 크기"), sizeCombo_);

        alwaysOnTopCheck_ = new QCheckBox(QStringLiteral("로봇을 항상 위에 표시"), robotGroup);
        alwaysOnTopCheck_->setObjectName(QStringLiteral("desktopPetAlwaysOnTopCheck"));
        alwaysOnTopCheck_->setChecked(alwaysOnTop);
        robotLayout->addRow(alwaysOnTopCheck_);
        layout->addWidget(robotGroup);

        auto* assistantGroup = new QGroupBox(QStringLiteral("AI 연결 정보 (읽기 전용)"), this);
        auto* assistantLayout = new QFormLayout(assistantGroup);
        assistantLayout->addRow(QStringLiteral("로그인"), new QLabel(QStringLiteral("현재 Codex 로그인 사용"), assistantGroup));
        assistantLayout->addRow(QStringLiteral("지원 CLI"), new QLabel(QStringLiteral("0.161.0"), assistantGroup));
        assistantLayout->addRow(QStringLiteral("지원 모델"), new QLabel(QStringLiteral("gpt-6.1-sol"), assistantGroup));
        statusLabel_ = new QLabel(
            assistantController ? assistantController->connectionStatus() : QStringLiteral("연결 전"), assistantGroup);
        statusLabel_->setObjectName(QStringLiteral("petAssistantConnectionStatus"));
        assistantLayout->addRow(QStringLiteral("연결 상태"), statusLabel_);
        auto* statusNote = new QLabel(
            QStringLiteral("연결 전은 실제 로그인 확인을 시작하기 전 상태입니다. 첫 대화를 보내면 로그인과 연결 상태를 확인합니다."), assistantGroup);
        statusNote->setWordWrap(true);
        assistantLayout->addRow(statusNote);
        layout->addWidget(assistantGroup);

        if (assistantController) {
            connect(assistantController, &PetAssistantController::connectionStatusChanged,
                    statusLabel_, [this](const QString& status) { statusLabel_->setText(status); });
        }

        auto* buttons = new QDialogButtonBox(this);
        buttons->setObjectName(QStringLiteral("desktopPetSettingsButtons"));
        auto* saveButton = buttons->addButton(QStringLiteral("저장"), QDialogButtonBox::AcceptRole);
        saveButton->setObjectName(QStringLiteral("desktopPetSettingsSaveButton"));
        auto* cancelButton = buttons->addButton(QStringLiteral("취소"), QDialogButtonBox::RejectRole);
        connect(saveButton, &QPushButton::clicked, this, [this]() { saveAndApply(); });
        connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
        layout->addWidget(buttons);
    }

    void setApplyHandler(
        DesktopPetWidget* pet,
        PetAssistantPanel* assistantPanel,
        std::function<void(bool)> visibilityHandler)
    {
        pet_ = pet;
        assistantPanel_ = assistantPanel;
        visibilityHandler_ = std::move(visibilityHandler);
    }

    QString petName() const
    {
        return AppConstants::normalizedPetName(nameInput_->text());
    }
    bool petVisible() const { return visibleCheck_->isChecked(); }
    int sizePercent() const { return sizeCombo_->currentData().toInt(); }
    bool alwaysOnTop() const { return alwaysOnTopCheck_->isChecked(); }

private:
    void saveAndApply()
    {
        const int size = sizePercent();
        const QString name = petName();
        const bool visible = petVisible();
        const bool top = alwaysOnTop();
        QSettings settings;
        settings.setValue(AppConstants::PetNameKey, name);
        settings.setValue(AppConstants::PetSizePercentKey, size);
        settings.setValue(AppConstants::PetAlwaysOnTopKey, top);
        settings.setValue(AppConstants::PetVisibleKey, visible);
        settings.sync();
        if (pet_) pet_->setDisplayName(name);
        if (assistantPanel_) assistantPanel_->setDisplayName(name);
        if (pet_) pet_->setSizePercent(size);
        if (visibilityHandler_) visibilityHandler_(visible);
        if (pet_) pet_->setAlwaysOnTop(top);
        settings.sync();
        accept();
    }

    QLineEdit* nameInput_ = nullptr;
    QCheckBox* visibleCheck_ = nullptr;
    QComboBox* sizeCombo_ = nullptr;
    QCheckBox* alwaysOnTopCheck_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    DesktopPetWidget* pet_ = nullptr;
    PetAssistantPanel* assistantPanel_ = nullptr;
    std::function<void(bool)> visibilityHandler_;
};
}

TaskQueueWidget::TaskQueueWidget(
    TaskQueueViewModel& pendingViewModel,
    TaskQueueViewModel& completedViewModel,
    QWidget* parent)
    : QWidget(parent),
      pendingViewModel_(pendingViewModel),
      completedViewModel_(completedViewModel)
{
    setWindowTitle(
        QStringLiteral("%1 v%2 - 퀘스트 보드")
            .arg(QCoreApplication::applicationName())
            .arg(QCoreApplication::applicationVersion()));
    resize(1120, 760);
    setMinimumSize(900, 620);
    const QByteArray savedMainWindowGeometry =
        QSettings().value(AppConstants::MainWindowGeometryKey).toByteArray();
    if (!savedMainWindowGeometry.isEmpty())
    {
        restoreGeometry(savedMainWindowGeometry);
    }

    auto* listTitleLabel = new QLabel(QStringLiteral("진행 중인 퀘스트"), this);
    QFont titleFont = listTitleLabel->font();
    titleFont.setPointSize(20);
    titleFont.setBold(true);
    listTitleLabel->setFont(titleFont);

    auto* queueTabsTitleLabel = new QLabel(
        QStringLiteral("퀘스트 전체보기"),
        this);
    queueTabsTitleLabel->setFont(titleFont);

    auto* titleInputLabel = new QLabel(QStringLiteral("퀘스트 제목"), this);
    QFont inputLabelFont = titleInputLabel->font();
    inputLabelFont.setBold(true);
    titleInputLabel->setFont(inputLabelFont);

    titleInput_ = new QLineEdit(this);
    titleInput_->setPlaceholderText(QStringLiteral("퀘스트 제목을 입력하세요."));
    titleInput_->setMinimumHeight(38);

    auto* detailsInputLabel = new QLabel(QStringLiteral("상세 설명"), this);
    detailsInputLabel->setFont(inputLabelFont);

    detailsInput_ = new QTextEdit(this);
    detailsInput_->setPlaceholderText(
        QStringLiteral("퀘스트의 배경, 요청사항 등 상세 내용을 작성하세요. (선택)"));
    detailsInput_->setFixedHeight(70);
    detailsInput_->setAcceptRichText(false);

    auto* externalLinkLabel = new QLabel(
        QStringLiteral("외부 링크"),
        this);
    externalLinkLabel->setFont(inputLabelFont);

    externalLinkInput_ = new QLineEdit(this);
    externalLinkInput_->setPlaceholderText(
        QStringLiteral("https://example.com 또는 example.com (선택)"));
    externalLinkInput_->setClearButtonEnabled(true);

    auto* optionsLayout = new QHBoxLayout();
    optionsLayout->setContentsMargins(0, 0, 0, 0);
    optionsLayout->setSpacing(3);
    auto* importanceLabel = new QLabel(QStringLiteral("중요도"), this);
    importanceLabel->setFont(inputLabelFont);

    auto* deadlineLabel = new QLabel(QStringLiteral("마감일"), this);
    deadlineLabel->setFont(inputLabelFont);
    deadlineInput_ = new NullableDateEdit(this);
    deadlineInput_->setToolTip(
        QStringLiteral("날짜를 선택하면 마감일이 설정됩니다."));
    deadlineClearButton_ = new QToolButton(this);
    deadlineClearButton_->setText(QStringLiteral("×"));
    deadlineClearButton_->setToolTip(QStringLiteral("마감일 지우기"));
    deadlineClearButton_->setFixedSize(28, 28);
    deadlineClearButton_->setVisible(false);

    optionsLayout->addWidget(importanceLabel);
    for (int index = 0; index < static_cast<int>(importanceStars_.size()); ++index)
    {
        auto* starButton = new QPushButton(this);
        starButton->setFlat(true);
        starButton->setFixedSize(25, 30);
        starButton->setCursor(Qt::PointingHandCursor);
        starButton->setProperty("importanceStar", true);
        starButton->setToolTip(
            QStringLiteral("중요도 %1").arg(index + 1));
        importanceStars_[index] = starButton;
        optionsLayout->addWidget(starButton);

        connect(starButton, &QPushButton::clicked, this, [this, index]() {
            setImportance(index + 1);
        });
    }
    optionsLayout->addStretch();
    deadlineInput_->setFixedWidth(128);
    optionsLayout->addWidget(deadlineLabel);
    optionsLayout->addWidget(deadlineInput_);
    optionsLayout->addWidget(deadlineClearButton_);

    addButton_ = new QPushButton(QStringLiteral("새 퀘스트  (Ctrl+Enter)"), this);
    addButton_->setObjectName(QStringLiteral("primaryButton"));
    addButton_->setDefault(true);
    addButton_->setMinimumHeight(38);
    addButton_->setToolTip(
        QStringLiteral(
            "Ctrl+N으로 새 퀘스트 입력 화면을 열 수 있습니다.\n"
            "새 퀘스트는 중요도 1, 마감일 X로 저장됩니다.\n"
            "목록에서 퀘스트를 선택한 뒤 완료 버튼을 누르세요."));

    cancelEditButton_ = new QPushButton(QStringLiteral("수정 취소"), this);
    cancelEditButton_->setToolTip(QStringLiteral("Esc"));
    cancelEditButton_->setVisible(false);

    queueCountLabel_ = new QLabel(this);

    pendingPreviewTable_ = new QTableView(this);
    pendingFullTable_ = new QTableView(this);
    completedTable_ = new QTableView(this);
    pendingPreviewTable_->setModel(&pendingViewModel_);
    pendingFullTable_->setModel(&pendingViewModel_);
    completedTable_->setModel(&completedViewModel_);

    QSettings tableSettings;
    const int pendingSortColumn = qBound(
        0,
        tableSettings.value(
                         AppConstants::PendingSortColumnKey,
                         TaskQueueViewModel::SequenceColumn)
            .toInt(),
        TaskQueueViewModel::ColumnCount - 1);
    const Qt::SortOrder pendingSortOrder =
        tableSettings.value(
                         AppConstants::PendingSortOrderKey,
                         static_cast<int>(Qt::AscendingOrder))
                    .toInt() == static_cast<int>(Qt::DescendingOrder)
            ? Qt::DescendingOrder
            : Qt::AscendingOrder;
    const int completedSortColumn = qBound(
        0,
        tableSettings.value(
                         AppConstants::CompletedSortColumnKey,
                         TaskQueueViewModel::SequenceColumn)
            .toInt(),
        TaskQueueViewModel::ColumnCount - 1);
    const Qt::SortOrder completedSortOrder =
        tableSettings.value(
                         AppConstants::CompletedSortOrderKey,
                         static_cast<int>(Qt::AscendingOrder))
                    .toInt() == static_cast<int>(Qt::DescendingOrder)
            ? Qt::DescendingOrder
            : Qt::AscendingOrder;

    pendingPreviewTable_->horizontalHeader()->setSortIndicator(
        pendingSortColumn,
        pendingSortOrder);
    pendingFullTable_->horizontalHeader()->setSortIndicator(
        pendingSortColumn,
        pendingSortOrder);
    completedTable_->horizontalHeader()->setSortIndicator(
        completedSortColumn,
        completedSortOrder);
    pendingPreviewTable_->setSortingEnabled(true);
    pendingFullTable_->setSortingEnabled(true);
    completedTable_->setSortingEnabled(true);

    const auto connectPendingSort =
        [this](QTableView* source, QTableView* peer) {
            connect(
                source->horizontalHeader(),
                &QHeaderView::sortIndicatorChanged,
                this,
                [peer](int column, Qt::SortOrder order) {
                    const QSignalBlocker blocker(peer->horizontalHeader());
                    peer->horizontalHeader()->setSortIndicator(column, order);
                    QSettings settings;
                    settings.setValue(
                        AppConstants::PendingSortColumnKey,
                        column);
                    settings.setValue(
                        AppConstants::PendingSortOrderKey,
                        static_cast<int>(order));
                });
        };
    connectPendingSort(pendingPreviewTable_, pendingFullTable_);
    connectPendingSort(pendingFullTable_, pendingPreviewTable_);
    connect(
        completedTable_->horizontalHeader(),
        &QHeaderView::sortIndicatorChanged,
        this,
        [](int column, Qt::SortOrder order) {
            QSettings settings;
            settings.setValue(
                AppConstants::CompletedSortColumnKey,
                column);
            settings.setValue(
                AppConstants::CompletedSortOrderKey,
                static_cast<int>(order));
        });

    const std::array<QTableView*, 3> tables{{
        pendingPreviewTable_,
        pendingFullTable_,
        completedTable_}};
    for (QTableView* table : tables)
    {
        table->setItemDelegateForColumn(
            TaskQueueViewModel::DeadlineColumn,
            new DeadlineColorDelegate(table));
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setContextMenuPolicy(Qt::CustomContextMenu);
        table->setAlternatingRowColors(true);
        table->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
        table->verticalHeader()->setVisible(false);
        table->horizontalHeader()->setSectionResizeMode(
            TaskQueueViewModel::SequenceColumn,
            QHeaderView::Interactive);
        table->horizontalHeader()->resizeSection(
            TaskQueueViewModel::SequenceColumn,
            92);
        table->horizontalHeader()->setSectionResizeMode(
            TaskQueueViewModel::TitleColumn,
            QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(
            TaskQueueViewModel::DetailsColumn,
            QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(
            TaskQueueViewModel::ImportanceColumn,
            QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(
            TaskQueueViewModel::DeadlineColumn,
            QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(
            TaskQueueViewModel::DateColumn,
            QHeaderView::ResizeToContents);
    }

    pendingPreviewTable_->setFixedHeight(180);
    pendingPreviewTable_->hideColumn(TaskQueueViewModel::DetailsColumn);
    pendingPreviewTable_->horizontalHeader()->setSectionResizeMode(
        TaskQueueViewModel::TitleColumn,
        QHeaderView::Stretch);
    pendingFullTable_->hideColumn(TaskQueueViewModel::DetailsColumn);
    completedTable_->hideColumn(TaskQueueViewModel::DetailsColumn);
    pendingFullTable_->horizontalHeader()->setSectionResizeMode(
        TaskQueueViewModel::TitleColumn,
        QHeaderView::Stretch);
    completedTable_->horizontalHeader()->setSectionResizeMode(
        TaskQueueViewModel::TitleColumn,
        QHeaderView::Stretch);

    auto* pendingFullPage = new QWidget(this);
    auto* pendingFullLayout = new QVBoxLayout(pendingFullPage);
    pendingFullLayout->setContentsMargins(6, 8, 6, 6);
    pendingFullLayout->addWidget(pendingFullTable_);

    auto* completedPage = new QWidget(this);
    auto* completedLayout = new QVBoxLayout(completedPage);
    completedLayout->setContentsMargins(6, 8, 6, 6);
    completedLayout->addWidget(completedTable_);

    queueTabs_ = new QTabWidget(this);
    queueTabs_->addTab(pendingFullPage, QStringLiteral("진행 중인 퀘스트"));
    queueTabs_->addTab(completedPage, QStringLiteral("완료한 퀘스트"));
    queueTabs_->setCurrentIndex(1);

    completeButton_ = new QPushButton(QStringLiteral("선택 퀘스트 완료"), this);
    completeButton_->setObjectName(QStringLiteral("successButton"));
    completeButton_->setFixedSize(132, 34);
    editButton_ = new QPushButton(QStringLiteral("선택 퀘스트 수정"), this);
    editButton_->setFixedSize(132, 34);

    auto* queueHeaderLayout = new QHBoxLayout();
    queueHeaderLayout->setSpacing(8);
    queueHeaderLayout->addWidget(listTitleLabel);
    queueHeaderLayout->addStretch();
    queueHeaderLayout->addWidget(completeButton_);

    statusLabel_ = new QLabel(
        QStringLiteral("퀘스트를 입력해 주세요."),
        this);
    statusLabel_->setWordWrap(true);
    statusLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* selectedTaskPage = new QWidget(this);
    auto* selectedTaskLayout = new QVBoxLayout(selectedTaskPage);
    selectedTaskLayout->setContentsMargins(14, 14, 14, 14);
    selectedTaskLayout->setSpacing(8);

    selectedDisplayWidget_ = new QWidget(selectedTaskPage);
    auto* selectedDisplayLayout = new QVBoxLayout(selectedDisplayWidget_);
    selectedDisplayLayout->setContentsMargins(0, 0, 0, 0);
    selectedDisplayLayout->setSpacing(8);

    selectedTitleLabel_ = new QLabel(
        QStringLiteral("오른쪽 목록에서 퀘스트를 선택하세요."),
        selectedDisplayWidget_);
    QFont selectedTitleFont = selectedTitleLabel_->font();
    selectedTitleFont.setPointSize(12);
    selectedTitleFont.setBold(true);
    selectedTitleLabel_->setFont(selectedTitleFont);
    selectedTitleLabel_->setWordWrap(true);
    selectedTitleLabel_->setTextFormat(Qt::RichText);
    selectedTitleLabel_->setTextInteractionFlags(
        Qt::TextSelectableByMouse |
        Qt::LinksAccessibleByMouse);
    selectedTitleLabel_->setOpenExternalLinks(true);

    selectedDetailsLabel_ = new QLabel(QStringLiteral("-"), selectedDisplayWidget_);
    selectedDetailsLabel_->setWordWrap(true);
    selectedDetailsLabel_->setTextFormat(Qt::RichText);
    selectedDetailsLabel_->setTextInteractionFlags(
        Qt::TextSelectableByMouse |
        Qt::LinksAccessibleByMouse);
    selectedDetailsLabel_->setOpenExternalLinks(true);
    selectedDetailsLabel_->setMinimumHeight(42);

    selectedMetaLabel_ = new QLabel(QStringLiteral("-"), selectedDisplayWidget_);
    selectedMetaLabel_->setWordWrap(true);
    selectedMetaLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    selectedDisplayLayout->addWidget(selectedTitleLabel_);
    selectedDisplayLayout->addWidget(selectedDetailsLabel_);
    selectedDisplayLayout->addWidget(selectedMetaLabel_);

    externalLinkButton_ = new QPushButton(
        QStringLiteral("외부 링크 열기"),
        selectedDisplayWidget_);
    externalLinkButton_->setObjectName(QStringLiteral("primaryButton"));
    externalLinkButton_->setVisible(false);
    externalLinkButton_->setMinimumHeight(34);

    auto* selectedTaskActionsLayout = new QHBoxLayout();
    selectedTaskActionsLayout->addWidget(externalLinkButton_);
    selectedTaskActionsLayout->addStretch();
    selectedTaskActionsLayout->addWidget(editButton_);
    selectedDisplayLayout->addLayout(selectedTaskActionsLayout);
    selectedDisplayLayout->addStretch();

    selectedEditHost_ = new QWidget(selectedTaskPage);
    auto* selectedEditLayout = new QVBoxLayout(selectedEditHost_);
    selectedEditLayout->setContentsMargins(0, 0, 0, 0);
    selectedEditHost_->setVisible(false);

    selectedTaskLayout->addWidget(selectedDisplayWidget_);
    selectedTaskLayout->addWidget(selectedEditHost_);

    taskFormWidget_ = new QWidget(this);
    auto* taskFormLayout = new QVBoxLayout(taskFormWidget_);
    taskFormLayout->setContentsMargins(0, 0, 0, 0);
    taskFormLayout->setSpacing(8);
    taskFormLayout->addWidget(titleInputLabel);
    taskFormLayout->addWidget(titleInput_);
    taskFormLayout->addWidget(detailsInputLabel);
    taskFormLayout->addWidget(detailsInput_);
    taskFormLayout->addWidget(externalLinkLabel);
    taskFormLayout->addWidget(externalLinkInput_);
    taskFormLayout->addLayout(optionsLayout);
    auto* formActionsLayout = new QHBoxLayout();
    formActionsLayout->setContentsMargins(0, 0, 0, 0);
    formActionsLayout->setSpacing(8);
    formActionsLayout->addWidget(addButton_, 1);
    formActionsLayout->addWidget(cancelEditButton_, 1);
    taskFormLayout->addLayout(formActionsLayout);
    taskFormLayout->addStretch();

    newTaskHost_ = new QWidget(this);
    auto* newTaskLayout = new QVBoxLayout(newTaskHost_);
    newTaskLayout->setContentsMargins(10, 10, 10, 10);
    newTaskLayout->addWidget(taskFormWidget_);

    workTabs_ = new QTabWidget(this);
    workTabs_->setMinimumHeight(360);
    workTabs_->addTab(newTaskHost_, QStringLiteral("새 퀘스트"));
    workTabs_->addTab(selectedTaskPage, QStringLiteral("퀘스트 상세"));
    workTabs_->setCurrentIndex(0);

    auto* leftPanel = new QWidget(this);
    leftPanel->setObjectName(QStringLiteral("leftPanel"));
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(20, 18, 24, 18);
    leftLayout->setSpacing(12);
    leftLayout->addWidget(workTabs_, 1);

    auto* pendingQueueGroup = new QWidget(leftPanel);
    pendingQueueGroup->setObjectName(QStringLiteral("queueGroup"));
    auto* pendingQueueGroupLayout = new QVBoxLayout(pendingQueueGroup);
    pendingQueueGroupLayout->setContentsMargins(14, 12, 14, 12);
    pendingQueueGroupLayout->setSpacing(8);
    pendingQueueGroupLayout->addLayout(queueHeaderLayout);
    pendingQueueGroupLayout->addWidget(queueCountLabel_);
    pendingQueueGroupLayout->addWidget(pendingPreviewTable_);
    leftLayout->addWidget(pendingQueueGroup);

    auto* rightPanel = new QWidget(this);
    rightPanel->setObjectName(QStringLiteral("rightPanel"));
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(24, 18, 20, 18);
    rightLayout->setSpacing(12);

    auto* weeklySummaryCard = new QWidget(rightPanel);
    weeklySummaryCard->setObjectName(QStringLiteral("weeklySummaryCard"));
    auto* weeklySummaryLayout = new QVBoxLayout(weeklySummaryCard);
    weeklySummaryLayout->setContentsMargins(14, 10, 14, 10);
    weeklySummaryLayout->setSpacing(4);
    auto* weeklySummaryTitle =
        new QLabel(QStringLiteral("이번 주 요약"), weeklySummaryCard);
    QFont weeklySummaryTitleFont = weeklySummaryTitle->font();
    weeklySummaryTitleFont.setBold(true);
    weeklySummaryTitle->setFont(weeklySummaryTitleFont);
    weeklySummaryLabel_ = new QLabel(weeklySummaryCard);
    weeklySummaryLabel_->setWordWrap(true);
    auto* weeklySummaryHeaderLayout = new QHBoxLayout();
    weeklySummaryHeaderLayout->setContentsMargins(0, 0, 0, 0);
    weeklySummaryHeaderLayout->setSpacing(8);
    weeklySummaryHeaderLayout->addWidget(weeklySummaryTitle);
    weeklySummaryHeaderLayout->addStretch();
    auto* weeklySummaryListButton =
        new QPushButton(QStringLiteral("목록 보기"), weeklySummaryCard);
    weeklySummaryListButton->setFixedHeight(30);
    weeklySummaryHeaderLayout->addWidget(weeklySummaryListButton);
    auto* weeklyReportButton =
        new QPushButton(QStringLiteral("업무 보고서"), weeklySummaryCard);
    weeklyReportButton->setFixedHeight(30);
    weeklySummaryHeaderLayout->addWidget(weeklyReportButton);
    connect(weeklyReportButton, &QPushButton::clicked, this, [this]() { showWeeklyReport(); });

    weeklySummaryLayout->addLayout(weeklySummaryHeaderLayout);
    weeklySummaryLayout->addWidget(weeklySummaryLabel_);

    rightLayout->addWidget(weeklySummaryCard);
    rightLayout->addWidget(queueTabsTitleLabel);
    rightLayout->addWidget(queueTabs_, 1);
    rightLayout->addWidget(statusLabel_);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(leftPanel);
    splitter->addWidget(rightPanel);
    splitter->setCollapsible(0, false);
    splitter->setCollapsible(1, false);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes(QList<int>() << 620 << 650);

    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(12, 10, 12, 10);
    rootLayout->addWidget(splitter);

    connect(addButton_, &QPushButton::clicked, this, [this]() { addTask(); });
    connect(
        weeklySummaryListButton,
        &QPushButton::clicked,
        this,
        [this]() { showWeeklySummaryList(); });
    connect(editButton_, &QPushButton::clicked, this, [this]() { startEditing(); });
    connect(cancelEditButton_, &QPushButton::clicked, this, [this]() {
        cancelEditing();
    });
    connect(completeButton_, &QPushButton::clicked, this, [this]() {
        if (selectedTable_ == completedTable_)
        {
            reopenSelectedTask();
        }
        else
        {
            completeSelectedTask();
        }
    });
    connect(externalLinkButton_, &QPushButton::clicked, this, [this]() {
        if (!selectedExternalUrl_.isEmpty())
        {
            QDesktopServices::openUrl(QUrl(selectedExternalUrl_));
        }
    });
    connect(deadlineClearButton_, &QToolButton::clicked, this, [this]() {
        deadlineInput_->clearDate();
        deadlineClearButton_->setVisible(false);
    });
    connect(deadlineInput_, &QDateEdit::dateChanged, this, [this](const QDate&) {
        if (deadlineInput_->hasDate())
        {
            deadlineClearButton_->setVisible(true);
        }
    });
    connect(
        deadlineInput_->calendarWidget(),
        &QCalendarWidget::clicked,
        this,
        [this](const QDate&) {
            deadlineClearButton_->setVisible(true);
        });
    setImportance(1);
    for (QTableView* table : tables)
    {
        connect(
            table->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this,
            [this, table]() { handleTableSelection(table); });
        connect(
            table,
            &QWidget::customContextMenuRequested,
            this,
            [this, table](const QPoint& position) {
                showTableContextMenu(table, position);
            });
        auto* copyShortcut =
            new QShortcut(QKeySequence::Copy, table);
        copyShortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(
            copyShortcut,
            &QShortcut::activated,
            this,
            [this, table]() {
                if (table->selectionModel()->hasSelection())
                {
                    selectedTable_ = table;
                    copySelectedTaskToClipboard();
                }
            });
        auto* deleteShortcut =
            new QShortcut(QKeySequence(Qt::Key_Delete), table);
        deleteShortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(
            deleteShortcut,
            &QShortcut::activated,
            this,
            [this, table]() {
                if (table->selectionModel()->hasSelection())
                {
                    selectedTable_ = table;
                    deleteSelectedTask();
                }
            });
    }

    auto* addShortcut = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_Return), this);
    connect(addShortcut, &QShortcut::activated, this, [this]() { addTask(); });

    auto* addNumpadShortcut = new QShortcut(
        QKeySequence(Qt::CTRL + Qt::Key_Enter),
        this);
    connect(addNumpadShortcut, &QShortcut::activated, this, [this]() { addTask(); });

    auto* newTaskShortcut =
        new QShortcut(QKeySequence::New, this);
    connect(
        newTaskShortcut,
        &QShortcut::activated,
        this,
        [this]() {
            if (editingTaskId_ == 0)
            {
                workTabs_->setCurrentIndex(0);
                titleInput_->setFocus();
            }
        });

    auto* editShortcut =
        new QShortcut(QKeySequence(Qt::Key_F2), this);
    connect(
        editShortcut,
        &QShortcut::activated,
        this,
        [this]() { startEditing(); });

    auto* cancelEditingShortcut =
        new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(
        cancelEditingShortcut,
        &QShortcut::activated,
        this,
        [this]() {
            if (editingTaskId_ != 0)
            {
                cancelEditing();
            }
        });

    auto* deadlineRefreshTimer = new QTimer(this);
    deadlineRefreshTimer->setInterval(60 * 1000);
    connect(deadlineRefreshTimer, &QTimer::timeout, this, [this]() {
        pendingViewModel_.refreshDeadlineIndicators();
        updateUiState();
    });
    deadlineRefreshTimer->start();

    externalWatcher_ = new ExternalTaskWatcher(pendingViewModel_.databasePath(), this);
    const auto locallyChanged = [this]() {
        if (!applyingExternal_) externalWatcher_->requestRefresh();
    };
    for (auto* model : {&pendingViewModel_, &completedViewModel_})
    {
        connect(model, &QAbstractItemModel::modelReset, this, locallyChanged);
        connect(model, &QAbstractItemModel::rowsInserted, this, locallyChanged);
        connect(model, &QAbstractItemModel::rowsRemoved, this, locallyChanged);
        connect(model, &QAbstractItemModel::dataChanged, this, locallyChanged);
    }
    auto* externalRefreshTimer = new QTimer(this);
    externalRefreshTimer->setInterval(100);
    connect(externalRefreshTimer, &QTimer::timeout, this, [this]() { refreshExternalTasks(); });
    externalWatcher_->start();
    externalRefreshTimer->start();
    setupDesktopPet();
    setupTrayIcon();
    updateUiState();
    updateSelectedTaskDetails();
    titleInput_->setFocus();
}

TaskQueueWidget::~TaskQueueWidget()
{
    if (assistantController_) {assistantController_->shutdown();delete assistantController_;assistantController_=nullptr;}
    delete assistantPanel_;assistantPanel_=nullptr;
    delete desktopPet_;desktopPet_=nullptr;
    delete externalWatcher_;
    externalWatcher_ = nullptr;
    QSettings settings;
    settings.setValue(
        AppConstants::MainWindowGeometryKey,
        saveGeometry());

    if (floatingQueueWidget_ != nullptr)
    {
        settings.setValue(
            AppConstants::FloatingQueueGeometryKey,
            floatingQueueWidget_->saveGeometry());
        delete floatingQueueWidget_;
        floatingQueueWidget_ = nullptr;
    }
}

void TaskQueueWidget::refreshExternalTasks()
{
    if (!externalWatcher_) return;
    // Popup actions may hold a row index until exec() returns (including the floating board).
    if (QApplication::activePopupWidget()) return;
    ExternalTaskWatcher::Snapshot snapshot;
    if (!externalWatcher_->takeSnapshot(&snapshot)) return;
    if (!snapshot.error.isEmpty())
    {
        statusLabel_->setText(snapshot.error);
        return;
    }
    const qint64 selectedId = selectedTask().id;
    QTableView* const previousTable = selectedTable_;
    const int activeTab = workTabs_->currentIndex();
    applyingExternal_ = true;
    pendingViewModel_.applySnapshot(snapshot.pending);
    completedViewModel_.applySnapshot(snapshot.completed);
    selectedTable_ = nullptr;
    int row = pendingViewModel_.rowForTaskId(selectedId);
    if (selectedId && row >= 0)
    {
        selectedTable_ = previousTable == pendingFullTable_ ? pendingFullTable_ : pendingPreviewTable_;
        selectedTable_->selectRow(row);
    }
    else if (selectedId && (row = completedViewModel_.rowForTaskId(selectedId)) >= 0)
    {
        selectedTable_ = completedTable_;
        completedTable_->selectRow(row);
    }
    applyingExternal_ = false;
    updateSelectedTaskDetails();
    updateUiState();
    workTabs_->setCurrentIndex(activeTab);
}

void TaskQueueWidget::showInitialView()
{
    const bool hasTray=trayIcon_ && trayIcon_->isVisible();
    if (QSettings().value(AppConstants::PetVisibleKey,true).toBool()) showDesktopPet();
    if (!hasTray) showFromTray();
}

void TaskQueueWidget::setupDesktopPet()
{
    desktopPet_=new DesktopPetWidget;
    assistantPanel_=new PetAssistantPanel;
    assistantController_=new PetAssistantController(*desktopPet_,*assistantPanel_,pendingViewModel_.databasePath(),this);
    QSettings settings;
    const QString petName =
        AppConstants::petNameFromSettings(settings.value(AppConstants::PetNameKey));
    bool sizeOk = false;
    const int savedSize = settings.value(AppConstants::PetSizePercentKey, 50).toInt(&sizeOk);
    desktopPet_->setSizePercent(sizeOk ? savedSize : 50);
    desktopPet_->setDisplayName(petName);
    assistantPanel_->setDisplayName(petName);
    desktopPet_->setAlwaysOnTop(settings.value(AppConstants::PetAlwaysOnTopKey, true).toBool());
    if (settings.contains(AppConstants::PetPositionKey)) desktopPet_->restorePosition(settings.value(AppConstants::PetPositionKey).toPoint());
    const auto newTask=[this]() {addTaskFromFloatingWidget();};
    const auto tasks=[this]() {showFromTray();queueTabs_->setCurrentIndex(0);};
    connect(desktopPet_,&DesktopPetWidget::assistantRequested,assistantController_,&PetAssistantController::showAssistant);
    connect(desktopPet_,&DesktopPetWidget::newTaskRequested,this,newTask);
    connect(desktopPet_,&DesktopPetWidget::tasksRequested,this,tasks);
    connect(desktopPet_,&DesktopPetWidget::weeklyReportRequested,this,[this]() {showWeeklyReport();});
    connect(desktopPet_,&DesktopPetWidget::settingsRequested,this,&TaskQueueWidget::showPetSettingsDialog);
    connect(desktopPet_,&DesktopPetWidget::hideRequested,this,[this]() {hideDesktopPet();});
    connect(desktopPet_,&DesktopPetWidget::positionChanged,this,[](QPoint position) {QSettings().setValue(AppConstants::PetPositionKey,position);});
    connect(assistantPanel_,&PetAssistantPanel::newTaskRequested,this,newTask);
    connect(assistantPanel_,&PetAssistantPanel::tasksRequested,this,tasks);
    connect(assistantPanel_,&PetAssistantPanel::weeklyReportRequested,this,[this]() {showWeeklyReport();});
    connect(assistantController_,&PetAssistantController::tasksChanged,this,[this]() {if (externalWatcher_) externalWatcher_->requestRefresh();});
    connect(qApp,&QCoreApplication::aboutToQuit,assistantController_,&PetAssistantController::shutdown);
    const auto count=[this]() {desktopPet_->setPendingCount(pendingViewModel_.taskCount());};
    connect(&pendingViewModel_,&QAbstractItemModel::modelReset,this,count);
    connect(&pendingViewModel_,&QAbstractItemModel::rowsInserted,this,count);
    connect(&pendingViewModel_,&QAbstractItemModel::rowsRemoved,this,count);
    count();
    // A hidden pet can be recovered even on systems without a system tray.
    auto* reopen=new QAction(QStringLiteral("로봇과 대화 열기"),this);
    reopen->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Space")));reopen->setShortcutContext(Qt::ApplicationShortcut);addAction(reopen);
    connect(reopen,&QAction::triggered,this,[this]() {showDesktopPet();assistantController_->showAssistant();});
}
void TaskQueueWidget::showDesktopPet()
{ if (!desktopPet_) return;desktopPet_->ensureVisibleOnScreen();desktopPet_->show();desktopPet_->raise();QSettings().setValue(AppConstants::PetVisibleKey,true); }
void TaskQueueWidget::hideDesktopPet()
{
    if (desktopPet_) desktopPet_->hide();if (assistantPanel_) assistantPanel_->hide();
    QSettings().setValue(AppConstants::PetVisibleKey,false);
    if (!trayIcon_ || !trayIcon_->isVisible()) showFromTray();
}
void TaskQueueWidget::showPetSettingsDialog()
{
    PetSettingsDialog dialog(assistantController_);
    dialog.setApplyHandler(desktopPet_, assistantPanel_, [this](bool visible) {
        if (visible) showDesktopPet();
        else hideDesktopPet();
    });
    dialog.exec();
}
void TaskQueueWidget::showWeeklyReport()
{
    WeeklyReportDialog dialog(this);
    dialog.setLocalTaskProvider([this]() {
        QList<Task> tasks;
        for (int row=0;row<completedViewModel_.taskCount();++row) tasks.append(completedViewModel_.taskAt(row));
        return tasks;
    });
    dialog.exec();updateSelectedTaskDetails();
}

void TaskQueueWidget::closeEvent(QCloseEvent* event)
{
    QSettings().setValue(
        AppConstants::MainWindowGeometryKey,
        saveGeometry());

    if (trayIcon_ != nullptr && trayIcon_->isVisible())
    {
        hide();
        showTrayHint();
        event->ignore();
        return;
    }

    QWidget::closeEvent(event);
}

void TaskQueueWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);

    if (event->type() == QEvent::WindowStateChange &&
        isMinimized() &&
        trayIcon_ != nullptr &&
        trayIcon_->isVisible())
    {
        QTimer::singleShot(0, this, [this]() {
            hide();
            showTrayHint();
        });
    }
}

void TaskQueueWidget::setupTrayIcon()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
    {
        return;
    }

    QApplication::setQuitOnLastWindowClosed(false);

    auto* trayMenu = new QMenu(this);
    QAction* petAction=trayMenu->addAction(QStringLiteral("로봇 표시"));
    QAction* assistantAction=trayMenu->addAction(QStringLiteral("AI 대화 열기"));
    QAction* hidePetAction=trayMenu->addAction(QStringLiteral("로봇·대화 숨기기"));
    trayMenu->addSeparator();
    QAction* showAction = trayMenu->addAction(QStringLiteral("창 열기"));
    floatingQueueAction_ =
        trayMenu->addAction(QStringLiteral("퀘스트 보드 표시"));
    floatingQueueAction_->setCheckable(true);
    QAction* settingsAction =
        trayMenu->addAction(QStringLiteral("Settings..."));

    trayMenu->addSeparator();
    QAction* versionAction = trayMenu->addAction(
        QStringLiteral("%1 버전 %2")
            .arg(QCoreApplication::applicationName())
            .arg(QCoreApplication::applicationVersion()));
    versionAction->setEnabled(false);
    QAction* quitAction = trayMenu->addAction(QStringLiteral("종료"));

    trayIcon_ = new QSystemTrayIcon(
        StyleManager::instance().applicationIcon(),
        this);
    trayIcon_->setToolTip(
        QStringLiteral("%1 v%2")
            .arg(QCoreApplication::applicationName())
            .arg(QCoreApplication::applicationVersion()));
    trayIcon_->setContextMenu(trayMenu);

    floatingQueueWidget_ = new QueueFloatingWidget(pendingViewModel_);
    QSettings settings;
    const int savedOpacity =
        qBound(
            20,
            settings.value(
                        AppConstants::FloatingQueueOpacityKey,
                        100)
                .toInt(),
            100);
    floatingQueueWidget_->setWindowOpacity(savedOpacity / 100.0);
    const QByteArray savedGeometry =
        settings.value(
            AppConstants::FloatingQueueGeometryKey).toByteArray();
    if (!savedGeometry.isEmpty())
    {
        floatingQueueWidget_->restoreGeometry(savedGeometry);
    }
    floatingQueueWidget_->setVisibilityChangedHandler([this](bool visible) {
        if (floatingQueueAction_ != nullptr)
        {
            const QSignalBlocker blocker(floatingQueueAction_);
            floatingQueueAction_->setChecked(visible);
        }
        QSettings stateSettings;
        stateSettings.setValue(
            AppConstants::FloatingQueueVisibleKey,
            visible);
        if (!visible && floatingQueueWidget_ != nullptr)
        {
            stateSettings.setValue(
                AppConstants::FloatingQueueGeometryKey,
                floatingQueueWidget_->saveGeometry());
        }
    });
    floatingQueueWidget_->setTaskActionHandler(
        [this](int row, bool edit) {
            openTaskFromFloatingWidget(row, edit);
        });
    floatingQueueWidget_->setAddTaskHandler(
        [this]() { addTaskFromFloatingWidget(); });
    floatingQueueWidget_->setCompleteTaskHandler(
        [this](int row) { completeTaskFromFloatingWidget(row); });

    connect(showAction, &QAction::triggered, this, [this]() { showFromTray(); });
    connect(petAction,&QAction::triggered,this,[this]() {showDesktopPet();});
    connect(assistantAction,&QAction::triggered,this,[this]() {showDesktopPet();assistantController_->showAssistant();});
    connect(hidePetAction,&QAction::triggered,this,[this]() {hideDesktopPet();});
    connect(
        settingsAction,
        &QAction::triggered,
        this,
        [this]() { showSettingsDialog(); });
    connect(
        floatingQueueAction_,
        &QAction::toggled,
        this,
        [this](bool visible) {
            if (visible)
            {
                floatingQueueWidget_->show();
                floatingQueueWidget_->raise();
                floatingQueueWidget_->activateWindow();
            }
            else
            {
                floatingQueueWidget_->close();
            }
        });
    connect(quitAction, &QAction::triggered, this, [this]() {
        assistantController_->shutdown();
        trayIcon_->hide();
        QApplication::quit();
    });
    connect(
        trayIcon_,
        &QSystemTrayIcon::activated,
        this,
        [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::DoubleClick)
            {
                showFromTray();
            }
        });

    // The pet is the startup view. Keep the board's geometry/opacity and manual
    // tray toggle, but do not replay the previous version's auto-visible state.
    trayIcon_->show();
}

void TaskQueueWidget::showSettingsDialog()
{
    const StyleManager::Theme originalTheme =
        StyleManager::instance().currentTheme();
    QSettings settings;
    const int originalOpacity =
        qBound(
            20,
            settings.value(
                        AppConstants::FloatingQueueOpacityKey,
                        100)
                .toInt(),
            100);
    const QString originalRedmineUrl =
        settings.value(AppConstants::RedmineUrlKey).toString();

    SettingsDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Settings"));
    dialog.setMinimumWidth(420);

    auto* titleLabel = new QLabel(QStringLiteral("화면 설정"), &dialog);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(12);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);

    auto* descriptionLabel = new QLabel(
        QStringLiteral("프로그램 전체에 적용할 화면 모드를 선택하세요."),
        &dialog);

    auto* petSettingsButton = new QPushButton(QStringLiteral("로봇 설정 열기..."), &dialog);
    petSettingsButton->setObjectName(QStringLiteral("openRobotSettingsButton"));
    auto* petSettingsDescription = new QLabel(
        QStringLiteral("로봇 설정은 별도 창에서 저장하고 즉시 적용합니다."), &dialog);
    petSettingsDescription->setWordWrap(true);
    connect(petSettingsButton, &QPushButton::clicked, &dialog, [this]() { showPetSettingsDialog(); });

    auto* themeLabel = new QLabel(QStringLiteral("화면 모드"), &dialog);
    auto* themeCombo = new QComboBox(&dialog);
    themeCombo->addItem(
        QStringLiteral("일반 모드"),
        static_cast<int>(StyleManager::Theme::Light));
    themeCombo->addItem(
        QStringLiteral("다크 모드"),
        static_cast<int>(StyleManager::Theme::Dark));
    themeCombo->setCurrentIndex(
        originalTheme == StyleManager::Theme::Dark ? 1 : 0);

    auto* themeLayout = new QHBoxLayout();
    themeLayout->setContentsMargins(0, 0, 0, 0);
    themeLayout->setSpacing(12);
    themeLayout->addWidget(themeLabel);
    themeLayout->addStretch();
    themeLayout->addWidget(themeCombo);

    auto* dataPathLabel =
        new QLabel(QStringLiteral("데이터 저장 위치"), &dialog);
    QFont dataPathLabelFont = dataPathLabel->font();
    dataPathLabelFont.setBold(true);
    dataPathLabel->setFont(dataPathLabelFont);

    auto* databasePathEdit = new QLineEdit(&dialog);
    databasePathEdit->setReadOnly(true);
    databasePathEdit->setText(pendingViewModel_.databasePath());
    databasePathEdit->setToolTip(pendingViewModel_.databasePath());
    databasePathEdit->setCursorPosition(0);
    auto* openDataFolderButton =
        new QPushButton(QStringLiteral("폴더 열기"), &dialog);
    auto* backupDataButton =
        new QPushButton(QStringLiteral("백업 저장"), &dialog);
    auto* dataPathLayout = new QHBoxLayout();
    dataPathLayout->setContentsMargins(0, 0, 0, 0);
    dataPathLayout->setSpacing(8);
    dataPathLayout->addWidget(databasePathEdit, 1);
    dataPathLayout->addWidget(openDataFolderButton);
    dataPathLayout->addWidget(backupDataButton);

    auto* boardSettingsLabel =
        new QLabel(QStringLiteral("퀘스트 보드 설정"), &dialog);
    QFont boardSettingsLabelFont = boardSettingsLabel->font();
    boardSettingsLabelFont.setBold(true);
    boardSettingsLabel->setFont(boardSettingsLabelFont);

    auto* opacityTitleLabel = new QLabel(QStringLiteral("투명도"), &dialog);
    auto* opacitySlider = new QSlider(Qt::Horizontal, &dialog);
    opacitySlider->setRange(20, 100);
    opacitySlider->setSingleStep(5);
    opacitySlider->setPageStep(10);
    opacitySlider->setValue(originalOpacity);
    opacitySlider->setToolTip(
        QStringLiteral("퀘스트 보드 투명도 (20% ~ 100%)"));
    auto* opacityValueLabel = new QLabel(
        QStringLiteral("%1%").arg(originalOpacity),
        &dialog);
    opacityValueLabel->setFixedWidth(48);
    opacityValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto* opacityLayout = new QHBoxLayout();
    opacityLayout->setContentsMargins(0, 0, 0, 0);
    opacityLayout->setSpacing(10);
    opacityLayout->addWidget(opacityTitleLabel);
    opacityLayout->addWidget(opacitySlider, 1);
    opacityLayout->addWidget(opacityValueLabel);

    auto* redmineSettingsLabel =
        new QLabel(QStringLiteral("Redmine 링크"), &dialog);
    QFont redmineLabelFont = redmineSettingsLabel->font();
    redmineLabelFont.setBold(true);
    redmineSettingsLabel->setFont(redmineLabelFont);

    auto* redmineUrlEdit = new QLineEdit(&dialog);
    redmineUrlEdit->setPlaceholderText(
        QStringLiteral("https://redmine.example.com"));
    redmineUrlEdit->setText(originalRedmineUrl);
    redmineUrlEdit->setClearButtonEnabled(true);

    auto* redmineDescriptionLabel = new QLabel(
        QStringLiteral(
            "퀘스트 제목이나 상세 설명의 #번호를 Redmine 일감 링크로 표시합니다."),
        &dialog);
    redmineDescriptionLabel->setWordWrap(true);

    auto* redmineForm = new QFormLayout();
    redmineForm->setContentsMargins(0, 0, 0, 0);
    redmineForm->setSpacing(8);
    redmineForm->addRow(QStringLiteral("도메인 주소"), redmineUrlEdit);

    auto* saveButton = new QPushButton(QStringLiteral("저장"), &dialog);
    saveButton->setObjectName(QStringLiteral("primaryButton"));
    auto* cancelButton = new QPushButton(QStringLiteral("취소"), &dialog);
    auto* actionLayout = new QHBoxLayout();
    actionLayout->addStretch();
    actionLayout->addWidget(cancelButton);
    actionLayout->addWidget(saveButton);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);
    layout->addWidget(titleLabel);
    layout->addWidget(descriptionLabel);
    layout->addWidget(petSettingsButton);
    layout->addWidget(petSettingsDescription);
    layout->addSpacing(8);
    layout->addLayout(themeLayout);
    layout->addSpacing(12);
    layout->addWidget(boardSettingsLabel);
    layout->addLayout(opacityLayout);
    layout->addSpacing(12);
    layout->addWidget(redmineSettingsLabel);
    layout->addLayout(redmineForm);
    layout->addWidget(redmineDescriptionLabel);
    auto* weeklyReportSettingsButton =
        new QPushButton(QStringLiteral("주간업무 보고서 설정 (API key / workspace, 별도 저장)..."), &dialog);
    layout->addWidget(weeklyReportSettingsButton);
    connect(weeklyReportSettingsButton, &QPushButton::clicked, &dialog, [this, &dialog, redmineUrlEdit]() {
        if (WeeklyReportDialog::showSettings(&dialog))
        {
            redmineUrlEdit->setText(QSettings().value(AppConstants::RedmineUrlKey).toString());
            updateSelectedTaskDetails();
        }
    });
    layout->addSpacing(12);
    layout->addWidget(dataPathLabel);
    layout->addLayout(dataPathLayout);
    layout->addSpacing(12);
    layout->addLayout(actionLayout);

    connect(
        themeCombo,
        static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
        &dialog,
        [themeCombo](int index) {
            const StyleManager::Theme theme =
                static_cast<StyleManager::Theme>(
                    themeCombo->itemData(index).toInt());
            StyleManager::instance().setTheme(*qApp, theme);
        });
    connect(
        opacitySlider,
        &QSlider::valueChanged,
        &dialog,
        [this, opacityValueLabel](int opacity) {
            opacityValueLabel->setText(
                QStringLiteral("%1%").arg(opacity));
            if (floatingQueueWidget_ != nullptr)
            {
                floatingQueueWidget_->setWindowOpacity(opacity / 100.0);
            }
        });
    connect(
        openDataFolderButton,
        &QPushButton::clicked,
        &dialog,
        [this]() {
            const QFileInfo databaseFile(pendingViewModel_.databasePath());
            QDesktopServices::openUrl(
                QUrl::fromLocalFile(databaseFile.absolutePath()));
        });
    connect(
        backupDataButton,
        &QPushButton::clicked,
        &dialog,
        [this, &dialog]() {
            const QString sourcePath = pendingViewModel_.databasePath();
            const QString documentsPath =
                QStandardPaths::writableLocation(
                    QStandardPaths::DocumentsLocation);
            const QString suggestedName =
                QStringLiteral("JSWorkPartner-backup-%1.db")
                    .arg(QDateTime::currentDateTime().toString(
                        QStringLiteral("yyyyMMdd-HHmmss")));
            const QString suggestedPath =
                QDir(documentsPath).filePath(suggestedName);
            const QString destinationPath = QFileDialog::getSaveFileName(
                &dialog,
                QStringLiteral("퀘스트 데이터 백업"),
                suggestedPath,
                QStringLiteral("SQLite 데이터베이스 (*.db);;모든 파일 (*.*)"));
            if (destinationPath.isEmpty())
            {
                return;
            }
            if (QFileInfo(sourcePath).absoluteFilePath().compare(
                    QFileInfo(destinationPath).absoluteFilePath(),
                    Qt::CaseInsensitive) == 0)
            {
                QMessageBox::warning(
                    &dialog,
                    QStringLiteral("백업 경로 확인"),
                    QStringLiteral(
                        "현재 사용 중인 데이터베이스 파일에는 백업할 수 없습니다.\n"
                        "다른 파일 이름이나 폴더를 선택해 주세요."));
                return;
            }

            QFile source(sourcePath);
            QSaveFile destination(destinationPath);
            if (!source.open(QIODevice::ReadOnly) ||
                !destination.open(QIODevice::WriteOnly))
            {
                QMessageBox::critical(
                    &dialog,
                    QStringLiteral("백업 실패"),
                    QStringLiteral("백업 파일을 열 수 없습니다."));
                return;
            }

            bool copySucceeded = true;
            while (!source.atEnd())
            {
                const QByteArray chunk = source.read(64 * 1024);
                if (chunk.isEmpty() && source.error() != QFile::NoError)
                {
                    copySucceeded = false;
                    break;
                }
                if (destination.write(chunk) != chunk.size())
                {
                    copySucceeded = false;
                    break;
                }
            }

            if (!copySucceeded || !destination.commit())
            {
                destination.cancelWriting();
                QMessageBox::critical(
                    &dialog,
                    QStringLiteral("백업 실패"),
                    QStringLiteral("데이터를 백업 파일에 저장하지 못했습니다."));
                return;
            }

            QMessageBox::information(
                &dialog,
                QStringLiteral("백업 완료"),
                QStringLiteral("퀘스트 데이터를 백업했습니다.\n%1")
                    .arg(QDir::toNativeSeparators(destinationPath)));
        });
    const auto saveSettings =
        [opacitySlider,
         redmineUrlEdit]() {
            QSettings settings;
            settings.setValue(
                AppConstants::FloatingQueueOpacityKey,
                opacitySlider->value());
            settings.setValue(
                AppConstants::RedmineUrlKey,
                normalizedRedmineUrl(redmineUrlEdit->text()));
        };
    connect(
        saveButton,
        &QPushButton::clicked,
        &dialog,
        [&dialog, saveSettings]() {
            saveSettings();
            dialog.accept();
        });
    connect(cancelButton, &QPushButton::clicked, &dialog, [&dialog]() {
        dialog.reject();
    });

    dialog.setCloseDecisionHandler(
        [&dialog,
         themeCombo,
         opacitySlider,
         redmineUrlEdit,
         originalTheme,
         originalOpacity,
         originalRedmineUrl,
         saveSettings]() {
            const StyleManager::Theme selectedTheme =
                static_cast<StyleManager::Theme>(
                    themeCombo->currentData().toInt());
            const bool hasChanges =
                selectedTheme != originalTheme ||
                opacitySlider->value() != originalOpacity ||
                normalizedRedmineUrl(redmineUrlEdit->text()) !=
                    originalRedmineUrl;
            if (!hasChanges)
            {
                return SettingsDialog::CloseDecision::Discard;
            }

            const QMessageBox::StandardButton answer =
                QMessageBox::question(
                    &dialog,
                    QStringLiteral("설정 변경사항"),
                    QStringLiteral("변경된 설정을 저장하시겠습니까?"),
                    QMessageBox::Save |
                        QMessageBox::Discard |
                        QMessageBox::Cancel,
                    QMessageBox::Save);

            if (answer == QMessageBox::Save)
            {
                saveSettings();
                return SettingsDialog::CloseDecision::Save;
            }
            if (answer == QMessageBox::Discard)
            {
                return SettingsDialog::CloseDecision::Discard;
            }
            return SettingsDialog::CloseDecision::Cancel;
        });

    if (dialog.exec() != QDialog::Accepted)
    {
        StyleManager::instance().setTheme(*qApp, originalTheme);
        if (floatingQueueWidget_ != nullptr)
        {
            floatingQueueWidget_->setWindowOpacity(
                originalOpacity / 100.0);
        }
    }
    else
    {
        updateSelectedTaskDetails();
    }
}

void TaskQueueWidget::showFromTray()
{
    showNormal();
    raise();
    activateWindow();
}

void TaskQueueWidget::showTrayHint()
{
    if (trayIcon_ == nullptr || trayHintShown_)
    {
        return;
    }

    trayHintShown_ = true;
    trayIcon_->showMessage(
        QStringLiteral("트레이에서 실행 중"),
        QStringLiteral(
            "트레이 아이콘을 클릭하면 퀘스트 보드를 다시 열 수 있습니다."),
        QSystemTrayIcon::Information,
        3000);
}

void TaskQueueWidget::addTask()
{
    const bool wasEditing =
        editingTaskId_ != 0 && editingViewModel_ != nullptr;
    const qint64 editedTaskId = editingTaskId_;
    QTableView* const editedTable = editingTable_;
    TaskQueueViewModel* const savedViewModel =
        wasEditing ? editingViewModel_ : &pendingViewModel_;
    const int editedRow =
        wasEditing ? savedViewModel->rowForTaskId(editedTaskId) : -1;
    if (wasEditing && editedRow < 0)
    {
        statusLabel_->setText(
            QStringLiteral("업무가 다른 곳에서 변경되었거나 삭제되었습니다. 입력 내용은 유지됩니다. 취소 후 다시 선택하세요."));
        return;
    }
    if (wasEditing && !sameEditableTask(editingOriginal_, savedViewModel->taskAt(editedRow)))
    {
        statusLabel_->setText(QStringLiteral("업무가 다른 곳에서 변경되었습니다. 입력 내용은 유지됩니다. 취소 후 최신 내용을 확인하세요."));
        return;
    }
    const QDate selectedDeadline = deadlineInput_->nullableDate();
    const QString deadline =
        selectedDeadline.isValid()
            ? selectedDeadline.toString(Qt::ISODate)
            : QString();

    const bool saved =
        wasEditing
            ? editingViewModel_->updateTask(
                  editedRow,
                  titleInput_->text(),
                  detailsInput_->toPlainText(),
                  externalLinkInput_->text(),
                  importance_,
                  deadline)
            : pendingViewModel_.addTask(
                  titleInput_->text(),
                  detailsInput_->toPlainText(),
                  externalLinkInput_->text(),
                  importance_,
                  deadline);

    if (!saved)
    {
        showViewModelError();
        titleInput_->setFocus();
        return;
    }

    titleInput_->clear();
    detailsInput_->clear();
    externalLinkInput_->clear();
    setImportance(1);
    deadlineInput_->clearDate();
    deadlineClearButton_->setVisible(false);
    editingTaskId_ = 0;
    editingViewModel_ = nullptr;
    editingTable_ = nullptr;
    addButton_->setText(QStringLiteral("새 퀘스트  (Ctrl+Enter)"));
    cancelEditButton_->setVisible(false);
    statusLabel_->setText(savedViewModel->lastMessage());
    if (wasEditing)
    {
        setEditingUi(false);
    }
    const int updatedRow =
        wasEditing ? savedViewModel->rowForTaskId(editedTaskId) : -1;
    if (wasEditing && editedTable != nullptr && updatedRow >= 0)
    {
        selectedTable_ = editedTable;
        editedTable->selectRow(updatedRow);
        workTabs_->setCurrentIndex(1);
    }
    else
    {
        workTabs_->setCurrentIndex(0);
    }
    updateSelectedTaskDetails();
    updateUiState();
    titleInput_->setFocus();
}

void TaskQueueWidget::startEditing()
{
    if (selectedTable_ == nullptr)
    {
        return;
    }

    TaskQueueViewModel* targetViewModel =
        selectedTable_ == completedTable_
            ? &completedViewModel_
            : &pendingViewModel_;
    const int row =
        selectedTable_ == completedTable_
            ? completedTable_->currentIndex().row()
            : selectedPendingRow();
    const Task task = targetViewModel->taskAt(row);
    if (task.id == 0)
    {
        return;
    }

    editingTaskId_ = task.id;
    editingOriginal_ = task;
    editingViewModel_ = targetViewModel;
    editingTable_ = selectedTable_;
    titleInput_->setText(task.title);
    detailsInput_->setPlainText(task.details);
    externalLinkInput_->setText(task.externalUrl);
    setImportance(task.importance);

    const QDate deadline = QDate::fromString(task.deadline, Qt::ISODate);
    deadlineInput_->setNullableDate(deadline);
    deadlineClearButton_->setVisible(deadline.isValid());

    addButton_->setText(QStringLiteral("수정 내용 저장  (Ctrl+Enter)"));
    cancelEditButton_->setVisible(true);
    setEditingUi(true);
    statusLabel_->setText(
        QStringLiteral("%1번 퀘스트를 수정 중입니다.").arg(task.sequence));
    titleInput_->setFocus();
    titleInput_->selectAll();
}

void TaskQueueWidget::cancelEditing()
{
    const bool wasEditing = editingTaskId_ != 0;
    editingTaskId_ = 0;
    editingViewModel_ = nullptr;
    editingTable_ = nullptr;
    titleInput_->clear();
    detailsInput_->clear();
    externalLinkInput_->clear();
    setImportance(1);
    deadlineInput_->clearDate();
    deadlineClearButton_->setVisible(false);
    addButton_->setText(QStringLiteral("새 퀘스트  (Ctrl+Enter)"));
    cancelEditButton_->setVisible(false);
    setEditingUi(false);
    if (wasEditing)
    {
        statusLabel_->setText(QStringLiteral("퀘스트 수정을 취소했습니다."));
        workTabs_->setCurrentIndex(1);
    }
    else
    {
        titleInput_->setFocus();
    }
}

void TaskQueueWidget::completeSelectedTask()
{
    const int row = selectedPendingRow();
    Task completedTask;
    if (!pendingViewModel_.completeTask(row, &completedTask))
    {
        showViewModelError();
        return;
    }

    if (!completedViewModel_.insertTaskSnapshot(completedTask) &&
        !completedViewModel_.loadCompletedTasks())
    {
        showViewModelError();
        return;
    }

    selectedTable_ = nullptr;
    pendingPreviewTable_->clearSelection();
    pendingFullTable_->clearSelection();
    completedTable_->clearSelection();
    statusLabel_->setText(pendingViewModel_.lastMessage());
    updateSelectedTaskDetails();
    updateUiState();
}

void TaskQueueWidget::reopenSelectedTask()
{
    if (selectedTable_ != completedTable_)
    {
        return;
    }

    const int row = completedTable_->currentIndex().row();
    Task reopenedTask;
    if (!completedViewModel_.reopenTask(row, &reopenedTask))
    {
        showViewModelError();
        return;
    }

    if (!pendingViewModel_.insertTaskSnapshot(reopenedTask) &&
        !pendingViewModel_.loadPendingTasks())
    {
        showViewModelError();
        return;
    }

    selectedTable_ = nullptr;
    pendingPreviewTable_->clearSelection();
    pendingFullTable_->clearSelection();
    completedTable_->clearSelection();
    statusLabel_->setText(completedViewModel_.lastMessage());
    updateSelectedTaskDetails();
    updateUiState();
}

void TaskQueueWidget::duplicateSelectedTask()
{
    const Task sourceTask = selectedTask();
    if (sourceTask.id == 0)
    {
        return;
    }

    const QString copyTitle = uniqueCopyTitle(sourceTask.title);
    if (!pendingViewModel_.addTask(
            copyTitle,
            sourceTask.details,
            sourceTask.externalUrl,
            sourceTask.importance,
            sourceTask.deadline))
    {
        showViewModelError();
        return;
    }

    int copiedRow = -1;
    for (int row = 0; row < pendingViewModel_.taskCount(); ++row)
    {
        if (pendingViewModel_.taskAt(row).title == copyTitle)
        {
            copiedRow = row;
            break;
        }
    }

    queueTabs_->setCurrentIndex(0);
    if (copiedRow >= 0)
    {
        pendingFullTable_->selectRow(copiedRow);
        selectedTable_ = pendingFullTable_;
    }
    statusLabel_->setText(pendingViewModel_.lastMessage());
    updateSelectedTaskDetails();
    updateUiState();
}

void TaskQueueWidget::copySelectedTaskToClipboard()
{
    const Task task = selectedTask();
    if (task.id == 0)
    {
        return;
    }

    QStringList lines;
    lines.append(
        QStringLiteral("퀘스트 %1: %2")
            .arg(task.sequence)
            .arg(task.title));
    lines.append(
        QStringLiteral("상태: %1")
            .arg(task.completed
                     ? QStringLiteral("완료")
                     : QStringLiteral("진행 중")));
    lines.append(
        QStringLiteral("중요도: %1/5").arg(task.importance));
    lines.append(
        QStringLiteral("마감일: %1")
            .arg(task.deadline.isEmpty()
                     ? QStringLiteral("없음")
                     : task.deadline));
    if (!task.details.isEmpty())
    {
        lines.append(QString());
        lines.append(QStringLiteral("상세 설명"));
        lines.append(task.details);
    }
    if (!task.externalUrl.isEmpty())
    {
        lines.append(QString());
        lines.append(
            QStringLiteral("외부 링크: %1").arg(task.externalUrl));
    }

    QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
    statusLabel_->setText(
        QStringLiteral("%1번 퀘스트 정보를 복사했습니다.")
            .arg(task.sequence));
}

void TaskQueueWidget::deleteSelectedTask()
{
    const Task task = selectedTask();
    if (task.id == 0 || selectedTable_ == nullptr)
    {
        return;
    }

    const QMessageBox::StandardButton answer = QMessageBox::warning(
        this,
        QStringLiteral("퀘스트 삭제 확인"),
        QStringLiteral(
            "'%1' 퀘스트를 삭제하시겠습니까?\n"
            "삭제한 퀘스트는 복구할 수 없습니다.")
            .arg(task.title),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (answer != QMessageBox::Yes)
    {
        return;
    }

    // The confirmation dialog runs a nested event loop; resolve the captured ID again.
    const int pendingRow = pendingViewModel_.rowForTaskId(task.id);
    const int completedRow = completedViewModel_.rowForTaskId(task.id);
    if (pendingRow < 0 && completedRow < 0)
    {
        statusLabel_->setText(QStringLiteral("업무가 이미 삭제되었습니다. 목록을 확인하세요."));
        return;
    }
    const bool deletingCompleted = pendingRow < 0;
    const int row = deletingCompleted ? completedRow : pendingRow;
    TaskQueueViewModel& targetViewModel =
        deletingCompleted ? completedViewModel_ : pendingViewModel_;

    if (!targetViewModel.deleteTask(row))
    {
        showViewModelError();
        return;
    }

    if (editingViewModel_ == &targetViewModel &&
        editingTaskId_ == task.id)
    {
        cancelEditing();
    }

    selectedTable_ = nullptr;
    pendingPreviewTable_->clearSelection();
    pendingFullTable_->clearSelection();
    completedTable_->clearSelection();
    statusLabel_->setText(targetViewModel.lastMessage());
    updateSelectedTaskDetails();
    updateUiState();
}

void TaskQueueWidget::showTableContextMenu(
    QTableView* table,
    const QPoint& position)
{
    const QModelIndex index = table->indexAt(position);
    if (!index.isValid())
    {
        return;
    }

    table->selectRow(index.row());
    selectedTable_ = table;
    handleTableSelection(table);

    QMenu menu(this);
    QAction* editAction = menu.addAction(QStringLiteral("수정"));
    QAction* copyAction =
        menu.addAction(QStringLiteral("퀘스트 정보 복사"));
    QAction* duplicateAction =
        menu.addAction(QStringLiteral("새 퀘스트로 복제"));
    QAction* reopenAction = nullptr;
    if (table == completedTable_)
    {
        reopenAction = menu.addAction(QStringLiteral("진행 중으로 복원"));
    }
    QAction* deleteAction = menu.addAction(QStringLiteral("삭제"));

    QAction* selectedAction =
        menu.exec(table->viewport()->mapToGlobal(position));
    if (editAction != nullptr && selectedAction == editAction)
    {
        startEditing();
    }
    else if (selectedAction == copyAction)
    {
        copySelectedTaskToClipboard();
    }
    else if (selectedAction == duplicateAction)
    {
        duplicateSelectedTask();
    }
    else if (reopenAction != nullptr && selectedAction == reopenAction)
    {
        reopenSelectedTask();
    }
    else if (selectedAction == deleteAction)
    {
        deleteSelectedTask();
    }
}

void TaskQueueWidget::openTaskFromFloatingWidget(int row, bool edit)
{
    if (row < 0 || row >= pendingViewModel_.taskCount())
    {
        return;
    }

    showFromTray();
    pendingPreviewTable_->selectRow(row);
    selectedTable_ = pendingPreviewTable_;
    updateSelectedTaskDetails();
    updateUiState();

    if (edit)
    {
        startEditing();
    }
    else
    {
        workTabs_->setCurrentIndex(1);
    }
}

void TaskQueueWidget::addTaskFromFloatingWidget()
{
    showFromTray();
    if (editingTaskId_ != 0)
    {
        cancelEditing();
    }
    workTabs_->setCurrentIndex(0);
    titleInput_->setFocus();
}

void TaskQueueWidget::completeTaskFromFloatingWidget(int row)
{
    if (row < 0 || row >= pendingViewModel_.taskCount())
    {
        return;
    }

    pendingPreviewTable_->selectRow(row);
    selectedTable_ = pendingPreviewTable_;
    completeSelectedTask();
}

void TaskQueueWidget::handleTableSelection(QTableView* source)
{
    if (applyingExternal_) return;
    if (source->selectionModel()->hasSelection())
    {
        selectedTable_ = source;

        const std::array<QTableView*, 3> tables{{
            pendingPreviewTable_,
            pendingFullTable_,
            completedTable_}};
        for (QTableView* table : tables)
        {
            if (table != source)
            {
                const QSignalBlocker blocker(table->selectionModel());
                table->clearSelection();
            }
        }
    }
    else if (selectedTable_ == source)
    {
        selectedTable_ = nullptr;
    }

    updateSelectedTaskDetails();
    updateUiState();
}

void TaskQueueWidget::setImportance(int importance)
{
    importance_ = qBound(1, importance, 5);

    for (int index = 0; index < static_cast<int>(importanceStars_.size()); ++index)
    {
        const bool selected = index < importance_;
        importanceStars_[index]->setText(
            selected ? QStringLiteral("★") : QStringLiteral("☆"));
        importanceStars_[index]->setProperty("starSelected", selected);
        importanceStars_[index]->style()->unpolish(importanceStars_[index]);
        importanceStars_[index]->style()->polish(importanceStars_[index]);
    }
}

void TaskQueueWidget::setEditingUi(bool editing)
{
    if (editing)
    {
        newTaskHost_->layout()->removeWidget(taskFormWidget_);
        selectedEditHost_->layout()->addWidget(taskFormWidget_);
        selectedDisplayWidget_->setVisible(false);
        selectedEditHost_->setVisible(true);
        workTabs_->setTabEnabled(0, false);
        workTabs_->setTabText(1, QStringLiteral("선택 퀘스트 수정"));
        workTabs_->setCurrentIndex(1);
        return;
    }

    selectedEditHost_->layout()->removeWidget(taskFormWidget_);
    newTaskHost_->layout()->addWidget(taskFormWidget_);
    selectedEditHost_->setVisible(false);
    selectedDisplayWidget_->setVisible(true);
    workTabs_->setTabEnabled(0, true);
    workTabs_->setTabText(1, QStringLiteral("퀘스트 상세"));
}

Task TaskQueueWidget::selectedTask() const
{
    if (selectedTable_ == completedTable_)
    {
        return completedViewModel_.taskAt(completedTable_->currentIndex().row());
    }

    const int row = selectedPendingRow();
    return pendingViewModel_.taskAt(row);
}

QString TaskQueueWidget::uniqueCopyTitle(const QString& sourceTitle) const
{
    const auto titleExists = [this](const QString& candidate) {
        const std::array<const TaskQueueViewModel*, 2> models{{
            &pendingViewModel_,
            &completedViewModel_}};
        for (const TaskQueueViewModel* model : models)
        {
            for (int row = 0; row < model->taskCount(); ++row)
            {
                if (model->taskAt(row).title.compare(
                        candidate,
                        Qt::CaseInsensitive) == 0)
                {
                    return true;
                }
            }
        }
        return false;
    };

    const QString baseTitle = sourceTitle.trimmed();
    QString candidate =
        QStringLiteral("%1 (복사본)").arg(baseTitle);
    int copyNumber = 2;
    while (titleExists(candidate))
    {
        candidate =
            QStringLiteral("%1 (복사본 %2)")
                .arg(baseTitle)
                .arg(copyNumber++);
    }
    return candidate;
}

int TaskQueueWidget::selectedPendingRow() const
{
    if (selectedTable_ == pendingPreviewTable_ &&
        pendingPreviewTable_->selectionModel()->hasSelection())
    {
        return pendingPreviewTable_->currentIndex().row();
    }

    if (selectedTable_ == pendingFullTable_ &&
        pendingFullTable_->selectionModel()->hasSelection())
    {
        return pendingFullTable_->currentIndex().row();
    }

    return -1;
}

void TaskQueueWidget::updateSelectedTaskDetails()
{
    if (selectedTable_ == nullptr ||
        !selectedTable_->selectionModel()->hasSelection())
    {
        selectedTitleLabel_->setText(
            QStringLiteral("오른쪽 목록에서 퀘스트를 선택하세요."));
        selectedDetailsLabel_->setText(QStringLiteral("-"));
        selectedMetaLabel_->setText(QStringLiteral("-"));
        selectedExternalUrl_.clear();
        externalLinkButton_->setVisible(false);
        return;
    }

    const Task task = selectedTask();

    if (task.id == 0)
    {
        selectedTitleLabel_->setText(
            QStringLiteral("오른쪽 목록에서 퀘스트를 선택하세요."));
        selectedDetailsLabel_->setText(QStringLiteral("-"));
        selectedMetaLabel_->setText(QStringLiteral("-"));
        selectedExternalUrl_.clear();
        externalLinkButton_->setVisible(false);
        return;
    }

    if (editingTaskId_ == 0)
    {
        workTabs_->setCurrentIndex(1);
    }
    QSettings settings;
    const QString redmineUrl =
        settings.value(AppConstants::RedmineUrlKey).toString();
    selectedTitleLabel_->setText(
        redmineReferenceHtml(task.title, redmineUrl));
    selectedDetailsLabel_->setText(
        task.details.isEmpty()
            ? QStringLiteral("상세 설명이 없습니다.")
            : redmineReferenceHtml(task.details, redmineUrl));

    QString stars;
    for (int index = 1; index <= 5; ++index)
    {
        stars.append(index <= task.importance ? QChar(0x2605) : QChar(0x2606));
        if (index < 5)
        {
            stars.append(QChar(' '));
        }
    }

    const QString deadline =
        task.deadline.isEmpty() ? QStringLiteral("X") : task.deadline;
    const QDateTime relevantTime =
        task.completed ? task.completedAt : task.createdAt;
    const QString timeLabel =
        task.completed ? QStringLiteral("완료 시각") : QStringLiteral("접수 시각");
    const QString timeText =
        relevantTime.isValid()
            ? relevantTime.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
            : QStringLiteral("-");

    selectedMetaLabel_->setText(
        QStringLiteral("중요도  %1\n마감일  %2\n%3  %4")
            .arg(stars)
            .arg(deadline)
            .arg(timeLabel)
            .arg(timeText));

    selectedExternalUrl_ = task.externalUrl;
    if (selectedExternalUrl_.isEmpty())
    {
        externalLinkButton_->setVisible(false);
    }
    else
    {
        const QUrl url(selectedExternalUrl_);
        const QString host = url.host().toLower();
        const QString path = url.path().toLower();
        if (host.contains(QStringLiteral("atlassian")) ||
            path.contains(QStringLiteral("/browse/")))
        {
            externalLinkButton_->setText(QStringLiteral("Jira 이슈 열기"));
        }
        else if (path.contains(QStringLiteral("/issues/")))
        {
            externalLinkButton_->setText(QStringLiteral("Redmine 이슈 열기"));
        }
        else
        {
            externalLinkButton_->setText(QStringLiteral("외부 링크 열기"));
        }
        externalLinkButton_->setToolTip(selectedExternalUrl_);
        externalLinkButton_->setVisible(true);
    }
}

void TaskQueueWidget::updateUiState()
{
    queueCountLabel_->setText(
        QStringLiteral(
            "진행 중인 퀘스트: %1개  ·  위에서 5개를 우선 표시합니다.")
            .arg(pendingViewModel_.taskCount()));

    queueTabs_->setTabText(
        0,
        QStringLiteral("진행 중인 퀘스트 (%1)")
            .arg(pendingViewModel_.taskCount()));
    queueTabs_->setTabText(
        1,
        QStringLiteral("완료한 퀘스트 (%1)")
            .arg(completedViewModel_.taskCount()));

    const QDate today = QDate::currentDate();
    const QDate weekStart = today.addDays(1 - today.dayOfWeek());
    const QDate weekEnd = weekStart.addDays(6);
    int receivedThisWeek = 0;
    int dueThisWeek = 0;
    int overdue = 0;
    int completedThisWeek = 0;

    for (int row = 0; row < pendingViewModel_.taskCount(); ++row)
    {
        const Task task = pendingViewModel_.taskAt(row);
        const QDate createdDate = task.createdAt.date();
        if (createdDate.isValid() &&
            createdDate >= weekStart &&
            createdDate <= weekEnd)
        {
            ++receivedThisWeek;
        }

        const QDate deadline = QDate::fromString(task.deadline, Qt::ISODate);
        if (!deadline.isValid())
        {
            continue;
        }
        if (deadline < today)
        {
            ++overdue;
        }
        else if (deadline >= weekStart && deadline <= weekEnd)
        {
            ++dueThisWeek;
        }
    }

    for (int row = 0; row < completedViewModel_.taskCount(); ++row)
    {
        const Task task = completedViewModel_.taskAt(row);
        const QDate createdDate = task.createdAt.date();
        if (createdDate.isValid() &&
            createdDate >= weekStart &&
            createdDate <= weekEnd)
        {
            ++receivedThisWeek;
        }

        const QDate completedDate = task.completedAt.date();
        if (completedDate.isValid() &&
            completedDate >= weekStart &&
            completedDate <= weekEnd)
        {
            ++completedThisWeek;
        }
    }

    weeklySummaryLabel_->setText(
        QStringLiteral(
            "%1 ~ %2  ·  접수 %3  ·  마감 예정 %4  ·  완료 %5  ·  지연 %6")
            .arg(weekStart.toString(QStringLiteral("MM.dd")))
            .arg(weekEnd.toString(QStringLiteral("MM.dd")))
            .arg(receivedThisWeek)
            .arg(dueThisWeek)
            .arg(completedThisWeek)
            .arg(overdue));

    const bool hasPendingSelection = selectedPendingRow() >= 0;
    const bool hasCompletedSelection =
        selectedTable_ == completedTable_ &&
        completedTable_->selectionModel()->hasSelection();
    const bool hasAnySelection =
        selectedTable_ != nullptr &&
        selectedTable_->selectionModel()->hasSelection();
    completeButton_->setText(
        hasCompletedSelection
            ? QStringLiteral("진행 중으로 복원")
            : QStringLiteral("선택 퀘스트 완료"));
    completeButton_->setEnabled(hasPendingSelection || hasCompletedSelection);
    editButton_->setVisible(hasAnySelection);
    editButton_->setEnabled(hasAnySelection);
}

QString TaskQueueWidget::summaryText(
    const QDate& rangeStart,
    const QDate& rangeEnd) const
{
    const QDate today = QDate::currentDate();
    QStringList received;
    QStringList inProgress;
    QStringList due;
    QStringList overdue;
    QStringList completed;

    for (int row = 0; row < pendingViewModel_.taskCount(); ++row)
    {
        const Task task = pendingViewModel_.taskAt(row);
        inProgress.append(QStringLiteral("- %1").arg(task.title));

        const QDate createdDate = task.createdAt.date();
        if (createdDate.isValid() &&
            createdDate >= rangeStart &&
            createdDate <= rangeEnd)
        {
            received.append(QStringLiteral("- %1").arg(task.title));
        }

        const QDate deadline = QDate::fromString(task.deadline, Qt::ISODate);
        if (!deadline.isValid() ||
            deadline < rangeStart ||
            deadline > rangeEnd)
        {
            continue;
        }
        if (deadline < today)
        {
            overdue.append(
                QStringLiteral("- %1 (%2일 지연)")
                    .arg(task.title)
                    .arg(deadline.daysTo(today)));
        }
        else
        {
            const int daysRemaining = today.daysTo(deadline);
            const QString countdown =
                daysRemaining == 0
                    ? QStringLiteral("D-DAY")
                    : QStringLiteral("D-%1").arg(daysRemaining);
            due.append(
                QStringLiteral("- %1 (%2)")
                    .arg(task.title)
                    .arg(countdown));
        }
    }

    for (int row = 0; row < completedViewModel_.taskCount(); ++row)
    {
        const Task task = completedViewModel_.taskAt(row);
        const QDate createdDate = task.createdAt.date();
        if (createdDate.isValid() &&
            createdDate >= rangeStart &&
            createdDate <= rangeEnd)
        {
            received.append(QStringLiteral("- %1").arg(task.title));
        }

        const QDate completedDate = task.completedAt.date();
        if (completedDate.isValid() &&
            completedDate >= rangeStart &&
            completedDate <= rangeEnd)
        {
            completed.append(
                QStringLiteral("- %1 (%2)")
                    .arg(task.title)
                    .arg(completedDate.toString(QStringLiteral("MM.dd"))));
        }
    }

    const auto sectionText = [](const QString& title, const QStringList& items) {
        return QStringLiteral("%1 (%2)\n%3")
            .arg(title)
            .arg(items.size())
            .arg(items.isEmpty() ? QStringLiteral("- 없음") : items.join('\n'));
    };

    return QStringLiteral(
        "퀘스트 요약 (%1 ~ %2)\n\n%3\n\n%4\n\n%5\n\n%6\n\n%7")
        .arg(rangeStart.toString(QStringLiteral("yyyy.MM.dd")))
        .arg(rangeEnd.toString(QStringLiteral("yyyy.MM.dd")))
        .arg(sectionText(QStringLiteral("[접수]"), received))
        .arg(sectionText(QStringLiteral("[현재 진행 중]"), inProgress))
        .arg(sectionText(QStringLiteral("[마감 예정]"), due))
        .arg(sectionText(QStringLiteral("[지연 퀘스트]"), overdue))
        .arg(sectionText(QStringLiteral("[완료]"), completed));
}

void TaskQueueWidget::showWeeklySummaryList()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("퀘스트 요약"));
    dialog.resize(680, 600);

    const QDate today = QDate::currentDate();
    const QDate thisWeekStart = today.addDays(1 - today.dayOfWeek());

    auto* periodCombo = new QComboBox(&dialog);
    periodCombo->addItem(QStringLiteral("이번 주"));
    periodCombo->addItem(QStringLiteral("지난 주"));
    periodCombo->addItem(QStringLiteral("이번 달"));
    periodCombo->addItem(QStringLiteral("직접 설정"));

    auto* startDateEdit = new QDateEdit(&dialog);
    auto* endDateEdit = new QDateEdit(&dialog);
    for (QDateEdit* dateEdit :
         std::array<QDateEdit*, 2>{{startDateEdit, endDateEdit}})
    {
        dateEdit->setCalendarPopup(true);
        dateEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    }
    startDateEdit->setDate(thisWeekStart);
    endDateEdit->setDate(thisWeekStart.addDays(6));

    auto* periodLayout = new QHBoxLayout();
    periodLayout->setContentsMargins(0, 0, 0, 0);
    periodLayout->setSpacing(8);
    periodLayout->addWidget(new QLabel(QStringLiteral("요약 기간"), &dialog));
    periodLayout->addWidget(periodCombo);
    periodLayout->addSpacing(8);
    periodLayout->addWidget(new QLabel(QStringLiteral("시작"), &dialog));
    periodLayout->addWidget(startDateEdit);
    periodLayout->addWidget(new QLabel(QStringLiteral("종료"), &dialog));
    periodLayout->addWidget(endDateEdit);
    periodLayout->addStretch();

    auto* summaryEdit = new QPlainTextEdit(&dialog);
    summaryEdit->setReadOnly(true);
    summaryEdit->setTextInteractionFlags(
        Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);

    auto* copyButton = new QPushButton(QStringLiteral("전체 복사"), &dialog);
    copyButton->setObjectName(QStringLiteral("primaryButton"));
    auto* closeButton = new QPushButton(QStringLiteral("닫기"), &dialog);

    auto* actionLayout = new QHBoxLayout();
    actionLayout->addStretch();
    actionLayout->addWidget(copyButton);
    actionLayout->addWidget(closeButton);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);
    layout->addLayout(periodLayout);
    layout->addWidget(summaryEdit, 1);
    layout->addLayout(actionLayout);

    const auto refreshSummary =
        [this, startDateEdit, endDateEdit, summaryEdit]() {
            summaryEdit->setPlainText(
                summaryText(startDateEdit->date(), endDateEdit->date()));
        };
    for (auto* model : {&pendingViewModel_, &completedViewModel_})
    {
        connect(model, &QAbstractItemModel::modelReset, &dialog, refreshSummary);
        connect(model, &QAbstractItemModel::rowsInserted, &dialog, refreshSummary);
        connect(model, &QAbstractItemModel::rowsRemoved, &dialog, refreshSummary);
        connect(model, &QAbstractItemModel::dataChanged, &dialog, refreshSummary);
    }
    const auto applyPreset =
        [periodCombo,
         startDateEdit,
         endDateEdit,
         today,
         thisWeekStart,
         refreshSummary]() {
            QDate start = startDateEdit->date();
            QDate end = endDateEdit->date();
            switch (periodCombo->currentIndex())
            {
            case 0:
                start = thisWeekStart;
                end = thisWeekStart.addDays(6);
                break;
            case 1:
                start = thisWeekStart.addDays(-7);
                end = thisWeekStart.addDays(-1);
                break;
            case 2:
                start = QDate(today.year(), today.month(), 1);
                end = start.addMonths(1).addDays(-1);
                break;
            default:
                refreshSummary();
                return;
            }

            const QSignalBlocker startBlocker(startDateEdit);
            const QSignalBlocker endBlocker(endDateEdit);
            startDateEdit->setDate(start);
            endDateEdit->setDate(end);
            refreshSummary();
        };

    connect(
        periodCombo,
        static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
        &dialog,
        [applyPreset](int) { applyPreset(); });
    connect(
        startDateEdit,
        &QDateEdit::dateChanged,
        &dialog,
        [periodCombo, startDateEdit, endDateEdit, refreshSummary](
            const QDate& date) {
            if (date > endDateEdit->date())
            {
                const QSignalBlocker blocker(endDateEdit);
                endDateEdit->setDate(date);
            }
            const QSignalBlocker blocker(periodCombo);
            periodCombo->setCurrentIndex(3);
            refreshSummary();
        });
    connect(
        endDateEdit,
        &QDateEdit::dateChanged,
        &dialog,
        [periodCombo, startDateEdit, endDateEdit, refreshSummary](
            const QDate& date) {
            if (date < startDateEdit->date())
            {
                const QSignalBlocker blocker(startDateEdit);
                startDateEdit->setDate(date);
            }
            const QSignalBlocker blocker(periodCombo);
            periodCombo->setCurrentIndex(3);
            refreshSummary();
        });
    connect(copyButton, &QPushButton::clicked, &dialog, [summaryEdit]() {
        QApplication::clipboard()->setText(summaryEdit->toPlainText());
    });
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);

    refreshSummary();
    dialog.exec();
}

void TaskQueueWidget::showViewModelError()
{
    QMessageBox::warning(
        this,
        QStringLiteral("퀘스트 처리 실패"),
        !pendingViewModel_.lastError().isEmpty()
            ? pendingViewModel_.lastError()
            : completedViewModel_.lastError());
}
