#include "lumen_document.hpp"
#include "lumen_editor.hpp"
#include <QTimeZone>

namespace {
constexpr int NativeRunProperty = QTextFormat::UserProperty + 715;
QJsonObject temporalAt(const QTextCursor &cursor) {
    return QJsonDocument::fromJson(cursor.charFormat().property(NativeRunProperty).toByteArray())
        .object()
        .value("canonical_inline")
        .toObject();
}
} // namespace

void RichEditor::insertTemporalReference(const QJsonObject &attributes) {
    if (isReadOnly())
        return;
    auto cursor = textCursor();
    const auto inlineNode = QJsonObject{{"type", "temporal"}, {"attrs", attributes}};
    QTextCharFormat format;
    format.setProperty(NativeRunProperty,
                       QJsonDocument(QJsonObject{{"canonical_inline", inlineNode},
                                                 {"instance", QUuid::createUuid().toString()}})
                           .toJson(QJsonDocument::Compact));
    format.setBackground(QColor(dark_ ? "#29292e" : "#eeeeef"));
    format.setToolTip("Editar fecha y hora");
    cursor.beginEditBlock();
    cursor.insertText(LumenDocument::temporalText(attributes), format);
    cursor.endEditBlock();
    setTextCursor(cursor);
    setCurrentCharFormat(QTextCharFormat());
    ensureBlockIds();
}

void RichEditor::showTemporalPicker(bool withTime, const QTextCursor &existing) {
    if (isReadOnly())
        return;
    auto insertion = existing.isNull() ? textCursor() : existing;
    auto original = temporalAt(insertion);
    auto attributes = original.value("attrs").toObject();
    const bool editing = original.value("type") == "temporal";
    const auto now = QDateTime::currentDateTime();
    QDateTime start =
        attributes.value("kind") == "datetime"
            ? QDateTime::fromString(attributes.value("start").toString(), Qt::ISODate)
            : QDateTime(QDate::fromString(attributes.value("start").toString(), Qt::ISODate), now.time());
    if (!start.isValid())
        start = now;
    QDateTime finish = attributes.value("kind") == "datetime"
                           ? QDateTime::fromString(attributes.value("end").toString(), Qt::ISODate)
                           : QDateTime(QDate::fromString(attributes.value("end").toString(), Qt::ISODate),
                                       start.time().addSecs(3600));
    if (!finish.isValid())
        finish = start.addSecs(3600);
    QDialog picker(this, Qt::Popup);
    picker.setObjectName("temporalPicker");
    picker.setWindowTitle("Fecha y hora");
    picker.setStyleSheet(dark_ ? "QDialog { background: #202024; color: #e4e4e7; }" : QString());
    auto *layout = new QHBoxLayout(&picker);
    auto makeCalendar = [&](const QString &name, const QDateTime &value) {
        auto *panel = new QWidget(&picker);
        auto *column = new QVBoxLayout(panel);
        auto *date = new QDateEdit(value.date(), panel);
        date->setDisplayFormat("d MMMM yyyy");
        date->setObjectName(name + "Date");
        auto *time = new QTimeEdit(value.time(), panel);
        time->setDisplayFormat("HH:mm");
        time->setObjectName(name + "Time");
        auto *fields = new QHBoxLayout;
        fields->addWidget(date);
        fields->addWidget(time);
        column->addLayout(fields);
        auto *calendar = new QCalendarWidget(panel);
        calendar->setObjectName(name + "Calendar");
        calendar->setGridVisible(false);
        calendar->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
        calendar->setFirstDayOfWeek(Qt::Monday);
        calendar->setSelectedDate(value.date());
        column->addWidget(calendar);
        connect(calendar, &QCalendarWidget::selectionChanged, date, [calendar, date] {
            date->setDate(calendar->selectedDate());
        });
        connect(date, &QDateEdit::dateChanged, calendar, &QCalendarWidget::setSelectedDate);
        layout->addWidget(panel);
        return panel;
    };
    auto *startPanel = makeCalendar("start", start);
    Q_UNUSED(startPanel);
    auto *endPanel = makeCalendar("end", finish);
    auto *side = new QVBoxLayout;
    layout->addLayout(side);
    auto *startDate = picker.findChild<QDateEdit *>("startDate");
    for (const auto &preset : QList<QPair<QString, int>>{
             {"Ayer", -1}, {"Hoy", 0}, {"Mañana", 1}, {"En 1 semana", 7}, {"En 2 semanas", 14}}) {
        auto *button = new QPushButton(preset.first, &picker);
        side->addWidget(button);
        connect(button, &QPushButton::clicked, &picker, [startDate, days = preset.second] {
            startDate->setDate(QDate::currentDate().addDays(days));
        });
    }
    auto *includeTime = new QCheckBox("Incluir hora", &picker);
    includeTime->setObjectName("includeTime");
    includeTime->setChecked(editing ? attributes.value("kind") == "datetime" : withTime);
    side->addWidget(includeTime);
    auto *includeEnd = new QCheckBox("Fecha de finalización", &picker);
    includeEnd->setObjectName("includeEnd");
    includeEnd->setChecked(attributes.contains("end"));
    side->addWidget(includeEnd);
    auto *errorLabel = new QLabel(&picker);
    errorLabel->setWordWrap(true);
    errorLabel->setMaximumWidth(190);
    side->addWidget(errorLabel);
    auto update = [&] {
        endPanel->setVisible(includeEnd->isChecked());
        for (auto *time : picker.findChildren<QTimeEdit *>())
            time->setVisible(includeTime->isChecked());
        picker.adjustSize();
        const auto available = screen()->availableGeometry();
        auto point = viewport()->mapToGlobal(cursorRect(insertion).bottomLeft());
        point.setX(
            qBound(available.left(), point.x(), qMax(available.left(), available.right() - picker.width())));
        point.setY(
            qBound(available.top(), point.y(), qMax(available.top(), available.bottom() - picker.height())));
        picker.move(point);
    };
    connect(includeTime, &QCheckBox::toggled, &picker, update);
    connect(includeEnd, &QCheckBox::toggled, &picker, update);
    if (editing) {
        auto *remove = new QPushButton("Eliminar fecha", &picker);
        side->addWidget(remove);
        connect(remove, &QPushButton::clicked, &picker, [&] {
            insertion.beginEditBlock();
            insertion.removeSelectedText();
            insertion.endEditBlock();
            setTextCursor(insertion);
            setCurrentCharFormat(QTextCharFormat());
            picker.reject();
        });
    }
    side->addStretch();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, &picker);
    buttons->button(QDialogButtonBox::Ok)->setText("Confirmar");
    buttons->button(QDialogButtonBox::Cancel)->setText("Cancelar");
    side->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &picker, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &picker, [&] {
        const auto startDay = picker.findChild<QDateEdit *>("startDate")->date();
        const auto endDay = picker.findChild<QDateEdit *>("endDate")->date();
        auto zone = QTimeZone(attributes.value("timezone").toString().toUtf8());
        if (!zone.isValid())
            zone = QTimeZone::systemTimeZone();
        const auto begin = QDateTime(startDay, picker.findChild<QTimeEdit *>("startTime")->time(), zone);
        const auto end = QDateTime(endDay, picker.findChild<QTimeEdit *>("endTime")->time(), zone);
        if (includeEnd->isChecked() && (includeTime->isChecked() ? end < begin : endDay < startDay)) {
            errorLabel->setText("La finalización debe ser posterior o igual al inicio.");
            return;
        }
        if (includeTime->isChecked() && (!begin.isValid() || (includeEnd->isChecked() && !end.isValid()))) {
            errorLabel->setText("La hora no existe en esta zona horaria.");
            return;
        }
        attributes["kind"] = includeTime->isChecked() ? "datetime" : "date";
        attributes["start"] =
            includeTime->isChecked() ? begin.toString(Qt::ISODate) : startDay.toString(Qt::ISODate);
        attributes.remove("end");
        attributes.remove("timezone");
        attributes.remove("display_hint");
        if (includeTime->isChecked())
            attributes["timezone"] = QString::fromUtf8(zone.id());
        if (includeEnd->isChecked())
            attributes["end"] =
                includeTime->isChecked() ? end.toString(Qt::ISODate) : endDay.toString(Qt::ISODate);
        setTextCursor(insertion);
        insertTemporalReference(attributes);
        picker.accept();
    });
    styleLayer(&picker);
    update();
    picker.exec();
    setFocus();
}
