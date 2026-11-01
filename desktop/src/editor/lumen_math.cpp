#include "lumen_math.hpp"
#include <QFont>
#include <QFontMetricsF>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QSet>
#include <QtMath>
#include <array>
#include <algorithm>
#include <memory>
#include <vector>

namespace {
constexpr qreal Axis = 0.26;          // math axis height, in em
constexpr int MaxDepth = 24;

QFont mathFont(qreal size, bool italic, bool bold = false) {
    QFont font;
    font.setFamilies({"STIX Two Text", "STIX Two Math", "Latin Modern Roman", "Noto Serif", "DejaVu Serif", "serif"});
    font.setPixelSize(qMax(1, qRound(size)));
    font.setItalic(italic);
    font.setWeight(bold ? QFont::Bold : QFont::Normal);
    return font;
}

struct Box {
    qreal w = 0, a = 0, d = 0;           // width, ascent, descent
    virtual ~Box() = default;
    virtual void paint(QPainter &p, qreal x, qreal y) const = 0;
};
using BoxPtr = std::shared_ptr<Box>;

struct SpaceBox : Box {
    explicit SpaceBox(qreal width) { w = width; }
    void paint(QPainter &, qreal, qreal) const override {}
};

struct TextBox : Box {
    QString text; QFont font; QColor color; qreal dy = 0; bool limits = false;
    TextBox(const QString &t, qreal size, bool italic, bool bold, const QColor &c, bool onAxis = false, qreal boost = 1.)
        : text(t), font(mathFont(size * boost, italic, bold)), color(c) {
        const QFontMetricsF fm(font);
        w = fm.horizontalAdvance(t);
        const QRectF tight = fm.tightBoundingRect(t);
        a = qMax<qreal>(0, -tight.top()); d = qMax<qreal>(0, tight.bottom());
        if (onAxis) {
            const qreal half = tight.height() / 2;
            dy = -size * Axis - tight.center().y();
            a = size * Axis + half; d = qMax<qreal>(0, half - size * Axis);
        }
    }
    void paint(QPainter &p, qreal x, qreal y) const override {
        p.setFont(font); p.setPen(color);
        p.drawText(QPointF(x, y + dy), text);
    }
};

struct HBox : Box {
    std::vector<BoxPtr> items;
    void add(const BoxPtr &b) { items.push_back(b); w += b->w; a = qMax(a, b->a); d = qMax(d, b->d); }
    void paint(QPainter &p, qreal x, qreal y) const override {
        for (const auto &b : items) { b->paint(p, x, y); x += b->w; }
    }
};

struct FracBox : Box {
    BoxPtr num, den; qreal size, thick, shiftNum = 0, shiftDen = 0; bool rule; QColor color;
    FracBox(BoxPtr n, BoxPtr dn, qreal s, bool hasRule, QColor c) : num(n), den(dn), size(s), rule(hasRule), color(c) {
        thick = rule ? qMax<qreal>(1, s * 0.06) : 0;
        const qreal gap = s * 0.16, axis = s * Axis, pad = s * 0.1;
        w = qMax(n->w, dn->w) + 2 * pad;
        shiftNum = axis + thick / 2 + gap + n->d;      // numerator baseline above baseline
        shiftDen = -axis + thick / 2 + gap + dn->a;    // denominator baseline below baseline
        a = shiftNum + n->a; d = qMax<qreal>(0, shiftDen + dn->d);
    }
    void paint(QPainter &p, qreal x, qreal y) const override {
        num->paint(p, x + (w - num->w) / 2, y - shiftNum);
        den->paint(p, x + (w - den->w) / 2, y + shiftDen);
        if (rule) { p.setPen(QPen(color, thick, Qt::SolidLine, Qt::FlatCap)); const qreal ly = y - size * Axis; p.drawLine(QPointF(x + size * 0.04, ly), QPointF(x + w - size * 0.04, ly)); }
    }
};

struct SqrtBox : Box {
    BoxPtr body, index; qreal size, sign, idxW = 0, top = 0, bottom = 0, thick; QColor color;
    SqrtBox(BoxPtr b, BoxPtr idx, qreal s, QColor c) : body(b), index(idx), size(s), color(c) {
        thick = qMax<qreal>(1, s * 0.055);
        sign = s * 0.62;
        if (index) idxW = qMax<qreal>(0, index->w - sign * 0.35);
        top = b->a + s * 0.2; bottom = b->d;
        w = idxW + sign + b->w + s * 0.12; a = top + thick; d = bottom;
    }
    void paint(QPainter &p, qreal x, qreal y) const override {
        const qreal H = top + bottom, x0 = x + idxW, yt = y - top, yb = y + bottom;
        QPainterPath path;
        path.moveTo(x0, yt + H * 0.55);
        path.lineTo(x0 + sign * 0.2, yt + H * 0.5);
        path.lineTo(x0 + sign * 0.5, yb);
        path.lineTo(x0 + sign, yt);
        path.lineTo(x0 + sign + body->w + size * 0.1, yt);
        p.setPen(QPen(color, thick, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin)); p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        body->paint(p, x0 + sign + size * 0.02, y);
        if (index) index->paint(p, x, yt + H * 0.45);
    }
};

struct ScriptBox : Box {
    BoxPtr base, sup, sub; bool limits; qreal upShift = 0, downShift = 0, size;
    ScriptBox(BoxPtr b, BoxPtr up, BoxPtr down, bool lim, qreal s) : base(b), sup(up), sub(down), limits(lim), size(s) {
        const qreal gap = s * 0.12;
        if (limits) {
            w = base->w; if (sup) w = qMax(w, sup->w); if (sub) w = qMax(w, sub->w);
            a = base->a + (sup ? gap + sup->d + sup->a : 0); d = base->d + (sub ? gap + sub->a + sub->d : 0);
            return;
        }
        upShift = qMax(s * 0.4, base->a * 0.7);
        downShift = qMax(s * 0.2, base->d + s * 0.12);
        if (sup && sub) {
            const qreal overlap = (-upShift + sup->d) - (downShift - sub->a) + gap;   // positive when too close
            if (overlap > 0) downShift += overlap;
        }
        w = base->w + qMax(sup ? sup->w : 0, sub ? sub->w : 0) + s * 0.04;
        a = base->a; d = base->d;
        if (sup) a = qMax(a, upShift + sup->a);
        if (sub) d = qMax(d, downShift + sub->d);
    }
    void paint(QPainter &p, qreal x, qreal y) const override {
        const qreal gap = size * 0.12;
        if (limits) {
            base->paint(p, x + (w - base->w) / 2, y);
            if (sup) sup->paint(p, x + (w - sup->w) / 2, y - base->a - gap - sup->d);
            if (sub) sub->paint(p, x + (w - sub->w) / 2, y + base->d + gap + sub->a);
            return;
        }
        base->paint(p, x, y);
        if (sup) sup->paint(p, x + base->w + size * 0.04, y - upShift);
        if (sub) sub->paint(p, x + base->w + size * 0.04, y + downShift);
    }
};

struct DelimBox : Box {
    QString left, right; BoxPtr body; qreal size, lw = 0, rw = 0, half = 0, sy = 1; QColor color; QFont font;
    static qreal glyphHeight(const QFont &font, const QString &ch) { return QFontMetricsF(font).tightBoundingRect(ch).height(); }
    DelimBox(const QString &l, const QString &r, BoxPtr b, qreal s, QColor c) : left(l), right(r), body(b), size(s), color(c), font(mathFont(s, false)) {
        const qreal axis = s * Axis;
        half = qMax(qMax(b->a - axis, b->d + axis), s * 0.5) * 1.04;
        const QFontMetricsF fm(font);
        if (!left.isEmpty()) lw = fm.horizontalAdvance(left) + s * 0.08;
        if (!right.isEmpty()) rw = fm.horizontalAdvance(right) + s * 0.08;
        w = lw + b->w + rw; a = half + axis; d = qMax<qreal>(0, half - axis);
    }
    void glyph(QPainter &p, const QString &ch, qreal x, qreal y) const {
        if (ch.isEmpty()) return;
        const QRectF tight = QFontMetricsF(font).tightBoundingRect(ch);
        const qreal total = half * 2, scale = (tight.height() > 1 && total > tight.height()) ? total / tight.height() : 1.;
        p.save(); p.translate(x, y - size * Axis); p.scale(1, scale);
        p.setFont(font); p.setPen(color); p.drawText(QPointF(size * 0.02, -tight.center().y()), ch); p.restore();
    }
    void paint(QPainter &p, qreal x, qreal y) const override {
        glyph(p, left, x, y);
        body->paint(p, x + lw, y);
        glyph(p, right, x + lw + body->w, y);
    }
};

struct GridBox : Box {
    std::vector<std::vector<BoxPtr>> rows; std::vector<qreal> colW, rowA, rowD; bool leftAlign; qreal size;
    GridBox(std::vector<std::vector<BoxPtr>> r, qreal s, bool alignLeft) : rows(std::move(r)), leftAlign(alignLeft), size(s) {
        size_t cols = 0; for (const auto &row : rows) cols = qMax(cols, row.size());
        colW.assign(cols, 0); rowA.assign(rows.size(), s * 0.5); rowD.assign(rows.size(), s * 0.1);
        for (size_t i = 0; i < rows.size(); ++i) for (size_t j = 0; j < rows[i].size(); ++j) {
            colW[j] = qMax(colW[j], rows[i][j]->w); rowA[i] = qMax(rowA[i], rows[i][j]->a); rowD[i] = qMax(rowD[i], rows[i][j]->d);
        }
        const qreal colGap = s * 0.9, rowGap = s * 0.35;
        for (size_t j = 0; j < cols; ++j) w += colW[j] + (j ? colGap : 0);
        qreal height = 0; for (size_t i = 0; i < rows.size(); ++i) height += rowA[i] + rowD[i] + (i ? rowGap : 0);
        a = height / 2 + s * Axis; d = qMax<qreal>(0, height / 2 - s * Axis);
    }
    void paint(QPainter &p, qreal x, qreal y) const override {
        const qreal colGap = size * 0.9, rowGap = size * 0.35;
        qreal cy = y - a;
        for (size_t i = 0; i < rows.size(); ++i) {
            cy += rowA[i]; qreal cx = x;
            for (size_t j = 0; j < colW.size(); ++j) {
                if (j < rows[i].size()) rows[i][j]->paint(p, cx + (leftAlign ? 0 : (colW[j] - rows[i][j]->w) / 2), cy);
                cx += colW[j] + colGap;
            }
            cy += rowD[i] + rowGap;
        }
    }
};

struct AccentBox : Box {
    BoxPtr body; QString kind; qreal size; QColor color; bool below;
    AccentBox(BoxPtr b, const QString &k, qreal s, QColor c) : body(b), kind(k), size(s), color(c), below(k == "underline") {
        w = b->w; a = b->a; d = b->d;
        if (below) d += s * 0.18; else a += s * (k == "overline" ? 0.14 : 0.24);
    }
    void paint(QPainter &p, qreal x, qreal y) const override {
        body->paint(p, x, y);
        const qreal t = qMax<qreal>(1, size * 0.05);
        p.setPen(QPen(color, t, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p.setBrush(Qt::NoBrush);
        const qreal top = y - body->a - size * 0.1, cx = x + w / 2;
        if (kind == "underline") { const qreal ly = y + body->d + size * 0.1; p.drawLine(QPointF(x, ly), QPointF(x + w, ly)); }
        else if (kind == "overline" || kind == "bar") p.drawLine(QPointF(x + (kind == "bar" ? w * 0.15 : 0), top), QPointF(x + w - (kind == "bar" ? w * 0.15 : 0), top));
        else if (kind == "hat" || kind == "widehat") { const qreal h = size * 0.14, hw = qMax(size * 0.18, w * 0.3); QPainterPath path; path.moveTo(cx - hw, top); path.lineTo(cx, top - h); path.lineTo(cx + hw, top); p.drawPath(path); }
        else if (kind == "tilde" || kind == "widetilde") { const qreal tw = qMax(size * 0.2, w * 0.3); QPainterPath path; path.moveTo(cx - tw, top); path.cubicTo(cx - tw / 2, top - size * 0.16, cx, top + size * 0.02, cx + tw, top - size * 0.1); p.drawPath(path); }
        else if (kind == "vec") { const qreal h = size * 0.06; p.drawLine(QPointF(x + w * 0.1, top), QPointF(x + w * 0.95, top)); QPainterPath head; head.moveTo(x + w * 0.95 - size * 0.1, top - h * 1.6); head.lineTo(x + w * 0.95, top); head.lineTo(x + w * 0.95 - size * 0.1, top + h * 1.6); p.drawPath(head); }
        else if (kind == "dot" || kind == "ddot") { p.setPen(Qt::NoPen); p.setBrush(color); const qreal r = qMax<qreal>(1, size * 0.045); if (kind == "dot") p.drawEllipse(QPointF(cx, top - r), r, r); else { p.drawEllipse(QPointF(cx - size * 0.1, top - r), r, r); p.drawEllipse(QPointF(cx + size * 0.1, top - r), r, r); } }
    }
};

enum Class { Ord, Bin, Rel, Op };
struct Symbol { const char *text; Class cls; };

const QHash<QString, QString> &greek() {
    static const QHash<QString, QString> map{
        {"alpha","α"},{"beta","β"},{"gamma","γ"},{"delta","δ"},{"epsilon","ϵ"},{"varepsilon","ε"},{"zeta","ζ"},{"eta","η"},{"theta","θ"},{"vartheta","ϑ"},{"iota","ι"},{"kappa","κ"},{"lambda","λ"},{"mu","μ"},{"nu","ν"},{"xi","ξ"},{"pi","π"},{"varpi","ϖ"},{"rho","ρ"},{"varrho","ϱ"},{"sigma","σ"},{"varsigma","ς"},{"tau","τ"},{"upsilon","υ"},{"phi","ϕ"},{"varphi","φ"},{"chi","χ"},{"psi","ψ"},{"omega","ω"}};
    return map;
}
const QHash<QString, QString> &greekUpper() {
    static const QHash<QString, QString> map{
        {"Gamma","Γ"},{"Delta","Δ"},{"Theta","Θ"},{"Lambda","Λ"},{"Xi","Ξ"},{"Pi","Π"},{"Sigma","Σ"},{"Upsilon","Υ"},{"Phi","Φ"},{"Psi","Ψ"},{"Omega","Ω"}};
    return map;
}
const QHash<QString, Symbol> &symbols() {
    static const QHash<QString, Symbol> map{
        {"pm",{"±",Bin}},{"mp",{"∓",Bin}},{"times",{"×",Bin}},{"div",{"÷",Bin}},{"cdot",{"⋅",Bin}},{"ast",{"∗",Bin}},{"circ",{"∘",Bin}},{"oplus",{"⊕",Bin}},{"otimes",{"⊗",Bin}},{"cup",{"∪",Bin}},{"cap",{"∩",Bin}},{"wedge",{"∧",Bin}},{"land",{"∧",Bin}},{"vee",{"∨",Bin}},{"lor",{"∨",Bin}},{"setminus",{"∖",Bin}},
        {"leq",{"≤",Rel}},{"le",{"≤",Rel}},{"geq",{"≥",Rel}},{"ge",{"≥",Rel}},{"neq",{"≠",Rel}},{"ne",{"≠",Rel}},{"approx",{"≈",Rel}},{"equiv",{"≡",Rel}},{"sim",{"∼",Rel}},{"simeq",{"≃",Rel}},{"cong",{"≅",Rel}},{"propto",{"∝",Rel}},{"ll",{"≪",Rel}},{"gg",{"≫",Rel}},{"in",{"∈",Rel}},{"notin",{"∉",Rel}},{"ni",{"∋",Rel}},{"subset",{"⊂",Rel}},{"supset",{"⊃",Rel}},{"subseteq",{"⊆",Rel}},{"supseteq",{"⊇",Rel}},{"to",{"→",Rel}},{"rightarrow",{"→",Rel}},{"leftarrow",{"←",Rel}},{"gets",{"←",Rel}},{"leftrightarrow",{"↔",Rel}},{"Rightarrow",{"⇒",Rel}},{"Leftarrow",{"⇐",Rel}},{"Leftrightarrow",{"⇔",Rel}},{"iff",{"⟺",Rel}},{"implies",{"⟹",Rel}},{"mapsto",{"↦",Rel}},{"perp",{"⊥",Rel}},{"parallel",{"∥",Rel}},{"mid",{"∣",Rel}},
        {"infty",{"∞",Ord}},{"partial",{"∂",Ord}},{"nabla",{"∇",Ord}},{"forall",{"∀",Ord}},{"exists",{"∃",Ord}},{"nexists",{"∄",Ord}},{"emptyset",{"∅",Ord}},{"varnothing",{"∅",Ord}},{"neg",{"¬",Ord}},{"lnot",{"¬",Ord}},{"ldots",{"…",Ord}},{"dots",{"…",Ord}},{"cdots",{"⋯",Ord}},{"vdots",{"⋮",Ord}},{"ddots",{"⋱",Ord}},{"prime",{"′",Ord}},{"hbar",{"ℏ",Ord}},{"ell",{"ℓ",Ord}},{"Re",{"ℜ",Ord}},{"Im",{"ℑ",Ord}},{"aleph",{"ℵ",Ord}},{"angle",{"∠",Ord}},{"degree",{"°",Ord}},{"lbrace",{"{",Ord}},{"rbrace",{"}",Ord}},{"langle",{"⟨",Ord}},{"rangle",{"⟩",Ord}},{"top",{"⊤",Ord}},{"bot",{"⊥",Ord}}};
    return map;
}
struct BigOp { const char *glyph; bool limits; };
const QHash<QString, BigOp> &bigOps() {
    static const QHash<QString, BigOp> map{
        {"sum",{"∑",true}},{"prod",{"∏",true}},{"coprod",{"∐",true}},{"bigcup",{"⋃",true}},{"bigcap",{"⋂",true}},{"bigoplus",{"⨁",true}},
        {"int",{"∫",false}},{"iint",{"∬",false}},{"iiint",{"∭",false}},{"oint",{"∮",false}}};
    return map;
}
const QSet<QString> &functions() {
    static const QSet<QString> set{"sin","cos","tan","cot","sec","csc","arcsin","arccos","arctan","sinh","cosh","tanh","coth","log","ln","lg","exp","det","dim","gcd","deg","ker","arg","hom","min","max","sup","inf","lim","limsup","liminf","Pr","mod"};
    return set;
}
const QSet<QString> &limitFunctions() {
    static const QSet<QString> set{"lim","limsup","liminf","min","max","sup","inf"};
    return set;
}

QString blackboard(const QString &text) {
    QString out;
    for (const QChar c : text) {
        const ushort u = c.unicode();
        if (u >= 'A' && u <= 'Z') {
            switch (u) { case 'C': out += QChar(0x2102); break; case 'H': out += QChar(0x210D); break; case 'N': out += QChar(0x2115); break;
                case 'P': out += QChar(0x2119); break; case 'Q': out += QChar(0x211A); break; case 'R': out += QChar(0x211D); break;
                case 'Z': out += QChar(0x2124); break; default: out += QString::fromUcs4(std::array<char32_t, 1>{char32_t(0x1D538 + (u - 'A'))}.data(), 1); }
        } else if (u >= 'a' && u <= 'z') out += QString::fromUcs4(std::array<char32_t, 1>{char32_t(0x1D552 + (u - 'a'))}.data(), 1);
        else if (u >= '0' && u <= '9') out += QString::fromUcs4(std::array<char32_t, 1>{char32_t(0x1D7D8 + (u - '0'))}.data(), 1);
        else out += c;
    }
    return out;
}

class Parser {
  public:
    Parser(const QString &source, const QColor &color) : s(source), color(color) {}
    bool ok = true; QString error;
    BoxPtr parse(qreal size, bool display) { return expression(size, display, 0, false); }
  private:
    const QString &s; QColor color; int p = 0;
    void fail(const QString &message) { if (ok) { ok = false; error = message; } }
    void skipSpace() { while (p < s.size() && s[p].isSpace()) ++p; }
    bool atEnd() { skipSpace(); return p >= s.size(); }
    QString peekCommand() {
        if (p >= s.size() || s[p] != '\\') return {};
        int q = p + 1;
        if (q < s.size() && s[q].isLetter()) { int e = q; while (e < s.size() && s[e].isLetter()) ++e; return s.mid(q, e - q); }
        return q < s.size() ? QString(s[q]) : QString();
    }
    BoxPtr text(const QString &t, qreal size, bool italic = false, bool bold = false, bool onAxis = false, qreal boost = 1.) {
        return std::make_shared<TextBox>(t, size, italic, bold, color, onAxis, boost);
    }
    BoxPtr space(qreal em, qreal size) { return std::make_shared<SpaceBox>(em * size); }
    qreal scriptSize(qreal size) const { return qMax(size * 0.7, 6.); }
    // Reads a {group}, a command or one character as a single argument.
    BoxPtr argument(qreal size, bool display, int depth) {
        skipSpace();
        if (p >= s.size()) { fail("Falta un argumento"); return std::make_shared<SpaceBox>(0); }
        if (s[p] == '{') { ++p; auto body = expression(size, display, depth + 1, false); skipSpace(); if (p < s.size() && s[p] == '}') ++p; else fail("Falta }"); return body; }
        auto box = atom(size, display, depth + 1, false); return box ? box : std::make_shared<SpaceBox>(0);
    }
    QString rawGroup() {
        skipSpace();
        if (p >= s.size() || s[p] != '{') { fail("Falta {"); return {}; }
        int depth = 0, start = p + 1;
        for (; p < s.size(); ++p) {
            if (s[p] == '\\' && p + 1 < s.size()) { ++p; continue; }
            if (s[p] == '{') ++depth; else if (s[p] == '}' && --depth == 0) { const QString r = s.mid(start, p - start); ++p; return r; }
        }
        fail("Falta }"); return s.mid(start);
    }
    // Parses until '}', '&', '\\', \right, \end or end of input.
    BoxPtr expression(qreal size, bool display, int depth, bool inGrid) {
        auto row = std::make_shared<HBox>();
        if (depth > MaxDepth) { fail("Expresión demasiado anidada"); return row; }
        bool previousAllowsBinary = false;
        while (true) {
            skipSpace();
            if (p >= s.size() || s[p] == '}' || (inGrid && s[p] == '&')) break;
            if (s[p] == '\\') {
                const QString command = peekCommand();
                if (command == "right" || command == "end" || (inGrid && command == "\\")) break;
            }
            Class cls = Ord;
            auto box = atom(size, display, depth, true, &cls);
            if (!box) break;
            if (cls == Bin && !previousAllowsBinary) cls = Ord;     // unary sign: no surrounding space
            if (cls == Bin) { row->add(space(0.22, size)); row->add(box); row->add(space(0.22, size)); }
            else if (cls == Rel) { row->add(space(0.28, size)); row->add(box); row->add(space(0.28, size)); }
            else row->add(box);
            previousAllowsBinary = cls != Bin && cls != Rel;
        }
        return row;
    }
    // Parses one nucleus with its scripts. Returns null on '}' or end.
    BoxPtr atom(qreal size, bool display, int depth, bool allowScripts, Class *cls = nullptr) {
        skipSpace();
        if (p >= s.size()) return nullptr;
        if (depth > MaxDepth) { fail("Expresión demasiado anidada"); return nullptr; }
        BoxPtr nucleus; bool limits = false; Class kind = Ord;
        const QChar c = s[p];
        if (c == '}') return nullptr;
        if (c == '{') { ++p; nucleus = expression(size, display, depth + 1, false); skipSpace(); if (p < s.size() && s[p] == '}') ++p; else fail("Falta }"); }
        else if (c == '^' || c == '_') { nucleus = space(0, size); }          // empty base
        else if (c == '\\') nucleus = command(size, display, depth, limits, kind);
        else if (c.isDigit() || (c == '.' && p + 1 < s.size() && s[p + 1].isDigit())) {
            int e = p; while (e < s.size() && (s[e].isDigit() || s[e] == '.')) ++e;
            nucleus = text(s.mid(p, e - p), size); p = e;
        } else if (c.isLetter()) { nucleus = text(QString(c), size, true); ++p; }
        else {
            ++p; const QString ch(c);
            if (c == '+' || c == '-') { nucleus = text(c == '-' ? QString(QChar(0x2212)) : ch, size); kind = Bin; }
            else if (c == '*') { nucleus = text("∗", size); kind = Bin; }
            else if (c == '=' || c == '<' || c == '>') { nucleus = text(ch, size); kind = Rel; }
            else if (c == '\'') { nucleus = text("′", size); }
            else if (c == '~') nucleus = space(0.25, size);
            else if (c == '&') { fail("& fuera de una matriz"); nucleus = space(0, size); }
            else nucleus = text(ch, size);
        }
        if (!nucleus) return nullptr;
        if (cls) *cls = kind;
        if (!allowScripts) return nucleus;
        BoxPtr sup, sub;
        while (true) {
            skipSpace();
            if (p < s.size() && (s[p] == '^' || s[p] == '_')) {
                const bool up = s[p] == '^'; ++p;
                auto script = argument(scriptSize(size), false, depth + 1);
                if (up) { if (sup) fail("Doble superíndice"); sup = script; } else { if (sub) fail("Doble subíndice"); sub = script; }
            } else if (p < s.size() && s[p] == '\'') { ++p; sup = sup ? sup : text("′", scriptSize(size)); }
            else break;
        }
        if (sup || sub) { if (cls) *cls = Ord; return std::make_shared<ScriptBox>(nucleus, sup, sub, limits && display, size); }
        return nucleus;
    }
    BoxPtr command(qreal size, bool display, int depth, bool &limits, Class &kind) {
        const QString name = peekCommand();
        p += 1 + name.size();
        const qreal childSize = display ? size : size * 0.88;
        if (greek().contains(name)) return text(greek().value(name), size, true);
        if (greekUpper().contains(name)) return text(greekUpper().value(name), size);
        if (symbols().contains(name)) { const Symbol sym = symbols().value(name); kind = sym.cls; return text(QString::fromUtf8(sym.text), size); }
        if (bigOps().contains(name)) { const BigOp op = bigOps().value(name); limits = op.limits; return std::make_shared<TextBox>(QString::fromUtf8(op.glyph), size, false, false, color, true, display ? 1.45 : 1.15); }
        if (functions().contains(name)) {
            auto box = std::make_shared<TextBox>(name == "mod" ? QStringLiteral("mod") : name, size, false, false, color);
            box->limits = limitFunctions().contains(name); limits = box->limits;
            auto row = std::make_shared<HBox>(); row->add(box); row->add(space(0.12, size));
            if (limits) { row->items.clear(); row->w = row->a = row->d = 0; return box; }
            return row;
        }
        if (name == "," ) return space(3. / 18, size);
        if (name == ":" || name == ">") return space(4. / 18, size);
        if (name == ";") return space(5. / 18, size);
        if (name == "!") return space(-3. / 18, size);
        if (name == "quad") return space(1, size);
        if (name == "qquad") return space(2, size);
        if (name == " ") return space(0.25, size);
        if (name == "displaystyle" || name == "textstyle" || name == "limits" || name == "nolimits" || name == "scriptstyle") return space(0, size);
        if (name == "%" || name == "$" || name == "&" || name == "#" || name == "_" || name == "{" || name == "}") return text(name, size);
        if (name == "\\") return space(0, size);
        if (name == "frac" || name == "dfrac" || name == "tfrac" || name == "binom") {
            const qreal fs = name == "dfrac" ? size : name == "tfrac" ? size * 0.8 : childSize;
            auto num = argument(fs, false, depth + 1), den = argument(fs, false, depth + 1);
            auto frac = std::make_shared<FracBox>(num, den, size, name != "binom", color);
            if (name == "binom") return std::make_shared<DelimBox>("(", ")", frac, size, color);
            return frac;
        }
        if (name == "sqrt") {
            BoxPtr index; skipSpace();
            if (p < s.size() && s[p] == '[') { const int e = s.indexOf(']', p); if (e < 0) fail("Falta ]"); else { const QString inner = s.mid(p + 1, e - p - 1); p = e + 1; Parser sub(inner, color); index = sub.parse(scriptSize(size) * 0.9, false); if (!sub.ok) fail(sub.error); } }
            return std::make_shared<SqrtBox>(argument(size, display, depth + 1), index, size, color);
        }
        if (name == "text" || name == "mathrm" || name == "textrm" || name == "operatorname" || name == "mathbf" || name == "textbf" || name == "boldsymbol" || name == "mathit" || name == "textit" || name == "mathbb" || name == "mathcal" || name == "mathsf") {
            if (name == "mathrm" || name == "mathbf" || name == "boldsymbol" || name == "mathit" || name == "mathbb" || name == "mathcal" || name == "mathsf") {
                skipSpace();
                if (p < s.size() && s[p] != '{') { const QString one(s[p++]); return text(name == "mathbb" ? blackboard(one) : one, size, name == "mathit" || name == "mathcal", name == "mathbf" || name == "boldsymbol"); }
            }
            QString body = rawGroup();
            if (name == "mathbb") body = blackboard(body);
            return text(body, size, name == "mathit" || name == "textit" || name == "mathcal", name == "mathbf" || name == "textbf" || name == "boldsymbol");
        }
        static const QSet<QString> accents{"hat","bar","vec","tilde","dot","ddot","overline","underline","widehat","widetilde"};
        if (accents.contains(name)) return std::make_shared<AccentBox>(argument(size, display, depth + 1), name, size, color);
        if (name == "left") {
            auto delimiter = [this]() { skipSpace(); if (p >= s.size()) { fail("Falta un delimitador"); return QString(); }
                if (s[p] == '\\') { const QString n = peekCommand(); p += 1 + n.size(); if (n == "{" || n == "lbrace") return QString("{"); if (n == "}" || n == "rbrace") return QString("}"); if (n == "langle") return QString("⟨"); if (n == "rangle") return QString("⟩"); if (n == "|") return QString("‖"); return QString(); }
                const QChar d = s[p++]; return d == '.' ? QString() : QString(d); };
            const QString left = delimiter();
            auto body = expression(size, display, depth + 1, false);
            skipSpace();
            QString right;
            if (peekCommand() == "right") { p += 6; right = delimiter(); } else fail("Falta \\right");
            return std::make_shared<DelimBox>(left, right, body, size, color);
        }
        if (name == "right") { fail("\\right sin \\left"); return space(0, size); }
        if (name == "begin") return environment(size, display, depth);
        fail("Comando desconocido: \\" + name);
        return std::make_shared<TextBox>("\\" + name, size, false, false, QColor("#d9534f"));
    }
    BoxPtr environment(qreal size, bool display, int depth) {
        Q_UNUSED(display);
        const QString env = rawGroup();
        static const QSet<QString> known{"matrix","pmatrix","bmatrix","Bmatrix","vmatrix","Vmatrix","cases","array","aligned","smallmatrix"};
        if (!known.contains(env)) { fail("Entorno desconocido: " + env); }
        if (env == "array") { skipSpace(); if (p < s.size() && s[p] == '{') rawGroup(); }
        std::vector<std::vector<BoxPtr>> rows(1);
        const qreal cell = env == "smallmatrix" ? size * 0.8 : size;
        while (true) {
            rows.back().push_back(expression(cell, false, depth + 1, true));
            skipSpace();
            if (p < s.size() && s[p] == '&') { ++p; continue; }
            if (p < s.size() && s[p] == '\\' && peekCommand() == "\\") { p += 2; rows.emplace_back(); continue; }
            break;
        }
        if (peekCommand() == "end") { p += 4; const QString closing = rawGroup(); if (closing != env) fail("\\end{" + closing + "} no coincide"); }
        else fail("Falta \\end{" + env + "}");
        if (rows.size() > 1 && rows.back().size() == 1 && rows.back()[0]->w == 0) rows.pop_back();
        auto grid = std::make_shared<GridBox>(std::move(rows), cell, env == "cases" || env == "aligned" || env == "array");
        QString l, r;
        if (env == "pmatrix") { l = "("; r = ")"; } else if (env == "bmatrix") { l = "["; r = "]"; }
        else if (env == "Bmatrix") { l = "{"; r = "}"; } else if (env == "vmatrix") { l = "|"; r = "|"; }
        else if (env == "Vmatrix") { l = "‖"; r = "‖"; } else if (env == "cases") l = "{";
        if (l.isEmpty() && r.isEmpty()) return grid;
        return std::make_shared<DelimBox>(l, r, grid, size, color);
    }
};
}

namespace LumenMath {
Result render(const QString &latex, qreal pixelSize, const QColor &color, bool display, qreal scale) {
    Result result;
    const qreal size = display ? pixelSize * 1.2 : pixelSize;
    const QString source = latex.trimmed();
    Parser parser(source, color);
    auto root = parser.parse(size, display);
    if (source.isEmpty()) root = std::make_shared<SpaceBox>(size * 0.6);
    // Unparsed trailing input (for example a stray '}') is reported, not drawn.
    result.ok = parser.ok; result.error = parser.error;
    const qreal pad = qMax<qreal>(2, size * 0.12);
    qreal baseline, height;
    if (display) { height = root->a + root->d + 2 * pad; baseline = root->a + pad; }
    else { const qreal half = qMax(root->a - size * Axis, root->d + size * Axis) + pad; height = 2 * half; baseline = half + size * Axis; }
    if (source.isEmpty()) {             // visible placeholder frame while editing
        height = qMax(height, size * 1.4); baseline = display ? height * 0.7 : baseline;
    }
    const qreal width = qMax<qreal>(root->w + 2 * pad, 4);
    result.width = width; result.height = height;
    result.image = QImage(qCeil(width * scale), qCeil(height * scale), QImage::Format_ARGB32_Premultiplied);
    result.image.setDevicePixelRatio(scale);
    result.image.fill(Qt::transparent);
    QPainter painter(&result.image);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    root->paint(painter, pad, baseline);
    if (source.isEmpty()) { painter.setPen(QPen(color, 1, Qt::DotLine)); painter.setBrush(Qt::NoBrush); painter.drawRoundedRect(QRectF(0.5, 0.5, width - 1, height - 1), 3, 3); }
    return result;
}
}
