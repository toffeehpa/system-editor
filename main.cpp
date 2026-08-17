#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QLineEdit>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QTextStream>
#include <QPrinter>
#include <QPrintDialog>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextOption>
#include <QKeyEvent>
#include <QMessageBox>
#include <QCloseEvent>
#include <QMimeData>
#include <QCheckBox>
#include <QPushButton>
#include <QSettings>
#include <QEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QLabel>
#include <QFontDialog>
#include <QFont>
#include <QRadioButton>
#include <QButtonGroup>
#include <QTextCodec>
#include <QStringDecoder>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextFragment>
#include <QTimer>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QProcess>
#include <QBuffer>
#include <QTableWidget>
#include <QHeaderView>
#include <functional>

class FindLineEdit : public QLineEdit
{
    Q_OBJECT
public:
    using QLineEdit::QLineEdit;

signals:
    void closeRequested();
    void nextRequested();
    void previousRequested();

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape) {
            emit closeRequested();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (event->modifiers() & Qt::ShiftModifier)
                emit previousRequested();
            else
                emit nextRequested();
            return;
        }
        QLineEdit::keyPressEvent(event);
    }
};

class RichAwareTextEdit : public QTextEdit
{
    Q_OBJECT
public:
    using QTextEdit::QTextEdit;

    std::function<bool(const QMimeData *)> pasteInterceptor;

    bool smartQuotesEnabled = false;
    bool smartDashesEnabled = false;
    bool smartLinksEnabled = false;
    bool textReplacementEnabled = false;
    QList<QPair<QString, QString>> textReplacementRules;

protected:
    void insertFromMimeData(const QMimeData *source) override
    {
        if (pasteInterceptor && pasteInterceptor(source))
            return;
        QTextEdit::insertFromMimeData(source);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (handleSmartTyping(event))
            return;

        QTextEdit::keyPressEvent(event);

        const bool wordBoundary = (event->key() == Qt::Key_Space
                                            || event->key() == Qt::Key_Return
                                            || event->key() == Qt::Key_Enter);
        if (wordBoundary) {
            applyTextReplacement();
            if (smartLinksEnabled && acceptRichText())
                linkifyPrecedingWord();
        }
    }

private:
    QChar charBeforeCursor() const
    {
        QTextCursor cursor = textCursor();
        if (cursor.position() == 0)
            return QChar();
        cursor.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor);
        return cursor.selectedText().at(0);
    }

    bool blockContainsCyrillic() const
    {
        for (const QChar &ch : textCursor().block().text()) {
            if (ch.unicode() >= 0x0400 && ch.unicode() <= 0x04FF)
                return true;
        }
        return false;
    }

    bool handleSmartTyping(QKeyEvent *event)
    {
        if (smartQuotesEnabled && event->text() == "\"") {
            if (!blockContainsCyrillic())
                return false; // if text not russian then leave quotes straight

            const QChar prev = charBeforeCursor();
            const bool openingContext = prev.isNull() || prev.isSpace() || prev == QChar(0x00AB) /* « */;
            textCursor().insertText(openingContext ? QString(QChar(0x00AB)) : QString(QChar(0x00BB)));
            return true;
        }

        if (smartDashesEnabled && event->text() == "-") {
            if (charBeforeCursor() == '-') {
                QTextCursor cursor = textCursor();
                cursor.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor);
                cursor.insertText(QString(QChar(0x2014))); // em dash sym —
                return true;
            }
            return false;
        }

        return false;
    }

    void linkifyPrecedingWord()
    {
        QTextCursor cursor = textCursor();
        const int endPos = cursor.position() - 1;
        if (endPos <= 0)
            return;

        const QTextBlock block = cursor.block();
        const QString blockText = block.text();
        const int posInBlock = endPos - block.position();
        if (posInBlock <= 0 || posInBlock > blockText.length())
            return;

        int start = posInBlock;
        while (start > 0 && !blockText.at(start - 1).isSpace())
            --start;

        const QString word = blockText.mid(start, posInBlock - start);

        static const QRegularExpression urlPattern(R"(^(https?://\S+|www\.\S+\.\S+)$)");
        if (!urlPattern.match(word).hasMatch())
            return;

        QString href = word;
        if (href.startsWith("www."))
            href.prepend("https://");

        QTextCursor linkCursor(document());
        linkCursor.setPosition(block.position() + start);
        linkCursor.setPosition(block.position() + posInBlock, QTextCursor::KeepAnchor);

        QTextCharFormat linkFormat;
        linkFormat.setAnchor(true);
        linkFormat.setAnchorHref(href);
        linkFormat.setForeground(QColor(0x1a, 0x73, 0xe8));
        linkFormat.setFontUnderline(true);
        linkCursor.mergeCharFormat(linkFormat);
    }

    void applyTextReplacement()
    {
        if (!textReplacementEnabled || textReplacementRules.isEmpty())
            return;

        QTextCursor cursor = textCursor();
        const int endPos = cursor.position() - 1;
        if (endPos <= 0)
            return;

        const QTextBlock block = cursor.block();
        const QString blockText = block.text();
        const int posInBlock = endPos - block.position();
        if (posInBlock <= 0 || posInBlock > blockText.length())
            return;

        int start = posInBlock;
        while (start > 0 && !blockText.at(start - 1).isSpace())
            --start;

        const QString word = blockText.mid(start, posInBlock - start);

        for (const auto &rule : textReplacementRules) {
            if (word.compare(rule.first, Qt::CaseInsensitive) == 0) {
                QTextCursor replaceCursor(document());
                replaceCursor.setPosition(block.position() + start);
                replaceCursor.setPosition(block.position() + posInBlock, QTextCursor::KeepAnchor);
                replaceCursor.insertText(rule.second);
                return;
            }
        }
    }
};

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr) : QDialog(parent)
    {
        setWindowTitle("Preferences");

        auto *layout = new QVBoxLayout(this);

        autoAppendTxtCheck = new QCheckBox("Automatically append .txt when saving Plain Text");
        layout->addWidget(autoAppendTxtCheck);

        auto *encodingRow = new QHBoxLayout();
        encodingRow->addWidget(new QLabel("Save Plain Text as:"));
        encodingCombo = new QComboBox();
        encodingCombo->addItems({"UTF-8", "UTF-16", "Windows-1251"});
        encodingRow->addWidget(encodingCombo);
        layout->addLayout(encodingRow);

        layout->addWidget(new QLabel("Default mode for new windows:"));
        modePlainRadio = new QRadioButton("Plain Text");
        modeRichRadio = new QRadioButton("Rich Text");
        auto *modeGroup = new QButtonGroup(this);
        modeGroup->addButton(modePlainRadio);
        modeGroup->addButton(modeRichRadio);
        layout->addWidget(modePlainRadio);
        layout->addWidget(modeRichRadio);

        smartQuotesCheck = new QCheckBox("Smart quotes (\" \u2192 \u00ab \u00bb)");
        layout->addWidget(smartQuotesCheck);

        smartDashesCheck = new QCheckBox("Smart dashes (-- \u2192 \u2014)");
        layout->addWidget(smartDashesCheck);

        smartLinksCheck = new QCheckBox("Smart links (auto-detect URLs in Rich Text)");
        layout->addWidget(smartLinksCheck);

        auto *plainFontRow = new QHBoxLayout();
        plainFontLabel = new QLabel();
        auto *plainFontBtn = new QPushButton("Choose...");
        connect(plainFontBtn, &QPushButton::clicked, this, &SettingsDialog::pickPlainFont);
        plainFontRow->addWidget(plainFontLabel, 1);
        plainFontRow->addWidget(plainFontBtn);
        layout->addLayout(plainFontRow);

        auto *richFontRow = new QHBoxLayout();
        richFontLabel = new QLabel();
        auto *richFontBtn = new QPushButton("Choose...");
        connect(richFontBtn, &QPushButton::clicked, this, &SettingsDialog::pickRichFont);
        richFontRow->addWidget(richFontLabel, 1);
        richFontRow->addWidget(richFontBtn);
        layout->addLayout(richFontRow);

        auto *codeFontRow = new QHBoxLayout();
        codeFontLabel = new QLabel();
        auto *codeFontBtn = new QPushButton("Choose...");
        connect(codeFontBtn, &QPushButton::clicked, this, &SettingsDialog::pickCodeFont);
        codeFontRow->addWidget(codeFontLabel, 1);
        codeFontRow->addWidget(codeFontBtn);
        layout->addLayout(codeFontRow);

        layout->addWidget(new QLabel("When pasting rich text into Plain Text:"));
        pasteAskRadio = new QRadioButton("Ask every time");
        pasteRichRadio = new QRadioButton("Always open in a Rich Text window");
        pastePlainRadio = new QRadioButton("Always paste as plain text");
        auto *pasteGroup = new QButtonGroup(this);
        pasteGroup->addButton(pasteAskRadio);
        pasteGroup->addButton(pasteRichRadio);
        pasteGroup->addButton(pastePlainRadio);
        layout->addWidget(pasteAskRadio);
        layout->addWidget(pasteRichRadio);
        layout->addWidget(pastePlainRadio);

        auto *resetBtn = new QPushButton("Reset to Defaults");
        connect(resetBtn, &QPushButton::clicked, this, &SettingsDialog::resetToDefaults);
        layout->addWidget(resetBtn);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::close);
        layout->addWidget(buttons);

        loadFromSettings();

        connect(autoAppendTxtCheck, &QCheckBox::toggled, this, [](bool v) {
            QSettings s;
            s.setValue("file/autoAppendTxtExtension", v);
        });
        connect(encodingCombo, &QComboBox::currentTextChanged, this, [](const QString &v) {
            QSettings s;
            s.setValue("file/saveEncoding", v);
        });
        connect(modeRichRadio, &QRadioButton::toggled, this, [](bool checked) {
                    if (checked) {
                        QSettings s;
                        s.setValue("file/defaultMode", "rich");
                    }
                });
        connect(modePlainRadio, &QRadioButton::toggled, this, [](bool checked) {
            if (checked) {
                QSettings s;
                s.setValue("file/defaultMode", "plain");
            }
        });
        connect(pasteAskRadio, &QRadioButton::toggled, this, [](bool checked) {
            if (checked) { QSettings s; s.setValue("paste/richTextChoice", 0); }
        });
        connect(pasteRichRadio, &QRadioButton::toggled, this, [](bool checked) {
            if (checked) { QSettings s; s.setValue("paste/richTextChoice", 1); }
        });
        connect(pastePlainRadio, &QRadioButton::toggled, this, [](bool checked) {
            if (checked) { QSettings s; s.setValue("paste/richTextChoice", 2); }
        });
        connect(smartQuotesCheck, &QCheckBox::toggled, this, [](bool v) {
                    QSettings s;
                    s.setValue("typing/smartQuotes", v);
                });
        connect(smartDashesCheck, &QCheckBox::toggled, this, [](bool v) {
            QSettings s;
            s.setValue("typing/smartDashes", v);
        });
        connect(smartLinksCheck, &QCheckBox::toggled, this, [](bool v) {
            QSettings s;
            s.setValue("typing/smartLinks", v);
        });
        connect(smartQuotesCheck, &QCheckBox::toggled, this, [this](bool v) {
            QSettings s;
            s.setValue("typing/smartQuotes", v);
            emit smartQuotesChanged(v);
        });
        connect(smartDashesCheck, &QCheckBox::toggled, this, [this](bool v) {
            QSettings s;
            s.setValue("typing/smartDashes", v);
            emit smartDashesChanged(v);
        });
        connect(smartLinksCheck, &QCheckBox::toggled, this, [this](bool v) {
            QSettings s;
            s.setValue("typing/smartLinks", v);
            emit smartLinksChanged(v);
        });
    }

signals:
    void smartQuotesChanged(bool enabled);
    void smartDashesChanged(bool enabled);
    void smartLinksChanged(bool enabled);

private:
    QCheckBox *autoAppendTxtCheck;
    QComboBox *encodingCombo;
    QRadioButton *modePlainRadio;
    QRadioButton *modeRichRadio;
    QCheckBox *smartQuotesCheck;
    QCheckBox *smartDashesCheck;
    QCheckBox *smartLinksCheck;
    QLabel *plainFontLabel;
    QLabel *richFontLabel;
    QLabel *codeFontLabel;
    QRadioButton *pasteAskRadio;
    QRadioButton *pasteRichRadio;
    QRadioButton *pastePlainRadio;

    void loadFromSettings()
    {
        QSettings s;
        autoAppendTxtCheck->setChecked(s.value("file/autoAppendTxtExtension", true).toBool());
        encodingCombo->setCurrentText(s.value("file/saveEncoding", "UTF-8").toString());
        if (s.value("file/defaultMode", "plain").toString() == "rich")
            modeRichRadio->setChecked(true);
        else
            modePlainRadio->setChecked(true);

        switch (s.value("paste/richTextChoice", 0).toInt()) {
            case 1: pasteRichRadio->setChecked(true); break;
            case 2: pastePlainRadio->setChecked(true); break;
            default: pasteAskRadio->setChecked(true); break;
        }
        smartQuotesCheck->setChecked(s.value("typing/smartQuotes", true).toBool());
        smartDashesCheck->setChecked(s.value("typing/smartDashes", true).toBool());
        smartLinksCheck->setChecked(s.value("typing/smartLinks", true).toBool());
        updateFontLabels();
    }

    void updateFontLabels()
    {
        QSettings s;
        const QFont plainFont = s.value("fonts/plainText", QFont()).value<QFont>();
        const QFont richFont = s.value("fonts/richText", QFont()).value<QFont>();
        const QFont codeFont = s.value("fonts/code", QFont("monospace")).value<QFont>();
        plainFontLabel->setText(QString("Plain Text: %1, %2pt").arg(plainFont.family()).arg(plainFont.pointSize()));
        richFontLabel->setText(QString("Rich Text: %1, %2pt").arg(richFont.family()).arg(richFont.pointSize()));
        codeFontLabel->setText(QString("Code: %1, %2pt").arg(codeFont.family()).arg(codeFont.pointSize()));
    }

    void pickPlainFont()
    {
        QSettings s;
        bool ok = false;
        const QFont chosen = QFontDialog::getFont(&ok, s.value("fonts/plainText", QFont()).value<QFont>(),
                                                    this, "Choose Plain Text Font");
        if (ok) {
            s.setValue("fonts/plainText", chosen);
            updateFontLabels();
        }
    }

    void pickRichFont()
    {
        QSettings s;
        bool ok = false;
        const QFont chosen = QFontDialog::getFont(&ok, s.value("fonts/richText", QFont()).value<QFont>(),
                                                    this, "Choose Rich Text Font");
        if (ok) {
            s.setValue("fonts/richText", chosen);
            updateFontLabels();
        }
    }

    void pickCodeFont()
    {
        QSettings s;
        bool ok = false;
        const QFont chosen = QFontDialog::getFont(&ok, s.value("fonts/code", QFont("monospace")).value<QFont>(),
                                                    this, "Choose Code Font");
        if (ok) {
            s.setValue("fonts/code", chosen);
            updateFontLabels();
        }
    }

    void resetToDefaults()
    {
        QSettings s;
        s.remove("file/autoAppendTxtExtension");
        s.remove("file/saveEncoding");
        s.remove("file/defaultMode");
        s.remove("typing/smartQuotes");
        s.remove("typing/smartDashes");
        s.remove("typing/smartLinks");
        s.remove("fonts/plainText");
        s.remove("fonts/richText");
        s.remove("fonts/code");
        s.remove("paste/richTextChoice");
        loadFromSettings();
        emit smartQuotesChanged(true);
        emit smartDashesChanged(true);
        emit smartLinksChanged(true);
    }
};

class ConfigHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    using QSyntaxHighlighter::QSyntaxHighlighter;

protected:
    void highlightBlock(const QString &text) override
    {
        static const QRegularExpression keyPattern(R"(^\s*([A-Za-z0-9_.\-]+)\s*[:=])");
        static const QRegularExpression commentPattern(R"(#.*$)");

        const auto keyMatch = keyPattern.match(text);
        if (keyMatch.hasMatch()) {
            QTextCharFormat keyFormat;
            keyFormat.setForeground(QColor(0x4a, 0x86, 0xc8));
            keyFormat.setFontWeight(QFont::Bold);
            setFormat(keyMatch.capturedStart(1), keyMatch.capturedLength(1), keyFormat);
        }

        const auto commentMatch = commentPattern.match(text);
        if (commentMatch.hasMatch()) {
            QTextCharFormat commentFormat;
            commentFormat.setForeground(QColor(0x6a, 0x99, 0x55));
            setFormat(commentMatch.capturedStart(), commentMatch.capturedLength(), commentFormat);
        }
    }
};

class TextReplacementsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TextReplacementsDialog(QWidget *parent = nullptr) : QDialog(parent)
    {
        setWindowTitle("Text Replacements");
        resize(400, 300);

        table = new QTableWidget(0, 2, this);
        table->setHorizontalHeaderLabels({"Replace", "With"});
        table->horizontalHeader()->setStretchLastSection(true);

        auto *addBtn = new QPushButton("Add");
        auto *removeBtn = new QPushButton("Remove");
        connect(addBtn, &QPushButton::clicked, this, [this] { table->insertRow(table->rowCount()); });
        connect(removeBtn, &QPushButton::clicked, this, [this] {
            const auto rows = table->selectionModel()->selectedRows();
            for (auto it = rows.rbegin(); it != rows.rend(); ++it)
                table->removeRow(it->row());
        });

        auto *btnRow = new QHBoxLayout();
        btnRow->addWidget(addBtn);
        btnRow->addWidget(removeBtn);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::close);

        auto *layout = new QVBoxLayout(this);
        layout->addWidget(table);
        layout->addLayout(btnRow);
        layout->addWidget(buttons);

        loadRules();
        connect(table, &QTableWidget::cellChanged, this, &TextReplacementsDialog::saveRules);
    }

signals:
    void rulesChanged();

private:
    QTableWidget *table;

    void loadRules()
    {
        table->blockSignals(true);
        table->setRowCount(0);
        QSettings settings;
        const int size = settings.beginReadArray("textReplacements");
        for (int i = 0; i < size; ++i) {
            settings.setArrayIndex(i);
            const int row = table->rowCount();
            table->insertRow(row);
            table->setItem(row, 0, new QTableWidgetItem(settings.value("from").toString()));
            table->setItem(row, 1, new QTableWidgetItem(settings.value("to").toString()));
        }
        settings.endArray();
        table->blockSignals(false);
    }

    void saveRules()
    {
        QSettings settings;
        settings.beginWriteArray("textReplacements");
        int index = 0;
        for (int row = 0; row < table->rowCount(); ++row) {
            const QTableWidgetItem *fromItem = table->item(row, 0);
            const QString from = fromItem ? fromItem->text() : QString();
            if (from.isEmpty())
                continue;
            const QTableWidgetItem *toItem = table->item(row, 1);
            settings.setArrayIndex(index++);
            settings.setValue("from", from);
            settings.setValue("to", toItem ? toItem->text() : QString());
        }
        settings.endArray();
        emit rulesChanged();
    }
};

class MainWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MainWidget(QWidget *parent = nullptr, bool startRich = false) : QWidget(parent)
    {
        editor = new RichAwareTextEdit(this);
        editor->pasteInterceptor = [this](const QMimeData *source) { return handleRichPaste(source); };
        applySmartTypingSettings();
        applyHighlighterForMode();
        applyRichTextMode(startRich);
        applyFontForMode();

        findInput = new FindLineEdit(this);
        findBar = new QWidget(this);
        findCountLabel = new QLabel(findBar);

        auto *findLayout = new QHBoxLayout(findBar);
        findLayout->setContentsMargins(4, 4, 4, 4);
        findLayout->addWidget(findInput);
        findLayout->addWidget(findCountLabel);
        findBar->setVisible(false);

        menuBar = new QMenuBar(this);
        menuBar->setStyleSheet("QMenuBar { padding: 0px; } QMenuBar::item { padding: 2px 8px; }");
        setupMenuBar(menuBar);

        auto *mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->setSpacing(0);
        mainLayout->setMenuBar(menuBar);
        mainLayout->addWidget(editor, 1);
        mainLayout->addWidget(findBar);

        menuBar->setVisible(false);

        connectFindBar();
        connect(editor, &QTextEdit::textChanged, this, &MainWidget::updateWindowTitle);

        qApp->installEventFilter(this);

        updateWindowTitle();
    }

private:
    enum class PasteChoice { Ask, OpenRichWindow, PasteAsPlain };

    RichAwareTextEdit *editor;
    QWidget *findBar;
    FindLineEdit *findInput;
    QLabel *findCountLabel;
    QList<QTextCursor> findMatches;
    int findMatchIndex = -1;
    QAction *richTextAction = nullptr;
    QString currentFilePath;
    bool richTextMode = false;
    bool isOtherFileType = false;
    ConfigHighlighter *configHighlighter = nullptr;
    QMenuBar *menuBar = nullptr;
    bool altTapCandidate = false;

    template <typename Func>
    QAction *addMenuAction(QMenu *menu, const QString &text, const QKeySequence &shortcut,
                            QObject *context, Func slot)
    {
        QAction *action = menu->addAction(text);
        action->setShortcut(shortcut);
        addAction(action);
        connect(action, &QAction::triggered, context, slot);
        return action;
    }

    void setupMenuBar(QMenuBar *bar)
    {
        auto *fileMenu = bar->addMenu("&File");
        addMenuAction(fileMenu, "&New", QKeySequence::New, this, &MainWidget::newDocument);
        addMenuAction(fileMenu, "&Open...", QKeySequence::Open, this, &MainWidget::openDocument);
        addMenuAction(fileMenu, "&Save", QKeySequence::Save, this, &MainWidget::saveDocument);
        addMenuAction(fileMenu, "Save &As...", QKeySequence::SaveAs, this, &MainWidget::saveDocumentAs);
        fileMenu->addSeparator();
        addMenuAction(fileMenu, "&Print...", QKeySequence::Print, this, &MainWidget::printDocument);
        fileMenu->addSeparator();
        addMenuAction(fileMenu, "&Close Window", QKeySequence::Close, this, &QWidget::close);
        addMenuAction(fileMenu, "&Quit", QKeySequence::Quit, this, &QWidget::close);

        auto *editMenu = bar->addMenu("&Edit");
        addMenuAction(editMenu, "&Undo", QKeySequence::Undo, this, [this] { editor->undo(); });
        addMenuAction(editMenu, "&Redo", QKeySequence::Redo, this, [this] { editor->redo(); });
        editMenu->addSeparator();
        addMenuAction(editMenu, "Cu&t", QKeySequence::Cut, this, [this] { editor->cut(); });
        addMenuAction(editMenu, "&Copy", QKeySequence::Copy, this, [this] { editor->copy(); });
        addMenuAction(editMenu, "&Paste", QKeySequence::Paste, this, [this] { editor->paste(); });
        addMenuAction(editMenu, "Select &All", QKeySequence::SelectAll, this, [this] { editor->selectAll(); });
        editMenu->addSeparator();
        addMenuAction(editMenu, "&Find...", QKeySequence::Find, this, &MainWidget::showFindBar);
        addMenuAction(editMenu, "Find &Next", QKeySequence::FindNext, this, &MainWidget::findNext);
        addMenuAction(editMenu, "Find Pre&vious", QKeySequence::FindPrevious, this, &MainWidget::findPrevious);

        auto *formatMenu = bar->addMenu("F&ormat");
        addMenuAction(formatMenu, "Zoom &In", QKeySequence::ZoomIn, this, [this] { editor->zoomIn(); });
        addMenuAction(formatMenu, "Zoom &Out", QKeySequence::ZoomOut, this, [this] { editor->zoomOut(); });
        formatMenu->addSeparator();

        auto *showInvisiblesAction = formatMenu->addAction("Show Invisible &Characters");
        showInvisiblesAction->setCheckable(true);
        showInvisiblesAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Apostrophe));
        addAction(showInvisiblesAction);
        connect(showInvisiblesAction, &QAction::toggled, this, &MainWidget::setShowInvisibles);

        formatMenu->addSeparator();
        auto *replacementsAction = formatMenu->addAction("Text &Replacements...");
        addAction(replacementsAction);
        connect(replacementsAction, &QAction::triggered, this, [this] {
            auto *dialog = new TextReplacementsDialog(this);
            connect(dialog, &TextReplacementsDialog::rulesChanged, this, [this] {
                editor->textReplacementRules = loadTextReplacementRules();
            });
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
        });

        formatMenu->addSeparator();
        richTextAction = formatMenu->addAction(richTextMode ? "Make &Plain Text" : "Make &Rich Text");
        addAction(richTextAction);
        connect(richTextAction, &QAction::triggered, this, &MainWidget::switchModeInNewWindow);

        auto *settingsMenu = bar->addMenu("&Settings");
        addMenuAction(settingsMenu, "&Preferences...", QKeySequence::Preferences, this, &MainWidget::openPreferences);
    }

    static bool loadTypingSetting(const QString &key)
    {
        QSettings settings;
        return settings.value(key, true).toBool();
    }

    static QList<QPair<QString, QString>> loadTextReplacementRules()
    {
        QSettings settings;
        QList<QPair<QString, QString>> rules;
        const int size = settings.beginReadArray("textReplacements");
        for (int i = 0; i < size; ++i) {
            settings.setArrayIndex(i);
            rules.append({settings.value("from").toString(), settings.value("to").toString()});
        }
        settings.endArray();
        return rules;
    }

    void applyFontForMode() const
    {
        QSettings settings;

        if (isOtherFileType) {
            editor->setFont(settings.value("fonts/code", QFont("monospace")).value<QFont>());
            return;
        }

        const QString key = richTextMode ? "fonts/richText" : "fonts/plainText";
        editor->setFont(settings.value(key, editor->font()).value<QFont>());
    }

    // in code mode there is not smart stuff enabled for this mode
    void applySmartTypingSettings() const
    {
        if (isOtherFileType) {
            editor->smartQuotesEnabled = false;
            editor->smartDashesEnabled = false;
            editor->smartLinksEnabled = false;
            editor->textReplacementEnabled = false;
            return;
        }

        editor->smartQuotesEnabled = loadTypingSetting("typing/smartQuotes");
        editor->smartDashesEnabled = loadTypingSetting("typing/smartDashes");
        editor->smartLinksEnabled = loadTypingSetting("typing/smartLinks");
        editor->textReplacementEnabled = true;
        editor->textReplacementRules = loadTextReplacementRules();
    }

    void applyHighlighterForMode()
    {
        if (!configHighlighter)
            configHighlighter = new ConfigHighlighter(editor->document());
        configHighlighter->setDocument(isOtherFileType ? editor->document() : nullptr);
    }

    void openPreferences()
    {
        SettingsDialog dialog(this);
        connect(&dialog, &SettingsDialog::smartQuotesChanged, this, [this](bool enabled) {
            editor->smartQuotesEnabled = enabled;
        });
        connect(&dialog, &SettingsDialog::smartDashesChanged, this, [this](bool enabled) {
            editor->smartDashesEnabled = enabled;
        });
        connect(&dialog, &SettingsDialog::smartLinksChanged, this, [this](bool enabled) {
            editor->smartLinksEnabled = enabled;
        });
        dialog.exec();
        applyFontForMode();
    }

    void connectFindBar()
    {
        connect(findInput, &QLineEdit::textChanged, this, &MainWidget::updateHighlights);
        connect(findInput, &FindLineEdit::closeRequested, this, &MainWidget::hideFindBar);
        connect(findInput, &FindLineEdit::nextRequested, this, [this] { performSearch(true); });
        connect(findInput, &FindLineEdit::previousRequested, this, [this] { performSearch(false); });
    }

    void updateWindowTitle()
    {
        QString name = currentFilePath.isEmpty() ? "untitled" : QFileInfo(currentFilePath).fileName();
        if (editor->document()->isModified())
            name.prepend("* ");
        setWindowTitle(name);
    }

    void applyRichTextMode(bool rich)
    {
        richTextMode = rich;
        editor->setAcceptRichText(rich);
    }

    void openWindowWithMode(bool rich, const QString &content = QString(), bool contentIsHtml = false)
    {
        auto *newWindow = new MainWidget(nullptr, rich);
        newWindow->setAttribute(Qt::WA_DeleteOnClose);

        if (!content.isEmpty()) {
            if (contentIsHtml)
                newWindow->editor->setHtml(content);
            else
                newWindow->editor->setPlainText(content);
            newWindow->editor->document()->setModified(true);
            newWindow->updateWindowTitle();
        }

        newWindow->resize(684, 420);
        newWindow->show();
    }

    void switchModeInNewWindow()
    {
        if (richTextMode)
            openWindowWithMode(false, editor->toPlainText());
        else
            openWindowWithMode(true, editor->toPlainText().isEmpty() ? QString() : editor->toHtml(), true);
    }

    static PasteChoice loadRememberedPasteChoice()
    {
        QSettings settings;
        return static_cast<PasteChoice>(
            settings.value("paste/richTextChoice", static_cast<int>(PasteChoice::Ask)).toInt());
    }

    static void saveRememberedPasteChoice(PasteChoice choice)
    {
        QSettings settings;
        settings.setValue("paste/richTextChoice", static_cast<int>(choice));
    }

    static bool htmlHasRealFormatting(const QString &html)
    {
        QTextDocument doc;
        doc.setHtml(html);

        for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
            if (block.textList())
                return true;

            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const QTextFragment fragment = it.fragment();
                if (!fragment.isValid())
                    continue;

                const QTextCharFormat format = fragment.charFormat();
                if (format.fontWeight() > QFont::Normal || format.fontItalic() || format.fontUnderline()
                    || format.fontStrikeOut() || format.isAnchor())
                    return true;
            }
        }

        return false;
    }

    bool handleRichPaste(const QMimeData *source)
    {
        if (richTextMode || !source->hasHtml() || !htmlHasRealFormatting(source->html()))
            return false;

        const QString html = source->html();
        const QString plain = source->text();
        QTimer::singleShot(0, this, [this, html, plain] { promptRichPaste(html, plain); });

        return true;
    }

    void promptRichPaste(const QString &html, const QString &plain)
    {
        PasteChoice choice = loadRememberedPasteChoice();
        if (choice == PasteChoice::Ask) {
            QMessageBox box(this);
            box.setWindowTitle("Rich Text Pasted");
            box.setText("The pasted content has formatting, but this document is Plain Text.");
            QPushButton *richBtn = box.addButton("Open in Rich Text Window", QMessageBox::AcceptRole);
            QPushButton *plainBtn = box.addButton("Paste as Plain Text", QMessageBox::RejectRole);
            box.setDefaultButton(plainBtn);
            auto *rememberBox = new QCheckBox("Remember my choice");
            box.setCheckBox(rememberBox);
            box.exec();

            QAbstractButton *clicked = box.clickedButton();
            choice = (clicked == richBtn) ? PasteChoice::OpenRichWindow : PasteChoice::PasteAsPlain;
            if (clicked && rememberBox->isChecked())
                saveRememberedPasteChoice(choice);
        }

        if (choice == PasteChoice::OpenRichWindow)
            openWindowWithMode(true, html, true);
        else
            editor->insertPlainText(plain);
    }

    void newDocument()
    {
        if (!confirmDiscardIfNeeded())
            return;

        editor->clear();
        currentFilePath.clear();
        isOtherFileType = false;
        applyFontForMode();
        applySmartTypingSettings();
        applyHighlighterForMode();
        editor->document()->setModified(false);
        updateWindowTitle();
    }

public:
    void openExternalFile(const QString &path, bool otherFileType)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return;

        editor->setPlainText(decodeFileContent(file.readAll()));
        currentFilePath = path;
        isOtherFileType = otherFileType;
        applyFontForMode();
        applySmartTypingSettings();
        applyHighlighterForMode();
        editor->document()->setModified(false);
        updateWindowTitle();
    }

    void openDocument()
    {
        const QString path = QFileDialog::getOpenFileName(
            this, "Open File", QString(),
            "Text Files (*.txt);;HTML Files (*.html *.htm);;All Files (*)");
        if (path.isEmpty())
            return;

        const QString suffix = QFileInfo(path).suffix().toLower();

        if (suffix == "html" || suffix == "htm") {
            openHtmlFileInNewWindow(path);
            return;
        }

        if (!confirmDiscardIfNeeded())
            return;

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return;

        editor->setPlainText(decodeFileContent(file.readAll()));
        currentFilePath = path;
        isOtherFileType = (suffix != "txt");
        applyFontForMode();
        applySmartTypingSettings();
        editor->document()->setModified(false);
        updateWindowTitle();
    }

    // with <!DOCTYPE HTML> user declares about code mode
    static bool containsHtmlDoctype(const QString &text)
    {
        static const QRegularExpression doctypePattern(
            R"(<!doctype\s+html)", QRegularExpression::CaseInsensitiveOption);
        return doctypePattern.match(text.left(1000)).hasMatch();
    }

public:
    static void openHtmlFileInNewWindow(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return;

        const QString content = decodeFileContent(file.readAll());
        const bool asCode = containsHtmlDoctype(content);

        auto *newWindow = new MainWidget(nullptr, !asCode);
        newWindow->setAttribute(Qt::WA_DeleteOnClose);
        newWindow->isOtherFileType = asCode;
        newWindow->applyFontForMode();
        newWindow->applySmartTypingSettings();
        newWindow->applyHighlighterForMode();

        if (asCode)
            newWindow->editor->setPlainText(content);
        else
            newWindow->editor->setHtml(content);

        newWindow->currentFilePath = path;
        newWindow->editor->document()->setModified(false);
        newWindow->updateWindowTitle();
        newWindow->resize(684, 420);
        newWindow->show();
    }

void saveDocument()
    {
        if (currentFilePath.isEmpty()) {
            saveDocumentAs();
            return;
        }

        const QByteArray content = richTextMode
            ? editor->toHtml().toUtf8()
            : encodePlainText(editor->toPlainText());

        QFile file(currentFilePath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(content);
            editor->document()->setModified(false);
            updateWindowTitle();
            return;
        }

        if (file.error() != QFile::PermissionsError)
            return;

        if (!offerPrivilegedSave())
            return;

        if (writeWithPrivileges(currentFilePath, content)) {
            editor->document()->setModified(false);
            updateWindowTitle();
        } else {
            QMessageBox::warning(this, "Save Failed",
                                  "Could not save the file, even with elevated privileges.");
        }
    }

    static QByteArray encodePlainText(const QString &text)
    {
        QSettings settings;
        const QString encoding = settings.value("file/saveEncoding", "UTF-8").toString();

        if (encoding == "Windows-1251") {
            QTextCodec *codec = QTextCodec::codecForName("Windows-1251");
            return codec->fromUnicode(text);
        }

        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        QTextStream out(&buffer);
        out.setEncoding(encoding == "UTF-16" ? QStringConverter::Utf16 : QStringConverter::Utf8);
        out << text;
        out.flush();
        return bytes;
    }

    bool offerPrivilegedSave()
    {
        const auto choice = QMessageBox::warning(
            this, "Permission Denied",
            "You don't have write access to this file. Try saving with elevated privileges?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        return choice == QMessageBox::Yes;
    }

    // Delegates to pkexec
    static bool writeWithPrivileges(const QString &path, const QByteArray &content)
    {
        QProcess proc;
        proc.start("pkexec", {"tee", path});
        if (!proc.waitForStarted())
            return false;

        proc.write(content);
        proc.closeWriteChannel();
        proc.waitForFinished(-1);

        return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
    }

    // on open BOM first (covers all UTF) then fallbacks to UTF-8 and finally assumes Windows encoding
    static QString decodeFileContent(const QByteArray &data)
    {
        if (const auto bomEncoding = QStringConverter::encodingForData(data)) {
            QStringDecoder decoder(*bomEncoding);
            return decoder(data);
        }

        QStringDecoder utf8Decoder(QStringConverter::Utf8);
        const QString asUtf8 = utf8Decoder(data);
        if (!utf8Decoder.hasError())
            return asUtf8;

        QTextCodec *codec = QTextCodec::codecForName("Windows-1251");
        return codec->toUnicode(data);
    }

    void saveDocumentAs()
    {
        const QString filter = richTextMode
            ? "HTML Files (*.html)"
            : "Text Files (*.txt);;All Files (*)";

        QString path = QFileDialog::getSaveFileName(this, "Save File As", QString(), filter);
        if (path.isEmpty())
            return;

        if (QFileInfo(path).suffix().isEmpty()) {
            QSettings settings;
            const bool autoAppend = richTextMode || settings.value("file/autoAppendTxtExtension", true).toBool();
            if (autoAppend)
                path += richTextMode ? ".html" : ".txt";
        }

        currentFilePath = path;
        saveDocument();
    }

    bool confirmDiscardIfNeeded()
    {
        if (!editor->document()->isModified())
            return true;

        const auto choice = QMessageBox::warning(
            this, "Unsaved Changes",
            "This document has unsaved changes. Save before continuing?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);

        if (choice == QMessageBox::Cancel)
            return false;

        if (choice == QMessageBox::Save) {
            saveDocument();
            return !editor->document()->isModified();
        }

        return true;
    }

    void printDocument()
    {
        QPrinter printer;
        QPrintDialog dialog(&printer, this);
        if (dialog.exec() == QDialog::Accepted)
            editor->print(&printer);
    }

void showFindBar()
    {
        findBar->setVisible(true);
        findInput->setFocus();
        findInput->selectAll();
        updateHighlights();
    }

    void hideFindBar()
    {
        findBar->setVisible(false);
        findMatches.clear();
        findMatchIndex = -1;
        editor->setExtraSelections({});
        editor->setFocus();
    }

    void updateHighlights()
    {
        findMatches.clear();
        findMatchIndex = -1;

        const QString term = findInput->text();
        if (!term.isEmpty()) {
            QTextCursor cursor(editor->document());
            while (true) {
                cursor = editor->document()->find(term, cursor);
                if (cursor.isNull())
                    break;
                findMatches.append(cursor);
            }
        }

        if (!findMatches.isEmpty()) {
            const int cursorPos = editor->textCursor().position();
            findMatchIndex = 0;
            for (int i = 0; i < findMatches.size(); ++i) {
                if (findMatches.at(i).selectionStart() >= cursorPos) {
                    findMatchIndex = i;
                    break;
                }
            }
        }

        jumpToCurrentMatch();
    }

    void jumpToCurrentMatch()
    {
        if (findMatchIndex < 0 || findMatches.isEmpty()) {
            editor->setExtraSelections({});
            findCountLabel->setText(findInput->text().isEmpty() ? "" : "0/0");
            return;
        }

        const QTextCursor &match = findMatches.at(findMatchIndex);
        editor->setTextCursor(match);

        QTextEdit::ExtraSelection sel;
        sel.cursor = match;
        sel.format.setBackground(QColor(255, 235, 100));
        editor->setExtraSelections({sel});

        findCountLabel->setText(QString("%1/%2").arg(findMatchIndex + 1).arg(findMatches.size()));
    }

    void findNext() { if (findBar->isVisible()) performSearch(true); else showFindBar(); }
    void findPrevious() { if (findBar->isVisible()) performSearch(false); else showFindBar(); }

    void performSearch(bool forward)
    {
        if (findMatches.isEmpty())
            return;

        findMatchIndex = forward
            ? (findMatchIndex + 1) % findMatches.size()
            : (findMatchIndex - 1 + findMatches.size()) % findMatches.size();

        jumpToCurrentMatch();
    }

    void setShowInvisibles(bool enabled) const
    {
        QTextOption opt = editor->document()->defaultTextOption();
        auto flags = opt.flags();
        if (enabled)
            flags |= QTextOption::ShowTabsAndSpaces | QTextOption::ShowLineAndParagraphSeparators;
        else
            flags &= ~(QTextOption::ShowTabsAndSpaces | QTextOption::ShowLineAndParagraphSeparators);
        opt.setFlags(flags);
        editor->document()->setDefaultTextOption(opt);
    }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (confirmDiscardIfNeeded())
            event->accept();
        else
            event->ignore();
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (QApplication::activeWindow() != this)
            return QWidget::eventFilter(watched, event);

        if (event->type() == QEvent::KeyPress) {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->key() == Qt::Key_Alt && keyEvent->modifiers() == Qt::AltModifier)
                altTapCandidate = true;
            else if (keyEvent->key() != Qt::Key_Alt)
                altTapCandidate = false;
        } else if (event->type() == QEvent::KeyRelease) {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->key() == Qt::Key_Alt) {
                if (altTapCandidate)
                    menuBar->setVisible(!menuBar->isVisible());
                altTapCandidate = false;
            }
        }

        return QWidget::eventFilter(watched, event);
    }
};

#include "main.moc"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("system-editor");
    QApplication::setApplicationName("system-editor");

    const QStringList args = QApplication::arguments();

    if (args.size() > 1) {
        const QString path = args.at(1);
        const QString suffix = QFileInfo(path).suffix().toLower();

        if (suffix == "html" || suffix == "htm") {
            MainWidget::openHtmlFileInNewWindow(path);
        } else {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly)) {
                QSettings settings;
                const bool startRich = settings.value("file/defaultMode", "plain").toString() == "rich";

                auto *window = new MainWidget(nullptr, startRich);
                window->setAttribute(Qt::WA_DeleteOnClose);
                window->openExternalFile(path, suffix != "txt");
                window->resize(684, 420);
                window->show();
            }
        }
    } else {
        QSettings settings;
        const bool startRich = settings.value("file/defaultMode", "plain").toString() == "rich";
        auto *window = new MainWidget(nullptr, startRich);
        window->setAttribute(Qt::WA_DeleteOnClose);
        window->resize(712, 420);
        window->show();
    }
    return app.exec();
}
