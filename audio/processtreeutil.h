#pragma once

#include <QVector>

class ProcessTreeUtil
{
public:
    static QVector<unsigned long> enumerateProcessTree(unsigned long rootPid);
};
