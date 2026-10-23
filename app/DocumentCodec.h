#pragma once
#include "WorkspaceIncludes.h"

class LineEndingBlockData final : public QTextBlockUserData {
public:
    explicit LineEndingBlockData(forge::core::LineEnding value) : ending(value) {}
    forge::core::LineEnding ending;
};

inline void storeLineEndings(QTextDocument* document,
                             const QVector<forge::core::LineEnding>& lineEndings) {
    QTextBlock block = document->firstBlock();
    qsizetype index = 0;
    while (block.isValid() && block.next().isValid()) {
        if (index < lineEndings.size()) {
            block.setUserData(new LineEndingBlockData(lineEndings.at(index)));
        }
        ++index;
        block = block.next();
    }
}

inline QVector<forge::core::LineEnding> documentLineEndings(QTextDocument* document) {
    QVector<forge::core::LineEnding> lineEndings;
    QTextBlock block = document->firstBlock();
    while (block.isValid() && block.next().isValid()) {
        auto* data = dynamic_cast<LineEndingBlockData*>(block.userData());
        lineEndings.append(data ? data->ending : forge::core::LineEnding::Lf);
        block = block.next();
    }
    return lineEndings;
}
