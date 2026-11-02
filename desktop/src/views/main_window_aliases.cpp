#include "main_window.hpp"

#include "lumen_aliases.hpp"


namespace {
class AliasInputFilter : public QObject {
  public:
    explicit AliasInputFilter(QObject *parent) : QObject(parent) {}
    std::function<void()> backspaceOnEmpty;

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            auto *edit = qobject_cast<QLineEdit *>(watched);
            if (key->key() == Qt::Key_Backspace && edit && edit->text().isEmpty() &&
                backspaceOnEmpty) {
                backspaceOnEmpty();
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }
};
}  // namespace

void MainWindow::showAliasEditor(const QString &objectId,
                                 std::shared_ptr<PreviewSession> previewSession) {
    // The editor always commits against the freshest copy the client has:
    // the preview's object when opened from a preview, the open page's
    // object (which carries unsaved property-panel edits) when opened from
    // the page, and the workspace cache otherwise.
    QJsonObject source;
    if (previewSession)
        source = previewSession->object;
    if (source.isEmpty() && str(current, "id") == objectId)
        source = current;
    if (source.isEmpty())
        source = objectFor(objectId);
    if (source.isEmpty()) {
        error("Could not edit aliases: object is not loaded.");
        return;
    }
    QStringList aliases = LumenAliases::of(source);

    QDialog d(this);
    d.setObjectName("aliasDialog");
    d.setWindowTitle("Aliases");
    d.setMinimumWidth(480);
    auto *layout = new QVBoxLayout(&d);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(10);
    layout->addWidget(label("Aliases", "dialogTitle"));
    layout->addWidget(label(
        "Alternate names for this object. Search finds them everywhere; "
        "picking an alias result inserts the alias as the link text.",
        "muted"));

    auto *chipRow = new WrapBar;
    chipRow->setObjectName("aliasChipRow");
    chipRow->setMinimumHeight(6);
    layout->addWidget(chipRow);

    auto *input = new QLineEdit;
    input->setObjectName("aliasInput");
    input->setPlaceholderText("Add an alias and press Enter");
    input->setMinimumHeight(36);
    layout->addWidget(input);

    // The limits are the server's contract (50 x 200); violations come
    // back as 400 on save and surface through the app's non-blocking
    // notice instead of blocking the dialog.
    layout->addWidget(label("Up to 50 aliases per object, 200 characters each.", "muted"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->setObjectName("aliasDialogButtons");
    layout->addWidget(buttons);

    // The chips row rebuilds on every change; the removal connect refers to
    // the rebuild itself, hence declare-then-assign (a lambda cannot
    // capture the variable it initializes).
    std::function<void()> rebuildChips;
    rebuildChips = [this, chipRow, &aliases, &rebuildChips]() {
        chipRow->clear();
        for (const auto &alias : aliases) {
            auto *chip = new QFrame;
            chip->setObjectName("aliasChip");
            chip->setProperty("aliasText", alias);
            chip->setStyleSheet(QString("QFrame#aliasChip{background:%1;border:1px solid %2;"
                                        "border-radius:13px;}")
                                    .arg(dark ? "#26262c" : "#f1f0f2", dark ? "#3a3a42" : "#e2e1e4"));
            auto *row = new QHBoxLayout(chip);
            row->setContentsMargins(10, 4, 4, 4);
            row->setSpacing(6);
            auto *text = new QLabel(alias);
            text->setToolTip("Search matches this alias");
            row->addWidget(text);
            auto *remove = new QToolButton;
            remove->setObjectName("aliasChipRemove");
            remove->setProperty("aliasText", alias);
            remove->setIcon(LumenIcons::icon("x", QColor(dark ? "#c9c8cd" : "#56555c"), 13));
            remove->setToolTip("Remove");
            remove->setAutoRaise(true);
            remove->setFixedSize(20, 20);
            row->addWidget(remove);
            connect(remove, &QToolButton::clicked, chip, [&aliases, &rebuildChips, alias] {
                aliases.removeAll(alias);
                rebuildChips();
            });
            chipRow->addChip(chip);
        }
    };
    rebuildChips();

    // Enter commits the typed alias as a chip; an exact duplicate (already
    // case-insensitive) is skipped — the server dedupes the same way,
    // keeping the first spelling.
    auto addAlias = [&aliases, input, &rebuildChips]() {
        const QString alias = input->text().trimmed();
        if (alias.isEmpty())
            return;
        for (const auto &existing : aliases)
            if (existing.compare(alias, Qt::CaseInsensitive) == 0) {
                input->clear();
                return;
            }
        aliases << alias;
        input->clear();
        rebuildChips();
    };
    connect(input, &QLineEdit::returnPressed, &d, addAlias);

    auto *keyFilter = new AliasInputFilter(&d);
    keyFilter->backspaceOnEmpty = [&aliases, &rebuildChips]() {
        if (aliases.isEmpty())
            return;
        aliases.removeLast();
        rebuildChips();
    };
    input->installEventFilter(keyFilter);

    connect(buttons->button(QDialogButtonBox::Save), &QPushButton::clicked, &d, &QDialog::accept);
    connect(buttons->button(QDialogButtonBox::Cancel), &QPushButton::clicked, &d, &QDialog::reject);
    input->setFocus();

    if (d.exec() != QDialog::Accepted)
        return;

    auto properties = source.value("properties").toObject();
    // The properties document is a full replacement (contracts/SYNC.md): an
    // empty alias list removes the key, exactly like the server normalizer.
    if (aliases.isEmpty())
        properties.remove("aliases");
    else
        properties["aliases"] = LumenAliases::toJson(aliases);
    saveLabel->setText("Saving aliases…");
    api.patch("/v1/objects/" + objectId, QJsonObject{{"properties", properties}, {"base_revision", source.value("revision")}},
              [this, objectId, previewSession, aliases](QJsonValue value, QString err) {
                  if (!err.isEmpty()) {
                      error("Could not save aliases: " + err);
                      return;
                  }
                  const auto updated = value.toObject();
                  if (updated.isEmpty())
                      return;
                  rememberObject(updated);
                  if (previewSession && !previewSession->dirty) {
                      previewSession->object = updated;
                      if (previewSession->status)
                          previewSession->status->setText(saveState(updated));
                  }
                  if (str(current, "id") == objectId) {
                      if (!dirty && !saving) {
                          current = updated;
                          if (route == "object")
                              renderEditor(false);  // refresh the aliases display
                      } else {
                          // The page has unsaved local edits: never rebuild
                          // it under the user. Merge just the saved aliases
                          // into the local copy (they are already stored
                          // server-side, so the next save keeps them) and
                          // refresh the read-only label in place.
                          auto local = current.value("properties").toObject();
                          if (aliases.isEmpty())
                              local.remove("aliases");
                          else
                              local["aliases"] = LumenAliases::toJson(aliases);
                          current["properties"] = local;
                          if (route == "object") {
                              for (auto *aliasesLabel : findChildren<QLabel *>("aliasesLabel")) {
                                  if (!aliasesLabel->isVisible() && aliasesLabel->parentWidget() &&
                                      !aliasesLabel->parentWidget()->isVisible())
                                      continue;  // stale page pending deletion
                                  aliasesLabel->setText(aliases.join(", "));
                                  aliasesLabel->setVisible(!aliases.isEmpty());
                              }
                          }
                      }
                  }
              });
}
