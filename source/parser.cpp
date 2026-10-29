#include "parser.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace fs = std::filesystem;

namespace {

// ================================================================ Tokens ===

struct Token {
    enum class Kind { Word, String, LBracket, RBracket, End } kind = Kind::End;
    std::string text;
    int line = 0;
};

struct TokenStream {
    std::vector<Token> tokens;
    size_t pos = 0;
    std::string file;  // for messages
    std::string dir;   // directory used to resolve relative paths

    const Token& peek() const { return tokens[pos]; }
    const Token& next() { const Token& t = tokens[pos]; if (t.kind != Token::Kind::End) ++pos; return t; }
    bool atEnd() const { return tokens[pos].kind == Token::Kind::End; }
};

bool tokenize(const std::string& src, const std::string& file, std::vector<Token>& out, Logger& log) {
    int line = 1;
    size_t i = 0, n = src.size();
    bool ok = true;
    while (i < n) {
        char c = src[i];
        if (c == '\n') { ++line; ++i; continue; }
        if (std::isspace((unsigned char)c)) { ++i; continue; }
        if (c == '#') {
            while (i < n && src[i] != '\n') ++i;
            continue;
        }
        Token t;
        t.line = line;
        if (c == '[') { t.kind = Token::Kind::LBracket; t.text = "["; ++i; }
        else if (c == ']') { t.kind = Token::Kind::RBracket; t.text = "]"; ++i; }
        else if (c == '"') {
            t.kind = Token::Kind::String;
            ++i;
            int startLine = line;
            bool closed = false;
            while (i < n) {
                if (src[i] == '"') { closed = true; ++i; break; }
                if (src[i] == '\n') { ++line; break; }  // strings may not span lines
                if (src[i] == '\\' && i + 1 < n) {
                    char e = src[i + 1];
                    t.text.push_back(e == 'n' ? '\n' : (e == 't' ? '\t' : e));
                    i += 2;
                    continue;
                }
                t.text.push_back(src[i++]);
            }
            if (!closed) {
                log.error(file + ":" + std::to_string(startLine) + ": unterminated string literal \"" + t.text + "\"");
                ok = false;
                if (i < n) ++i;
            }
        } else {
            t.kind = Token::Kind::Word;
            while (i < n && !std::isspace((unsigned char)src[i]) && src[i] != '"' && src[i] != '[' && src[i] != ']' &&
                   src[i] != '#')
                t.text.push_back(src[i++]);
        }
        out.push_back(t);
    }
    Token end;
    end.kind = Token::Kind::End;
    end.line = line;
    out.push_back(end);
    return ok;
}

bool parseNumber(const std::string& s, double& v) {
    if (s.empty()) return false;
    char* endp = nullptr;
    v = std::strtod(s.c_str(), &endp);
    return endp && *endp == '\0' && std::isfinite(v);
}

// ============================================================ Parameters ===

struct Param {
    std::string type, name;
    std::vector<double> nums;
    std::vector<std::string> strs;
    std::vector<bool> bools;
    int line = 0;
    mutable bool used = false;
};

std::string lowerStr(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

class ParamSet {
public:
    std::vector<Param> params;
    std::string where;  // "file:line" of the directive, for messages
    Logger* log = nullptr;

    const Param* find(const std::string& name) const {
        for (const Param& p : params)
            if (p.name == name) { p.used = true; return &p; }
        return nullptr;
    }
    bool has(const std::string& name) const {
        for (const Param& p : params)
            if (p.name == name) return true;
        return false;
    }
    static bool isNumericType(const std::string& t) {
        return t == "float" || t == "integer" || t == "point2" || t == "vector2" || t == "point3" || t == "vector3" ||
               t == "normal" || t == "normal3" || t == "point" || t == "vector" || t == "rgb" || t == "color" ||
               t == "spectrum" || t == "blackbody";
    }
    void typeError(const Param& p, const std::string& expected) const {
        log->error(where + ": parameter \"" + p.name + "\" has type '" + p.type + "' but '" + expected + "' was expected; using default");
    }

    double getFloat(const std::string& name, double def) const {
        const Param* p = find(name);
        if (!p) return def;
        if (p->type != "float" && p->type != "integer") { typeError(*p, "float"); return def; }
        if (p->nums.size() != 1) log->warning(where + ": parameter \"" + name + "\" expects 1 value, got " + std::to_string(p->nums.size()));
        return p->nums.empty() ? def : p->nums[0];
    }
    int getInt(const std::string& name, int def) const {
        const Param* p = find(name);
        if (!p) return def;
        if (p->type != "integer") {
            if (p->type == "float" && !p->nums.empty()) {
                log->warning(where + ": parameter \"" + name + "\" should be declared 'integer'; truncating float value");
                return (int)p->nums[0];
            }
            typeError(*p, "integer");
            return def;
        }
        return p->nums.empty() ? def : (int)p->nums[0];
    }
    std::vector<double> getFloats(const std::string& name) const {
        const Param* p = find(name);
        if (!p) return {};
        if (!isNumericType(p->type)) { typeError(*p, "float[]"); return {}; }
        return p->nums;
    }
    std::vector<int> getInts(const std::string& name) const {
        const Param* p = find(name);
        if (!p) return {};
        if (p->type != "integer") { typeError(*p, "integer[]"); return {}; }
        std::vector<int> r;
        for (double d : p->nums) r.push_back((int)d);
        return r;
    }
    std::string getString(const std::string& name, const std::string& def) const {
        const Param* p = find(name);
        if (!p) return def;
        if (p->type != "string" && p->type != "texture") { typeError(*p, "string"); return def; }
        return p->strs.empty() ? def : p->strs[0];
    }
    bool getBool(const std::string& name, bool def) const {
        const Param* p = find(name);
        if (!p) return def;
        if (p->type != "bool") { typeError(*p, "bool"); return def; }
        return p->bools.empty() ? def : (bool)p->bools[0];
    }
    Vec3 getPoint3(const std::string& name, const Vec3& def) const {
        const Param* p = find(name);
        if (!p) return def;
        if (p->nums.size() != 3 || !isNumericType(p->type)) {
            log->error(where + ": parameter \"" + name + "\" must have exactly 3 numeric values; using default");
            return def;
        }
        return Vec3(p->nums[0], p->nums[1], p->nums[2]);
    }
    // Colour-valued parameter (rgb / color / spectrum / blackbody / float).
    bool getColor(const std::string& name, Color& out) const;
    // Reports parameters that were never queried.
    void reportUnused() const {
        for (const Param& p : params)
            if (!p.used) log->warning(where + ": unused/unsupported parameter \"" + p.type + " " + p.name + "\" ignored");
    }
    // Marks known PBRT parameters this renderer intentionally ignores.
    void ignore(std::initializer_list<const char*> names) const {
        for (const char* n : names)
            for (const Param& p : params)
                if (p.name == n && !p.used) {
                    p.used = true;
                    log->info(where + ": PBRT parameter \"" + p.name + "\" is not used by this renderer");
                }
    }
};

// Approximate RGB of a blackbody emitter (normalised to max component 1).
Color blackbodyRGB(double T) {
    auto planck = [&](double lambdaNm) {
        const double c = 299792458.0, h = 6.62606957e-34, kb = 1.3806488e-23;
        double l = lambdaNm * 1e-9;
        return (2 * h * c * c) / (std::pow(l, 5) * (std::exp((h * c) / (l * kb * T)) - 1));
    };
    Color c(planck(610), planck(550), planck(465));
    double m = c.maxComponent();
    return m > 0 ? c / m : Color(1);
}

bool ParamSet::getColor(const std::string& name, Color& out) const {
    const Param* p = find(name);
    if (!p) return false;
    if (p->type == "rgb" || p->type == "color") {
        if (p->nums.size() != 3) {
            log->error(where + ": colour parameter \"" + name + "\" needs 3 values; using default");
            return false;
        }
        out = Color(p->nums[0], p->nums[1], p->nums[2]);
    } else if (p->type == "float") {
        if (p->nums.empty()) return false;
        out = Color(p->nums[0]);
    } else if (p->type == "blackbody") {
        if (p->nums.empty() || p->nums[0] <= 0) {
            log->error(where + ": blackbody parameter \"" + name + "\" needs a positive temperature");
            return false;
        }
        out = blackbodyRGB(p->nums[0]);
        log->info(where + ": blackbody " + std::to_string(p->nums[0]) + "K approximated as RGB");
    } else if (p->type == "spectrum") {
        if (!p->strs.empty()) {
            log->warning(where + ": named spectrum \"" + p->strs[0] + "\" for \"" + name + "\" is not supported; using default");
            return false;
        }
        if (p->nums.size() < 2 || p->nums.size() % 2) {
            log->error(where + ": spectrum \"" + name + "\" must contain (wavelength, value) pairs");
            return false;
        }
        double s = 0;
        for (size_t i = 1; i < p->nums.size(); i += 2) s += p->nums[i];
        out = Color(s / (p->nums.size() / 2));
        log->warning(where + ": sampled spectrum \"" + name + "\" approximated by its average value (grey)");
    } else {
        typeError(*p, "rgb");
        return false;
    }
    if (out.x < 0 || out.y < 0 || out.z < 0) {
        log->error(where + ": colour parameter \"" + name + "\" has negative components; clamped to 0");
        out = vmax(out, Color(0));
    }
    return true;
}

// ======================================================== Graphics state ===

struct AreaLightDesc {
    bool active = false;
    Color L{1, 1, 1};
    bool twoSided = false;
};

struct GraphicsState {
    Transform ctm;
    std::shared_ptr<Material> material;
    bool reverseOrientation = false;
    AreaLightDesc areaLight;
};

// A shape call recorded inside ObjectBegin/ObjectEnd for later instancing.
struct ShapeCall {
    std::string type;
    ParamSet params;
    GraphicsState gs;
    std::string dir;
};

class Parser {
public:
    Parser(Scene& scene, Logger& log) : scene_(scene), log_(log) {
        gs_.material = std::make_shared<Material>();
        gs_.material->name = "default";
        gs_.material->sourceType = "diffuse";
    }

    bool parseFile(const std::string& path, int includeDepth);
    void finish();

private:
    // ---- helpers
    std::string where(const TokenStream& ts, int line) const { return ts.file + ":" + std::to_string(line); }
    bool readString(TokenStream& ts, const std::string& directive, int line, std::string& out);
    bool readNumbers(TokenStream& ts, int count, const std::string& directive, int line, std::vector<double>& out,
                     bool allowBrackets);
    bool parseParams(TokenStream& ts, ParamSet& ps, int line);
    void skipToNextDirective(TokenStream& ts);
    std::string resolvePath(const std::string& dir, const std::string& file) const;
    void applyTransform(const Transform& t) { gs_.ctm = gs_.ctm * t; }

    // ---- directive handlers
    void camera(const std::string& type, const ParamSet& ps);
    void film(const std::string& type, const ParamSet& ps);
    void sampler(const std::string& type, const ParamSet& ps);
    void integrator(const std::string& type, const ParamSet& ps);
    std::shared_ptr<Material> makeMaterial(const std::string& type, const ParamSet& ps, const std::string& name);
    std::shared_ptr<Texture> getTexture(const ParamSet& ps, std::initializer_list<const char*> names, const Color& def);
    void texture(const std::string& name, const std::string& valueType, const std::string& cls, const ParamSet& ps,
                 const std::string& dir);
    void shape(const std::string& type, const ParamSet& ps, const GraphicsState& gs, const std::string& dir);
    void lightSource(const std::string& type, const ParamSet& ps);
    void areaLightSource(const std::string& type, const ParamSet& ps);
    void addMesh(std::shared_ptr<Mesh> mesh, const GraphicsState& gs, const std::string& where);

    Scene& scene_;
    Logger& log_;
    GraphicsState gs_;
    struct StackEntry { GraphicsState gs; bool transformOnly; int line; std::string file; };
    std::vector<StackEntry> stack_;
    std::map<std::string, std::shared_ptr<Material>> namedMaterials_;
    std::map<std::string, std::shared_ptr<Texture>> textures_;
    std::map<std::string, Transform> namedCoordSys_;
    std::map<std::string, std::vector<ShapeCall>> objects_;
    std::string currentObject_;
    std::string baseDir_ = ".";
    bool inObject_ = false;
    bool inWorld_ = false;
    bool cameraSeen_ = false, filmSeen_ = false, integratorSeen_ = false;
    Transform cameraFromWorld_;
    std::string cameraType_ = "perspective";
    ParamSet cameraParams_;
    std::string filmFilename_;
    int shapeCount_ = 0;
};

const std::set<std::string> kDirectives = {
    "Accelerator", "ActiveTransform", "AreaLightSource", "Attribute", "AttributeBegin", "AttributeEnd", "Camera",
    "ColorSpace", "ConcatTransform", "CoordinateSystem", "CoordSysTransform", "Film", "Identity", "Import",
    "Include", "Integrator", "LightSource", "LookAt", "MakeNamedMaterial", "MakeNamedMedium", "Material",
    "MediumInterface", "NamedMaterial", "ObjectBegin", "ObjectEnd", "ObjectInstance", "Option", "PixelFilter",
    "ReverseOrientation", "Rotate", "Sampler", "Scale", "Shape", "Texture", "Transform", "TransformBegin",
    "TransformEnd", "TransformTimes", "Translate", "WorldBegin", "WorldEnd"};

void Parser::skipToNextDirective(TokenStream& ts) {
    while (!ts.atEnd()) {
        const Token& t = ts.peek();
        if (t.kind == Token::Kind::Word && kDirectives.count(t.text)) return;
        ts.next();
    }
}

bool Parser::readString(TokenStream& ts, const std::string& directive, int line, std::string& out) {
    const Token& t = ts.peek();
    if (t.kind != Token::Kind::String) {
        log_.error(where(ts, line) + ": " + directive + " expects a quoted string argument but found '" +
                   (t.kind == Token::Kind::End ? std::string("end of file") : t.text) + "'");
        return false;
    }
    out = ts.next().text;
    return true;
}

bool Parser::readNumbers(TokenStream& ts, int count, const std::string& directive, int line, std::vector<double>& out,
                         bool allowBrackets) {
    out.clear();
    bool bracket = false;
    if (allowBrackets && ts.peek().kind == Token::Kind::LBracket) {
        bracket = true;
        ts.next();
    }
    while ((int)out.size() < count) {
        const Token& t = ts.peek();
        double v;
        if (t.kind != Token::Kind::Word || !parseNumber(t.text, v)) {
            log_.error(where(ts, line) + ": malformed " + directive + ": expected " + std::to_string(count) +
                       " numbers but found " + std::to_string(out.size()) +
                       (t.kind == Token::Kind::End ? std::string(" before end of file") : " followed by '" + t.text + "'"));
            return false;
        }
        out.push_back(v);
        ts.next();
    }
    if (bracket) {
        if (ts.peek().kind != Token::Kind::RBracket) {
            log_.error(where(ts, line) + ": malformed " + directive + ": expected ']' after " + std::to_string(count) + " numbers");
            return false;
        }
        ts.next();
    }
    return true;
}

bool Parser::parseParams(TokenStream& ts, ParamSet& ps, int line) {
    static const std::set<std::string> knownTypes = {"integer", "float",   "point2",   "vector2", "point3",
                                                     "vector3", "normal",  "normal3",  "point",   "vector",
                                                     "rgb",     "color",   "spectrum", "string",  "bool",
                                                     "texture", "blackbody"};
    ps.log = &log_;
    ps.where = where(ts, line);
    bool ok = true;
    while (ts.peek().kind == Token::Kind::String) {
        Token decl = ts.next();
        std::istringstream ss(decl.text);
        std::string type, name, extra;
        ss >> type >> name;
        bool malformed = name.empty() || (ss >> extra);
        // Read the value(s) first so that we can skip them on error.
        std::vector<Token> values;
        if (ts.peek().kind == Token::Kind::LBracket) {
            ts.next();
            while (ts.peek().kind != Token::Kind::RBracket) {
                if (ts.peek().kind == Token::Kind::End || ts.peek().kind == Token::Kind::LBracket ||
                    (ts.peek().kind == Token::Kind::Word && kDirectives.count(ts.peek().text))) {
                    log_.error(where(ts, decl.line) + ": unterminated '[' in value list of parameter \"" + decl.text + "\"");
                    return false;
                }
                values.push_back(ts.next());
            }
            ts.next();
        } else if (ts.peek().kind == Token::Kind::Word || ts.peek().kind == Token::Kind::String) {
            if (ts.peek().kind == Token::Kind::Word && kDirectives.count(ts.peek().text)) {
                log_.error(where(ts, decl.line) + ": parameter \"" + decl.text + "\" has no value");
                ok = false;
                continue;
            }
            values.push_back(ts.next());
        } else {
            log_.error(where(ts, decl.line) + ": parameter \"" + decl.text + "\" has no value");
            ok = false;
            continue;
        }
        if (malformed) {
            log_.error(where(ts, decl.line) + ": malformed parameter declaration \"" + decl.text +
                       "\" (expected \"type name\"); parameter ignored");
            ok = false;
            continue;
        }
        if (type == "point") type = "point3";
        if (type == "vector") type = "vector3";
        if (type == "normal3") type = "normal";
        if (type == "color") type = "rgb";
        if (!knownTypes.count(type)) {
            log_.error(where(ts, decl.line) + ": unknown parameter type '" + type + "' in \"" + decl.text + "\"; parameter ignored");
            ok = false;
            continue;
        }
        Param p;
        p.type = type;
        p.name = name;
        p.line = decl.line;
        bool bad = false;
        for (const Token& v : values) {
            if (type == "string" || type == "texture") {
                if (v.kind != Token::Kind::String) { bad = true; break; }
                p.strs.push_back(v.text);
            } else if (type == "bool") {
                std::string s = lowerStr(v.text);
                if (s == "true") p.bools.push_back(true);
                else if (s == "false") p.bools.push_back(false);
                else { bad = true; break; }
            } else if (type == "spectrum" && v.kind == Token::Kind::String) {
                p.strs.push_back(v.text);  // named spectrum or spectrum file
            } else {
                double d;
                if (v.kind != Token::Kind::Word || !parseNumber(v.text, d)) {
                    log_.error(where(ts, decl.line) + ": invalid numeric value '" + v.text + "' for parameter \"" + decl.text + "\"; parameter ignored");
                    bad = true;
                    break;
                }
                p.nums.push_back(d);
            }
        }
        if (bad) {
            if (type == "string" || type == "texture" || type == "bool")
                log_.error(where(ts, decl.line) + ": invalid value for " + type + " parameter \"" + name + "\"; parameter ignored");
            ok = false;
            continue;
        }
        size_t groups = 1;
        if (type == "point3" || type == "vector3" || type == "normal" || type == "rgb") groups = 3;
        if (type == "point2" || type == "vector2") groups = 2;
        size_t count = p.nums.size() + p.strs.size() + p.bools.size();
        if (count == 0 || (p.strs.empty() && p.nums.size() % groups != 0)) {
            log_.error(where(ts, decl.line) + ": parameter \"" + decl.text + "\" has " + std::to_string(count) +
                       " values, which is not a non-zero multiple of " + std::to_string(groups) + "; parameter ignored");
            ok = false;
            continue;
        }
        if ((type == "integer") ) {
            for (double d : p.nums)
                if (d != std::floor(d)) {
                    log_.warning(where(ts, decl.line) + ": integer parameter \"" + name + "\" has non-integer value; truncated");
                    break;
                }
        }
        for (const Param& q : ps.params)
            if (q.name == name) log_.warning(where(ts, decl.line) + ": parameter \"" + name + "\" specified more than once; first value used");
        ps.params.push_back(p);
    }
    return ok;
}

std::string Parser::resolvePath(const std::string& dir, const std::string& file) const {
    fs::path p(file);
    if (p.is_absolute()) return p.string();
    return (fs::path(dir) / p).lexically_normal().string();
}

// ============================================================ Directives ===

bool Parser::parseFile(const std::string& path, int includeDepth) {
    if (includeDepth > 32) {
        log_.error("Include depth exceeded 32 while including '" + path + "' (recursive include?)");
        return false;
    }
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        log_.error("cannot open scene file '" + path + "'");
        return false;
    }
    std::stringstream buf;
    buf << f.rdbuf();
    TokenStream ts;
    ts.file = path;
    // As in PBRT, relative paths (Include, textures, meshes) are resolved
    // against the directory of the top-level scene file.
    if (includeDepth == 0) {
        baseDir_ = fs::path(path).parent_path().string();
        if (baseDir_.empty()) baseDir_ = ".";
    }
    ts.dir = baseDir_;
    tokenize(buf.str(), path, ts.tokens, log_);

    while (!ts.atEnd()) {
        Token tok = ts.next();
        int line = tok.line;
        std::string w = where(ts, line);
        if (tok.kind != Token::Kind::Word) {
            log_.error(w + ": unexpected " + (tok.kind == Token::Kind::String ? "string \"" + tok.text + "\"" : "'" + tok.text + "'") +
                       " where a directive was expected; skipping");
            skipToNextDirective(ts);
            continue;
        }
        const std::string& d = tok.text;
        if (!kDirectives.count(d)) {
            log_.error(w + ": unknown directive '" + d + "'; skipping until the next directive");
            skipToNextDirective(ts);
            continue;
        }
        std::vector<double> nums;
        std::string s1, s2, s3;
        ParamSet ps;

        auto needWorld = [&](bool world) {
            if (world && !inWorld_) {
                log_.error(w + ": '" + d + "' is only allowed after WorldBegin; ignored");
                return false;
            }
            if (!world && inWorld_) {
                log_.error(w + ": '" + d + "' is only allowed before WorldBegin; ignored");
                return false;
            }
            return true;
        };
        auto typeAndParams = [&]() -> bool {
            if (!readString(ts, d, line, s1)) { skipToNextDirective(ts); return false; }
            parseParams(ts, ps, line);
            return true;
        };

        if (d == "Translate") {
            if (readNumbers(ts, 3, d, line, nums, false)) applyTransform(Transform::translate(Vec3(nums[0], nums[1], nums[2])));
            else skipToNextDirective(ts);
        } else if (d == "Scale") {
            if (readNumbers(ts, 3, d, line, nums, false)) {
                if (nums[0] == 0 || nums[1] == 0 || nums[2] == 0)
                    log_.error(w + ": Scale with a zero factor makes the transform singular; ignored");
                else
                    applyTransform(Transform::scale(Vec3(nums[0], nums[1], nums[2])));
            } else skipToNextDirective(ts);
        } else if (d == "Rotate") {
            if (readNumbers(ts, 4, d, line, nums, false)) {
                Vec3 axis(nums[1], nums[2], nums[3]);
                if (lengthSquared(axis) == 0) log_.error(w + ": Rotate axis is the zero vector; ignored");
                else applyTransform(Transform::rotate(nums[0], axis));
            } else skipToNextDirective(ts);
        } else if (d == "LookAt") {
            if (readNumbers(ts, 9, d, line, nums, false)) {
                Vec3 pos(nums[0], nums[1], nums[2]), look(nums[3], nums[4], nums[5]), up(nums[6], nums[7], nums[8]);
                Vec3 dir = look - pos;
                if (lengthSquared(dir) == 0) {
                    log_.error(w + ": LookAt eye and target are identical; ignored");
                } else {
                    dir = normalize(dir);
                    Vec3 right = cross(normalize(up), dir);
                    if (length(right) < 1e-9) {
                        log_.error(w + ": LookAt 'up' vector is parallel to the viewing direction; ignored");
                    } else {
                        right = normalize(right);
                        Vec3 newUp = cross(dir, right);
                        Mat4 m;  // worldFromCamera
                        m.m[0][0] = right.x; m.m[1][0] = right.y; m.m[2][0] = right.z;
                        m.m[0][1] = newUp.x; m.m[1][1] = newUp.y; m.m[2][1] = newUp.z;
                        m.m[0][2] = dir.x;   m.m[1][2] = dir.y;   m.m[2][2] = dir.z;
                        m.m[0][3] = pos.x;   m.m[1][3] = pos.y;   m.m[2][3] = pos.z;
                        Transform worldFromCamera;
                        Transform::fromMatrix(m, worldFromCamera);
                        applyTransform(worldFromCamera.inverse());
                    }
                }
            } else skipToNextDirective(ts);
        } else if (d == "Transform" || d == "ConcatTransform") {
            if (readNumbers(ts, 16, d, line, nums, true)) {
                Mat4 m;
                // PBRT matrices are given column-major (i.e. the transpose of row-major).
                for (int i = 0; i < 4; ++i)
                    for (int j = 0; j < 4; ++j) m.m[i][j] = nums[j * 4 + i];
                Transform t;
                if (!Transform::fromMatrix(m, t)) {
                    log_.error(w + ": " + d + " matrix is singular; ignored");
                } else if (d == "Transform") {
                    gs_.ctm = t;
                } else {
                    applyTransform(t);
                }
            } else skipToNextDirective(ts);
        } else if (d == "Identity") {
            gs_.ctm = Transform();
        } else if (d == "CoordinateSystem") {
            if (readString(ts, d, line, s1)) namedCoordSys_[s1] = gs_.ctm;
            else skipToNextDirective(ts);
        } else if (d == "CoordSysTransform") {
            if (readString(ts, d, line, s1)) {
                auto it = namedCoordSys_.find(s1);
                if (it == namedCoordSys_.end()) log_.error(w + ": unknown coordinate system \"" + s1 + "\"");
                else gs_.ctm = it->second;
            } else skipToNextDirective(ts);
        } else if (d == "ActiveTransform") {
            if (ts.peek().kind == Token::Kind::Word) ts.next();
            log_.warning(w + ": ActiveTransform (motion blur) is not supported; ignored");
        } else if (d == "TransformTimes") {
            readNumbers(ts, 2, d, line, nums, false);
            log_.warning(w + ": TransformTimes (motion blur) is not supported; ignored");
        } else if (d == "ReverseOrientation") {
            gs_.reverseOrientation = !gs_.reverseOrientation;
        } else if (d == "AttributeBegin" || d == "TransformBegin") {
            if (d == "TransformBegin") log_.info(w + ": TransformBegin is deprecated in PBRT-v4 (treated as a transform push)");
            stack_.push_back({gs_, d == "TransformBegin", line, ts.file});
        } else if (d == "AttributeEnd" || d == "TransformEnd") {
            if (stack_.empty()) {
                log_.error(w + ": unmatched " + d + "; ignored");
            } else {
                StackEntry e = stack_.back();
                stack_.pop_back();
                if (e.transformOnly != (d == "TransformEnd"))
                    log_.warning(w + ": " + d + " closes a " + (e.transformOnly ? "TransformBegin" : "AttributeBegin") +
                                 " opened at line " + std::to_string(e.line));
                if (e.transformOnly) gs_.ctm = e.gs.ctm;
                else gs_ = e.gs;
            }
        } else if (d == "Attribute") {
            if (typeAndParams()) {
                for (auto& p : ps.params) p.used = true;
                log_.warning(w + ": 'Attribute' directive is not supported; ignored");
            }
        } else if (d == "Option") {
            parseParams(ts, ps, line);
            for (auto& p : ps.params) p.used = true;
            log_.warning(w + ": 'Option' directive is not supported; ignored");
        } else if (d == "ColorSpace") {
            if (readString(ts, d, line, s1) && s1 != "srgb")
                log_.warning(w + ": colour space \"" + s1 + "\" not supported; sRGB primaries assumed");
        } else if (d == "Camera") {
            if (typeAndParams() && needWorld(false)) camera(s1, ps);
        } else if (d == "Film") {
            if (typeAndParams() && needWorld(false)) film(s1, ps);
        } else if (d == "Sampler") {
            if (typeAndParams() && needWorld(false)) sampler(s1, ps);
        } else if (d == "Integrator") {
            if (typeAndParams() && needWorld(false)) integrator(s1, ps);
        } else if (d == "PixelFilter") {
            if (typeAndParams() && needWorld(false)) {
                for (auto& p : ps.params) p.used = true;
                if (s1 != "box")
                    log_.warning(w + ": pixel filter \"" + s1 + "\" not supported; a box filter (sample average) is used");
            }
        } else if (d == "Accelerator") {
            if (typeAndParams() && needWorld(false)) {
                for (auto& p : ps.params) p.used = true;
                if (s1 == "none") {
                    scene_.settings.useBVH = false;
                    log_.info(w + ": acceleration structure disabled (brute force intersection)");
                } else if (s1 != "bvh") {
                    log_.warning(w + ": accelerator \"" + s1 + "\" not supported; using BVH");
                }
            }
        } else if (d == "WorldBegin") {
            if (inWorld_) log_.error(w + ": duplicate WorldBegin; ignored");
            else {
                inWorld_ = true;
                if (!cameraSeen_) {
                    log_.warning(w + ": no Camera specified before WorldBegin; using default perspective camera (fov 90)");
                    cameraFromWorld_ = gs_.ctm;
                }
                namedCoordSys_["world"] = Transform();
                gs_.ctm = Transform();
            }
        } else if (d == "WorldEnd") {
            log_.info(w + ": WorldEnd (PBRT-v3) ignored");
        } else if (d == "Include" || d == "Import") {
            if (readString(ts, d, line, s1)) {
                std::string p = resolvePath(ts.dir, s1);
                log_.info(w + ": including '" + p + "'");
                if (!parseFile(p, includeDepth + 1)) log_.error(w + ": failed to include '" + s1 + "'");
            } else skipToNextDirective(ts);
        } else if (d == "Material") {
            if (typeAndParams() && needWorld(true)) {
                auto m = makeMaterial(s1, ps, "anonymous@" + w);
                gs_.material = m;
            }
        } else if (d == "MakeNamedMaterial") {
            if (typeAndParams() && needWorld(true)) {
                std::string type = ps.getString("type", "");
                if (type.empty()) {
                    log_.error(w + ": MakeNamedMaterial \"" + s1 + "\" has no \"string type\" parameter; using diffuse");
                    type = "diffuse";
                }
                if (namedMaterials_.count(s1)) log_.warning(w + ": named material \"" + s1 + "\" redefined");
                namedMaterials_[s1] = makeMaterial(type, ps, s1);
            }
        } else if (d == "NamedMaterial") {
            if (readString(ts, d, line, s1) && needWorld(true)) {
                auto it = namedMaterials_.find(s1);
                if (it == namedMaterials_.end()) log_.error(w + ": undefined named material \"" + s1 + "\"; keeping current material");
                else gs_.material = it->second;
            }
        } else if (d == "Texture") {
            if (readString(ts, d, line, s1) && readString(ts, d, line, s2) && readString(ts, d, line, s3)) {
                parseParams(ts, ps, line);
                if (needWorld(true)) texture(s1, s2, s3, ps, ts.dir);
            } else skipToNextDirective(ts);
        } else if (d == "Shape") {
            if (typeAndParams() && needWorld(true)) {
                if (inObject_) objects_[currentObject_].push_back({s1, ps, gs_, ts.dir});
                else shape(s1, ps, gs_, ts.dir);
            }
        } else if (d == "LightSource") {
            if (typeAndParams() && needWorld(true)) lightSource(s1, ps);
        } else if (d == "AreaLightSource") {
            if (typeAndParams() && needWorld(true)) areaLightSource(s1, ps);
        } else if (d == "ObjectBegin") {
            if (readString(ts, d, line, s1) && needWorld(true)) {
                if (inObject_) log_.error(w + ": nested ObjectBegin is not allowed");
                stack_.push_back({gs_, false, line, ts.file});
                inObject_ = true;
                currentObject_ = s1;
                objects_[s1].clear();
            }
        } else if (d == "ObjectEnd") {
            if (!inObject_) log_.error(w + ": ObjectEnd without ObjectBegin");
            else {
                inObject_ = false;
                if (!stack_.empty()) { gs_ = stack_.back().gs; stack_.pop_back(); }
            }
        } else if (d == "ObjectInstance") {
            if (readString(ts, d, line, s1) && needWorld(true)) {
                auto it = objects_.find(s1);
                if (it == objects_.end()) log_.error(w + ": unknown object \"" + s1 + "\" in ObjectInstance");
                else
                    for (const ShapeCall& c : it->second) {
                        GraphicsState g = c.gs;
                        g.ctm = gs_.ctm * c.gs.ctm;
                        ParamSet p = c.params;
                        shape(c.type, p, g, c.dir);
                    }
            }
        } else if (d == "MakeNamedMedium" || d == "MediumInterface") {
            // Consume arguments.
            while (ts.peek().kind == Token::Kind::String && ts.peek().text.find(' ') == std::string::npos) ts.next();
            parseParams(ts, ps, line);
            for (auto& p : ps.params) p.used = true;
            log_.warning(w + ": participating media ('" + d + "') are not supported; ignored");
        }
    }
    return true;
}

void Parser::finish() {
    if (!stack_.empty())
        log_.warning(std::to_string(stack_.size()) + " AttributeBegin/TransformBegin block(s) not closed at end of file (first opened at " +
                     stack_.back().file + ":" + std::to_string(stack_.back().line) + ")");
    if (inObject_) log_.error("ObjectBegin \"" + currentObject_ + "\" not closed at end of file");
    if (!inWorld_) log_.error("scene file has no WorldBegin; the world is empty");

    // Camera is created at the end so that the Film resolution is known.
    Camera& cam = scene_.camera;
    cam.width = scene_.settings.width;
    cam.height = scene_.settings.height;
    cam.cameraToWorld = cameraFromWorld_.inverse();
    if (cameraSeen_) {
        ParamSet& ps = cameraParams_;
        std::string w = ps.where;
        cam.type = cameraType_ == "orthographic" ? Camera::Type::Orthographic : Camera::Type::Perspective;
        double fov = ps.getFloat("fov", 90);
        if (!(fov > 0 && fov < 180)) {
            log_.error(w + ": fov " + std::to_string(fov) + " out of range (0, 180); using 90");
            fov = 90;
        }
        cam.fov = fov;
        double lr = ps.getFloat("lensradius", 0);
        if (lr < 0) {
            log_.error(w + ": negative lensradius; using 0");
            lr = 0;
        }
        cam.lensRadius = lr;
        double fd = ps.getFloat("focaldistance", 1e6);
        if (fd <= 0) {
            log_.error(w + ": focaldistance must be positive; using 1e6");
            fd = 1e6;
        }
        cam.focalDistance = fd;
        std::vector<double> sw = ps.getFloats("screenwindow");
        if (!sw.empty()) {
            if (sw.size() != 4 || sw[0] >= sw[1] || sw[2] >= sw[3])
                log_.error(w + ": screenwindow needs 4 values [xmin xmax ymin ymax] with min < max; ignored");
            else
                for (int i = 0; i < 4; ++i) cam.screenWindow[i] = sw[i];
        }
        double far = ps.getFloat("frameaspectratio", -1);
        if (far > 0 && sw.empty()) {
            if (far > 1) { cam.screenWindow[0] = -far; cam.screenWindow[1] = far; cam.screenWindow[2] = -1; cam.screenWindow[3] = 1; }
            else { cam.screenWindow[0] = -1; cam.screenWindow[1] = 1; cam.screenWindow[2] = -1 / far; cam.screenWindow[3] = 1 / far; }
        }
        ps.ignore({"shutteropen", "shutterclose", "lensfile", "aperturediameter", "focusdistance"});
        ps.reportUnused();
    }
    cam.setup();
    log_.info("camera: " + std::string(cam.type == Camera::Type::Perspective ? "perspective" : "orthographic") +
              (cam.type == Camera::Type::Perspective ? ", fov " + std::to_string(cam.fov) : "") +
              ", lens radius " + std::to_string(cam.lensRadius) + ", focal distance " + std::to_string(cam.focalDistance));

    if (scene_.primitives.empty()) log_.warning("scene contains no geometry");
    if (scene_.pointLights.empty() && scene_.distantLights.empty() && scene_.areaLights.empty() &&
        scene_.background.isBlack())
        log_.warning("scene contains no light sources; the image will be black (apart from ambient light)");
}

// ------------------------------------------------------- Camera / options ---

void Parser::camera(const std::string& type, const ParamSet& ps) {
    if (cameraSeen_) log_.warning(ps.where + ": multiple Camera directives; the last one is used");
    cameraSeen_ = true;
    cameraFromWorld_ = gs_.ctm;
    namedCoordSys_["camera"] = gs_.ctm.inverse();
    cameraType_ = type;
    if (type != "perspective" && type != "orthographic") {
        log_.error(ps.where + ": unsupported camera type \"" + type + "\"; using perspective");
        cameraType_ = "perspective";
    }
    cameraParams_ = ps;
}

void Parser::film(const std::string& type, const ParamSet& ps) {
    filmSeen_ = true;
    RenderSettings& s = scene_.settings;
    if (type != "rgb" && type != "image")
        log_.warning(ps.where + ": film type \"" + type + "\" not supported; treated as \"rgb\"");
    int xr = ps.getInt("xresolution", 1280), yr = ps.getInt("yresolution", 720);
    if (xr <= 0 || yr <= 0 || xr > 16384 || yr > 16384) {
        log_.error(ps.where + ": film resolution " + std::to_string(xr) + "x" + std::to_string(yr) +
                   " out of range [1, 16384]; using 640x480");
        xr = 640;
        yr = 480;
    }
    s.width = xr;
    s.height = yr;
    std::string fn = ps.getString("filename", "");
    if (!fn.empty()) {
        fs::path p(fn);
        std::string ext = lowerStr(p.extension().string());
        s.outputName = p.stem().string();
        if (ext == ".ppm") s.outputExt = ".ppm";
        else if (ext == ".png") s.outputExt = ".png";
        else {
            log_.warning(ps.where + ": output format '" + ext + "' not supported; writing PNG instead");
            s.outputExt = ".png";
        }
        if (p.has_parent_path())
            log_.info(ps.where + ": directory part of filename ignored; images are always written to the output folder");
    }
    // Tone mapping (renderer-specific parameters).
    std::string tm = lowerStr(ps.getString("tonemap", ""));
    if (!tm.empty()) {
        s.tonemapSpecified = true;
        if (tm == "reinhard") s.tonemap.op = ToneMapSettings::Op::Reinhard;
        else if (tm == "clamp" || tm == "none") s.tonemap.op = ToneMapSettings::Op::Clamp;
        else log_.error(ps.where + ": unknown tonemap \"" + tm + "\" (expected reinhard|clamp)");
    }
    std::string mode = lowerStr(ps.getString("tonemapmode", "luminance"));
    if (mode == "channel" || mode == "perchannel" || mode == "rgb") s.tonemap.perChannel = true;
    else if (mode == "luminance") s.tonemap.perChannel = false;
    else log_.error(ps.where + ": unknown tonemapmode \"" + mode + "\" (expected luminance|channel)");
    s.tonemap.key = ps.getFloat("key", 0.18);
    s.tonemap.whitePoint = ps.getFloat("whitepoint", -1);
    s.tonemap.exposure = ps.getFloat("exposure", 1.0);
    if (s.tonemap.exposure <= 0) {
        log_.error(ps.where + ": exposure must be positive; using 1");
        s.tonemap.exposure = 1;
    }
    s.tonemap.srgb = ps.getBool("srgb", true);
    s.writeHDR = ps.getBool("writehdr", s.writeHDR);
    ps.ignore({"diagonal", "sensor", "iso", "whitebalance", "exposuretime", "maxcomponentvalue", "savefp16",
               "cropwindow", "pixelbounds"});
    ps.reportUnused();
}

static bool parseStrategy(const std::string& name, SampleStrategy& s) {
    std::string n = lowerStr(name);
    if (n == "uniform" || n == "random" || n == "independent") s = SampleStrategy::Uniform;
    else if (n == "grid" || n == "regular") s = SampleStrategy::Grid;
    else if (n == "stratified" || n == "jittered") s = SampleStrategy::Stratified;
    else if (n == "halton") s = SampleStrategy::Halton;
    else return false;
    return true;
}

static int roundToSquare(int n) {
    int k = std::max(1, (int)std::lround(std::sqrt((double)n)));
    return k * k;
}

void Parser::sampler(const std::string& type, const ParamSet& ps) {
    RenderSettings& s = scene_.settings;
    int spp = 1;
    if (type == "stratified") {
        int xs = ps.getInt("xsamples", 4), ys = ps.getInt("ysamples", 4);
        if (xs != ys) log_.warning(ps.where + ": stratified sampler with xsamples != ysamples; using a square grid");
        int k = std::max(1, std::max(xs, ys));
        spp = k * k;
        bool jitter = ps.getBool("jitter", true);
        s.pixelSampler = jitter ? SampleStrategy::Stratified : SampleStrategy::Grid;
    } else {
        spp = ps.getInt("pixelsamples", 16);
        if (type == "independent" || type == "random") s.pixelSampler = SampleStrategy::Uniform;
        else if (type == "halton") s.pixelSampler = SampleStrategy::Halton;
        else if (type == "grid") s.pixelSampler = SampleStrategy::Grid;
        else {
            log_.warning(ps.where + ": sampler \"" + type + "\" not supported; using halton");
            s.pixelSampler = SampleStrategy::Halton;
        }
    }
    if (spp < 1 || spp > 65536) {
        log_.error(ps.where + ": pixel sample count " + std::to_string(spp) + " out of range [1, 65536]; using 1");
        spp = 1;
    }
    if (s.pixelSampler == SampleStrategy::Grid && roundToSquare(spp) != spp) {
        log_.warning(ps.where + ": grid sampling needs a square sample count; " + std::to_string(spp) + " rounded to " +
                     std::to_string(roundToSquare(spp)));
        spp = roundToSquare(spp);
    }
    s.spp = spp;
    int seed = ps.getInt("seed", 0);
    s.seed = (uint64_t)seed;
    ps.ignore({"randomization"});
    ps.reportUnused();
}

void Parser::integrator(const std::string& type, const ParamSet& ps) {
    RenderSettings& s = scene_.settings;
    integratorSeen_ = true;
    std::string t = lowerStr(type);
    if (t == "whitted" || t == "wsrt") {
        s.integrator = "whitted";
        s.maxDepth = 5;
    } else if (t == "distributed" || t == "drt") {
        s.integrator = "distributed";
        s.maxDepth = 3;
    } else {
        log_.warning(ps.where + ": integrator \"" + type + "\" not supported; using the distributed ray tracer");
        s.integrator = "distributed";
        s.maxDepth = 3;
    }
    int md = ps.getInt("maxdepth", s.maxDepth);
    if (md < 0 || md > 64) {
        log_.error(ps.where + ": maxdepth " + std::to_string(md) + " out of range [0, 64]; clamped");
        md = std::max(0, std::min(64, md));
    }
    s.maxDepth = md;
    s.rayEpsilon = ps.getFloat("rayepsilon", s.rayEpsilon);
    if (s.rayEpsilon < 0) {
        log_.error(ps.where + ": rayepsilon must be non-negative; using 1e-4");
        s.rayEpsilon = 1e-4;
    }
    Color amb;
    if (ps.getColor("ambient", amb)) s.ambient = amb;
    if (s.integrator == "distributed") {
        int bf = ps.getInt("branching", s.branching);
        if (bf < 0 || bf > 4096) {
            log_.error(ps.where + ": branching factor " + std::to_string(bf) + " out of range [0, 4096]; using 4");
            bf = 4;
        }
        s.branching = bf;
        int ls = ps.getInt("lightsamples", s.lightSamples);
        if (ls < 1 || ls > 65536) {
            log_.error(ps.where + ": lightsamples " + std::to_string(ls) + " out of range [1, 65536]; using 16");
            ls = 16;
        }
        std::string strat = ps.getString("lightsampler", "uniform");
        if (!parseStrategy(strat, s.lightSampler)) {
            log_.error(ps.where + ": unknown lightsampler \"" + strat + "\" (expected uniform|grid|halton|stratified); using uniform");
            s.lightSampler = SampleStrategy::Uniform;
        }
        if ((s.lightSampler == SampleStrategy::Grid || s.lightSampler == SampleStrategy::Stratified) &&
            roundToSquare(ls) != ls) {
            log_.warning(ps.where + ": " + std::string(strategyName(s.lightSampler)) + " light sampling needs a square sample count; " +
                         std::to_string(ls) + " rounded to " + std::to_string(roundToSquare(ls)));
            ls = roundToSquare(ls);
        }
        s.lightSamples = ls;
        s.randomizeHalton = ps.getBool("randomizehalton", true);
        s.caustics = ps.getBool("caustics", false);
        std::string hemi = lowerStr(ps.getString("hemisphere", "uniform"));
        if (hemi == "cosine") s.cosineHemisphere = true;
        else if (hemi == "uniform") s.cosineHemisphere = false;
        else log_.error(ps.where + ": unknown hemisphere sampling \"" + hemi + "\" (expected uniform|cosine)");
    }
    ps.ignore({"regularize", "lightsampler_pbrt"});
    ps.reportUnused();
}

// ------------------------------------------------- Materials / textures ---

std::shared_ptr<Texture> Parser::getTexture(const ParamSet& ps, std::initializer_list<const char*> names, const Color& def) {
    for (const char* n : names) {
        const Param* p = nullptr;
        for (const Param& q : ps.params)
            if (q.name == n) p = &q;
        if (!p) continue;
        if (p->type == "texture") {
            p->used = true;
            std::string tn = p->strs.empty() ? "" : p->strs[0];
            auto it = textures_.find(tn);
            if (it == textures_.end()) {
                log_.error(ps.where + ": undefined texture \"" + tn + "\" for parameter \"" + n + "\"; using default");
                return Texture::constant(def);
            }
            return it->second;
        }
        Color c;
        if (ps.getColor(n, c)) return Texture::constant(c);
        return Texture::constant(def);
    }
    return Texture::constant(def);
}

static double roughnessToExponent(double r) {
    r = std::max(r, 1e-3);
    return clampd(2.0 / (r * r) - 2.0, 1.0, 10000.0);
}

std::shared_ptr<Material> Parser::makeMaterial(const std::string& typeIn, const ParamSet& ps, const std::string& name) {
    auto m = std::make_shared<Material>();
    m->name = name;
    std::string type = lowerStr(typeIn);
    m->sourceType = type;
    const std::string& w = ps.where;
    auto color = [&](std::initializer_list<const char*> names, const Color& def) {
        for (const char* n : names) {
            Color c;
            if (ps.has(n) && ps.getColor(n, c)) return c;
        }
        return def;
    };
    auto optionalKa = [&]() {
        if (ps.has("Ka")) {
            m->hasKa = true;
            m->Ka = color({"Ka"}, Color(0));
        }
    };

    if (type == "diffuse" || type == "matte" || type == "lambertian") {
        m->Kd = getTexture(ps, {"reflectance", "Kd"}, Color(0.5));
        optionalKa();
    } else if (type == "phong" || type == "blinnphong" || type == "blinn-phong" || type == "plastic") {
        m->Kd = getTexture(ps, {"Kd", "reflectance", "diffuse"}, Color(0.5));
        m->Ks = color({"Ks", "specular"}, Color(type == "plastic" ? 0.25 : 0.3));
        if (ps.has("exponent") || ps.has("shininess"))
            m->exponent = ps.has("exponent") ? ps.getFloat("exponent", 32) : ps.getFloat("shininess", 32);
        else if (ps.has("roughness"))
            m->exponent = roughnessToExponent(ps.getFloat("roughness", 0.1));
        if (m->exponent < 0) {
            log_.error(w + ": negative specular exponent; using 32");
            m->exponent = 32;
        }
        m->Kr = color({"Kr", "reflectivity"}, Color(0));
        optionalKa();
        ps.ignore({"remaproughness"});
    } else if (type == "mirror") {
        m->Kd = Texture::constant(Color(0));
        m->Kr = color({"Kr", "reflectance"}, Color(0.9));
    } else if (type == "conductor") {
        m->Kd = Texture::constant(Color(0));
        Color refl(0.9);
        if (ps.has("reflectance")) refl = color({"reflectance"}, refl);
        else if (ps.has("eta") || ps.has("k"))
            log_.warning(w + ": spectral conductor eta/k approximated by a grey reflectance of 0.9 (use \"rgb reflectance\")");
        ps.ignore({"eta", "k"});
        double rough = ps.getFloat("roughness", 0);
        if (ps.has("uroughness")) rough = ps.getFloat("uroughness", rough);
        if (ps.has("vroughness")) rough = 0.5 * (rough + ps.getFloat("vroughness", rough));
        ps.ignore({"remaproughness"});
        if (rough > 0) {
            m->Ks = refl;
            m->exponent = roughnessToExponent(rough);
            log_.info(w + ": rough conductor approximated by a Phong lobe with exponent " + std::to_string(m->exponent));
        } else {
            m->Kr = refl;
        }
    } else if (type == "dielectric" || type == "glass" || type == "thindielectric") {
        if (type == "thindielectric") log_.warning(w + ": thindielectric approximated by a solid dielectric");
        m->Kd = Texture::constant(Color(0));
        m->dielectric = true;
        const Param* ep = nullptr;
        for (const Param& q : ps.params)
            if (q.name == "eta" || q.name == "index") ep = &q;
        if (ep) {
            ep->used = true;
            if (!ep->nums.empty() && (ep->type == "float")) m->eta = ep->nums[0];
            else log_.warning(w + ": non-constant eta not supported; using 1.5");
        }
        if (m->eta <= 0) {
            log_.error(w + ": index of refraction must be positive; using 1.5");
            m->eta = 1.5;
        }
        m->Kr = color({"Kr"}, Color(1));
        m->Kt = color({"Kt"}, Color(1));
        double rough = ps.getFloat("roughness", 0);
        if (ps.has("uroughness")) rough = ps.getFloat("uroughness", 0);
        if (ps.has("vroughness")) ps.getFloat("vroughness", 0);
        ps.ignore({"remaproughness"});
        if (rough > 0) log_.warning(w + ": rough dielectric not supported; rendered as smooth");
    } else if (type == "coateddiffuse") {
        m->Kd = getTexture(ps, {"reflectance"}, Color(0.5));
        double rough = ps.getFloat("roughness", 0);
        if (rough > 0) {
            m->Ks = Color(0.04);
            m->exponent = roughnessToExponent(rough);
        } else {
            m->Kr = Color(0.04);
        }
        ps.ignore({"thickness", "albedo", "g", "maxdepth", "nsamples", "eta", "remaproughness", "uroughness", "vroughness"});
        log_.warning(w + ": coateddiffuse approximated by diffuse + 4% specular coat");
    } else if (type == "interface" || type == "none" || type.empty()) {
        log_.warning(w + ": material \"" + type + "\" has no surface; shapes using it are skipped");
        return nullptr;
    } else {
        log_.error(w + ": unsupported material type \"" + typeIn + "\"; using grey diffuse");
        m->Kd = getTexture(ps, {"reflectance", "Kd"}, Color(0.5));
        for (auto& p : ps.params) p.used = true;
    }
    ps.ignore({"type", "displacement", "normalmap"});

    // Validate energy conservation for constant colours.
    if (m->Kd->isConstant()) {
        Color kd = m->Kd->eval(Vec2());
        double mx = (kd + m->Ks).maxComponent();
        if (mx > 1.0 + 1e-9 && scene_.settings.integrator == "distributed")
            log_.warning(w + ": Kd + Ks exceeds 1 (" + std::to_string(mx) +
                         "); the material does not conserve energy (the DRT normalises it)");
    }
    if (m->Kr.maxComponent() > 1 || m->Kt.maxComponent() > 1)
        log_.warning(w + ": Kr/Kt greater than 1 make the material emit energy");
    ps.reportUnused();
    return m;
}

void Parser::texture(const std::string& name, const std::string& valueType, const std::string& cls, const ParamSet& ps,
                     const std::string& dir) {
    if (valueType != "spectrum" && valueType != "float" && valueType != "color")
        log_.error(ps.where + ": texture value type \"" + valueType + "\" must be \"spectrum\" or \"float\"");
    auto t = std::make_shared<Texture>();
    std::string c = lowerStr(cls);
    if (c == "imagemap") {
        t->type = Texture::Type::ImageMap;
        std::string fn = ps.getString("filename", "");
        std::string filter = lowerStr(ps.getString("filter", "bilinear"));
        t->bilinear = !(filter == "point" || filter == "nearest");
        std::string enc = lowerStr(ps.getString("encoding", "srgb"));
        bool invert = ps.getBool("invert", false);
        ps.ignore({"maxanisotropy", "wrap"});
        auto img = std::make_shared<Image>();
        std::string err;
        std::string path = resolvePath(dir, fn);
        for (const char* n : {"uscale", "vscale", "udelta", "vdelta", "scale", "mapping"}) ps.find(n);
        if (fn.empty()) {
            log_.error(ps.where + ": imagemap texture \"" + name + "\" has no filename; using magenta");
            t = Texture::constant(Color(1, 0, 1));
        } else if (!readImage(path, *img, err)) {
            log_.error(ps.where + ": texture \"" + name + "\": " + err + "; using magenta");
            t = Texture::constant(Color(1, 0, 1));
        } else {
            bool isPFM = lowerStr(fs::path(path).extension().string()) == ".pfm";
            if (enc == "srgb" && !isPFM)
                for (Color& p : img->pixels) p = Color(srgbToLinear(p.x), srgbToLinear(p.y), srgbToLinear(p.z));
            else if (enc.rfind("gamma", 0) == 0) {
                double g = std::atof(enc.c_str() + 5);
                if (g > 0)
                    for (Color& p : img->pixels) p = Color(std::pow(p.x, g), std::pow(p.y, g), std::pow(p.z, g));
            } else if (enc != "linear" && enc != "srgb")
                log_.warning(ps.where + ": unknown encoding \"" + enc + "\"; treated as linear");
            if (invert)
                for (Color& p : img->pixels) p = Color(1) - p;
            t->image = img;
            log_.info(ps.where + ": loaded texture \"" + name + "\" from '" + path + "' (" + std::to_string(img->width) +
                      "x" + std::to_string(img->height) + ")");
        }
    } else if (c == "checkerboard") {
        t->type = Texture::Type::Checkerboard;
        int dim = ps.getInt("dimension", 2);
        if (dim != 2) log_.warning(ps.where + ": only 2D checkerboard supported");
        t->tex1 = getTexture(ps, {"tex1"}, Color(1));
        t->tex2 = getTexture(ps, {"tex2"}, Color(0));
    } else if (c == "constant") {
        t = getTexture(ps, {"value"}, Color(1));
        t = std::make_shared<Texture>(*t);
    } else if (c == "scale") {
        auto base = getTexture(ps, {"tex"}, Color(1));
        t = std::make_shared<Texture>(*base);
        Color sc(1);
        const Param* p = ps.find("scale");
        if (p && !p->nums.empty()) sc = p->nums.size() == 3 ? Color(p->nums[0], p->nums[1], p->nums[2]) : Color(p->nums[0]);
        t->scale = t->scale * sc;
    } else {
        log_.error(ps.where + ": unsupported texture class \"" + cls + "\"; using constant grey");
        for (auto& p : ps.params) p.used = true;
        t = Texture::constant(Color(0.5));
    }
    if (t->type != Texture::Type::Constant || c == "constant" || c == "scale") {
        std::string mapping = lowerStr(ps.getString("mapping", "uv"));
        if (mapping != "uv") log_.warning(ps.where + ": texture mapping \"" + mapping + "\" not supported; using uv");
        t->uscale = ps.getFloat("uscale", t->uscale);
        t->vscale = ps.getFloat("vscale", t->vscale);
        t->udelta = ps.getFloat("udelta", t->udelta);
        t->vdelta = ps.getFloat("vdelta", t->vdelta);
        if (ps.has("scale") && c == "imagemap") {
            double s = ps.getFloat("scale", 1);
            t->scale = Color(s);
        }
    }
    if (textures_.count(name)) log_.warning(ps.where + ": texture \"" + name + "\" redefined");
    textures_[name] = t;
    ps.reportUnused();
}

// --------------------------------------------------------------- Shapes ---

void Parser::addMesh(std::shared_ptr<Mesh> mesh, const GraphicsState& gs, const std::string& where) {
    (void)where;
    scene_.meshes.push_back(mesh);
    AreaLight* light = nullptr;
    if (gs.areaLight.active) {
        scene_.areaLights.push_back(std::make_unique<AreaLight>());
        light = scene_.areaLights.back().get();
        light->L = gs.areaLight.L;
        light->twoSided = gs.areaLight.twoSided;
        light->shapeName = "trianglemesh";
    }
    size_t ntri = mesh->indices.size() / 3;
    for (size_t i = 0; i < ntri; ++i) {
        auto t = std::make_shared<Triangle>(mesh, (int)i);
        if (t->area() <= 0) continue;  // skip degenerate triangles
        t->material = gs.material;
        t->light = light;
        if (light) light->prims.push_back(t.get());
        scene_.primitives.push_back(t);
    }
}

void Parser::shape(const std::string& typeIn, const ParamSet& psIn, const GraphicsState& gs, const std::string& dir) {
    ParamSet ps = psIn;
    std::string type = lowerStr(typeIn);
    const std::string& w = ps.where;
    if (!gs.material) {
        for (auto& p : ps.params) p.used = true;
        log_.info(w + ": shape with 'interface' material skipped");
        return;
    }
    ps.ignore({"alpha", "emissionfilename"});
    bool reverse = gs.reverseOrientation;
    ++shapeCount_;

    auto addSingle = [&](std::shared_ptr<Primitive> prim, const char* name) {
        prim->material = gs.material;
        if (gs.areaLight.active) {
            scene_.areaLights.push_back(std::make_unique<AreaLight>());
            AreaLight* light = scene_.areaLights.back().get();
            light->L = gs.areaLight.L;
            light->twoSided = gs.areaLight.twoSided;
            light->shapeName = name;
            light->prims.push_back(prim.get());
            prim->light = light;
        }
        scene_.primitives.push_back(prim);
    };

    if (type == "sphere") {
        double r = ps.getFloat("radius", 1);
        if (!(r > 0)) {
            log_.error(w + ": sphere radius must be positive (got " + std::to_string(r) + "); shape skipped");
            ps.reportUnused();
            return;
        }
        if (ps.has("zmin") || ps.has("zmax") || ps.has("phimax")) {
            log_.warning(w + ": partial spheres (zmin/zmax/phimax) not supported; full sphere used");
            ps.ignore({"zmin", "zmax", "phimax"});
        }
        addSingle(std::make_shared<Sphere>(gs.ctm, r, reverse), "sphere");
    } else if (type == "disk") {
        double r = ps.getFloat("radius", 1), h = ps.getFloat("height", 0);
        if (!(r > 0)) {
            log_.error(w + ": disk radius must be positive; shape skipped");
            ps.reportUnused();
            return;
        }
        if (ps.getFloat("innerradius", 0) > 0) log_.warning(w + ": disk innerradius not supported; ignored");
        ps.ignore({"phimax"});
        addSingle(std::make_shared<Disk>(gs.ctm, r, h, reverse), "disk");
    } else if (type == "plane") {
        Vec3 p = ps.getPoint3("p", Vec3(0, 0, 0));
        if (ps.has("point")) p = ps.getPoint3("point", p);
        Vec3 n = ps.getPoint3("n", Vec3(0, 1, 0));
        if (ps.has("normal")) n = ps.getPoint3("normal", n);
        if (lengthSquared(n) == 0) {
            log_.error(w + ": plane normal is the zero vector; shape skipped");
            ps.reportUnused();
            return;
        }
        Vec3 wn = normalize(gs.ctm.normal(n));
        if (reverse != gs.ctm.swapsHandedness()) wn = -wn;
        if (gs.areaLight.active) log_.error(w + ": an infinite plane cannot be an area light; emission ignored");
        auto prim = std::make_shared<Plane>(gs.ctm.point(p), wn, gs.ctm.vector(Vec3(1, 0, 0)));
        prim->material = gs.material;
        scene_.primitives.push_back(prim);
    } else if (type == "trianglemesh" || type == "bilinearmesh" || type == "loopsubdiv") {
        if (type == "loopsubdiv") {
            log_.warning(w + ": loopsubdiv: subdivision not supported; rendering the control mesh");
            ps.ignore({"levels"});
        }
        auto mesh = std::make_shared<Mesh>();
        std::vector<double> P = ps.getFloats("P");
        std::vector<int> idx = ps.getInts("indices");
        std::vector<double> N = ps.getFloats("N");
        std::vector<double> UV = ps.getFloats("uv");
        if (UV.empty()) UV = ps.getFloats("st");
        ps.ignore({"S", "faceIndices", "emissionfilename"});
        if (P.empty() || P.size() % 3) {
            log_.error(w + ": " + type + " requires \"point3 P\" with a multiple of 3 values; shape skipped");
            ps.reportUnused();
            return;
        }
        size_t nv = P.size() / 3;
        int per = type == "bilinearmesh" ? 4 : 3;
        if (idx.empty()) {
            if ((int)nv == per) {
                for (int i = 0; i < per; ++i) idx.push_back(i);
            } else {
                log_.error(w + ": " + type + " requires \"integer indices\"; shape skipped");
                ps.reportUnused();
                return;
            }
        }
        if (idx.size() % per) {
            log_.error(w + ": number of indices (" + std::to_string(idx.size()) + ") is not a multiple of " +
                       std::to_string(per) + "; shape skipped");
            ps.reportUnused();
            return;
        }
        for (int i : idx)
            if (i < 0 || (size_t)i >= nv) {
                log_.error(w + ": vertex index " + std::to_string(i) + " out of range [0, " + std::to_string(nv - 1) +
                           "]; shape skipped");
                ps.reportUnused();
                return;
            }
        for (size_t i = 0; i < nv; ++i) mesh->P.push_back(gs.ctm.point(Vec3(P[3 * i], P[3 * i + 1], P[3 * i + 2])));
        if (!N.empty()) {
            if (N.size() != P.size()) log_.error(w + ": \"normal N\" must have one normal per vertex; normals ignored");
            else
                for (size_t i = 0; i < nv; ++i)
                    mesh->N.push_back(normalize(gs.ctm.normal(Vec3(N[3 * i], N[3 * i + 1], N[3 * i + 2]))));
            if (!mesh->N.empty() && reverse) for (Vec3& n : mesh->N) n = -n;
        }
        if (!UV.empty()) {
            if (UV.size() != nv * 2) log_.error(w + ": \"point2 uv\" must have one uv per vertex; uvs ignored");
            else
                for (size_t i = 0; i < nv; ++i) mesh->UV.push_back(Vec2(UV[2 * i], UV[2 * i + 1]));
        } else if (type == "bilinearmesh" && nv == 4) {
            mesh->UV = {Vec2(0, 0), Vec2(1, 0), Vec2(0, 1), Vec2(1, 1)};
        }
        if (per == 4) {
            for (size_t q = 0; q < idx.size(); q += 4) {
                // Bilinear patch vertex order is p00, p10, p01, p11.
                int a = idx[q], b = idx[q + 1], c = idx[q + 2], d = idx[q + 3];
                mesh->indices.insert(mesh->indices.end(), {a, b, d, a, d, c});
            }
        } else {
            mesh->indices = idx;
        }
        mesh->flipNormals = reverse != gs.ctm.swapsHandedness();
        addMesh(mesh, gs, w);
    } else if (type == "objmesh" || type == "obj") {
        std::string fn = ps.getString("filename", "");
        bool smooth = ps.getBool("smooth", false);
        if (fn.empty()) {
            log_.error(w + ": objmesh requires \"string filename\"; shape skipped");
            ps.reportUnused();
            return;
        }
        auto mesh = std::make_shared<Mesh>();
        std::string path = resolvePath(dir, fn);
        Timer t;
        if (!loadOBJ(path, *mesh, smooth, log_)) {
            log_.error(w + ": failed to load OBJ '" + path + "'; shape skipped");
            ps.reportUnused();
            return;
        }
        for (Vec3& p : mesh->P) p = gs.ctm.point(p);
        for (Vec3& n : mesh->N) n = normalize(gs.ctm.normal(n)) * (reverse ? -1.0 : 1.0);
        mesh->flipNormals = reverse != gs.ctm.swapsHandedness();
        log_.info(w + ": loaded OBJ '" + path + "' (" + std::to_string(mesh->P.size()) + " vertices, " +
                  std::to_string(mesh->indices.size() / 3) + " triangles) in " + std::to_string(t.seconds()) + " s");
        addMesh(mesh, gs, w);
    } else if (type == "plymesh") {
        for (auto& p : ps.params) p.used = true;
        log_.error(w + ": plymesh is not supported (convert to OBJ and use Shape \"objmesh\"); shape skipped");
        return;
    } else {
        for (auto& p : ps.params) p.used = true;
        log_.error(w + ": unsupported shape type \"" + typeIn + "\"; shape skipped");
        return;
    }
    ps.reportUnused();
}

// --------------------------------------------------------------- Lights ---

void Parser::lightSource(const std::string& typeIn, const ParamSet& ps) {
    std::string type = lowerStr(typeIn);
    double scale = ps.getFloat("scale", 1);
    if (scale < 0) {
        log_.error(ps.where + ": negative light scale; using 1");
        scale = 1;
    }
    if (ps.has("power")) {
        ps.find("power");
        log_.warning(ps.where + ": light \"power\" parameter not supported; ignored (use I/L and scale)");
    }
    if (type == "point" || type == "spot") {
        if (type == "spot") {
            log_.warning(ps.where + ": spot light approximated by a point light (cone ignored)");
            ps.ignore({"to", "coneangle", "conedelta"});
        }
        Color I(1);
        if (ps.has("I")) ps.getColor("I", I);
        Vec3 from = ps.getPoint3("from", Vec3(0));
        PointLight pl;
        pl.position = gs_.ctm.point(from);
        pl.intensity = I * scale;
        scene_.pointLights.push_back(pl);
    } else if (type == "distant") {
        Color L(1);
        if (ps.has("L")) ps.getColor("L", L);
        Vec3 from = ps.getPoint3("from", Vec3(0)), to = ps.getPoint3("to", Vec3(0, 0, 1));
        Vec3 d = gs_.ctm.vector(from - to);
        if (lengthSquared(d) == 0) {
            log_.error(ps.where + ": distant light with from == to; light skipped");
        } else {
            DistantLight dl;
            dl.toLight = normalize(d);
            dl.radiance = L * scale;
            scene_.distantLights.push_back(dl);
        }
    } else if (type == "infinite") {
        Color L(1);
        if (ps.has("L")) ps.getColor("L", L);
        if (ps.has("filename")) {
            log_.warning(ps.where + ": environment maps not supported; using constant radiance");
            ps.ignore({"filename", "illuminance", "portal"});
        }
        scene_.background = scene_.background + L * scale;
    } else {
        for (auto& p : ps.params) p.used = true;
        log_.error(ps.where + ": unsupported light type \"" + typeIn + "\"; light skipped");
        return;
    }
    ps.reportUnused();
}

void Parser::areaLightSource(const std::string& type, const ParamSet& ps) {
    if (lowerStr(type) != "diffuse") log_.warning(ps.where + ": area light type \"" + type + "\" not supported; treated as diffuse");
    Color L(1);
    if (ps.has("L")) ps.getColor("L", L);
    double scale = ps.getFloat("scale", 1);
    if (scale < 0) {
        log_.error(ps.where + ": negative area light scale; using 1");
        scale = 1;
    }
    if (ps.has("power")) {
        ps.find("power");
        log_.warning(ps.where + ": area light \"power\" parameter not supported; ignored");
    }
    ps.ignore({"filename"});
    gs_.areaLight.active = true;
    gs_.areaLight.L = L * scale;
    gs_.areaLight.twoSided = ps.getBool("twosided", false);
    ps.reportUnused();
}

}  // namespace

// ================================================================== OBJ ===

bool loadOBJ(const std::string& path, Mesh& mesh, bool computeSmoothNormals, Logger& log) {
    std::ifstream f(path);
    if (!f) {
        log.error("cannot open OBJ file '" + path + "'");
        return false;
    }
    std::vector<Vec3> pos, nrm;
    std::vector<Vec2> tex;
    std::map<std::tuple<int, int, int>, int> remap;
    bool anyNormals = false, anyUV = false;
    std::vector<std::tuple<int, int, int>> corners;  // unified vertex keys
    std::string line;
    int lineNo = 0, errors = 0;
    auto err = [&](const std::string& msg) {
        if (++errors <= 10) log.error(path + ":" + std::to_string(lineNo) + ": " + msg);
    };
    auto fixIndex = [&](int i, size_t n) -> int {
        if (i > 0) return i - 1;
        if (i < 0) return (int)n + i;
        return -1;
    };
    std::vector<int> faceIdx;
    while (std::getline(f, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ss(line);
        std::string tag;
        if (!(ss >> tag) || tag[0] == '#') continue;
        if (tag == "v") {
            Vec3 p;
            if (!(ss >> p.x >> p.y >> p.z)) err("malformed vertex");
            pos.push_back(p);
        } else if (tag == "vn") {
            Vec3 n;
            if (!(ss >> n.x >> n.y >> n.z)) err("malformed normal");
            nrm.push_back(n);
        } else if (tag == "vt") {
            Vec2 t;
            if (!(ss >> t.x)) err("malformed texture coordinate");
            if (!(ss >> t.y)) t.y = 0;
            tex.push_back(t);
        } else if (tag == "f") {
            std::string vs;
            std::vector<std::tuple<int, int, int>> face;
            bool bad = false;
            while (ss >> vs) {
                int vi = 0, ti = 0, ni = 0;
                size_t s1 = vs.find('/');
                try {
                    vi = std::stoi(vs.substr(0, s1));
                    if (s1 != std::string::npos) {
                        size_t s2 = vs.find('/', s1 + 1);
                        std::string ts = vs.substr(s1 + 1, s2 == std::string::npos ? std::string::npos : s2 - s1 - 1);
                        if (!ts.empty()) ti = std::stoi(ts);
                        if (s2 != std::string::npos && s2 + 1 < vs.size()) ni = std::stoi(vs.substr(s2 + 1));
                    }
                } catch (...) {
                    err("malformed face vertex '" + vs + "'");
                    bad = true;
                    break;
                }
                int v = fixIndex(vi, pos.size()), t = ti ? fixIndex(ti, tex.size()) : -1, n = ni ? fixIndex(ni, nrm.size()) : -1;
                if (v < 0 || v >= (int)pos.size() || (ti && (t < 0 || t >= (int)tex.size())) ||
                    (ni && (n < 0 || n >= (int)nrm.size()))) {
                    err("face index out of range in '" + vs + "'");
                    bad = true;
                    break;
                }
                face.emplace_back(v, t, n);
            }
            if (bad) continue;
            if (face.size() < 3) {
                err("face with fewer than 3 vertices");
                continue;
            }
            for (size_t k = 1; k + 1 < face.size(); ++k) {  // fan triangulation
                corners.push_back(face[0]);
                corners.push_back(face[k]);
                corners.push_back(face[k + 1]);
            }
        }
        // o, g, s, usemtl, mtllib, l, ... are ignored.
    }
    if (errors > 10) log.error(path + ": " + std::to_string(errors - 10) + " further errors suppressed");
    if (corners.empty()) {
        log.error(path + ": OBJ file contains no faces");
        return false;
    }
    for (auto& c : corners) {
        if (std::get<1>(c) >= 0) anyUV = true;
        if (std::get<2>(c) >= 0) anyNormals = true;
    }
    for (auto& c : corners) {
        auto it = remap.find(c);
        int id;
        if (it == remap.end()) {
            id = (int)mesh.P.size();
            remap[c] = id;
            mesh.P.push_back(pos[std::get<0>(c)]);
            if (anyUV) mesh.UV.push_back(std::get<1>(c) >= 0 ? tex[std::get<1>(c)] : Vec2(0, 0));
            if (anyNormals) mesh.N.push_back(std::get<2>(c) >= 0 ? normalize(nrm[std::get<2>(c)]) : Vec3(0));
        } else {
            id = it->second;
        }
        mesh.indices.push_back(id);
    }
    if (!anyNormals && computeSmoothNormals) {
        // Area-weighted vertex normals, accumulated per *position* so that
        // vertices split only by UV seams still share a normal.
        std::vector<Vec3> acc(pos.size(), Vec3(0));
        std::vector<int> posOf(mesh.P.size());
        for (auto& kv : remap) posOf[kv.second] = std::get<0>(kv.first);
        for (size_t i = 0; i < mesh.indices.size(); i += 3) {
            const Vec3 &a = mesh.P[mesh.indices[i]], &b = mesh.P[mesh.indices[i + 1]], &c = mesh.P[mesh.indices[i + 2]];
            Vec3 n = cross(b - a, c - a);
            for (int k = 0; k < 3; ++k) acc[posOf[mesh.indices[i + k]]] += n;
        }
        mesh.N.resize(mesh.P.size());
        for (size_t i = 0; i < mesh.P.size(); ++i) mesh.N[i] = normalize(acc[posOf[i]]);
    } else if (anyNormals) {
        for (Vec3& n : mesh.N)
            if (lengthSquared(n) == 0) {
                log.warning(path + ": some face vertices have no normal; per-vertex normals dropped");
                mesh.N.clear();
                break;
            }
    }
    return true;
}

bool parseSceneFile(const std::string& path, Scene& scene, Logger& log) {
    Parser parser(scene, log);
    if (!parser.parseFile(path, 0)) return false;
    parser.finish();
    return true;
}
