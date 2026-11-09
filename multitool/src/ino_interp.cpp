#include "ino_interp.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <set>

using namespace ino;

namespace ino {

// =====================================================================
// Lexer
// =====================================================================
enum TokT { T_EOF, T_ID, T_NUM, T_STR, T_OP };
struct Tok {
    TokT t;
    std::string s;
    long n;
    int line;
};

struct Lexer {
    const std::string &src;
    size_t i = 0;
    int line = 1;
    std::map<std::string, std::vector<Tok>> &defines;
    std::string err;
    Lexer(const std::string &s, std::map<std::string, std::vector<Tok>> &d) : src(s), defines(d) {}

    bool fail(const std::string &m) {
        if (err.empty()) err = "Zeile " + std::to_string(line) + ": " + m;
        return false;
    }

    static int escapeChar(char c) {
        switch (c) {
        case 'n': return '\n';
        case 't': return '\t';
        case 'r': return '\r';
        case '0': return 0;
        default: return c;
        }
    }

    bool run(std::vector<Tok> &out, bool lineStart = true) {
        while (i < src.size()) {
            char c = src[i];
            if (c == '\n') {
                line++;
                i++;
                lineStart = true;
                continue;
            }
            if (isspace((unsigned char)c)) {
                i++;
                continue;
            }
            if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
                while (i < src.size() && src[i] != '\n') i++;
                continue;
            }
            if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
                i += 2;
                while (i + 1 < src.size() && !(src[i] == '*' && src[i + 1] == '/')) {
                    if (src[i] == '\n') line++;
                    i++;
                }
                i += 2;
                continue;
            }
            if (c == '#' && lineStart) {
                size_t e = src.find('\n', i);
                if (e == std::string::npos) e = src.size();
                std::string d = src.substr(i + 1, e - i - 1);
                i = e;
                size_t p = 0;
                while (p < d.size() && isspace((unsigned char)d[p])) p++;
                if (d.compare(p, 6, "define") == 0) {
                    p += 6;
                    while (p < d.size() && isspace((unsigned char)d[p])) p++;
                    size_t ns = p;
                    while (p < d.size() && (isalnum((unsigned char)d[p]) || d[p] == '_')) p++;
                    std::string name = d.substr(ns, p - ns);
                    if (name.empty()) return fail("#define ohne Namen");
                    if (p < d.size() && d[p] == '(') return fail("#define mit Parametern wird nicht unterstuetzt");
                    std::string rest = d.substr(p);
                    std::vector<Tok> body;
                    Lexer sub(rest, defines);
                    sub.line = line;
                    if (!sub.run(body, false)) return fail(sub.err);
                    if (!body.empty() && body.back().t == T_EOF) body.pop_back();
                    defines[name] = body;
                }
                continue;  // #include usw. ignorieren
            }
            lineStart = false;
            if (isdigit((unsigned char)c)) {
                long v = 0;
                if (c == '0' && i + 1 < src.size() && (src[i + 1] == 'x' || src[i + 1] == 'X')) {
                    i += 2;
                    while (i < src.size() && isxdigit((unsigned char)src[i])) {
                        char h = tolower(src[i++]);
                        v = v * 16 + (isdigit((unsigned char)h) ? h - '0' : h - 'a' + 10);
                    }
                } else if (c == '0' && i + 1 < src.size() && (src[i + 1] == 'b' || src[i + 1] == 'B')) {
                    i += 2;
                    while (i < src.size() && (src[i] == '0' || src[i] == '1')) v = v * 2 + (src[i++] - '0');
                } else {
                    while (i < src.size() && isdigit((unsigned char)src[i])) v = v * 10 + (src[i++] - '0');
                    if (i < src.size() && src[i] == '.') {  // Kommazahl -> ganzzahliger Anteil
                        i++;
                        while (i < src.size() && isdigit((unsigned char)src[i])) i++;
                        if (i < src.size() && (src[i] == 'e' || src[i] == 'E')) return fail("Exponent-Zahlen nicht unterstuetzt");
                    }
                }
                while (i < src.size() && strchr("uUlLfF", src[i]) && src[i]) i++;
                out.push_back({T_NUM, "", v, line});
                continue;
            }
            if (isalpha((unsigned char)c) || c == '_') {
                size_t s = i;
                while (i < src.size() && (isalnum((unsigned char)src[i]) || src[i] == '_')) i++;
                std::string id = src.substr(s, i - s);
                auto it = defines.find(id);
                if (it != defines.end()) {
                    for (auto t : it->second) {
                        t.line = line;
                        out.push_back(t);
                    }
                } else out.push_back({T_ID, id, 0, line});
                continue;
            }
            if (c == '"') {
                std::string s;
                i++;
                while (i < src.size() && src[i] != '"') {
                    if (src[i] == '\n') return fail("String nicht geschlossen");
                    if (src[i] == '\\' && i + 1 < src.size()) {
                        s += (char)escapeChar(src[i + 1]);
                        i += 2;
                    } else s += src[i++];
                }
                if (i >= src.size()) return fail("String nicht geschlossen");
                i++;
                out.push_back({T_STR, s, 0, line});
                continue;
            }
            if (c == '\'') {
                long v;
                i++;
                if (i < src.size() && src[i] == '\\' && i + 1 < src.size()) {
                    v = escapeChar(src[i + 1]);
                    i += 2;
                } else if (i < src.size()) v = (unsigned char)src[i++];
                else return fail("Zeichen-Literal fehlerhaft");
                if (i >= src.size() || src[i] != '\'') return fail("Zeichen-Literal fehlerhaft");
                i++;
                out.push_back({T_NUM, "", v, line});
                continue;
            }
            static const char *ops3[] = {"<<=", ">>="};
            static const char *ops2[] = {"==", "!=", "<=", ">=", "&&", "||", "++", "--", "+=", "-=", "*=",
                                         "/=", "%=", "<<", ">>", "&=", "|=", "^=", "->", "::"};
            bool found = false;
            for (auto o : ops3)
                if (src.compare(i, 3, o) == 0) {
                    out.push_back({T_OP, o, 0, line});
                    i += 3;
                    found = true;
                    break;
                }
            if (!found)
                for (auto o : ops2)
                    if (src.compare(i, 2, o) == 0) {
                        out.push_back({T_OP, o, 0, line});
                        i += 2;
                        found = true;
                        break;
                    }
            if (!found) {
                if (strchr("+-*/%<>=!&|^~?:;,.(){}[]", c)) {
                    out.push_back({T_OP, std::string(1, c), 0, line});
                    i++;
                } else return fail(std::string("Unbekanntes Zeichen '") + c + "'");
            }
        }
        out.push_back({T_EOF, "", 0, line});
        return true;
    }
};

// =====================================================================
// AST
// =====================================================================
enum NK {
    N_NUM, N_STR, N_VAR, N_INDEX, N_ASSIGN, N_UNARY, N_BIN, N_TERN, N_CALL, N_PREINC, N_POSTINC,
    N_BLOCK, N_EXPR, N_DECL, N_DECLLIST, N_IF, N_WHILE, N_DO, N_FOR, N_RET, N_BREAK, N_CONT, N_EMPTY
};

struct Node {
    NK k;
    int line = 0;
    long num = 0;  // Zahl bzw. Arraygroesse (-1 = Skalar, 0 = aus Initialisierung)
    std::string s, op;
    std::vector<NodeP> c;
    bool flag = false;  // DECL: hat Init-Liste {...}
};

static NodeP mk(NK k, int line) {
    auto n = std::make_shared<Node>();
    n->k = k;
    n->line = line;
    return n;
}

static const std::set<std::string> TYPE_WORDS = {
    "void", "int", "long", "unsigned", "signed", "short", "byte", "bool", "boolean", "char", "float", "double", "word",
    "uint8_t", "uint16_t", "uint32_t", "int8_t", "int16_t", "int32_t", "size_t", "const", "static", "volatile"};

static const std::map<std::string, long> CONSTS = {
    {"HIGH", 1}, {"LOW", 0}, {"INPUT", 0}, {"OUTPUT", 1}, {"INPUT_PULLUP", 2}, {"INPUT_PULLDOWN", 3},
    {"true", 1}, {"false", 0}, {"LED_BUILTIN", 2}, {"HEX", 16}, {"DEC", 10}, {"BIN", 2}, {"OCT", 8},
    {"BTN_SEL", 14}, {"BTN_UP", 32}, {"BTN_DOWN", 33}, {"IR_TX", 12}, {"IR_RX", 13}, {"NULL", 0}};

// =====================================================================
// Parser
// =====================================================================
struct Parser {
    std::vector<Tok> &t;
    size_t p = 0;
    std::string err;
    std::map<std::string, Func> &funcs;
    std::vector<NodeP> &globals;
    Parser(std::vector<Tok> &tk, std::map<std::string, Func> &f, std::vector<NodeP> &g) : t(tk), funcs(f), globals(g) {}

    Tok &cur() { return t[p]; }
    Tok &peek(int o = 1) { return t[std::min(p + o, t.size() - 1)]; }
    bool isOp(const char *o) { return cur().t == T_OP && cur().s == o; }
    bool isOpAt(size_t q, const char *o) { return q < t.size() && t[q].t == T_OP && t[q].s == o; }
    bool accept(const char *o) {
        if (isOp(o)) {
            p++;
            return true;
        }
        return false;
    }
    bool fail(const std::string &m) {
        if (err.empty()) {
            std::string near = cur().t == T_EOF ? "Dateiende" : (cur().t == T_NUM ? std::to_string(cur().n) : cur().s);
            err = "Zeile " + std::to_string(cur().line) + ": " + m + " (bei '" + near + "')";
        }
        return false;
    }
    bool expect(const char *o) {
        if (accept(o)) return true;
        return fail(std::string("'") + o + "' erwartet");
    }
    bool isTypeAt(size_t q) { return q < t.size() && t[q].t == T_ID && TYPE_WORDS.count(t[q].s); }
    bool skipType() {
        if (!isTypeAt(p)) return fail("Typ erwartet");
        while (isTypeAt(p)) p++;
        while (isOp("*") || isOp("&")) p++;
        return true;
    }

    // ---------- Ausdruecke ----------
    NodeP expr() { return assign(); }

    NodeP assign() {
        NodeP l = ternary();
        if (!l) return nullptr;
        static const std::set<std::string> AOPS = {"=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="};
        if (cur().t == T_OP && AOPS.count(cur().s)) {
            if (l->k != N_VAR && l->k != N_INDEX) return fail("Zuweisung an Nicht-Variable"), nullptr;
            auto n = mk(N_ASSIGN, cur().line);
            n->op = cur().s;
            p++;
            NodeP r = assign();
            if (!r) return nullptr;
            n->c = {l, r};
            return n;
        }
        return l;
    }

    NodeP ternary() {
        NodeP c = binary(0);
        if (!c) return nullptr;
        if (isOp("?")) {
            int ln = cur().line;
            p++;
            NodeP a = assign();
            if (!a || !expect(":")) return nullptr;
            NodeP b = assign();
            if (!b) return nullptr;
            auto n = mk(N_TERN, ln);
            n->c = {c, a, b};
            return n;
        }
        return c;
    }

    static int prec(const std::string &o) {
        if (o == "||") return 1;
        if (o == "&&") return 2;
        if (o == "|") return 3;
        if (o == "^") return 4;
        if (o == "&") return 5;
        if (o == "==" || o == "!=") return 6;
        if (o == "<" || o == ">" || o == "<=" || o == ">=") return 7;
        if (o == "<<" || o == ">>") return 8;
        if (o == "+" || o == "-") return 9;
        if (o == "*" || o == "/" || o == "%") return 10;
        return -1;
    }

    NodeP binary(int minPrec) {
        NodeP l = unary();
        if (!l) return nullptr;
        while (cur().t == T_OP) {
            int pr = prec(cur().s);
            if (pr < 0 || pr <= minPrec - 1 || pr < minPrec) break;
            auto n = mk(N_BIN, cur().line);
            n->op = cur().s;
            p++;
            NodeP r = binary(pr + 1);
            if (!r) return nullptr;
            n->c = {l, r};
            l = n;
        }
        return l;
    }

    NodeP unary() {
        int ln = cur().line;
        if (isOp("-") || isOp("!") || isOp("~") || isOp("+")) {
            auto n = mk(N_UNARY, ln);
            n->op = cur().s;
            p++;
            NodeP a = unary();
            if (!a) return nullptr;
            n->c = {a};
            return n;
        }
        if (isOp("++") || isOp("--")) {
            auto n = mk(N_PREINC, ln);
            n->op = cur().s;
            p++;
            NodeP a = unary();
            if (!a) return nullptr;
            if (a->k != N_VAR && a->k != N_INDEX) return fail("++/-- braucht eine Variable"), nullptr;
            n->c = {a};
            return n;
        }
        // Cast: (int) x
        if (isOp("(") && isTypeAt(p + 1)) {
            size_t q = p + 1;
            while (isTypeAt(q)) q++;
            while (isOpAt(q, "*")) q++;
            if (isOpAt(q, ")")) {
                p = q + 1;
                return unary();
            }
        }
        return postfix();
    }

    NodeP postfix() {
        NodeP n = primary();
        if (!n) return nullptr;
        while (isOp("++") || isOp("--")) {
            if (n->k != N_VAR && n->k != N_INDEX) return fail("++/-- braucht eine Variable"), nullptr;
            auto x = mk(N_POSTINC, cur().line);
            x->op = cur().s;
            p++;
            x->c = {n};
            n = x;
        }
        return n;
    }

    NodeP primary() {
        Tok &tk = cur();
        int ln = tk.line;
        if (tk.t == T_NUM) {
            p++;
            auto n = mk(N_NUM, ln);
            n->num = tk.n;
            return n;
        }
        if (tk.t == T_STR) {
            auto n = mk(N_STR, ln);
            while (cur().t == T_STR) n->s += t[p++].s;
            return n;
        }
        if (accept("(")) {
            NodeP e = expr();
            if (!e || !expect(")")) return nullptr;
            return e;
        }
        if (tk.t == T_ID) {
            std::string name = tk.s;
            p++;
            // Funktions-Cast int(x)
            if (TYPE_WORDS.count(name) && isOp("(")) {
                p++;
                NodeP e = expr();
                if (!e || !expect(")")) return nullptr;
                return e;
            }
            while (isOp(".") && peek().t == T_ID) {
                name += "." + peek().s;
                p += 2;
            }
            if (isOp("(")) {
                p++;
                auto n = mk(N_CALL, ln);
                n->s = name;
                if (!isOp(")")) {
                    for (;;) {
                        NodeP a = expr();
                        if (!a) return nullptr;
                        n->c.push_back(a);
                        if (accept(",")) continue;
                        break;
                    }
                }
                if (!expect(")")) return nullptr;
                return n;
            }
            if (name.find('.') != std::string::npos) return fail("Objekt-Zugriff nur fuer Funktionsaufrufe"), nullptr;
            if (isOp("[")) {
                p++;
                auto n = mk(N_INDEX, ln);
                n->s = name;
                NodeP e = expr();
                if (!e || !expect("]")) return nullptr;
                n->c = {e};
                return n;
            }
            auto ci = CONSTS.find(name);
            if (ci != CONSTS.end()) {
                auto n = mk(N_NUM, ln);
                n->num = ci->second;
                return n;
            }
            auto n = mk(N_VAR, ln);
            n->s = name;
            return n;
        }
        return fail("Ausdruck erwartet"), nullptr;
    }

    // ---------- Anweisungen ----------
    // Deklaration nach dem Typ: name [N] = init, name2 ... ;
    NodeP declList(bool needSemi = true) {
        auto list = mk(N_DECLLIST, cur().line);
        for (;;) {
            if (cur().t != T_ID) return fail("Variablenname erwartet"), nullptr;
            auto d = mk(N_DECL, cur().line);
            d->s = cur().s;
            d->num = -1;
            p++;
            if (accept("[")) {
                if (isOp("]")) d->num = 0;
                else {
                    if (cur().t != T_NUM) return fail("Arraygroesse muss eine Zahl sein"), nullptr;
                    d->num = cur().n;
                    p++;
                }
                if (!expect("]")) return nullptr;
            }
            if (accept("=")) {
                if (accept("{")) {
                    d->flag = true;
                    if (!isOp("}")) {
                        for (;;) {
                            NodeP e = assign();
                            if (!e) return nullptr;
                            d->c.push_back(e);
                            if (accept(",")) {
                                if (isOp("}")) break;
                                continue;
                            }
                            break;
                        }
                    }
                    if (!expect("}")) return nullptr;
                } else {
                    NodeP e = assign();
                    if (!e) return nullptr;
                    d->c.push_back(e);
                }
            }
            if (d->num == 0 && !d->flag) return fail("Arraygroesse fehlt"), nullptr;
            list->c.push_back(d);
            if (accept(",")) continue;
            break;
        }
        if (needSemi && !expect(";")) return nullptr;
        return list;
    }

    NodeP stmt() {
        int ln = cur().line;
        if (accept(";")) return mk(N_EMPTY, ln);
        if (isOp("{")) return block();
        if (cur().t == T_ID) {
            const std::string &w = cur().s;
            if (w == "if") {
                p++;
                auto n = mk(N_IF, ln);
                if (!expect("(")) return nullptr;
                NodeP c = expr();
                if (!c || !expect(")")) return nullptr;
                NodeP a = stmt();
                if (!a) return nullptr;
                n->c = {c, a};
                if (cur().t == T_ID && cur().s == "else") {
                    p++;
                    NodeP b = stmt();
                    if (!b) return nullptr;
                    n->c.push_back(b);
                }
                return n;
            }
            if (w == "while") {
                p++;
                auto n = mk(N_WHILE, ln);
                if (!expect("(")) return nullptr;
                NodeP c = expr();
                if (!c || !expect(")")) return nullptr;
                NodeP b = stmt();
                if (!b) return nullptr;
                n->c = {c, b};
                return n;
            }
            if (w == "do") {
                p++;
                auto n = mk(N_DO, ln);
                NodeP b = stmt();
                if (!b) return nullptr;
                if (!(cur().t == T_ID && cur().s == "while")) return fail("'while' erwartet"), nullptr;
                p++;
                if (!expect("(")) return nullptr;
                NodeP c = expr();
                if (!c || !expect(")") || !expect(";")) return nullptr;
                n->c = {b, c};
                return n;
            }
            if (w == "for") {
                p++;
                auto n = mk(N_FOR, ln);
                if (!expect("(")) return nullptr;
                NodeP init, cond, step;
                if (accept(";")) init = mk(N_EMPTY, ln);
                else if (isTypeAt(p)) {
                    if (!skipType()) return nullptr;
                    init = declList(true);
                } else {
                    auto e = mk(N_EXPR, ln);
                    NodeP x = expr();
                    if (!x || !expect(";")) return nullptr;
                    e->c = {x};
                    init = e;
                }
                if (!init) return nullptr;
                if (isOp(";")) {
                    cond = mk(N_NUM, ln);
                    cond->num = 1;
                } else cond = expr();
                if (!cond || !expect(";")) return nullptr;
                if (isOp(")")) step = mk(N_EMPTY, ln);
                else {
                    step = mk(N_EXPR, ln);
                    NodeP x = expr();
                    if (!x) return nullptr;
                    step->c = {x};
                }
                if (!expect(")")) return nullptr;
                NodeP b = stmt();
                if (!b) return nullptr;
                n->c = {init, cond, step, b};
                return n;
            }
            if (w == "return") {
                p++;
                auto n = mk(N_RET, ln);
                if (!isOp(";")) {
                    NodeP e = expr();
                    if (!e) return nullptr;
                    n->c = {e};
                }
                if (!expect(";")) return nullptr;
                return n;
            }
            if (w == "break" || w == "continue") {
                p++;
                if (!expect(";")) return nullptr;
                return mk(w == "break" ? N_BREAK : N_CONT, ln);
            }
            if (w == "String") return fail("Typ String wird nicht unterstuetzt (nur Zahlen)"), nullptr;
            if (w == "switch") return fail("switch wird nicht unterstuetzt (if/else benutzen)"), nullptr;
            if (isTypeAt(p)) {
                if (!skipType()) return nullptr;
                return declList(true);
            }
        }
        auto n = mk(N_EXPR, ln);
        NodeP e = expr();
        if (!e || !expect(";")) return nullptr;
        n->c = {e};
        return n;
    }

    NodeP block() {
        auto n = mk(N_BLOCK, cur().line);
        if (!expect("{")) return nullptr;
        while (!isOp("}")) {
            if (cur().t == T_EOF) return fail("'}' fehlt"), nullptr;
            NodeP s = stmt();
            if (!s) return nullptr;
            n->c.push_back(s);
        }
        p++;
        return n;
    }

    bool program() {
        while (cur().t != T_EOF) {
            if (accept(";")) continue;
            if (cur().t == T_ID && cur().s == "String") return fail("Typ String wird nicht unterstuetzt (nur Zahlen)");
            if (!isTypeAt(p)) return fail("Typ (z.B. void/int) erwartet");
            bool isVoid = false;
            for (size_t q = p; isTypeAt(q); q++)
                if (t[q].s == "void") isVoid = true;
            if (!skipType()) return false;
            if (cur().t != T_ID) return fail("Name erwartet");
            if (isOpAt(p + 1, "(")) {
                std::string name = cur().s;
                p += 2;
                Func f;
                f.returnsValue = !isVoid;
                if (cur().t == T_ID && cur().s == "void" && isOpAt(p + 1, ")")) p++;
                if (!isOp(")")) {
                    for (;;) {
                        if (!skipType()) return false;
                        if (cur().t != T_ID) return fail("Parametername erwartet");
                        f.params.push_back(cur().s);
                        p++;
                        if (accept("[")) return fail("Array-Parameter nicht unterstuetzt");
                        if (accept(",")) continue;
                        break;
                    }
                }
                if (!expect(")")) return false;
                if (accept(";")) continue;  // Prototyp
                f.body = block();
                if (!f.body) return false;
                funcs[name] = f;
            } else {
                NodeP d = declList(true);
                if (!d) return false;
                globals.push_back(d);
            }
        }
        return true;
    }
};

}  // namespace ino

bool InoInterp::load(const std::string &src) {
    funcs.clear();
    globalInits.clear();
    errorMsg.clear();
    std::map<std::string, std::vector<Tok>> defines;
    std::vector<Tok> toks;
    Lexer lx(src, defines);
    if (!lx.run(toks)) {
        errorMsg = lx.err;
        return false;
    }
    Parser ps(toks, funcs, globalInits);
    if (!ps.program()) {
        errorMsg = ps.err;
        return false;
    }
    if (!funcs.count("setup") && !funcs.count("loop")) {
        errorMsg = "Weder setup() noch loop() gefunden";
        return false;
    }
    return true;
}

// =====================================================================
// Ausfuehrung
// =====================================================================
namespace ino {
enum Flow { F_NORMAL, F_BREAK, F_CONT, F_RET, F_STOP };

struct InoExec {
    InoInterp &I;
    InoHal &h;
    InoExec(InoInterp &i, InoHal &hh) : I(i), h(hh) {}

    bool fail(int line, const std::string &m) {
        if (!I.failed) {
            I.failed = true;
            I.errorMsg = "Zeile " + std::to_string(line) + ": " + m;
        }
        return false;
    }

    Var *lookup(const std::string &name) {
        for (size_t i = I.scopes.size(); i-- > I.frameBase.back();) {
            auto it = I.scopes[i].find(name);
            if (it != I.scopes[i].end()) return &it->second;
        }
        auto it = I.scopes[0].find(name);
        return it != I.scopes[0].end() ? &it->second : nullptr;
    }

    bool tick() {
        if (I.failed || h.stopRequested()) return false;
        if ((++I.steps & 1023) == 0) h.yield();
        return true;
    }

    // Referenz auf Variable/Array-Element
    bool ref(const NodeP &n, long *&out) {
        Var *v = lookup(n->s);
        if (!v) return fail(n->line, "Unbekannte Variable '" + n->s + "'");
        if (n->k == N_INDEX) {
            if (!v->isArray) return fail(n->line, "'" + n->s + "' ist kein Array");
            long idx;
            if (!eval(n->c[0], idx)) return false;
            if (idx < 0 || idx >= (long)v->data.size())
                return fail(n->line, "Index " + std::to_string(idx) + " ausserhalb von '" + n->s + "' (Groesse " +
                                         std::to_string(v->data.size()) + ")");
            out = &v->data[idx];
            return true;
        }
        if (v->isArray) return fail(n->line, "'" + n->s + "' ist ein Array, Index fehlt");
        out = &v->data[0];
        return true;
    }

    static std::string fmt(long v, long base) {
        if (base == 10 || base < 2 || base > 16) return std::to_string(v);
        unsigned long u = (unsigned long)(unsigned int)v;
        if (u == 0) return "0";
        std::string s;
        while (u) {
            s.insert(s.begin(), "0123456789ABCDEF"[u % base]);
            u /= base;
        }
        return s;
    }

    // Text fuer print(): String-Literal oder Zahl (optional Basis)
    bool printText(const NodeP &call, std::string &out) {
        out.clear();
        if (call->c.empty()) return true;
        if (call->c[0]->k == N_STR) {
            out = call->c[0]->s;
            return true;
        }
        long v, base = 10;
        if (!eval(call->c[0], v)) return false;
        if (call->c.size() > 1 && !eval(call->c[1], base)) return false;
        out = fmt(v, base);
        return true;
    }

    bool args(const NodeP &n, size_t cnt, long *a) {
        if (n->c.size() != cnt)
            return fail(n->line, n->s + "() erwartet " + std::to_string(cnt) + " Argument(e), bekommen " +
                                     std::to_string(n->c.size()));
        for (size_t i = 0; i < cnt; i++)
            if (!eval(n->c[i], a[i])) return false;
        return true;
    }

    bool call(const NodeP &n, long &out) {
        const std::string &f = n->s;
        out = 0;
        long a[5];
        std::string err;
        // ---- eigene Funktionen ----
        auto fi = I.funcs.find(f);
        if (fi != I.funcs.end()) {
            Func &fn = fi->second;
            if (n->c.size() != fn.params.size())
                return fail(n->line, f + "() erwartet " + std::to_string(fn.params.size()) + " Argument(e)");
            if (I.depth > 20) return fail(n->line, "Rekursion zu tief (max. 20)");
            std::map<std::string, Var> frame;
            for (size_t i = 0; i < fn.params.size(); i++) {
                long v;
                if (!eval(n->c[i], v)) return false;
                frame[fn.params[i]].data = {v};
            }
            I.frameBase.push_back(I.scopes.size());
            I.scopes.push_back(std::move(frame));
            I.depth++;
            I.retVal = 0;
            Flow fl = exec(fn.body);
            I.depth--;
            I.scopes.pop_back();
            I.frameBase.pop_back();
            if (fl == F_STOP) return false;
            out = I.retVal;
            return true;
        }
        // ---- Ausgabe ----
        if (f == "Serial.print" || f == "Serial.println" || f == "oled.print" || f == "oled.println" ||
            f == "display.print" || f == "display.println") {
            std::string s;
            if (!printText(n, s)) return false;
            if (f.back() == 'n' && f.compare(f.size() - 7, 7, "println") == 0) s += "\n";
            if (f[0] == 'S') h.serialOut(s);
            else h.oledOut(s);
            return true;
        }
        if (f == "Serial.begin" || f == "Serial.flush" || f == "randomSeed" || f == "oled.display" ||
            f == "display.display" || f == "oled.setCursor" || f == "oled.setTextSize" || f == "display.setCursor" ||
            f == "display.setTextSize" || f == "oled.begin" || f == "display.begin") {
            for (auto &x : n->c) {
                long dummy;
                if (x->k != N_STR && !eval(x, dummy)) return false;
            }
            return true;
        }
        if (f == "oled.clear" || f == "display.clear" || f == "display.clearDisplay" || f == "oled.clearDisplay") {
            h.oledClear();
            return true;
        }
        if (f == "Serial.available" || f == "Serial.read") {
            out = f == "Serial.read" ? -1 : 0;
            return true;
        }
        // ---- GPIO ----
        if (f == "pinMode") {
            if (!args(n, 2, a)) return false;
            return h.pinMode(a[0], a[1], err) || fail(n->line, err);
        }
        if (f == "digitalWrite") {
            if (!args(n, 2, a)) return false;
            return h.digitalWrite(a[0], a[1], err) || fail(n->line, err);
        }
        if (f == "digitalRead") {
            if (!args(n, 1, a)) return false;
            return h.digitalRead(a[0], out, err) || fail(n->line, err);
        }
        if (f == "analogRead") {
            if (!args(n, 1, a)) return false;
            return h.analogRead(a[0], out, err) || fail(n->line, err);
        }
        if (f == "analogWrite") {
            if (!args(n, 2, a)) return false;
            return h.analogWrite(a[0], a[1], err) || fail(n->line, err);
        }
        if (f == "irSend") {
            if (n->c.size() != 1 || n->c[0]->k != N_STR) return fail(n->line, "irSend(\"Name\") erwartet einen Text");
            return h.irSend(n->c[0]->s, err) || fail(n->line, err);
        }
        // ---- Zeit ----
        if (f == "delay") {
            if (!args(n, 1, a)) return false;
            if (a[0] > 0) h.delayMs(a[0]);
            return !h.stopRequested();
        }
        if (f == "delayMicroseconds") {
            if (!args(n, 1, a)) return false;
            if (a[0] > 0) h.delayUs(a[0]);
            return true;
        }
        if (f == "millis") {
            out = h.millis();
            return true;
        }
        if (f == "micros") {
            out = h.micros();
            return true;
        }
        // ---- Mathe ----
        if (f == "random") {
            if (n->c.size() == 1) {
                if (!args(n, 1, a)) return false;
                out = h.random(0, a[0]);
            } else {
                if (!args(n, 2, a)) return false;
                out = h.random(a[0], a[1]);
            }
            return true;
        }
        if (f == "abs") {
            if (!args(n, 1, a)) return false;
            out = a[0] < 0 ? -a[0] : a[0];
            return true;
        }
        if (f == "min" || f == "max") {
            if (!args(n, 2, a)) return false;
            out = (f == "min") ? (a[0] < a[1] ? a[0] : a[1]) : (a[0] > a[1] ? a[0] : a[1]);
            return true;
        }
        if (f == "constrain") {
            if (!args(n, 3, a)) return false;
            out = a[0] < a[1] ? a[1] : (a[0] > a[2] ? a[2] : a[0]);
            return true;
        }
        if (f == "map") {
            if (!args(n, 5, a)) return false;
            if (a[2] == a[1]) return fail(n->line, "map(): Bereich hat Breite 0");
            out = (a[0] - a[1]) * (a[4] - a[3]) / (a[2] - a[1]) + a[3];
            return true;
        }
        if (f == "sizeof") {
            if (n->c.size() != 1) return fail(n->line, "sizeof(x) erwartet ein Argument");
            const NodeP &x = n->c[0];
            if (x->k == N_VAR) {
                Var *v = lookup(x->s);
                if (!v) return fail(n->line, "Unbekannte Variable '" + x->s + "'");
                out = (long)v->data.size() * 4;
            } else out = 4;
            return true;
        }
        return fail(n->line, "Unbekannte Funktion '" + f + "()'");
    }

    bool eval(const NodeP &n, long &out) {
        switch (n->k) {
        case N_NUM: out = n->num; return true;
        case N_STR: return fail(n->line, "Text nur in print()/irSend() erlaubt");
        case N_VAR:
        case N_INDEX: {
            long *r;
            if (!ref(n, r)) return false;
            out = *r;
            return true;
        }
        case N_CALL: return call(n, out);
        case N_ASSIGN: {
            long *r, v;
            if (!eval(n->c[1], v)) return false;
            if (!ref(n->c[0], r)) return false;
            const std::string &o = n->op;
            if (o == "=") *r = v;
            else if (o == "+=") *r += v;
            else if (o == "-=") *r -= v;
            else if (o == "*=") *r *= v;
            else if (o == "/=" || o == "%=") {
                if (v == 0) return fail(n->line, "Division durch 0");
                if (o == "/=") *r /= v;
                else *r %= v;
            } else if (o == "&=") *r &= v;
            else if (o == "|=") *r |= v;
            else if (o == "^=") *r ^= v;
            else if (o == "<<=") *r = (v >= 0 && v < 32) ? (long)((unsigned long)*r << v) : 0;
            else if (o == ">>=") *r = (v >= 0 && v < 32) ? (*r >> v) : 0;
            out = *r;
            return true;
        }
        case N_PREINC:
        case N_POSTINC: {
            long *r;
            if (!ref(n->c[0], r)) return false;
            long old = *r;
            *r += (n->op == "++") ? 1 : -1;
            out = n->k == N_PREINC ? *r : old;
            return true;
        }
        case N_UNARY: {
            long v;
            if (!eval(n->c[0], v)) return false;
            if (n->op == "-") out = -v;
            else if (n->op == "!") out = !v;
            else if (n->op == "~") out = ~v;
            else out = v;
            return true;
        }
        case N_TERN: {
            long c;
            if (!eval(n->c[0], c)) return false;
            return eval(n->c[c ? 1 : 2], out);
        }
        case N_BIN: {
            const std::string &o = n->op;
            long a, b;
            if (!eval(n->c[0], a)) return false;
            if (o == "&&") {
                if (!a) return out = 0, true;
                if (!eval(n->c[1], b)) return false;
                return out = (b != 0), true;
            }
            if (o == "||") {
                if (a) return out = 1, true;
                if (!eval(n->c[1], b)) return false;
                return out = (b != 0), true;
            }
            if (!eval(n->c[1], b)) return false;
            if (o == "+") out = a + b;
            else if (o == "-") out = a - b;
            else if (o == "*") out = a * b;
            else if (o == "/" || o == "%") {
                if (b == 0) return fail(n->line, "Division durch 0");
                out = o == "/" ? a / b : a % b;
            } else if (o == "==") out = a == b;
            else if (o == "!=") out = a != b;
            else if (o == "<") out = a < b;
            else if (o == ">") out = a > b;
            else if (o == "<=") out = a <= b;
            else if (o == ">=") out = a >= b;
            else if (o == "&") out = a & b;
            else if (o == "|") out = a | b;
            else if (o == "^") out = a ^ b;
            else if (o == "<<") out = (b >= 0 && b < 32) ? (long)((unsigned long)a << b) : 0;
            else if (o == ">>") out = (b >= 0 && b < 32) ? (a >> b) : 0;
            else return fail(n->line, "Operator " + o + " unbekannt");
            return true;
        }
        default: return fail(n->line, "Anweisung statt Ausdruck");
        }
    }

    Flow declare(const NodeP &list) {
        for (auto &d : list->c) {
            Var v;
            if (d->num == -1) {
                long x = 0;
                if (!d->c.empty() && !eval(d->c[0], x)) return F_STOP;
                if (d->flag && d->c.size() > 1) return fail(d->line, "Init-Liste fuer Nicht-Array"), F_STOP;
                v.data = {x};
            } else {
                long size = d->num ? d->num : (long)d->c.size();
                if (size <= 0 || size > 2048) return fail(d->line, "Arraygroesse 1..2048 erlaubt"), F_STOP;
                if ((long)d->c.size() > size) return fail(d->line, "Zu viele Initialwerte"), F_STOP;
                v.isArray = true;
                v.data.assign(size, 0);
                for (size_t i = 0; i < d->c.size(); i++)
                    if (!eval(d->c[i], v.data[i])) return F_STOP;
            }
            I.scopes.back()[d->s] = std::move(v);
        }
        return F_NORMAL;
    }

    Flow exec(const NodeP &n) {
        if (!tick()) return F_STOP;
        h.atLine(n->line);
        switch (n->k) {
        case N_EMPTY: return F_NORMAL;
        case N_EXPR: {
            long v;
            return eval(n->c[0], v) ? F_NORMAL : F_STOP;
        }
        case N_DECLLIST: return declare(n);
        case N_BLOCK: {
            I.scopes.emplace_back();
            Flow r = F_NORMAL;
            for (auto &s : n->c) {
                r = exec(s);
                if (r != F_NORMAL) break;
            }
            I.scopes.pop_back();
            return r;
        }
        case N_IF: {
            long c;
            if (!eval(n->c[0], c)) return F_STOP;
            if (c) return exec(n->c[1]);
            if (n->c.size() > 2) return exec(n->c[2]);
            return F_NORMAL;
        }
        case N_WHILE:
            for (;;) {
                long c;
                if (!tick() || !eval(n->c[0], c)) return F_STOP;
                if (!c) return F_NORMAL;
                Flow r = exec(n->c[1]);
                if (r == F_BREAK) return F_NORMAL;
                if (r == F_RET || r == F_STOP) return r;
            }
        case N_DO:
            for (;;) {
                Flow r = exec(n->c[0]);
                if (r == F_BREAK) return F_NORMAL;
                if (r == F_RET || r == F_STOP) return r;
                long c;
                if (!tick() || !eval(n->c[1], c)) return F_STOP;
                if (!c) return F_NORMAL;
            }
        case N_FOR: {
            I.scopes.emplace_back();
            Flow res = F_NORMAL;
            Flow r = exec(n->c[0]);
            if (r != F_NORMAL) res = F_STOP;
            else
                for (;;) {
                    long c;
                    if (!tick() || !eval(n->c[1], c)) {
                        res = F_STOP;
                        break;
                    }
                    if (!c) break;
                    r = exec(n->c[3]);
                    if (r == F_BREAK) break;
                    if (r == F_RET || r == F_STOP) {
                        res = r;
                        break;
                    }
                    if (exec(n->c[2]) == F_STOP) {
                        res = F_STOP;
                        break;
                    }
                }
            I.scopes.pop_back();
            return res;
        }
        case N_RET:
            I.retVal = 0;
            if (!n->c.empty() && !eval(n->c[0], I.retVal)) return F_STOP;
            return F_RET;
        case N_BREAK: return F_BREAK;
        case N_CONT: return F_CONT;
        default: return fail(n->line, "Ausdruck ohne Wirkung"), F_STOP;
        }
    }

    bool callByName(const std::string &name) {
        auto n = std::make_shared<Node>();
        n->k = N_CALL;
        n->s = name;
        long dummy;
        return call(n, dummy);
    }
};

}  // namespace ino

bool InoInterp::run(InoHal &h) {
    hal = &h;
    failed = false;
    errorMsg.clear();
    steps = 0;
    depth = 0;
    scopes.clear();
    scopes.emplace_back();
    frameBase.assign(1, 1);
    InoExec ex(*this, h);
    for (auto &g : globalInits)
        if (ex.exec(g) == F_STOP) return !failed;
    if (funcs.count("setup") && !ex.callByName("setup")) return !failed;
    if (!funcs.count("loop")) return true;
    while (!h.stopRequested()) {
        if (!ex.callByName("loop")) return !failed;
        h.yield();
    }
    return true;
}
