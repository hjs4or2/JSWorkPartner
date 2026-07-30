#include <QtWidgets/QApplication>
#include <QtWidgets/QMessageBox>

#include <Windows.h>

#include "TaskQueueViewModel.h"
#include "TaskQueueWidget.h"
#include "TaskRepository.h"
#include "StyleManager.h"

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("JSWorkManager"));
    QCoreApplication::setApplicationName(QStringLiteral("JSWorkManager"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.0.1.2"));
    QApplication::setApplicationDisplayName(QStringLiteral("JSWorkManager"));

    const HANDLE instanceMutex = CreateMutexW(
        nullptr,
        FALSE,
        L"Local\\JSWorkManager.SingleInstance.0A88D32A");
    if (instanceMutex == nullptr)
    {
        QMessageBox::critical(
            nullptr,
            QStringLiteral("프로그램 시작 실패"),
            QStringLiteral("중복 실행 확인용 Mutex를 만들지 못했습니다."));
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(instanceMutex);
        QMessageBox::information(
            nullptr,
            QStringLiteral("JSWorkManager"),
            QStringLiteral("JSWorkManager가 이미 실행 중입니다.\n"
                           "시스템 트레이를 확인해 주세요."));
        return 0;
    }

    StyleManager::instance().apply(application);

    TaskRepository repository;
    if (!repository.initialize())
    {
        QMessageBox::critical(
            nullptr,
            QStringLiteral("프로그램 시작 실패"),
            repository.lastError());
        CloseHandle(instanceMutex);
        return 1;
    }

    TaskQueueViewModel pendingViewModel(repository);
    TaskQueueViewModel completedViewModel(repository);
    if (!pendingViewModel.loadPendingTasks() ||
        !completedViewModel.loadCompletedTasks())
    {
        QMessageBox::critical(
            nullptr,
            QStringLiteral("퀘스트 로드 실패"),
            !pendingViewModel.lastError().isEmpty()
                ? pendingViewModel.lastError()
                : completedViewModel.lastError());
        CloseHandle(instanceMutex);
        return 1;
    }

    TaskQueueWidget window(pendingViewModel, completedViewModel);
    window.showInitialView();

    const int exitCode = application.exec();
    CloseHandle(instanceMutex);
    return exitCode;
}
