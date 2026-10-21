#include "singleinstance.h"
#include <QDir>
#include <QString>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <iterator>
#include <vector>

namespace {

constexpr ULONG ProcessHandleInformation = 51;
constexpr ULONG ObjectNameInformation = 1;
constexpr ULONG ObjectTypeInformation = 2;
constexpr LONG InfoLengthMismatch = LONG(0xC0000004);
const QString GuardSuffix = "-SingleInstanceMutex-";

struct HandleEntry {
    HANDLE handle;
    ULONG_PTR handleCount;
    ULONG_PTR pointerCount;
    ULONG grantedAccess;
    ULONG typeIndex;
    ULONG attributes;
    ULONG reserved;
};

struct HandleSnapshot {
    ULONG_PTR count;
    ULONG_PTR reserved;
    HandleEntry handles[1];
};

struct NtText {
    USHORT length;
    USHORT capacity;
    PWSTR buffer;
};

using NtQuery = LONG(NTAPI *)(HANDLE, ULONG, PVOID, ULONG, PULONG);

NtQuery ntdll(const char *name) {
    return reinterpret_cast<NtQuery>(reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), name)));
}

QString objectText(NtQuery queryObject, HANDLE handle, ULONG kind) {
    alignas(8) char buffer[2048];
    if (queryObject(handle, kind, buffer, sizeof(buffer), nullptr) < 0) return {};
    const auto *text = reinterpret_cast<const NtText *>(buffer);
    return QString::fromWCharArray(text->buffer, text->length / sizeof(wchar_t));
}

bool isGuard(NtQuery queryObject, HANDLE handle) {
    return objectText(queryObject, handle, ObjectTypeInformation) == "Mutant"
        && objectText(queryObject, handle, ObjectNameInformation).contains(GuardSuffix);
}

int releaseIn(DWORD pid, NtQuery queryProcess, NtQuery queryObject) {
    HANDLE process = OpenProcess(PROCESS_DUP_HANDLE | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!process) return 0;
    int released = 0;
    std::vector<char> buffer(64 * 1024);
    ULONG needed = 0;
    LONG status;
    while ((status = queryProcess(process, ProcessHandleInformation, buffer.data(), ULONG(buffer.size()), &needed)) == InfoLengthMismatch)
        buffer.resize(std::max<size_t>(needed, buffer.size() * 2));
    if (status >= 0) {
        const auto *snapshot = reinterpret_cast<const HandleSnapshot *>(buffer.data());
        for (ULONG_PTR i = 0; i < snapshot->count; ++i) {
            HANDLE remote = snapshot->handles[i].handle;
            HANDLE local = nullptr;
            if (!DuplicateHandle(process, remote, GetCurrentProcess(), &local, 0, FALSE, DUPLICATE_SAME_ACCESS)) continue;
            const bool guard = isGuard(queryObject, local);
            CloseHandle(local);
            if (guard && DuplicateHandle(process, remote, nullptr, nullptr, 0, FALSE, DUPLICATE_CLOSE_SOURCE)) released++;
        }
    }
    CloseHandle(process);
    return released;
}

QString imagePath(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    wchar_t path[MAX_PATH * 4];
    DWORD size = DWORD(std::size(path));
    const bool found = QueryFullProcessImageNameW(process, 0, path, &size);
    CloseHandle(process);
    return found ? QDir::cleanPath(QDir::fromNativeSeparators(QString::fromWCharArray(path, int(size)))) : QString();
}

}

int SingleInstance::release(const QString &instances) {
    const NtQuery queryProcess = ntdll("NtQueryInformationProcess");
    const NtQuery queryObject = ntdll("NtQueryObject");
    if (!queryProcess || !queryObject) return 0;
    HANDLE processes = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (processes == INVALID_HANDLE_VALUE) return 0;
    int released = 0;
    const QString root = QDir::cleanPath(instances) + "/";
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(processes, &entry); more; more = Process32NextW(processes, &entry))
        if (imagePath(entry.th32ProcessID).startsWith(root, Qt::CaseInsensitive))
            released += releaseIn(entry.th32ProcessID, queryProcess, queryObject);
    CloseHandle(processes);
    return released;
}

#else

int SingleInstance::release(const QString &) { return 0; }

#endif
