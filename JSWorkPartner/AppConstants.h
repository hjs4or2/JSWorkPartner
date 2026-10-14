#pragma once

#include <QtCore/QString>
#include <QtCore/QVariant>

namespace AppConstants
{
extern const QString ApplicationName;
extern const QString OrganizationName;
extern const QString LegacyApplicationName;
extern const QString LegacyOrganizationName;
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
extern const QString PetPositionKey;
extern const QString PetVisibleKey;
extern const QString PetSizePercentKey;
extern const QString PetDesignIdKey;
extern const QString PetAlwaysOnTopKey;
extern const QString PetNameKey;
extern const QString PetDefaultName;
constexpr int PetNameMaximumLength = 20;
QString normalizedPetName(const QString& value);
QString petNameFromSettings(const QVariant& value);
extern const QString PetMcpExecutableKey;
extern const QString RedmineUrlKey;
extern const QString LegacyRedmineApiKeyKey;
extern const QString LegacyRedmineProjectIdKey;
extern const QString LegacyRedmineProjectNameKey;

extern const wchar_t* const SingleInstanceMutexName;
}
