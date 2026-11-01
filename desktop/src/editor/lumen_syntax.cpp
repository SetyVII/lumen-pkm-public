#include "lumen_syntax.hpp"
#include <QColor>
#include <QSet>
#include <QTextBlock>
#include <QTextCharFormat>

namespace LumenSyntax {
const QList<Language> &languages() {
    static const QList<Language> list{{"cpp", "C++"}, {"java", "Java"}, {"bash", "Bash"}, {"go", "Go"}};
    return list;
}
QString normalize(const QString &raw) {
    const QString id = raw.trimmed().toLower();
    if (id == "cpp" || id == "c++" || id == "cc" || id == "cxx" || id == "hpp" || id == "c" || id == "h") return "cpp";
    if (id == "java") return "java";
    if (id == "bash" || id == "sh" || id == "shell" || id == "zsh") return "bash";
    if (id == "go" || id == "golang") return "go";
    return {};
}
QString label(const QString &id) {
    for (const auto &language : languages()) if (language.id == id) return language.label;
    return QStringLiteral("Texto");
}
}

namespace {
enum State { Normal = 0, BlockComment = 1, GoRaw = 2, JavaText = 3, CppRaw = 4 };

struct Rules {
    QSet<QString> keywords, types, literals;
    bool slashComments = true, hash = false, preprocessor = false, annotations = false, dollar = false;
    bool backtickRaw = false, tripleQuote = false, cppRaw = false;
};

const Rules &rulesFor(const QString &language) {
    static const Rules cpp = [] {
        Rules r; r.preprocessor = true; r.cppRaw = true;
        r.keywords = {"alignas","alignof","and","asm","auto","break","case","catch","class","co_await","co_return","co_yield","concept","const","consteval","constexpr","constinit","const_cast","continue","decltype","default","delete","do","dynamic_cast","else","enum","explicit","export","extern","final","for","friend","goto","if","inline","mutable","namespace","new","noexcept","not","operator","or","override","private","protected","public","register","reinterpret_cast","requires","return","sizeof","static","static_assert","static_cast","struct","switch","template","this","throw","try","typedef","typeid","typename","union","using","virtual","volatile","while"};
        r.types = {"bool","char","char8_t","char16_t","char32_t","double","float","int","long","short","signed","unsigned","void","wchar_t","size_t","int8_t","int16_t","int32_t","int64_t","uint8_t","uint16_t","uint32_t","uint64_t","string","vector","map","unique_ptr","shared_ptr","optional","variant","span"};
        r.literals = {"true","false","nullptr","NULL"};
        return r;
    }();
    static const Rules java = [] {
        Rules r; r.annotations = true; r.tripleQuote = true;
        r.keywords = {"abstract","assert","break","case","catch","class","continue","default","do","else","enum","extends","final","finally","for","if","implements","import","instanceof","interface","native","new","non-sealed","package","permits","private","protected","public","record","return","sealed","static","strictfp","super","switch","synchronized","this","throw","throws","transient","try","var","volatile","while","yield"};
        r.types = {"boolean","byte","char","double","float","int","long","short","void","String","Object","List","Map","Set","Integer","Long","Double","Optional","Stream"};
        r.literals = {"true","false","null"};
        return r;
    }();
    static const Rules bash = [] {
        Rules r; r.slashComments = false; r.hash = true; r.dollar = true;
        r.keywords = {"if","then","elif","else","fi","for","while","until","do","done","case","esac","in","function","select","time","return","break","continue","exit","local","export","readonly","declare","unset","shift","source","alias","trap","set","eval","exec"};
        r.types = {"echo","printf","cd","ls","cat","grep","sed","awk","find","mkdir","rm","cp","mv","chmod","chown","curl","wget","tar","git","docker","sudo","read","test","pwd","touch","head","tail","sort","uniq","xargs","tee"};
        r.literals = {"true","false"};
        return r;
    }();
    static const Rules go = [] {
        Rules r; r.backtickRaw = true;
        r.keywords = {"break","case","chan","const","continue","default","defer","else","fallthrough","for","func","go","goto","if","import","interface","map","package","range","return","select","struct","switch","type","var"};
        r.types = {"any","bool","byte","comparable","complex64","complex128","error","float32","float64","int","int8","int16","int32","int64","rune","string","uint","uint8","uint16","uint32","uint64","uintptr","make","new","len","cap","append","copy","delete","panic","recover","print","println","close"};
        r.literals = {"true","false","nil","iota"};
        return r;
    }();
    if (language == "java") return java;
    if (language == "bash") return bash;
    if (language == "go") return go;
    return cpp;
}

struct Palette { QColor keyword, type, literal, string, number, comment, function, meta, variable; };
Palette paletteFor(bool dark) {
    if (dark) return {QColor("#c792ea"), QColor("#7fc8d9"), QColor("#f78c6c"), QColor("#a5d6a0"), QColor("#f2b36d"), QColor("#7d7b86"), QColor("#82aaff"), QColor("#e5c07b"), QColor("#e8a0bf")};
    return {QColor("#8250c8"), QColor("#1a7f95"), QColor("#c2410c"), QColor("#2f7d32"), QColor("#b45309"), QColor("#8a8893"), QColor("#2563b8"), QColor("#9a6700"), QColor("#b0366f")};
}
QTextCharFormat fmt(const QColor &color, bool italic = false, bool bold = false) {
    QTextCharFormat f; f.setForeground(color); f.setFontItalic(italic);
    if (bold) f.setFontWeight(QFont::DemiBold);
    return f;
}
bool newline(QChar c) { return c == '\n' || c == QChar::LineSeparator; }
bool identStart(QChar c) { return c.isLetter() || c == '_'; }
bool identPart(QChar c) { return c.isLetterOrNumber() || c == '_'; }
}

LumenSyntaxHighlighter::LumenSyntaxHighlighter(QTextDocument *document) : QSyntaxHighlighter(document) {}
void LumenSyntaxHighlighter::setDark(bool dark) {
    if (dark_ == dark) return;
    dark_ = dark;
    rehighlight();
}

void LumenSyntaxHighlighter::highlightBlock(const QString &text) {
    setCurrentBlockState(Normal);
    const auto format = currentBlock().blockFormat();
    if (format.property(LumenSyntax::BlockStyleProperty).toString() != "code") return;
    const QString language = LumenSyntax::normalize(format.property(LumenSyntax::CodeLanguageProperty).toString());
    if (language.isEmpty()) return;
    const Rules &rules = rulesFor(language);
    const Palette colors = paletteFor(dark_);
    const auto comment = fmt(colors.comment, true), string = fmt(colors.string), number = fmt(colors.number),
        keyword = fmt(colors.keyword, false, true), type = fmt(colors.type), literal = fmt(colors.literal),
        function = fmt(colors.function), meta = fmt(colors.meta), variable = fmt(colors.variable);
    const int n = text.size();
    int i = 0;
    int state = previousBlockState();
    auto closeAt = [&](int from, const QString &closer) {
        const int found = text.indexOf(closer, from);
        return found < 0 ? -1 : found + closer.size();
    };
    // A continuing multi-line token owns the start of this block.
    if (state == BlockComment || state == GoRaw || state == JavaText || state == CppRaw) {
        const QString closer = state == BlockComment ? "*/" : state == GoRaw ? "`" : state == JavaText ? "\"\"\"" : ")\"";
        const int end = closeAt(0, closer);
        const auto &f = state == BlockComment ? comment : string;
        if (end < 0) { setFormat(0, n, f); setCurrentBlockState(state); return; }
        setFormat(0, end, f); i = end;
    }
    bool lineStart = true;
    while (i < n) {
        const QChar c = text[i];
        if (newline(c)) { lineStart = true; ++i; continue; }
        if (c.isSpace()) { ++i; continue; }
        const bool atLineStart = lineStart;
        lineStart = false;
        if (rules.preprocessor && atLineStart && c == '#') {
            int end = i; while (end < n && !newline(text[end])) ++end;
            setFormat(i, end - i, meta); i = end; continue;
        }
        if (rules.hash && c == '#' && (i == 0 || text[i - 1].isSpace() || newline(text[i - 1]))) {
            int end = i; while (end < n && !newline(text[end])) ++end;
            setFormat(i, end - i, comment); i = end; continue;
        }
        if (rules.slashComments && c == '/' && i + 1 < n && text[i + 1] == '/') {
            int end = i; while (end < n && !newline(text[end])) ++end;
            setFormat(i, end - i, comment); i = end; continue;
        }
        if (rules.slashComments && c == '/' && i + 1 < n && text[i + 1] == '*') {
            const int end = closeAt(i + 2, "*/");
            if (end < 0) { setFormat(i, n - i, comment); setCurrentBlockState(BlockComment); return; }
            setFormat(i, end - i, comment); i = end; continue;
        }
        if (rules.tripleQuote && text.mid(i, 3) == "\"\"\"") {
            const int end = closeAt(i + 3, "\"\"\"");
            if (end < 0) { setFormat(i, n - i, string); setCurrentBlockState(JavaText); return; }
            setFormat(i, end - i, string); i = end; continue;
        }
        if (rules.backtickRaw && c == '`') {
            const int end = closeAt(i + 1, "`");
            if (end < 0) { setFormat(i, n - i, string); setCurrentBlockState(GoRaw); return; }
            setFormat(i, end - i, string); i = end; continue;
        }
        if (rules.cppRaw && c == 'R' && text.mid(i, 3) == "R\"(") {
            const int end = closeAt(i + 3, ")\"");
            if (end < 0) { setFormat(i, n - i, string); setCurrentBlockState(CppRaw); return; }
            setFormat(i, end - i, string); i = end; continue;
        }
        if (c == '"' || c == '\'') {
            int end = i + 1;
            while (end < n && !newline(text[end]) && text[end] != c) {
                if (text[end] == '\\' && !rules.dollar) ++end;       // bash single quotes have no escapes
                else if (text[end] == '\\' && c == '"') ++end;
                ++end;
            }
            if (end < n && text[end] == c) ++end;
            end = qMin(end, n);
            setFormat(i, end - i, string);
            if (rules.dollar && c == '"')                              // variables inside bash double quotes
                for (int j = i + 1; j < end - 1; ++j)
                    if (text[j] == '$' && j + 1 < end && (identStart(text[j + 1]) || text[j + 1] == '{' || text[j + 1] == '(')) {
                        int stop = j + 1;
                        if (text[stop] == '{') { while (stop < end && text[stop] != '}') ++stop; ++stop; }
                        else if (text[stop] == '(') { while (stop < end && text[stop] != ')') ++stop; ++stop; }
                        else while (stop < end && identPart(text[stop])) ++stop;
                        setFormat(j, qMin(stop, end) - j, variable); j = stop - 1;
                    }
            i = end; continue;
        }
        if (rules.dollar && c == '$') {
            int end = i + 1;
            if (end < n && text[end] == '{') { while (end < n && text[end] != '}') ++end; if (end < n) ++end; }
            else if (end < n && text[end] == '(') { ++end; }
            else if (end < n && (text[end].isDigit() || QString("?@*#!$-").contains(text[end]))) ++end;
            else while (end < n && identPart(text[end])) ++end;
            setFormat(i, end - i, variable); i = end; continue;
        }
        if (rules.dollar && c == '-' && i > 0 && text[i - 1].isSpace() && i + 1 < n && (text[i + 1].isLetter() || text[i + 1] == '-')) {
            int end = i + 1; while (end < n && (identPart(text[end]) || text[end] == '-')) ++end;
            setFormat(i, end - i, meta); i = end; continue;
        }
        if (rules.annotations && c == '@' && i + 1 < n && identStart(text[i + 1])) {
            int end = i + 1; while (end < n && (identPart(text[end]) || text[end] == '.')) ++end;
            setFormat(i, end - i, meta); i = end; continue;
        }
        if (c.isDigit() || (c == '.' && i + 1 < n && text[i + 1].isDigit())) {
            int end = i + 1;
            while (end < n && (text[end].isLetterOrNumber() || text[end] == '.' || text[end] == '_' || text[end] == '\'' ||
                   ((text[end] == '+' || text[end] == '-') && (text[end - 1] == 'e' || text[end - 1] == 'E') && !text.mid(i, 2).startsWith("0x")))) ++end;
            setFormat(i, end - i, number); i = end; continue;
        }
        if (identStart(c)) {
            int end = i + 1; while (end < n && identPart(text[end])) ++end;
            const QString word = text.mid(i, end - i);
            int look = end; while (look < n && (text[look] == ' ' || text[look] == '\t')) ++look;
            const bool call = look < n && text[look] == '(';
            if (rules.keywords.contains(word)) setFormat(i, end - i, keyword);
            else if (rules.literals.contains(word)) setFormat(i, end - i, literal);
            else if (rules.types.contains(word)) setFormat(i, end - i, call && language != "bash" && language != "go" ? function : type);
            else if (language == "java" && word[0].isUpper() && word.size() > 1 && !word.isUpper()) setFormat(i, end - i, type);
            else if (language == "cpp" && word == "std") setFormat(i, end - i, type);
            else if (call) setFormat(i, end - i, function);
            i = end; continue;
        }
        ++i;
    }
}
