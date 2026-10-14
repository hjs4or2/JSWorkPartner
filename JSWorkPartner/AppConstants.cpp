#include "AppConstants.h"

#include "GeneratedVersion.h"

namespace AppConstants
{
const QString ApplicationName = QStringLiteral("JSWorkPartner");
const QString OrganizationName = QStringLiteral("JSWorkPartner");
// Keep the existing data and settings identifiers for migration compatibility.
const QString LegacyApplicationName = QStringLiteral("JSWorkManager");
const QString LegacyOrganizationName = QStringLiteral("JSWorkManager");
const QString ApplicationVersion = QStringLiteral(JSWORKPARTNER_VERSION);
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
const QString PetPositionKey = QStringLiteral("desktopPet/position");
const QString PetVisibleKey = QStringLiteral("desktopPet/visible");
const QString PetSizePercentKey = QStringLiteral("desktopPet/sizePercent");
const QString PetDesignIdKey = QStringLiteral("desktopPet/designId");
const QString PetAlwaysOnTopKey = QStringLiteral("desktopPet/alwaysOnTop");
const QString PetNameKey = QStringLiteral("desktopPet/name");
const QString PetDefaultName = QStringLiteral("모아");
const QString PetMcpExecutableKey = QStringLiteral("desktopPet/mcpExecutable");

QString normalizedPetName(const QString& value)
{
    for (int index = 0; index < value.size(); ++index)
    {
        const QChar character = value.at(index);
        if (character.isHighSurrogate())
        {
            if (index + 1 >= value.size() || !value.at(index + 1).isLowSurrogate())
                return PetDefaultName;
            ++index;
            continue;
        }
        if (character.isLowSurrogate())
            return PetDefaultName;

        const QChar::Category category = character.category();
        if (category == QChar::Other_Control ||
            category == QChar::Separator_Line ||
            category == QChar::Separator_Paragraph)
            return PetDefaultName;
    }

    const QString normalized = value.trimmed();
    if (normalized.isEmpty() || normalized.size() > PetNameMaximumLength)
        return PetDefaultName;
    return normalized;
}

QString petNameFromSettings(const QVariant& value)
{
    if (!value.isValid() || value.type() != QVariant::String)
        return PetDefaultName;
    return normalizedPetName(value.toString());
}
const QString RedmineUrlKey = QStringLiteral("redmine/baseUrl");
const QString LegacyRedmineApiKeyKey =
    QStringLiteral("redmine/apiKeyProtected");
const QString LegacyRedmineProjectIdKey =
    QStringLiteral("redmine/projectId");
const QString LegacyRedmineProjectNameKey =
    QStringLiteral("redmine/projectName");

// Share the mutex with the previous product to prevent concurrent C++ instances.
const wchar_t* const SingleInstanceMutexName =
    L"Local\\JSWorkManager.SingleInstance.0A88D32A";
}
