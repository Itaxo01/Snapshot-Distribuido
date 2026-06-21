// JSON minimalista: escritor + parser. Modulo coeso e reutilizavel (qualquer
// camada pode ler/escrever snapshots). Header-only: o parser so e emitido em
// quem realmente o usa (o worker so escreve; o Monitor le e escreve).
//
// Numeros sao guardados como o TOKEN CRU para nao perder precisao -- um
// snapshot_id u64 nao caberia exatamente num double.
#pragma once

#include <cctype>
#include <cstdlib>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace comum::json {

// ---------------------------------------------------------------------------
// Escritor: encadeie chamadas e termine com str().
//   Escritor().numero("a",1).texto("b","x").str()  ->  {"a":1,"b":"x"}
// ---------------------------------------------------------------------------
class Escritor {
public:
    Escritor& numero(const std::string& chave, long long v) {
        sep(); buf_ << '"' << chave << "\":" << v; return *this;
    }
    Escritor& numero_u(const std::string& chave, unsigned long long v) {
        sep(); buf_ << '"' << chave << "\":" << v; return *this;
    }
    Escritor& texto(const std::string& chave, const std::string& v) {
        sep(); buf_ << '"' << chave << "\":\"" << escapar(v) << '"'; return *this;
    }
    // Insere um valor que JA e JSON (ex.: um array montado a parte).
    Escritor& bruto(const std::string& chave, const std::string& json_valor) {
        sep(); buf_ << '"' << chave << "\":" << json_valor; return *this;
    }
    std::string str() const { return "{" + buf_.str() + "}"; }

private:
    void sep() { if (!primeiro_) buf_ << ','; primeiro_ = false; }
    static std::string escapar(const std::string& s) {
        std::string o;
        for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; }
        return o;
    }
    std::ostringstream buf_;
    bool primeiro_ = true;
};

// Monta um array JSON a partir de itens que ja sao JSON.
inline std::string array(const std::vector<std::string>& itens) {
    std::string s = "[";
    for (std::size_t i = 0; i < itens.size(); ++i) { if (i) s += ','; s += itens[i]; }
    return s + "]";
}

// ---------------------------------------------------------------------------
// Valor parseado.
// ---------------------------------------------------------------------------
struct Valor {
    enum class Tipo { Nulo, Bool, Numero, Texto, Array, Objeto };
    Tipo tipo = Tipo::Nulo;

    bool                          b = false;
    std::string                   num;   // token numerico cru (sem perda)
    std::string                   txt;
    std::vector<Valor>            arr;
    std::map<std::string, Valor>  obj;

    long long          inteiro() const { return std::strtoll(num.c_str(), nullptr, 10); }
    unsigned long long u64()     const { return std::strtoull(num.c_str(), nullptr, 10); }
    double             real()    const { return std::strtod(num.c_str(), nullptr); }

    // Acesso a campo de objeto; nullptr se ausente.
    const Valor* campo(const std::string& chave) const {
        auto it = obj.find(chave);
        return it == obj.end() ? nullptr : &it->second;
    }
};

namespace detail {

struct Parser {
    const std::string& s;
    std::size_t        i  = 0;
    bool               ok = true;
    explicit Parser(const std::string& str) : s(str) {}

    void ws() { while (i < s.size() && (s[i]==' '||s[i]=='\t'||s[i]=='\n'||s[i]=='\r')) ++i; }
    bool eof() const { return i >= s.size(); }
    char peek() const { return i < s.size() ? s[i] : '\0'; }

    Valor valor() {
        ws();
        if (!ok || eof()) { ok = false; return {}; }
        char c = peek();
        if (c == '{') return objeto();
        if (c == '[') return lista();
        if (c == '"') { Valor v; v.tipo = Valor::Tipo::Texto; v.txt = texto(); return v; }
        if (c == 't' || c == 'f' || c == 'n') return literal();
        if (c == '-' || (c >= '0' && c <= '9')) return numero();
        ok = false; return {};
    }

    std::string texto() {  // assume s[i] == '"'
        std::string out;
        ++i;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return out;
            if (c == '\\') {
                if (i >= s.size()) { ok = false; return out; }
                char e = s[i++];
                switch (e) {
                    case '"': out += '"'; break;  case '\\': out += '\\'; break;
                    case '/': out += '/'; break;  case 'n': out += '\n'; break;
                    case 't': out += '\t'; break; case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break; case 'f': out += '\f'; break;
                    case 'u': if (i + 4 <= s.size()) i += 4; out += '?'; break;
                    default:  out += e; break;
                }
            } else {
                out += c;
            }
        }
        ok = false; return out;  // sem aspas de fechamento
    }

    Valor numero() {
        std::size_t ini = i;
        if (peek() == '-') ++i;
        while (i < s.size() &&
               (std::isdigit((unsigned char)s[i]) || s[i]=='.' || s[i]=='e' || s[i]=='E' ||
                s[i]=='+' || s[i]=='-')) ++i;
        Valor v; v.tipo = Valor::Tipo::Numero; v.num = s.substr(ini, i - ini);
        return v;
    }

    Valor literal() {
        if (s.compare(i, 4, "true") == 0)  { i += 4; Valor v; v.tipo = Valor::Tipo::Bool; v.b = true;  return v; }
        if (s.compare(i, 5, "false") == 0) { i += 5; Valor v; v.tipo = Valor::Tipo::Bool; v.b = false; return v; }
        if (s.compare(i, 4, "null") == 0)  { i += 4; return {}; }
        ok = false; return {};
    }

    Valor objeto() {
        Valor v; v.tipo = Valor::Tipo::Objeto;
        ++i; ws();
        if (peek() == '}') { ++i; return v; }
        while (ok) {
            ws();
            if (peek() != '"') { ok = false; break; }
            std::string chave = texto();
            ws();
            if (peek() != ':') { ok = false; break; }
            ++i;
            Valor val = valor();
            if (!ok) break;
            v.obj.emplace(std::move(chave), std::move(val));
            ws();
            char c = peek();
            if (c == ',') { ++i; continue; }
            if (c == '}') { ++i; break; }
            ok = false; break;
        }
        return v;
    }

    Valor lista() {
        Valor v; v.tipo = Valor::Tipo::Array;
        ++i; ws();
        if (peek() == ']') { ++i; return v; }
        while (ok) {
            Valor val = valor();
            if (!ok) break;
            v.arr.push_back(std::move(val));
            ws();
            char c = peek();
            if (c == ',') { ++i; continue; }
            if (c == ']') { ++i; break; }
            ok = false; break;
        }
        return v;
    }
};

}  // namespace detail

// Faz o parse de um documento JSON. std::nullopt se malformado.
inline std::optional<Valor> parse(const std::string& s) {
    detail::Parser p(s);
    Valor v = p.valor();
    p.ws();
    if (!p.ok || !p.eof()) return std::nullopt;
    return v;
}

}  // namespace comum::json
