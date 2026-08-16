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

protected:
    void insertFromMimeData(const QMimeData *source) override
    {
        if (pasteInterceptor && pasteInterceptor(source))
            return;
        QTextEdit::insertFromMimeData(source);
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
        applyRichTextMode(startRich);

        findInput = new FindLineEdit(this);
        findBar = new QWidget(this);

        auto *findLayout = new QHBoxLayout(findBar);
        findLayout->setContentsMargins(4, 4, 4, 4);
        findLayout->addWidget(findInput);
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
    QAction *richTextAction = nullptr;
    QString currentFilePath;
    bool richTextMode = false;
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
        richTextAction = formatMenu->addAction(richTextMode ? "Make &Plain Text" : "Make &Rich Text");
        addAction(richTextAction);
        connect(richTextAction, &QAction::triggered, this, &MainWidget::switchModeInNewWindow);
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

    bool handleRichPaste(const QMimeData *source)
    {
        if (richTextMode || !source->hasHtml())
            return false;

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

            choice = (box.clickedButton() == richBtn) ? PasteChoice::OpenRichWindow : PasteChoice::PasteAsPlain;
            if (rememberBox->isChecked())
                saveRememberedPasteChoice(choice);
        }

        if (choice == PasteChoice::OpenRichWindow)
            openWindowWithMode(true, source->html(), true);
        else
            editor->insertPlainText(source->text());

        return true;
    }

    void newDocument()
    {
        if (!confirmDiscardIfNeeded())
            return;

        editor->clear();
        currentFilePath.clear();
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
        const bool rich = (suffix == "html" || suffix == "htm");

        if (rich) {
            openFileInNewWindow(path);
            return;
        }

        if (!confirmDiscardIfNeeded())
            return;

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return;

        editor->setPlainText(QTextStream(&file).readAll());
        currentFilePath = path;
        editor->document()->setModified(false);
        updateWindowTitle();
    }

    void openFileInNewWindow(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return;

        auto *newWindow = new MainWidget(nullptr, true);
        newWindow->setAttribute(Qt::WA_DeleteOnClose);
        newWindow->editor->setHtml(QTextStream(&file).readAll());
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

        QFile file(currentFilePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return;

        QTextStream out(&file);
        if (richTextMode)
            out << editor->toHtml();
        else
            out << editor->toPlainText();

        editor->document()->setModified(false);
        updateWindowTitle();
    }

    void saveDocumentAs()
    {
        const QString filter = richTextMode
            ? "HTML Files (*.html)"
            : "Text Files (*.txt);;All Files (*)";

        QString path = QFileDialog::getSaveFileName(this, "Save File As", QString(), filter);
        if (path.isEmpty())
            return;

        if (QFileInfo(path).suffix().isEmpty())
            path += richTextMode ? ".html" : ".txt";

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

    void hideFindBar() const
    {
        findBar->setVisible(false);
        editor->setExtraSelections({});
        editor->setFocus();
    }

    void updateHighlights() const
    {
        QList<QTextEdit::ExtraSelection> selections;
        const QString term = findInput->text();

        if (!term.isEmpty()) {
            QTextCursor cursor(editor->document());
            QTextCharFormat format;
            format.setBackground(QColor(255, 235, 100));

            while (true) {
                cursor = editor->document()->find(term, cursor);
                if (cursor.isNull())
                    break;
                QTextEdit::ExtraSelection sel;
                sel.cursor = cursor;
                sel.format = format;
                selections.append(sel);
            }
        }

        editor->setExtraSelections(selections);
    }

    void findNext() { if (findBar->isVisible()) performSearch(true); else showFindBar(); }
    void findPrevious() { if (findBar->isVisible()) performSearch(false); else showFindBar(); }

    void performSearch(bool forward) const
    {
        const QString term = findInput->text();
        if (term.isEmpty())
            return;

        QTextDocument::FindFlags flags;
        if (!forward)
            flags |= QTextDocument::FindBackward;

        QTextCursor found = editor->document()->find(term, editor->textCursor(), flags);
        if (found.isNull()) {
            QTextCursor wrapPoint(editor->document());
            if (!forward)
                wrapPoint.movePosition(QTextCursor::End);
            found = editor->document()->find(term, wrapPoint, flags);
        }
        if (!found.isNull())
            editor->setTextCursor(found);
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
    QApplication::setOrganizationName("systemeditor");
    QApplication::setApplicationName("systemeditor");

    MainWidget window;
    window.resize(712, 420);
    window.show();

    return app.exec();
}
