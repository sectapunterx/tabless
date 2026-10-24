#include "markdown/MdMath.h"

#include <QHash>

namespace heap::md {

namespace {

const QHash<QString, QString>& symbols() {
  static const QHash<QString, QString> map = {
      // Greek
      {"alpha", "α"},
      {"beta", "β"},
      {"gamma", "γ"},
      {"delta", "δ"},
      {"epsilon", "ϵ"},
      {"varepsilon", "ε"},
      {"zeta", "ζ"},
      {"eta", "η"},
      {"theta", "θ"},
      {"vartheta", "ϑ"},
      {"iota", "ι"},
      {"kappa", "κ"},
      {"lambda", "λ"},
      {"mu", "μ"},
      {"nu", "ν"},
      {"xi", "ξ"},
      {"pi", "π"},
      {"varpi", "ϖ"},
      {"rho", "ρ"},
      {"varrho", "ϱ"},
      {"sigma", "σ"},
      {"varsigma", "ς"},
      {"tau", "τ"},
      {"upsilon", "υ"},
      {"phi", "ϕ"},
      {"varphi", "φ"},
      {"chi", "χ"},
      {"psi", "ψ"},
      {"omega", "ω"},
      {"Gamma", "Γ"},
      {"Delta", "Δ"},
      {"Theta", "Θ"},
      {"Lambda", "Λ"},
      {"Xi", "Ξ"},
      {"Pi", "Π"},
      {"Sigma", "Σ"},
      {"Upsilon", "Υ"},
      {"Phi", "Φ"},
      {"Psi", "Ψ"},
      {"Omega", "Ω"},
      // Operators and relations
      {"pm", "±"},
      {"mp", "∓"},
      {"times", "×"},
      {"div", "÷"},
      {"cdot", "·"},
      {"ast", "∗"},
      {"star", "⋆"},
      {"circ", "∘"},
      {"bullet", "•"},
      {"le", "≤"},
      {"leq", "≤"},
      {"ge", "≥"},
      {"geq", "≥"},
      {"ne", "≠"},
      {"neq", "≠"},
      {"ll", "≪"},
      {"gg", "≫"},
      {"approx", "≈"},
      {"equiv", "≡"},
      {"sim", "∼"},
      {"simeq", "≃"},
      {"cong", "≅"},
      {"propto", "∝"},
      {"in", "∈"},
      {"notin", "∉"},
      {"ni", "∋"},
      {"subset", "⊂"},
      {"supset", "⊃"},
      {"subseteq", "⊆"},
      {"supseteq", "⊇"},
      {"cup", "∪"},
      {"cap", "∩"},
      {"setminus", "∖"},
      {"forall", "∀"},
      {"exists", "∃"},
      {"nexists", "∄"},
      {"neg", "¬"},
      {"lnot", "¬"},
      {"land", "∧"},
      {"wedge", "∧"},
      {"lor", "∨"},
      {"vee", "∨"},
      {"oplus", "⊕"},
      {"otimes", "⊗"},
      {"perp", "⊥"},
      {"parallel", "∥"},
      {"mid", "∣"},
      {"vdash", "⊢"},
      {"models", "⊨"},
      // Arrows
      {"to", "→"},
      {"rightarrow", "→"},
      {"leftarrow", "←"},
      {"gets", "←"},
      {"leftrightarrow", "↔"},
      {"Rightarrow", "⇒"},
      {"Leftarrow", "⇐"},
      {"Leftrightarrow", "⇔"},
      {"implies", "⟹"},
      {"iff", "⟺"},
      {"mapsto", "↦"},
      {"uparrow", "↑"},
      {"downarrow", "↓"},
      {"longrightarrow", "⟶"},
      // Big operators and the rest
      {"sum", "∑"},
      {"prod", "∏"},
      {"coprod", "∐"},
      {"int", "∫"},
      {"iint", "∬"},
      {"oint", "∮"},
      {"bigcup", "⋃"},
      {"bigcap", "⋂"},
      {"infty", "∞"},
      {"partial", "∂"},
      {"nabla", "∇"},
      {"emptyset", "∅"},
      {"varnothing", "∅"},
      {"angle", "∠"},
      {"degree", "°"},
      {"prime", "′"},
      {"hbar", "ℏ"},
      {"ell", "ℓ"},
      {"Re", "ℜ"},
      {"Im", "ℑ"},
      {"aleph", "ℵ"},
      {"ldots", "…"},
      {"dots", "…"},
      {"cdots", "⋯"},
      {"vdots", "⋮"},
      {"ddots", "⋱"},
      {"langle", "⟨"},
      {"rangle", "⟩"},
      {"lfloor", "⌊"},
      {"rfloor", "⌋"},
      {"lceil", "⌈"},
      {"rceil", "⌉"},
      {"lvert", "|"},
      {"rvert", "|"},
      {"vert", "|"},
      {"Vert", "‖"},
      {"lbrace", "{"},
      {"rbrace", "}"},
      {"backslash", "\\"},
      {"therefore", "∴"},
      {"because", "∵"},
      {"top", "⊤"},
      {"bot", "⊥"},
      {"square", "□"},
      {"checkmark", "✓"},
      // Spacing
      {"quad", " "},
      {"qquad", "  "},
  };
  return map;
}

const QHash<QString, QString>& accents() {
  static const QHash<QString, QString> map = {
      {"hat", "̂"},
      {"widehat", "̂"},
      {"bar", "̄"},
      {"overline", "̅"},
      {"vec", "⃗"},
      {"dot", "̇"},
      {"ddot", "̈"},
      {"tilde", "̃"},
      {"widetilde", "̃"},
      {"check", "̌"},
      {"breve", "̆"},
      {"acute", "́"},
      {"grave", "̀"},
  };
  return map;
}

const QHash<QChar, QString>& blackboard() {
  static const QHash<QChar, QString> map = {
      {'N', "ℕ"},
      {'Z', "ℤ"},
      {'Q', "ℚ"},
      {'R', "ℝ"},
      {'C', "ℂ"},
      {'P', "ℙ"},
      {'H', "ℍ"},
      {'E', "𝔼"},
      {'F', "𝔽"},
      {'1', "𝟙"},
  };
  return map;
}

bool isFunction(const QString& name) {
  static const QStringList names = {"sin", "cos", "tan", "cot", "sec", "csc",    "arcsin", "arccos", "arctan", "sinh", "cosh", "tanh",
                                    "log", "ln",  "lg",  "exp", "lim", "liminf", "limsup", "max",    "min",    "sup",  "inf",  "det",
                                    "dim", "ker", "deg", "gcd", "lcm", "arg",    "Pr",     "mod",    "bmod",   "hom"};
  return names.contains(name);
}

QString escaped(const QString& s) {
  QString out = s;
  out.replace(QLatin1Char('&'), QLatin1String("&amp;"));
  out.replace(QLatin1Char('<'), QLatin1String("&lt;"));
  out.replace(QLatin1Char('>'), QLatin1String("&gt;"));
  return out;
}

// One pass over the source; html and plain grow together.
class Typesetter {
 public:
  explicit Typesetter(const QString& tex) : m_s(tex) {
  }

  bool run(QString& html, QString& plain) {
    Piece p = expr(QChar());
    html = p.html;
    plain = p.plain;
    return m_ok && m_i >= m_s.size();
  }

 private:
  struct Piece {
    QString html;
    QString plain;
    bool single = true;  // one symbol: a fraction does not bracket it
  };

  QChar peek() const {
    return m_i < m_s.size() ? m_s.at(m_i) : QChar();
  }

  void skipSpace() {
    while(m_i < m_s.size() && m_s.at(m_i).isSpace()) {
      ++m_i;
    }
  }

  Piece expr(QChar close) {
    Piece out;
    out.single = false;
    int atoms = 0;
    while(m_ok && m_i < m_s.size()) {
      const QChar c = peek();
      if(!close.isNull() && c == close) {
        ++m_i;
        out.single = atoms <= 1;
        return out;
      }
      if(c == QLatin1Char('}')) {
        m_ok = false;  // a brace that closes nothing
        return out;
      }
      if(c == QLatin1Char('^') || c == QLatin1Char('_')) {
        ++m_i;
        const Piece arg = atom();
        const char* tag = c == QLatin1Char('^') ? "sup" : "sub";
        out.html += QStringLiteral("<%1>%2</%1>").arg(QLatin1String(tag), arg.html);
        out.plain += (c == QLatin1Char('^') ? QStringLiteral("^") : QStringLiteral("_")) + arg.plain;
        continue;
      }
      if(c.isSpace()) {
        ++m_i;  // TeX maths ignores spaces; binary operators get their own
        continue;
      }
      const Piece a = atom();
      out.html += a.html;
      out.plain += a.plain;
      ++atoms;
    }
    if(!close.isNull()) {
      m_ok = false;  // an open brace never closed
    }
    out.single = atoms <= 1;
    return out;
  }

  Piece group() {
    skipSpace();
    if(peek() == QLatin1Char('{')) {
      ++m_i;
      return expr(QLatin1Char('}'));
    }
    return atom();
  }

  // The raw text of a {…} argument (\text, \mathrm, \operatorname).
  QString rawGroup() {
    skipSpace();
    if(peek() != QLatin1Char('{')) {
      const QChar c = peek();
      ++m_i;
      return c;
    }
    ++m_i;
    int depth = 1;
    QString out;
    while(m_i < m_s.size()) {
      const QChar c = m_s.at(m_i++);
      if(c == QLatin1Char('{')) {
        ++depth;
      } else if(c == QLatin1Char('}') && --depth == 0) {
        return out;
      }
      out += c;
    }
    m_ok = false;
    return out;
  }

  Piece atom() {
    skipSpace();
    if(m_i >= m_s.size()) {
      m_ok = false;  // ^ or _ with nothing after it
      return {};
    }
    const QChar c = m_s.at(m_i);
    if(c == QLatin1Char('{')) {
      ++m_i;
      return expr(QLatin1Char('}'));
    }
    if(c == QLatin1Char('\\')) {
      return command();
    }
    ++m_i;
    if(c.isLetter()) {
      return {QStringLiteral("<i>%1</i>").arg(escaped(c)), QString(c), true};
    }
    if(c.isDigit()) {
      // A number reads as one: 12.5 is not three atoms.
      QString num(c);
      while(m_i < m_s.size() &&
            (m_s.at(m_i).isDigit() || (m_s.at(m_i) == QLatin1Char('.') && m_i + 1 < m_s.size() && m_s.at(m_i + 1).isDigit()))) {
        num += m_s.at(m_i++);
      }
      return {num, num, true};
    }
    static const QString binary = QStringLiteral("+-=<>");
    if(binary.contains(c)) {
      const QString sym = c == QLatin1Char('-') ? QStringLiteral("−") : QString(c);
      return {QStringLiteral(" %1 ").arg(escaped(sym)), QStringLiteral(" %1 ").arg(sym), true};
    }
    if(c == QLatin1Char('&') || c == QLatin1Char('#') || c == QLatin1Char('%')) {
      m_ok = false;  // alignment, macro parameters, comments: not maths we set
      return {};
    }
    return {escaped(c), QString(c), true};
  }

  Piece command() {
    ++m_i;  // the backslash
    if(m_i >= m_s.size()) {
      m_ok = false;
      return {};
    }
    const QChar first = m_s.at(m_i);
    if(!first.isLetter()) {
      ++m_i;
      // \, \; \: \! spacing; \{ \} \$ \% \_ \& \# literals; \\ a line break.
      if(first == QLatin1Char(',') || first == QLatin1Char(':') || first == QLatin1Char(';')) {
        return {QStringLiteral(" "), QStringLiteral(" "), true};
      }
      if(first == QLatin1Char('!')) {
        return {};
      }
      if(first == QLatin1Char(' ')) {
        return {QStringLiteral(" "), QStringLiteral(" "), true};
      }
      if(first == QLatin1Char('\\')) {
        return {QStringLiteral("<br/>"), QStringLiteral("\n"), true};
      }
      return {escaped(first), QString(first), true};
    }
    QString name;
    while(m_i < m_s.size() && m_s.at(m_i).isLetter()) {
      name += m_s.at(m_i++);
    }
    if(symbols().contains(name)) {
      const QString sym = symbols().value(name);
      static const QStringList spaced = {
          "le",   "leq",     "ge",     "geq",    "ne",       "neq",  "approx",     "equiv",      "sim",
          "cong", "in",      "notin",  "subset", "subseteq", "to",   "rightarrow", "Rightarrow", "Leftrightarrow",
          "iff",  "implies", "mapsto", "pm",     "times",    "cdot", "propto"};
      if(spaced.contains(name)) {
        return {QStringLiteral(" %1 ").arg(escaped(sym)), QStringLiteral(" %1 ").arg(sym), true};
      }
      return {escaped(sym), sym, true};
    }
    if(isFunction(name)) {
      return {name, name, true};
    }
    if(name == QLatin1String("frac") || name == QLatin1String("dfrac") || name == QLatin1String("tfrac")) {
      const Piece num = group();
      const Piece den = group();
      // A piece of more than one symbol goes in brackets: (a+b)/2.
      const auto wrap = [](bool single, const QString& s) {
        return single ? s : QStringLiteral("(") + s + QStringLiteral(")");
      };
      return {QStringLiteral("<sup>%1</sup>⁄<sub>%2</sub>").arg(wrap(num.single, num.html), wrap(den.single, den.html)),
              wrap(num.single, num.plain) + QStringLiteral("/") + wrap(den.single, den.plain),
              false};
    }
    if(name == QLatin1String("sqrt")) {
      QString index;
      skipSpace();
      if(peek() == QLatin1Char('[')) {
        ++m_i;
        while(m_i < m_s.size() && m_s.at(m_i) != QLatin1Char(']')) {
          index += m_s.at(m_i++);
        }
        ++m_i;
      }
      const Piece body = group();
      const QString root = index.isEmpty() ? QStringLiteral("√") : QStringLiteral("<sup>%1</sup>√").arg(escaped(index));
      return {root + QStringLiteral("<span style=\"text-decoration: overline\">%1</span>").arg(body.html),
              (index.isEmpty() ? QString() : index) + QStringLiteral("√") + body.plain,
              false};
    }
    if(accents().contains(name)) {
      const Piece body = group();
      return {body.html + accents().value(name), body.plain + accents().value(name), body.single};
    }
    if(name == QLatin1String("mathbb")) {
      const QString raw = rawGroup();
      QString out;
      for(const QChar ch : raw) {
        out += blackboard().value(ch, QString(ch));
      }
      return {escaped(out), out, raw.size() <= 1};
    }
    if(name == QLatin1String("text") || name == QLatin1String("mathrm") || name == QLatin1String("textrm") ||
       name == QLatin1String("operatorname") || name == QLatin1String("mbox") || name == QLatin1String("textsf") ||
       name == QLatin1String("mathsf") || name == QLatin1String("textnormal") || name == QLatin1String("textup")) {
      // "~" in text is TeX's unbreakable space.
      const QString raw = QString(rawGroup()).replace(QLatin1Char('~'), QChar(0x00A0));
      return {escaped(raw), raw, raw.size() <= 1};
    }
    if(name == QLatin1String("mathbf") || name == QLatin1String("textbf") || name == QLatin1String("boldsymbol")) {
      const Piece body = group();
      return {QStringLiteral("<b>%1</b>").arg(body.html), body.plain, body.single};
    }
    if(name == QLatin1String("mathit") || name == QLatin1String("textit") || name == QLatin1String("mathcal") ||
       name == QLatin1String("mathscr")) {
      const Piece body = group();
      return {QStringLiteral("<i>%1</i>").arg(body.html), body.plain, body.single};
    }
    if(name == QLatin1String("mathtt") || name == QLatin1String("texttt")) {
      const QString raw = rawGroup();
      return {QStringLiteral("<code>%1</code>").arg(escaped(raw)), raw, false};
    }
    // Sizing and delimiters that only change heights: drawn at one size.
    static const QStringList ignored = {
        "left", "right", "big", "Big", "bigg", "Bigg", "bigl", "bigr", "Bigl", "Bigr", "displaystyle", "textstyle", "limits", "nolimits"};
    if(ignored.contains(name)) {
      return {};
    }
    m_ok = false;  // \begin{…}, \newcommand, anything not set here
    return {};
  }

  const QString m_s;
  qsizetype m_i = 0;
  bool m_ok = true;
};

}  // namespace

MathRender renderMath(const QString& tex, bool display) {
  MathRender out;
  QString html;
  QString plain;
  Typesetter t(tex.trimmed());
  if(!t.run(html, plain)) {
    // Not guessed at: the source, marked, so nothing is drawn wrongly.
    out.ok = false;
    out.plain = tex;
    out.html = QStringLiteral("<code>%1</code>").arg(escaped(tex));
    return out;
  }
  out.plain = plain.simplified();
  out.html = display ? QStringLiteral("<span style=\"font-size: large\">%1</span>").arg(html) : html;
  return out;
}

}  // namespace heap::md
