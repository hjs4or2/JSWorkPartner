#pragma once

#include <QtCore/QString>

namespace AppConstants
{
extern const QString ApplicationName;
extern const QString OrganizationName;
extern const QString ApplicationVersion;
extern const QString DatabaseFileName;

extern const QString MainWindowGeometryKey;
extern const QString PendingSortColumnKey;
extern const QString PendingSortOrderKey;
extern const QString CompletedSortColumnKey;
extern const QString CompletedSortOrderKey;
extern const QString FloatingQueueGeometryKey;
extern const QString FloatingQueueOpacityKey;
extern const QString FloatingQueueVisibleKey;
extern const QString RedmineUrlKey;
extern const QString LegacyRedmineApiKeyKey;
extern const QString LegacyRedmineProjectIdKey;
extern const QString LegacyRedmineProjectNameKey;

extern const wchar_t* const SingleInstanceMutexName;
}
