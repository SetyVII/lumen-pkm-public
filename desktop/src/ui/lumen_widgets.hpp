#pragma once

#include "lumen_icons.hpp"
#include <QDialog>
#include <QFrame>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QTextEdit>
#include <QtWidgets>
#include <functional>

inline QJsonArray arr(const QJsonValue &v, const QString &key = "objects") {
    if (v.isArray())
        return v.toArray();
    return v.toObject().value(key).toArray();
}
inline QString str(const QJsonObject &o, const char *key) {
    return o.value(key).toString();
}
inline QIcon icon(const QString &name) {
    return LumenIcons::icon(name);
}
inline QLabel *label(const QString &text, const QString &name = "", QWidget *parent = nullptr) {
    auto *w = new QLabel(text, parent);
    w->setTextFormat(Qt::PlainText);
    w->setObjectName(name);
    w->setWordWrap(true);
    return w;
}
inline QPushButton *button(const QString &text, const QString &ico = "", const QString &name = "") {
    auto *b = new QPushButton(text);
    b->setObjectName(name);
    b->setAccessibleName(text.isEmpty()?QString(ico).replace('-',' '):text);
    if (!ico.isEmpty())
        b->setIcon(icon(ico));
    b->setCursor(Qt::PointingHandCursor);
    b->setMinimumHeight(34);
    return b;
}
inline void clearLayout(QLayout *l) {
    while (auto *item = l->takeAt(0)) {
        if (item->widget()) {
            // Hide before the deferred delete: an unmanaged widget stays
            // visible until the event loop processes the deleteLater, so
            // during that window findChildren() returns a stale, pending-
            // deletion page (cause of the 0.6 offscreen SIGSEGV in the
            // acceptance run).
            item->widget()->hide();
            item->widget()->deleteLater();
        }
        if (item->layout())
            clearLayout(item->layout());
        delete item;
    }
}

class CommandInput : public QLineEdit {
  public:
    QListWidget *results = nullptr;
    using QLineEdit::QLineEdit;

  protected:
    void keyPressEvent(QKeyEvent *e) override {
        if (results && (e->key() == Qt::Key_Down || e->key() == Qt::Key_Up)) {
            int next = results->currentRow() + (e->key() == Qt::Key_Down ? 1 : -1);
            results->setCurrentRow(qBound(0, next, results->count() - 1));
            return;
        }
        QLineEdit::keyPressEvent(e);
    }
};

class DocumentTitle : public QTextEdit {
  public:
    explicit DocumentTitle(const QString &text, QWidget *parent = nullptr) : QTextEdit(parent) {
        setAcceptRichText(false);
        setPlainText(text);
        setFrameShape(QFrame::NoFrame);
        setTabChangesFocus(true);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        document()->setDocumentMargin(0);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this,
                [this] {
                    resizeToText();
                });
        resizeToText();
    }
    QString text() const {
        return toPlainText();
    }
    void resizeToText() {
        setFixedHeight(qBound(43, int(document()->size().height() + 10), 160));
    }

  protected:
    void resizeEvent(QResizeEvent *event) override {
        QTextEdit::resizeEvent(event);
        resizeToText();
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            focusNextChild();
            return;
        }
        QTextEdit::keyPressEvent(event);
    }
};
class GuardedDialog : public QDialog {
  public:
    std::function<bool()> mayClose;
    using QDialog::QDialog;

  protected:
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Escape) {
            close();
            return;
        }
        QDialog::keyPressEvent(event);
    }
    void closeEvent(QCloseEvent *event) override {
        if (mayClose && !mayClose()) {
            event->ignore();
            return;
        }
        QDialog::closeEvent(event);
    }
};
class ClickableCard : public QFrame {
  public:
    std::function<void()> onClicked;
    using QFrame::QFrame;

  protected:
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && onClicked) {
            onClicked();
            e->accept();
            return;
        }
        QFrame::mouseReleaseEvent(e);
    }
};

// A chip row that can never clip a label mid-word: chips wrap onto extra
// lines when the row runs out of width, a chip that alone is wider than the
// row elides its text with "…" instead of being cut, and the trailing group
// stays right-aligned whenever everything fits on one line (the classic
// header: context chips left, actions right). Introduced by the aliases
// stage after a visual review found the object header clipping its last
// chips at the right edge; both object-page headers (page and preview) use
// it, so every future chip in those rows inherits the behavior.
class WrapBar : public QWidget {
  public:
    explicit WrapBar(QWidget *parent = nullptr) : QWidget(parent) {
        auto policy = sizePolicy();
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
    }
    void addChip(QWidget *chip) { append(chip, false); }
    void addTrailingChip(QWidget *chip) { append(chip, true); }
    // Drops every chip (the alias editor rebuilds its row on each change).
    void clear() {
        chips_.clear();
        for (auto *child : findChildren<QWidget *>(Qt::FindDirectChildrenOnly))
            child->deleteLater();
        updateGeometry();
    }

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return layoutRow(width, false); }
    QSize sizeHint() const override { return {layoutWidth(), heightForWidth(layoutWidth())}; }
    QSize minimumSizeHint() const override { return {0, heightForWidth(0)}; }

  protected:
    void resizeEvent(QResizeEvent *event) override {
        QWidget::resizeEvent(event);
        relayout();
    }
    void showEvent(QShowEvent *event) override {
        QWidget::showEvent(event);
        relayout();
    }
    bool event(QEvent *event) override {
        // Font/theme changes alter the natural chip widths.
        if (event->type() == QEvent::ApplicationFontChange ||
            event->type() == QEvent::FontChange)
            relayout();
        return QWidget::event(event);
    }

  private:
    struct Chip {
        QWidget *widget = nullptr;
        bool trailing = false;
    };
    QList<Chip> chips_;
    int spacing_ = 8;

    void append(QWidget *chip, bool trailing) {
        if (!chip)
            return;
        chip->setParent(this);
        chip->show();
        chips_.append(Chip{chip, trailing});
        relayout();
    }
    static QString fullText(const QWidget *chip) {
        return chip->property("wrapBarFullText").toString();
    }
    // Elide is text-based: restore the full label first so the natural size
    // hint is measured against the real string, then shrink if needed.
    static void restoreText(QWidget *chip) {
        const QString text = fullText(chip);
        if (text.isEmpty())
            return;
        if (auto *push = qobject_cast<QPushButton *>(chip))
            push->setText(text);
        else if (auto *tool = qobject_cast<QToolButton *>(chip))
            tool->setText(text);
        else if (auto *lbl = qobject_cast<QLabel *>(chip))
            lbl->setText(text);
        chip->setProperty("wrapBarFullText", QVariant());
    }
    static QString elideText(QWidget *chip, int targetWidth) {
        // Elide from the FULL text: on a second pass the chip may already
        // show an elided label, and eliding that would lose the original.
        QString text = fullText(chip);
        if (text.isEmpty())
            text = currentText(chip);
        if (text.isEmpty())
            return {};
        chip->setProperty("wrapBarFullText", text);
        if (chip->toolTip().isEmpty())
            chip->setToolTip(text);
        const QFontMetrics metrics(chip->font());
        // The chrome (icon, paddings, borders) is what does not scale with
        // the text; reserve it and elide the label into what is left. The
        // current hint carries the CURRENT text, whatever it is, so the
        // subtraction yields the same chrome either way.
        const int chrome = chip->sizeHint().width() - metrics.horizontalAdvance(currentText(chip));
        const int textWidth = qMax(0, targetWidth - chrome);
        const QString elided = metrics.elidedText(text, Qt::ElideRight, textWidth);
        if (auto *push = qobject_cast<QPushButton *>(chip))
            push->setText(elided);
        else if (auto *tool = qobject_cast<QToolButton *>(chip))
            tool->setText(elided);
        else if (auto *lbl = qobject_cast<QLabel *>(chip))
            lbl->setText(elided);
        return elided;
    }
    static QString currentText(QWidget *chip) {
        if (auto *push = qobject_cast<QPushButton *>(chip))
            return push->text();
        if (auto *tool = qobject_cast<QToolButton *>(chip))
            return tool->text();
        if (auto *lbl = qobject_cast<QLabel *>(chip))
            return lbl->text();
        return {};
    }
    // The natural (full-text) size hint WITHOUT touching the chip: layout
    // queries (heightForWidth) run against widgets in any state, and
    // mutating text during a query desynchronizes the measured height from
    // the applied geometry. The elided text only ever shortens the width,
    // so the full-text hint is reconstructed arithmetically.
    static QSize naturalHint(QWidget *chip) {
        const auto hint = chip->sizeHint();
        const QString full = fullText(chip);
        if (full.isEmpty())
            return hint;
        const QFontMetrics metrics(chip->font());
        const int delta = metrics.horizontalAdvance(full) - metrics.horizontalAdvance(currentText(chip));
        return {qMax(1, hint.width() + delta), hint.height()};
    }
    // Total width of the single-line presentation (all chips, one row).
    int layoutWidth() const {
        int width = 0;
        for (int i = 0; i < chips_.size(); ++i)
            width += naturalHint(chips_[i].widget).width();
        if (chips_.size() > 1)
            width += spacing_ * (chips_.size() - 1);
        return width;
    }
    // Computes the row geometry. With `apply` false it only measures; with
    // true it places the chips and elides the ones that cannot fit a line.
    int layoutRow(int available, bool apply) const {
        if (chips_.isEmpty())
            return 0;
        if (available < 1)
            available = 1;
        QList<QSize> hints;
        hints.reserve(chips_.size());
        int trailingCount = 0, trailingWidth = 0, totalWidth = 0;
        for (int i = 0; i < chips_.size(); ++i) {
            const auto hint = naturalHint(chips_[i].widget);
            hints << hint;
            totalWidth += hint.width();
            if (chips_[i].trailing) {
                ++trailingCount;
                trailingWidth += hint.width();
            }
        }
        const int gaps = spacing_ * (chips_.size() - 1);
        const bool singleLine = totalWidth + gaps <= available;
        int x = 0, y = 0, rowHeight = 0, height = 0;
        for (int i = 0; i < chips_.size(); ++i) {
            QWidget *chip = chips_[i].widget;
            int width = hints[i].width();
            // Qt clamps setGeometry to the widget's minimum size (the app's
            // buttons set a 34px floor), so the row math must use the same
            // effective height — otherwise a "placed" 31px row renders 34px
            // and falls outside the computed bar height.
            const int hintHeight = qMax(hints[i].height(), chip->minimumHeight());
            if (width > available) {
                // One chip alone exceeds the row: elide instead of clipping.
                width = available;
                if (apply)
                    elideText(chip, available);
            } else if (apply) {
                restoreText(chip);
            }
            if (!singleLine && x > 0 && x + width > available) {
                x = 0;
                y += rowHeight + spacing_;
                rowHeight = 0;
            }
            if (apply) {
                int placeX = x;
                if (singleLine && trailingCount > 0 && chips_[i].trailing) {
                    // Right-align the trailing group on the single line: the
                    // last trailing chip ends exactly at the row's edge.
                    int tailWidth = -spacing_;
                    for (int j = i; j < chips_.size(); ++j)
                        if (chips_[j].trailing)
                            tailWidth += hints[j].width() + spacing_;
                    placeX = qMax(available - tailWidth, x);
                }
                chip->setGeometry(placeX, y, width, hintHeight);
            }
            x += width + spacing_;
            rowHeight = qMax(rowHeight, hintHeight);
            height = qMax(height, y + hintHeight);
        }
        return height;
    }
    void relayout() {
        const int needed = layoutRow(width(), true);
        // The bar must never be shorter than the rows it just placed: if
        // the layout's heightForWidth query ran against stale chip metrics,
        // enforce the applied height so no wrapped row falls outside the
        // bar's rect (this converges: the next query sees fresh metrics).
        if (needed != minimumHeight())
            setMinimumHeight(needed);
        updateGeometry();
    }
};
