#pragma once
#include <QWidget>
class QPlainTextEdit;
class QLineEdit;
class QCheckBox;
class QLabel;
class FindBar final : public QWidget {
    Q_OBJECT
public:
    explicit FindBar(QWidget* parent = nullptr);
    void open(QPlainTextEdit* editor, bool replace = false);
    void setEditor(QPlainTextEdit* editor);
    void find(bool backwards = false);

private:
    void replace(bool all);
    QPlainTextEdit* m_editor = nullptr;
    QLineEdit* m_query;
    QLineEdit* m_replacement;
    QCheckBox* m_case;
    QCheckBox* m_word;
    QCheckBox* m_regex;
    QLabel* m_status;
    QWidget* m_replaceRow;
};
