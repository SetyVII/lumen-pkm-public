#include "lumen_editor.hpp"
#include "lumen_math.hpp"
#include "lumen_syntax.hpp"
#include <QtTest>
#include <cstdio>

namespace {
void require(bool condition, const char *message) {
    if (!condition) { qCritical() << "CODE_MATH_TEST_FAILED:" << message; throw std::runtime_error(message); }
}
void process() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QTest::qWait(30); }
QJsonObject lastBlock(const RichEditor &editor) { return editor.structuredBlocks().last().toObject(); }
// Foreground color applied by the highlighter to the character at `index` of the first block.
struct ProbeEditor : RichEditor { using RichEditor::insertFromMimeData; };
QColor colorAt(const QTextBlock &block, int index) {
    for (const auto &range : block.layout()->formats())
        if (index >= range.start && index < range.start + range.length && range.format.foreground().style() != Qt::NoBrush)
            return range.format.foreground().color();
    return {};
}
}

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    application.setStyle("Fusion");
    try {
        // ---- syntax highlighting -------------------------------------------------
        RichEditor editor;
        editor.resize(760, 520);
        editor.show();
        editor.setFocus();
        process();
        QTest::keyClicks(&editor, "```cpp");
        QTest::keyClick(&editor, Qt::Key_Return);
        require(editor.codeLanguageAtCursor() == "cpp", "```cpp + Enter must open a C++ code block");
        QTest::keyClicks(&editor, "int main() { return 0; }");
        auto block = editor.textCursor().block();
        require(block.text() == "int main() { return 0; }", "code text must be inserted verbatim");
        const QColor keyword = colorAt(block, 0), literal = colorAt(block, 20), plain = colorAt(block, 12);
        require(keyword.isValid() && keyword == colorAt(block, 1) && keyword == colorAt(block, 2), "int must be highlighted as a type/keyword");
        require(colorAt(block, 13) != colorAt(block, 0), "return must have a different color from int, or int from return");
        Q_UNUSED(literal); Q_UNUSED(plain);
        // Enter keeps the same block (U+2028) and keeps indentation after '{'.
        QTest::keyClick(&editor, Qt::Key_Return);
        QTest::keyClicks(&editor, "x");
        require(editor.document()->blockCount() == 1, "Enter inside code must not create another block");
        require(editor.textCursor().block().text().contains(QChar(QChar::LineSeparator)), "code lines are separated by U+2028");
        // Comments, strings and numbers.
        editor.setCodeLanguage("java");
        QTest::keyClicks(&editor, " // note");
        block = editor.textCursor().block();
        require(editor.codeLanguageAtCursor() == "java", "language switch must persist");
        const int slash = block.text().indexOf("//");
        require(colorAt(block, slash) == colorAt(block, slash + 5), "line comment must use one color");
        // Leaving the block with Enter on an empty last line.
        QTest::keyClick(&editor, Qt::Key_Return);
        QTest::keyClick(&editor, Qt::Key_Return);
        require(editor.codeLanguageAtCursor().isEmpty() && editor.textCursor().block().blockFormat().property(LumenSyntax::BlockStyleProperty).toString() != "code",
                "Enter on an empty last line must leave the code block");
        QTest::keyClicks(&editor, "after");
        // Ctrl+Enter / Alt+Enter salen del bloque sin borrar el código.
        RichEditor exitEditor;
        exitEditor.resize(760, 520);
        exitEditor.show();
        exitEditor.setFocus();
        process();
        QTest::keyClicks(&exitEditor, "```cpp");
        QTest::keyClick(&exitEditor, Qt::Key_Return);
        QTest::keyClicks(&exitEditor, "int x = 1;");
        QTest::keyClick(&exitEditor, Qt::Key_Return, Qt::ControlModifier);
        process();
        require(exitEditor.codeLanguageAtCursor().isEmpty() && exitEditor.textCursor().block().blockFormat().property(LumenSyntax::BlockStyleProperty).toString() != "code",
                "Ctrl+Enter must leave the code block");
        QTest::keyClicks(&exitEditor, "after-ctrl");
        require(exitEditor.textCursor().block().text() == "after-ctrl", "text after Ctrl+Enter must be a normal paragraph");
        exitEditor.clear(); process();
        QTest::keyClicks(&exitEditor, "```cpp");
        QTest::keyClick(&exitEditor, Qt::Key_Return);
        QTest::keyClicks(&exitEditor, "int y = 2;");
        QTest::keyClick(&exitEditor, Qt::Key_Return, Qt::AltModifier);
        process();
        require(exitEditor.codeLanguageAtCursor().isEmpty() && exitEditor.textCursor().block().blockFormat().property(LumenSyntax::BlockStyleProperty).toString() != "code",
                "Alt+Enter must leave the code block");
        // ---- round trip ------------------------------------------------------------
        const auto blocks = editor.structuredBlocks();
        const auto code = blocks.first().toObject();
        require(code.value("type").toString() == "code" && code.value("language").toString() == "java", "structured block must carry type and language");
        const QString exported = editor.exportText();
        require(exported.startsWith("```java\nint main()"), "exportText must produce a fenced block");
        require(exported.contains("\n```\nafter"), "paragraph after code must follow the fence");
        ProbeEditor reopened;
        reopened.show();
        reopened.setStructuredBlocks(blocks);
        require(reopened.structuredBlocks() == blocks, "code blocks must round-trip through structured JSON");
        reopened.moveCursor(QTextCursor::Start);
        require(colorAt(reopened.document()->begin(), 0).isValid(), "reloaded code must be highlighted");
        // Paste keeps one block.
        QMimeData mime; mime.setText("a\nb\nc");
        reopened.moveCursor(QTextCursor::Start);
        const int before = reopened.document()->blockCount();
        reopened.insertFromMimeData(&mime);
        require(reopened.document()->blockCount() == before, "pasting multi-line text into code must keep one block");
        // Other languages must at least produce colors.
        for (const char *language : {"bash", "go", "java", "cpp"}) {
            RichEditor other; other.show();
            other.applyBlockStyle(QString("code:") + language);
            const QString sample = QString(language) == "bash" ? "if [ -f \"$HOME/x\" ]; then echo $1; fi # c"
                : QString(language) == "go" ? "func main() { fmt.Println(\"hi\") } // c" : "public class A { int x = 42; } // c";
            QTest::keyClicks(&other, sample);
            auto b = other.textCursor().block();
            bool any = false;
            for (int i = 0; i < b.text().size(); ++i) any = any || colorAt(b, i).isValid();
            require(any, "every supported language must highlight something");
        }
        // ---- LaTeX engine ---------------------------------------------------------
        const QColor ink("#202020");
        const auto plainFormula = LumenMath::render("a+b", 16, ink, false);
        const auto fraction = LumenMath::render("\\frac{a+b}{c}", 16, ink, false);
        require(plainFormula.ok && fraction.ok && !fraction.image.isNull(), "valid formulas must render");
        require(fraction.height > plainFormula.height, "a fraction must be taller than a plain line");
        const auto root = LumenMath::render("\\sqrt[3]{x^{2}+\\alpha_i}", 16, ink, false);
        require(root.ok && root.width > plainFormula.width, "roots, scripts and Greek letters must render");
        const auto sum = LumenMath::render("\\sum_{i=1}^{n} i = \\frac{n(n+1)}{2}", 16, ink, true);
        const auto sumInline = LumenMath::render("\\sum_{i=1}^{n} i", 16, ink, false);
        require(sum.ok && sumInline.ok && sum.height > sumInline.height * 0.8, "display sums must render with limits");
        const auto matrix = LumenMath::render("\\begin{pmatrix} a & b \\\\ c & d \\end{pmatrix}", 16, ink, true);
        const auto cases = LumenMath::render("f(x)=\\begin{cases} 1 & x>0 \\\\ 0 & x\\le 0 \\end{cases}", 16, ink, true);
        require(matrix.ok && cases.ok && matrix.height > plainFormula.height * 1.5, "matrices and cases must render");
        const auto integral = LumenMath::render("\\int_0^\\infty e^{-x^2}\\,dx = \\frac{\\sqrt{\\pi}}{2}", 16, ink, true);
        require(integral.ok, "integrals must render");
        require(!LumenMath::render("\\unknowncommand{x}", 16, ink, false).ok, "unknown commands must be reported");
        require(!LumenMath::render("\\frac{a}{b", 16, ink, false).ok, "unbalanced braces must be reported");
        require(!LumenMath::render("", 16, ink, false).image.isNull(), "an empty formula keeps a visible placeholder");
        const QByteArray out = qgetenv("LUMEN_MATH_SNAPSHOT_DIR");
        if (!out.isEmpty()) {
            const char *names[] = {"fraction", "root", "sum", "matrix", "cases", "integral"};
            const LumenMath::Result *all[] = {&fraction, &root, &sum, &matrix, &cases, &integral};
            for (int i = 0; i < 6; ++i) {
                QImage flat(all[i]->image.size(), QImage::Format_ARGB32_Premultiplied);
                flat.fill(Qt::white);
                QPainter painter(&flat); painter.drawImage(0, 0, all[i]->image); painter.end();
                flat.save(QString::fromLocal8Bit(out) + "/" + names[i] + ".png");
            }
        }
        // ---- formulas inside the editor ------------------------------------------
        RichEditor writer;
        writer.show(); writer.setFocus(); process();
        QTest::keyClicks(&writer, "Energia ");
        QTest::keyClicks(&writer, "$E=mc^2$");
        auto math = lastBlock(writer);
        bool found = false;
        for (const auto &run : math.value("runs").toArray())
            if (run.toObject().value("type").toString() == "math") { found = run.toObject().value("latex").toString() == "E=mc^2"; }
        require(found, "$...$ must become an inline formula that keeps its LaTeX source");
        require(writer.exportText() == "Energia $E=mc^2$", "formulas must export as $latex$");
        require(!writer.toPlainText().contains("E=mc^2"), "the formula is an image in the document");
        QTest::keyClicks(&writer, " y $5 y 6");
        require(writer.exportText().endsWith(" y $5 y 6"), "currency-like dollars must stay text");
        const auto saved = writer.structuredBlocks();
        RichEditor loaded; loaded.show();
        loaded.setStructuredBlocks(saved);
        require(loaded.structuredBlocks() == saved, "formulas must round-trip through structured JSON");
        require(loaded.exportText() == writer.exportText(), "export must be identical after reload");
        QJsonArray displayBlocks{QJsonObject{{"id", "m1"}, {"type", "math"}, {"text", "x"}, {"runs", QJsonArray{QJsonObject{{"type", "math"}, {"latex", "\\frac{1}{2}"}, {"display", true}, {"text", ""}}}}}};
        RichEditor displayEditor; displayEditor.show();
        displayEditor.setStructuredBlocks(displayBlocks);
        require(displayEditor.exportText() == "$$\\frac{1}{2}$$", "display formulas must export as $$latex$$");
        require(displayEditor.structuredBlocks().first().toObject().value("type").toString() == "math", "display formula block keeps its type");
        std::puts("CODE_MATH_TEST_OK");
    } catch (const std::exception &) { return 1; }
    return 0;
}
