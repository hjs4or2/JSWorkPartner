#include "AppConstants.h"

namespace AppConstants
{
const QString ApplicationName = QStringLiteral("JSWorkManager");
const QString OrganizationName = QStringLiteral("JSWorkManager");
// 마지막 자리는 1.0.0 릴리스 이후의 리비전 번호입니다.
const QString ApplicationVersion = QStringLiteral("1.0.0.1");
const QString DatabaseFileName = QStringLiteral("work_manager.db");

const QString MainWindowGeometryKey =
    QStringLiteral("mainWindow/geometry");
const QString PendingSortColumnKey =
    QStringLiteral("taskTable/pendingSortColumn");
const QString PendingSortOrderKey =
    QStringLiteral("taskTable/pendingSortOrder");
const QString CompletedSortColumnKey =
    QStringLiteral("taskTable/completedSortColumn");
const QString CompletedSortOrderKey =
    QStringLiteral("taskTable/completedSortOrder");
const QString FloatingQueueGeometryKey =
    QStringLiteral("floatingQueue/geometry");
const QString FloatingQueueOpacityKey =
    QStringLiteral("floatingQueue/opacity");
const QString FloatingQueueVisibleKey =
    QStringLiteral("floatingQueue/visible");
const QString RedmineUrlKey = QStringLiteral("redmine/baseUrl");
const QString LegacyRedmineApiKeyKey =
    QStringLiteral("redmine/apiKeyProtected");
const QString LegacyRedmineProjectIdKey =
    QStringLiteral("redmine/projectId");
const QString LegacyRedmineProjectNameKey =
    QStringLiteral("redmine/projectName");

const wchar_t* const SingleInstanceMutexName =
    L"Local\\JSWorkManager.SingleInstance.0A88D32A";
}
