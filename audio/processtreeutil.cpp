#include "processtreeutil.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include <QHash>
#include <QQueue>
#include <QSet>

QVector<unsigned long> ProcessTreeUtil::enumerateProcessTree(unsigned long rootPid)
{
    QVector<unsigned long> processIds;
    if (rootPid == 0) {
        return processIds;
    }

    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        processIds.push_back(rootPid);
        return processIds;
    }

    QHash<unsigned long, QVector<unsigned long>> childrenByParent;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            childrenByParent[entry.th32ParentProcessID].push_back(entry.th32ProcessID);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    QSet<unsigned long> visited;
    QQueue<unsigned long> pending;
    pending.enqueue(rootPid);

    while (!pending.isEmpty()) {
        const unsigned long pid = pending.dequeue();
        if (pid == 0 || visited.contains(pid)) {
            continue;
        }
        visited.insert(pid);
        processIds.push_back(pid);

        for (unsigned long childPid : childrenByParent.value(pid)) {
            if (!visited.contains(childPid)) {
                pending.enqueue(childPid);
            }
        }
    }

    if (processIds.isEmpty()) {
        processIds.push_back(rootPid);
    }
    return processIds;
}
