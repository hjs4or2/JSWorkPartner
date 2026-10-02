using JSWorkPartner.CSharp.Data;
using JSWorkPartner.CSharp.Services;

var testDirectory = Path.Combine(Path.GetTempPath(), $"JSWorkPartner.CSharp.Tests.{Guid.NewGuid():N}");
Directory.CreateDirectory(testDirectory);

try
{
    var repository = new TaskRepository(Path.Combine(testDirectory, "test.db"));
    repository.Initialize();
    var service = new TaskService(repository);

    var created = service.Add("  첫 퀘스트  ", "  설명  ", "example.com/issues/1", 3,
        new DateTime(2030, 1, 2));
    Assert(created.Title == "첫 퀘스트", "제목 정규화");
    Assert(created.ExternalUrl == "https://example.com/issues/1", "URL 정규화");
    Assert(service.LoadPending().Count == 1, "등록 및 진행 목록 조회");

    AssertThrows(() => service.Add("첫 퀘스트", "", null, 1, null), "중복 제목 거부");
    AssertThrows(() => service.Add("잘못된 링크", "", "file:///tmp/test", 1, null), "외부 링크 거부");
    AssertThrows(() => service.Add("잘못된 중요도", "", null, 6, null), "중요도 거부");

    service.Update(created.Id, "수정된 퀘스트", "수정 설명", null, 5, null);
    Assert(service.LoadPending().Single().Title == "수정된 퀘스트", "수정");

    service.Complete(created.Id);
    Assert(service.LoadPending().Count == 0 && service.LoadCompleted().Count == 1, "완료");

    service.Reopen(created.Id);
    Assert(service.LoadPending().Count == 1 && service.LoadCompleted().Count == 0, "복원");

    service.Delete(created.Id);
    Assert(service.LoadPending().Count == 0, "삭제");

    Console.WriteLine("All C# task workflow tests passed.");
    return 0;
}
catch (Exception exception)
{
    Console.Error.WriteLine(exception);
    return 1;
}
finally
{
    Directory.Delete(testDirectory, recursive: true);
}

static void Assert(bool condition, string name)
{
    if (!condition) throw new InvalidOperationException($"검증 실패: {name}");
}

static void AssertThrows(Action action, string name)
{
    try { action(); }
    catch (InvalidOperationException) { return; }
    throw new InvalidOperationException($"예외 검증 실패: {name}");
}
