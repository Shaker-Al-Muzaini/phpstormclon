#include "EditorWidgets.h"

LineNumberArea::LineNumberArea(ForgeCodeEditor* editor) : QWidget(editor), m_editor(editor) {}

QSize LineNumberArea::sizeHint() const {
    return QSize(m_editor->lineNumberAreaWidth(), 0);
}

void LineNumberArea::paintEvent(QPaintEvent* event) {
    m_editor->paintLineNumbers(event);
}

void LineNumberArea::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_editor->toggleFoldAtY(qRound(event->position().y()));
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}
