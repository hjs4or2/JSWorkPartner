using System.Runtime.InteropServices;
using System.Text;

namespace JSWorkPartner.CSharp.Data;

internal sealed class NativeSqlite : IDisposable
{
    private const int Ok = 0;
    private const int Row = 100;
    private const int Done = 101;
    private IntPtr _database;

    public NativeSqlite(string path)
    {
        var result = Native.sqlite3_open_v2(Utf8(path), out _database, 0x00000002 | 0x00000004, null);
        if (result != Ok)
        {
            var message = ErrorMessage();
            Dispose();
            throw new InvalidOperationException($"SQLite DB를 열 수 없습니다: {message}");
        }
        Check(Native.sqlite3_busy_timeout(_database, 5000), "DB 잠금 대기 시간을 설정하지 못했습니다");
    }

    public long LastInsertRowId => Native.sqlite3_last_insert_rowid(_database);

    public void Execute(string sql)
    {
        var result = Native.sqlite3_exec(_database, Utf8(sql), IntPtr.Zero, IntPtr.Zero, out var error);
        if (result == Ok) return;
        var message = error == IntPtr.Zero ? ErrorMessage() : Marshal.PtrToStringUTF8(error);
        if (error != IntPtr.Zero) Native.sqlite3_free(error);
        throw new InvalidOperationException($"SQLite 명령을 실행하지 못했습니다: {message}");
    }

    public Statement Prepare(string sql) => new(this, sql);
    public int Changes() => Native.sqlite3_changes(_database);

    public void Dispose()
    {
        if (_database == IntPtr.Zero) return;
        Native.sqlite3_close_v2(_database);
        _database = IntPtr.Zero;
    }

    private string ErrorMessage() => _database == IntPtr.Zero
        ? "알 수 없는 SQLite 오류"
        : Marshal.PtrToStringUTF8(Native.sqlite3_errmsg(_database)) ?? "알 수 없는 SQLite 오류";

    private void Check(int result, string context)
    {
        if (result != Ok) throw new InvalidOperationException($"{context}: {ErrorMessage()}");
    }

    private static byte[] Utf8(string value) => Encoding.UTF8.GetBytes(value + '\0');

    internal sealed class Statement : IDisposable
    {
        private readonly NativeSqlite _owner;
        private IntPtr _statement;

        public Statement(NativeSqlite owner, string sql)
        {
            _owner = owner;
            var result = Native.sqlite3_prepare_v2(owner._database, Utf8(sql), -1,
                out _statement, IntPtr.Zero);
            if (result != Ok) throw new InvalidOperationException(
                $"SQLite 명령을 준비하지 못했습니다: {owner.ErrorMessage()}");
        }

        public void Bind(int index, long value) => Check(Native.sqlite3_bind_int64(_statement, index, value));
        public void Bind(int index, int value) => Check(Native.sqlite3_bind_int(_statement, index, value));

        public void Bind(int index, string? value)
        {
            if (value is null)
            {
                Check(Native.sqlite3_bind_null(_statement, index));
                return;
            }
            var bytes = Encoding.UTF8.GetBytes(value);
            Check(Native.sqlite3_bind_text(_statement, index, bytes, bytes.Length, new IntPtr(-1)));
        }

        public bool Step()
        {
            var result = Native.sqlite3_step(_statement);
            if (result == Row) return true;
            if (result == Done) return false;
            throw new InvalidOperationException($"SQLite 명령을 실행하지 못했습니다: {_owner.ErrorMessage()}");
        }

        public void ExecuteNonQuery()
        {
            if (Step()) throw new InvalidOperationException("데이터 변경 명령이 예기치 않은 행을 반환했습니다.");
        }

        public long Int64(int column) => Native.sqlite3_column_int64(_statement, column);
        public int Int32(int column) => Native.sqlite3_column_int(_statement, column);
        public bool IsNull(int column) => Native.sqlite3_column_type(_statement, column) == 5;
        public string Text(int column) => IsNull(column) ? string.Empty
            : Marshal.PtrToStringUTF8(Native.sqlite3_column_text(_statement, column)) ?? string.Empty;

        public void Dispose()
        {
            if (_statement == IntPtr.Zero) return;
            Native.sqlite3_finalize(_statement);
            _statement = IntPtr.Zero;
        }

        private void Check(int result)
        {
            if (result != Ok) throw new InvalidOperationException(
                $"SQLite 매개변수를 설정하지 못했습니다: {_owner.ErrorMessage()}");
        }
    }

    private static class Native
    {
        private const string Library = "winsqlite3.dll";

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int sqlite3_open_v2(byte[] filename, out IntPtr database, int flags, byte[]? vfs);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int sqlite3_close_v2(IntPtr database);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern IntPtr sqlite3_errmsg(IntPtr database);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int sqlite3_busy_timeout(IntPtr database, int milliseconds);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int sqlite3_exec(IntPtr database, byte[] sql, IntPtr callback, IntPtr argument, out IntPtr error);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void sqlite3_free(IntPtr pointer);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int sqlite3_prepare_v2(IntPtr database, byte[] sql, int bytes, out IntPtr statement, IntPtr remaining);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int sqlite3_finalize(IntPtr statement);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int sqlite3_step(IntPtr statement);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int sqlite3_bind_int64(IntPtr statement, int index, long value);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int sqlite3_bind_int(IntPtr statement, int index, int value);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int sqlite3_bind_null(IntPtr statement, int index);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int sqlite3_bind_text(IntPtr statement, int index, byte[] value, int bytes, IntPtr destructor);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern long sqlite3_column_int64(IntPtr statement, int column);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int sqlite3_column_int(IntPtr statement, int column);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int sqlite3_column_type(IntPtr statement, int column);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern IntPtr sqlite3_column_text(IntPtr statement, int column);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int sqlite3_changes(IntPtr database);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern long sqlite3_last_insert_rowid(IntPtr database);
    }
}
