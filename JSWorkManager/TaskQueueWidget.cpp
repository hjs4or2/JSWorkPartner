#include "TaskQueueWidget.h"

#include "NullableDateEdit.h"
#include "QueueFloatingWidget.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDate>
#include <QtCore/QEvent>
#include <QtCore/QSettings>
#include <QtCore/QSignalBlocker>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtGui/QClipboard>
#include <QtGui/QCloseEvent>
#include <QtGui/QDesktopServices>
#include <QtGui/QFont>
#include <QtGui/QKeySequence>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCalendarWidget>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDateEdit>
#include <QtWidgets/QDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QMenu>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QShortcut>
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
#include "Task.h"

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
    externalLinkInput_->setPlaceholderText(QString());
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
            "새 퀘스트는 중요도 1, 마감일 X로 저장됩니다.\n"
            "목록에서 퀘스트를 선택한 뒤 완료 버튼을 누르세요."));

    cancelEditButton_ = new QPushButton(QStringLiteral("수정 취소"), this);
    cancelEditButton_->setVisible(false);

    queueCountLabel_ = new QLabel(this);

    pendingPreviewTable_ = new QTableView(this);
    pendingFullTable_ = new QTableView(this);
    completedTable_ = new QTableView(this);
    pendingPreviewTable_->setModel(&pendingViewModel_);
    pendingFullTable_->setModel(&pendingViewModel_);
    completedTable_->setModel(&completedViewModel_);

    const std::array<QTableView*, 3> tables{{
        pendingPreviewTable_,
        pendingFullTable_,
        completedTable_}};
    for (QTableView* table : tables)
    {
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setContextMenuPolicy(Qt::CustomContextMenu);
        table->setAlternatingRowColors(true);
        table->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
        table->verticalHeader()->setVisible(false);
        table->horizontalHeader()->setSectionResizeMode(
            TaskQueueViewModel::SequenceColumn,
            QHeaderView::ResizeToContents);
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

    auto* databasePathLabel = new QLabel(
        QStringLiteral("저장 DB: %1").arg(pendingViewModel_.databasePath()),
        this);
    databasePathLabel->setWordWrap(true);
    databasePathLabel->setToolTip(pendingViewModel_.databasePath());

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

    selectedDetailsLabel_ = new QLabel(QStringLiteral("-"), selectedDisplayWidget_);
    selectedDetailsLabel_->setWordWrap(true);
    selectedDetailsLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
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
    workTabs_->addTab(selectedTaskPage, QStringLiteral("선택 퀘스트 상세"));
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
    pendingQueueGroupLayout->addWidget(statusLabel_);
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

    weeklySummaryLayout->addLayout(weeklySummaryHeaderLayout);
    weeklySummaryLayout->addWidget(weeklySummaryLabel_);

    rightLayout->addWidget(weeklySummaryCard);
    rightLayout->addWidget(queueTabsTitleLabel);
    rightLayout->addWidget(queueTabs_, 1);
    rightLayout->addWidget(databasePathLabel);

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
        completeSelectedTask();
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
    }

    auto* addShortcut = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_Return), this);
    connect(addShortcut, &QShortcut::activated, this, [this]() { addTask(); });

    auto* addNumpadShortcut = new QShortcut(
        QKeySequence(Qt::CTRL + Qt::Key_Enter),
        this);
    connect(addNumpadShortcut, &QShortcut::activated, this, [this]() { addTask(); });

    auto* deadlineRefreshTimer = new QTimer(this);
    deadlineRefreshTimer->setInterval(60 * 1000);
    connect(deadlineRefreshTimer, &QTimer::timeout, this, [this]() {
        pendingViewModel_.refreshDeadlineIndicators();
        updateUiState();
    });
    deadlineRefreshTimer->start();

    setupTrayIcon();
    updateUiState();
    updateSelectedTaskDetails();
    titleInput_->setFocus();
}

TaskQueueWidget::~TaskQueueWidget()
{
    if (floatingQueueWidget_ != nullptr)
    {
        QSettings settings;
        settings.setValue(
            QStringLiteral("floatingQueue/geometry"),
            floatingQueueWidget_->saveGeometry());
        delete floatingQueueWidget_;
        floatingQueueWidget_ = nullptr;
    }
}

void TaskQueueWidget::closeEvent(QCloseEvent* event)
{
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
    const QByteArray savedGeometry =
        settings.value(QStringLiteral("floatingQueue/geometry")).toByteArray();
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
            QStringLiteral("floatingQueue/visible"),
            visible);
        if (!visible && floatingQueueWidget_ != nullptr)
        {
            stateSettings.setValue(
                QStringLiteral("floatingQueue/geometry"),
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

    if (settings.value(
            QStringLiteral("floatingQueue/visible"),
            false).toBool())
    {
        floatingQueueAction_->setChecked(true);
    }

    trayIcon_->show();
}

void TaskQueueWidget::showSettingsDialog()
{
    const StyleManager::Theme originalTheme =
        StyleManager::instance().currentTheme();

    QDialog dialog(this);
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
    layout->addSpacing(8);
    layout->addLayout(themeLayout);
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
    connect(saveButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, &dialog, [&dialog]() {
        dialog.reject();
    });

    if (dialog.exec() != QDialog::Accepted)
    {
        StyleManager::instance().setTheme(*qApp, originalTheme);
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
    const bool wasEditing = editingRow_ >= 0 && editingViewModel_ != nullptr;
    const int editedRow = editingRow_;
    QTableView* const editedTable = selectedTable_;
    TaskQueueViewModel* const savedViewModel =
        wasEditing ? editingViewModel_ : &pendingViewModel_;
    const QDate selectedDeadline = deadlineInput_->nullableDate();
    const QString deadline =
        selectedDeadline.isValid()
            ? selectedDeadline.toString(Qt::ISODate)
            : QString();

    const bool saved =
        editingRow_ >= 0 && editingViewModel_ != nullptr
            ? editingViewModel_->updateTask(
                  editingRow_,
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
    editingRow_ = -1;
    editingViewModel_ = nullptr;
    addButton_->setText(QStringLiteral("새 퀘스트  (Ctrl+Enter)"));
    cancelEditButton_->setVisible(false);
    statusLabel_->setText(savedViewModel->lastMessage());
    if (wasEditing)
    {
        setEditingUi(false);
    }
    if (wasEditing && editedTable != nullptr && editedRow >= 0)
    {
        selectedTable_ = editedTable;
        editedTable->selectRow(editedRow);
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

    editingRow_ = row;
    editingViewModel_ = targetViewModel;
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
    const bool wasEditing = editingRow_ >= 0;
    editingRow_ = -1;
    editingViewModel_ = nullptr;
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
    if (!pendingViewModel_.completeTask(row))
    {
        showViewModelError();
        return;
    }

    if (!completedViewModel_.loadCompletedTasks())
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

    const bool deletingCompleted = selectedTable_ == completedTable_;
    const int row = deletingCompleted
                        ? completedTable_->currentIndex().row()
                        : selectedPendingRow();
    TaskQueueViewModel& targetViewModel =
        deletingCompleted ? completedViewModel_ : pendingViewModel_;

    if (!targetViewModel.deleteTask(row))
    {
        showViewModelError();
        return;
    }

    if (editingViewModel_ == &targetViewModel && editingRow_ == row)
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
    QAction* deleteAction = menu.addAction(QStringLiteral("삭제"));

    QAction* selectedAction =
        menu.exec(table->viewport()->mapToGlobal(position));
    if (editAction != nullptr && selectedAction == editAction)
    {
        startEditing();
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
    if (editingRow_ >= 0)
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
    workTabs_->setTabText(1, QStringLiteral("선택 퀘스트 상세"));
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

    if (editingRow_ < 0)
    {
        workTabs_->setCurrentIndex(1);
    }
    selectedTitleLabel_->setText(task.title);
    selectedDetailsLabel_->setText(
        task.details.isEmpty()
            ? QStringLiteral("상세 설명이 없습니다.")
            : task.details);

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
            "%1 ~ %2  ·  접수 %3  ·  마감 %4  ·  완료 %5  ·  지연 %6")
            .arg(weekStart.toString(QStringLiteral("MM.dd")))
            .arg(weekEnd.toString(QStringLiteral("MM.dd")))
            .arg(receivedThisWeek)
            .arg(dueThisWeek)
            .arg(completedThisWeek)
            .arg(overdue));

    const bool hasPendingSelection = selectedPendingRow() >= 0;
    const bool hasAnySelection =
        selectedTable_ != nullptr &&
        selectedTable_->selectionModel()->hasSelection();
    completeButton_->setEnabled(hasPendingSelection);
    editButton_->setVisible(hasAnySelection);
    editButton_->setEnabled(hasAnySelection);
}

QString TaskQueueWidget::summaryText(
    const QDate& rangeStart,
    const QDate& rangeEnd) const
{
    const QDate today = QDate::currentDate();
    QStringList received;
    QStringList due;
    QStringList overdue;
    QStringList completed;

    for (int row = 0; row < pendingViewModel_.taskCount(); ++row)
    {
        const Task task = pendingViewModel_.taskAt(row);
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
            due.append(
                QStringLiteral("- %1 (%2)")
                    .arg(task.title)
                    .arg(deadline.toString(QStringLiteral("MM.dd"))));
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
        "퀘스트 요약 (%1 ~ %2)\n\n%3\n\n%4\n\n%5\n\n%6")
        .arg(rangeStart.toString(QStringLiteral("yyyy.MM.dd")))
        .arg(rangeEnd.toString(QStringLiteral("yyyy.MM.dd")))
        .arg(sectionText(QStringLiteral("[접수]"), received))
        .arg(sectionText(QStringLiteral("[마감]"), due))
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
