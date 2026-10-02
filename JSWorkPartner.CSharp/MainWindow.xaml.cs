using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using JSWorkPartner.CSharp.Data;
using JSWorkPartner.CSharp.Models;
using JSWorkPartner.CSharp.Services;

namespace JSWorkPartner.CSharp;

public partial class MainWindow : Window
{
    private readonly TaskRepository _repository = new();
    private readonly TaskService _service;
    private readonly ObservableCollection<TaskItem> _pending = [];
    private readonly ObservableCollection<TaskItem> _completed = [];
    private long? _editingId;

    public MainWindow()
    {
        InitializeComponent();
        _service = new TaskService(_repository);
        PendingGrid.ItemsSource = _pending;
        CompletedGrid.ItemsSource = _completed;
        Loaded += MainWindow_Loaded;
    }

    private void MainWindow_Loaded(object sender, RoutedEventArgs e)
    {
        TryRun(() =>
        {
            _repository.Initialize();
            DatabasePathText.Text = _repository.DatabasePath;
            RefreshTasks();
            TitleBox.Focus();
        }, closeOnFailure: true);
    }

    private void RefreshTasks()
    {
        Replace(_pending, _service.LoadPending());
        Replace(_completed, _service.LoadCompleted());
        PendingTab.Header = $"진행 중 ({_pending.Count})";
        CompletedTab.Header = $"완료 ({_completed.Count})";
        SummaryText.Text = $"진행 중 {_pending.Count}개 · 완료 {_completed.Count}개";
        UpdateSelectionState();
    }

    private void Save_Click(object sender, RoutedEventArgs e)
    {
        TryRun(() =>
        {
            var importance = int.Parse(((ComboBoxItem)ImportanceBox.SelectedItem).Tag.ToString()!);
            var deadline = HasDeadlineBox.IsChecked == true ? DeadlinePicker.SelectedDate : null;
            if (_editingId is long id)
            {
                _service.Update(id, TitleBox.Text, DetailsBox.Text, ExternalUrlBox.Text,
                    importance, deadline);
            }
            else
            {
                _service.Add(TitleBox.Text, DetailsBox.Text, ExternalUrlBox.Text,
                    importance, deadline);
            }
            ResetForm();
            RefreshTasks();
        });
    }

    private void Edit_Click(object sender, RoutedEventArgs e)
    {
        var task = SelectedTask();
        if (task is null)
        {
            ShowWarning("수정할 퀘스트를 선택해 주세요.");
            return;
        }
        _editingId = task.Id;
        FormHeading.Text = "퀘스트 수정";
        SaveButton.Content = "수정 저장";
        CancelEditButton.Visibility = Visibility.Visible;
        TitleBox.Text = task.Title;
        DetailsBox.Text = task.Details;
        ExternalUrlBox.Text = task.ExternalUrl ?? string.Empty;
        ImportanceBox.SelectedIndex = Math.Clamp(task.Importance - 1, 0, 4);
        HasDeadlineBox.IsChecked = !string.IsNullOrWhiteSpace(task.Deadline);
        DeadlinePicker.SelectedDate = DateTime.TryParse(task.Deadline, out var date) ? date : null;
        TitleBox.Focus();
        TitleBox.SelectAll();
    }

    private void CancelEdit_Click(object sender, RoutedEventArgs e) => ResetForm();

    private void Transition_Click(object sender, RoutedEventArgs e)
    {
        var task = SelectedTask();
        if (task is null)
        {
            ShowWarning("상태를 변경할 퀘스트를 선택해 주세요.");
            return;
        }
        TryRun(() =>
        {
            if (task.Completed) _service.Reopen(task.Id);
            else _service.Complete(task.Id);
            ResetForm();
            RefreshTasks();
        });
    }

    private void Delete_Click(object sender, RoutedEventArgs e)
    {
        var task = SelectedTask();
        if (task is null)
        {
            ShowWarning("삭제할 퀘스트를 선택해 주세요.");
            return;
        }
        if (MessageBox.Show($"'{task.Title}' 퀘스트를 삭제할까요?", "삭제 확인",
                MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes) return;
        TryRun(() =>
        {
            _service.Delete(task.Id);
            if (_editingId == task.Id) ResetForm();
            RefreshTasks();
        });
    }

    private void Duplicate_Click(object sender, RoutedEventArgs e)
    {
        var task = SelectedTask();
        if (task is null)
        {
            ShowWarning("복제할 퀘스트를 선택해 주세요.");
            return;
        }
        TryRun(() =>
        {
            var title = UniqueCopyTitle(task.Title);
            var deadline = DateTime.TryParse(task.Deadline, out var date) ? date : (DateTime?)null;
            _service.Add(title, task.Details, task.ExternalUrl, task.Importance, deadline);
            RefreshTasks();
        });
    }

    private string UniqueCopyTitle(string source)
    {
        var existing = _pending.Concat(_completed)
            .Select(task => task.Title).ToHashSet(StringComparer.OrdinalIgnoreCase);
        var candidate = $"{source.Trim()} (복사본)";
        for (var number = 2; existing.Contains(candidate); number++)
        {
            candidate = $"{source.Trim()} (복사본 {number})";
        }
        return candidate;
    }

    private void OpenLink_Click(object sender, RoutedEventArgs e)
    {
        var url = SelectedTask()?.ExternalUrl;
        if (string.IsNullOrWhiteSpace(url))
        {
            ShowWarning("선택한 퀘스트에 외부 링크가 없습니다.");
            return;
        }
        TryRun(() => Process.Start(new ProcessStartInfo(url) { UseShellExecute = true }));
    }

    private void OpenDataFolder_Click(object sender, RoutedEventArgs e) =>
        TryRun(() => Process.Start(new ProcessStartInfo
        {
            FileName = Path.GetDirectoryName(_repository.DatabasePath)!,
            UseShellExecute = true
        }));

    private void Refresh_Click(object sender, RoutedEventArgs e) => TryRun(RefreshTasks);

    private void DeadlineToggleChanged(object sender, RoutedEventArgs e)
    {
        if (DeadlinePicker is null) return;
        DeadlinePicker.IsEnabled = HasDeadlineBox.IsChecked == true;
        if (DeadlinePicker.IsEnabled && DeadlinePicker.SelectedDate is null)
        {
            DeadlinePicker.SelectedDate = DateTime.Today;
        }
    }

    private void Grid_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (sender == PendingGrid && PendingGrid.SelectedItem is not null)
            CompletedGrid.SelectedItem = null;
        else if (sender == CompletedGrid && CompletedGrid.SelectedItem is not null)
            PendingGrid.SelectedItem = null;
        UpdateSelectionState();
    }

    private void TaskTabs_SelectionChanged(object sender, SelectionChangedEventArgs e) =>
        UpdateSelectionState();

    private TaskItem? SelectedTask() =>
        PendingGrid.SelectedItem as TaskItem ?? CompletedGrid.SelectedItem as TaskItem;

    private void UpdateSelectionState()
    {
        if (SelectedTask() is not { } task)
        {
            SelectedTaskText.Text = "퀘스트를 선택하세요.";
            TransitionButton.Content = TaskTabs.SelectedItem == CompletedTab ? "진행 중으로 복원" : "완료";
            return;
        }
        SelectedTaskText.Text = $"#{task.Sequence}  {task.Title}";
        TransitionButton.Content = task.Completed ? "진행 중으로 복원" : "완료";
    }

    private void ResetForm()
    {
        _editingId = null;
        FormHeading.Text = "새 퀘스트";
        SaveButton.Content = "저장";
        CancelEditButton.Visibility = Visibility.Collapsed;
        TitleBox.Clear();
        DetailsBox.Clear();
        ExternalUrlBox.Clear();
        ImportanceBox.SelectedIndex = 0;
        HasDeadlineBox.IsChecked = false;
        DeadlinePicker.SelectedDate = null;
        TitleBox.Focus();
    }

    private void TryRun(Action action, bool closeOnFailure = false)
    {
        try { action(); }
        catch (Exception exception)
        {
            MessageBox.Show(exception.Message, "퀘스트 처리 실패",
                MessageBoxButton.OK, MessageBoxImage.Error);
            if (closeOnFailure) Close();
        }
    }

    private static void ShowWarning(string message) =>
        MessageBox.Show(message, "JSWorkPartner", MessageBoxButton.OK, MessageBoxImage.Information);

    private static void Replace(ObservableCollection<TaskItem> target,
        IEnumerable<TaskItem> source)
    {
        target.Clear();
        foreach (var item in source) target.Add(item);
    }
}
