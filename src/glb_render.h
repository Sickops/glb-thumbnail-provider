// glb_render.h - Lecteur GLB (glTF 2.0 binaire) + rasteriseur logiciel.
// Code portable (aucune dépendance Windows) : le décodage des textures est
// délégué à un callback (WIC côté Windows).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace glbr {

// ============================================================ JSON minimal
struct JV {
    enum Type { Null, Bool, Num, Str, Arr, Obj } t = Null;
    double n = 0;
    bool b = false;
    std::string s;
    std::vector<JV> a;
    std::vector<std::pair<std::string, JV>> o;

    const JV* get(const char* k) const {
        if (t != Obj) return nullptr;
        for (auto& kv : o)
            if (kv.first == k) return &kv.second;
        return nullptr;
    }
    const JV* at(size_t i) const { return (t == Arr && i < a.size()) ? &a[i] : nullptr; }
};

inline double jnum(const JV* v, const char* k, double d) {
    if (!v) return d;
    const JV* x = v->get(k);
    return (x && x->t == JV::Num) ? x->n : d;
}
inline int jint(const JV* v, const char* k, int d) {
    double x = jnum(v, k, (double)d);
    if (!(x > -2e9 && x < 2e9)) return d;
    return (int)x;
}
inline std::string jstr(const JV* v, const char* k) {
    if (!v) return std::string();
    const JV* x = v->get(k);
    return (x && x->t == JV::Str) ? x->s : std::string();
}

struct JParser {
    const char* p;
    const char* e;

    void ws() {
        while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }
    static void utf8(std::string& s, unsigned cp) {
        if (cp < 0x80) s += (char)cp;
        else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
        else { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    }
    bool str(std::string& out) {
        if (p >= e || *p != '"') return false;
        ++p;
        while (p < e && *p != '"') {
            if (*p == '\\') {
                if (++p >= e) return false;
                switch (*p) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {
                        if (e - p < 5) return false;
                        unsigned cp = 0;
                        for (int i = 1; i <= 4; i++) {
                            char c = p[i];
                            cp <<= 4;
                            if (c >= '0' && c <= '9') cp |= c - '0';
                            else if (c >= 'a' && c <= 'f') cp |= c - 'a' + 10;
                            else if (c >= 'A' && c <= 'F') cp |= c - 'A' + 10;
                            else return false;
                        }
                        utf8(out, cp);
                        p += 4;
                        break;
                    }
                    default: out += *p; break;
                }
                ++p;
            } else out += *p++;
        }
        if (p >= e) return false;
        ++p;
        return true;
    }
    bool parse(JV& out, int depth) {
        if (depth > 64) return false;
        ws();
        if (p >= e) return false;
        char c = *p;
        if (c == '{') {
            ++p;
            out.t = JV::Obj;
            ws();
            if (p < e && *p == '}') { ++p; return true; }
            for (;;) {
                ws();
                std::string k;
                if (!str(k)) return false;
                ws();
                if (p >= e || *p != ':') return false;
                ++p;
                out.o.emplace_back(std::move(k), JV());
                if (!parse(out.o.back().second, depth + 1)) return false;
                ws();
                if (p >= e) return false;
                if (*p == ',') { ++p; continue; }
                if (*p == '}') { ++p; return true; }
                return false;
            }
        }
        if (c == '[') {
            ++p;
            out.t = JV::Arr;
            ws();
            if (p < e && *p == ']') { ++p; return true; }
            for (;;) {
                out.a.emplace_back();
                if (!parse(out.a.back(), depth + 1)) return false;
                ws();
                if (p >= e) return false;
                if (*p == ',') { ++p; continue; }
                if (*p == ']') { ++p; return true; }
                return false;
            }
        }
        if (c == '"') { out.t = JV::Str; return str(out.s); }
        if (e - p >= 4 && !strncmp(p, "true", 4)) { p += 4; out.t = JV::Bool; out.b = true; return true; }
        if (e - p >= 5 && !strncmp(p, "false", 5)) { p += 5; out.t = JV::Bool; out.b = false; return true; }
        if (e - p >= 4 && !strncmp(p, "null", 4)) { p += 4; out.t = JV::Null; return true; }
        char* endp = nullptr;
        double v = strtod(p, &endp);  // le texte est terminé par un '\0'
        if (endp == p) return false;
        p = endp;
        out.t = JV::Num;
        out.n = v;
        return true;
    }
};

// ============================================================ Maths
inline void matIdentity(float* m) { memset(m, 0, 64); m[0] = m[5] = m[10] = m[15] = 1.f; }
inline void matMul(const float* a, const float* b, float* out) {  // column-major
    float t[16];
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k];
            t[c * 4 + r] = s;
        }
    memcpy(out, t, 64);
}
inline void matTRS(const float* t, const float* q, const float* s, float* m) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float n = std::sqrt(x * x + y * y + z * z + w * w);
    if (n > 1e-12f) { x /= n; y /= n; z /= n; w /= n; } else { x = y = z = 0; w = 1; }
    float r00 = 1 - 2 * (y * y + z * z), r01 = 2 * (x * y - z * w), r02 = 2 * (x * z + y * w);
    float r10 = 2 * (x * y + z * w), r11 = 1 - 2 * (x * x + z * z), r12 = 2 * (y * z - x * w);
    float r20 = 2 * (x * z - y * w), r21 = 2 * (y * z + x * w), r22 = 1 - 2 * (x * x + y * y);
    m[0] = r00 * s[0]; m[1] = r10 * s[0]; m[2] = r20 * s[0]; m[3] = 0;
    m[4] = r01 * s[1]; m[5] = r11 * s[1]; m[6] = r21 * s[1]; m[7] = 0;
    m[8] = r02 * s[2]; m[9] = r12 * s[2]; m[10] = r22 * s[2]; m[11] = 0;
    m[12] = t[0]; m[13] = t[1]; m[14] = t[2]; m[15] = 1;
}

// ============================================================ Conteneur GLB
struct Gltf {
    JV root;
    std::string jsonText;
    const uint8_t* bin = nullptr;
    size_t binLen = 0;

    static uint32_t rd32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }

    bool load(const uint8_t* d, size_t n) {
        if (n < 20 || rd32(d) != 0x46546C67u) return false;  // "glTF"
        size_t total = rd32(d + 8);
        if (total > n || total < 20) total = n;
        size_t off = 12;
        bool haveJson = false;
        while (off + 8 <= total) {
            size_t len = rd32(d + off), type = rd32(d + off + 4);
            off += 8;
            if (len > total - off) len = total - off;
            if (type == 0x4E4F534Au && !haveJson) {
                jsonText.assign((const char*)d + off, len);
                haveJson = true;
            } else if (type == 0x004E4942u && !bin) {
                bin = d + off;
                binLen = len;
            }
            off += (len + 3) & ~(size_t)3;
        }
        if (!haveJson) return false;
        JParser jp{jsonText.c_str(), jsonText.c_str() + jsonText.size()};
        return jp.parse(root, 0) && root.t == JV::Obj;
    }

    const JV* arr(const char* name, int i) const {
        if (i < 0) return nullptr;
        const JV* a = root.get(name);
        return a ? a->at((size_t)i) : nullptr;
    }
    size_t arrSize(const char* name) const {
        const JV* a = root.get(name);
        return (a && a->t == JV::Arr) ? a->a.size() : 0;
    }

    static float fetch(const uint8_t* p, int ct, bool norm) {
        switch (ct) {
            case 5120: { int8_t v; memcpy(&v, p, 1); return norm ? std::max(v / 127.f, -1.f) : (float)v; }
            case 5121: { return norm ? p[0] / 255.f : (float)p[0]; }
            case 5122: { int16_t v; memcpy(&v, p, 2); return norm ? std::max(v / 32767.f, -1.f) : (float)v; }
            case 5123: { uint16_t v; memcpy(&v, p, 2); return norm ? v / 65535.f : (float)v; }
            case 5125: { uint32_t v; memcpy(&v, p, 4); return (float)v; }
            case 5126: { float v; memcpy(&v, p, 4); return v; }
        }
        return 0.f;
    }
    static int compSize(int ct) {
        switch (ct) {
            case 5120: case 5121: return 1;
            case 5122: case 5123: return 2;
            case 5125: case 5126: return 4;
        }
        return 0;
    }

    // Localise les octets d'un accesseur. Renvoie false si invalide.
    bool locate(int ai, const uint8_t*& base, size_t& stride, size_t& count, int& ncomp, int& ct, bool& norm) const {
        const JV* acc = arr("accessors", ai);
        if (!acc || !bin) return false;
        int bvi = jint(acc, "bufferView", -1);
        const JV* bv = arr("bufferViews", bvi);
        if (!bv || jint(bv, "buffer", 0) != 0) return false;
        double dc = jnum(acc, "count", 0);
        if (!(dc >= 0 && dc <= (double)binLen)) return false;
        count = (size_t)dc;
        ct = jint(acc, "componentType", 0);
        int cs = compSize(ct);
        if (!cs) return false;
        std::string ty = jstr(acc, "type");
        ncomp = ty == "SCALAR" ? 1 : ty == "VEC2" ? 2 : ty == "VEC3" ? 3 : ty == "VEC4" ? 4 : 0;
        if (!ncomp) return false;
        const JV* nv = acc->get("normalized");
        norm = nv && nv->t == JV::Bool && nv->b;
        double o1 = jnum(bv, "byteOffset", 0), o2 = jnum(acc, "byteOffset", 0);
        double bs = jnum(bv, "byteStride", 0);
        size_t el = (size_t)ncomp * cs;
        stride = bs > 0 ? (size_t)bs : el;
        if (stride < el || o1 < 0 || o2 < 0) return false;
        size_t off = (size_t)o1 + (size_t)o2;
        if (off > binLen) return false;
        if (count > 0 && off + (count - 1) * stride + el > binLen) return false;
        base = bin + off;
        return true;
    }

    bool readFloats(int ai, int want, float fill, std::vector<float>& out) const {
        const uint8_t* base; size_t stride, count; int nc, ct; bool norm;
        if (!locate(ai, base, stride, count, nc, ct, norm)) return false;
        int cs = compSize(ct);
        out.assign(count * want, fill);
        for (size_t i = 0; i < count; i++)
            for (int c = 0; c < want && c < nc; c++)
                out[i * want + c] = fetch(base + i * stride + c * cs, ct, norm);
        return true;
    }
    bool readIndices(int ai, std::vector<uint32_t>& out) const {
        const uint8_t* base; size_t stride, count; int nc, ct; bool norm;
        if (!locate(ai, base, stride, count, nc, ct, norm) || nc != 1) return false;
        if (ct != 5121 && ct != 5123 && ct != 5125) return false;
        out.resize(count);
        for (size_t i = 0; i < count; i++) {
            const uint8_t* p = base + i * stride;
            if (ct == 5121) out[i] = p[0];
            else if (ct == 5123) { uint16_t v; memcpy(&v, p, 2); out[i] = v; }
            else { uint32_t v; memcpy(&v, p, 4); out[i] = v; }
        }
        return true;
    }
};

// ============================================================ Rendu
// Pixels de texture : 0xAARRGGBB, non prémultipliés.
using TexDecoder = std::function<bool(const uint8_t* data, size_t len, int& w, int& h, std::vector<uint32_t>& px)>;

struct Tex { bool tried = false, ok = false; int w = 0, h = 0; std::vector<uint32_t> px; };
struct MatInfo {
    float base[4] = {0.8f, 0.8f, 0.8f, 1.f};  // déjà converti vers l'espace d'affichage
    int img = -1;
    bool mask = false, blend = false, unlit = false;
    float cutoff = 0.5f;
};
struct Prim {
    std::vector<float> pos, nrm, uv, col;
    std::vector<uint32_t> idx;  // liste de triangles
    bool hasN = false, hasUV = false, hasCol = false;
    int mat = -1;
};
struct SV { float sx, sy, z, vx, vy, nx, ny, nz, u, v, c[4]; };

class Renderer {
public:
    Gltf g;
    TexDecoder dec;
    std::vector<MatInfo> mats;
    std::vector<Tex> imgs;
    MatInfo defMat;

    struct Item { int mesh; float M[16]; };
    std::vector<Item> items;
    int steps = 0;

    // --- matériaux
    static float toDisp(float v) { return v <= 0 ? 0 : (v >= 1 ? 1 : std::pow(v, 1.f / 2.2f)); }
    int texImage(const JV* texInfo) const {
        if (!texInfo) return -1;
        const JV* t = g.arr("textures", jint(texInfo, "index", -1));
        if (!t) return -1;
        int src = jint(t, "source", -1);
        if (src < 0) {
            const JV* ex = t->get("extensions");
            if (ex && ex->t == JV::Obj)
                for (auto& kv : ex->o) {
                    int s = jint(&kv.second, "source", -1);
                    if (s >= 0) { src = s; break; }
                }
        }
        return src;
    }
    void loadMaterials() {
        size_t n = g.arrSize("materials");
        mats.assign(n, MatInfo());
        for (size_t i = 0; i < n; i++) {
            const JV* m = g.arr("materials", (int)i);
            MatInfo& mi = mats[i];
            const JV* pbr = m->get("pbrMetallicRoughness");
            const JV* ex = m->get("extensions");
            const JV* sg = ex ? ex->get("KHR_materials_pbrSpecularGlossiness") : nullptr;
            const JV* fac = nullptr;
            const JV* ti = nullptr;
            if (pbr) { fac = pbr->get("baseColorFactor"); ti = pbr->get("baseColorTexture"); }
            else if (sg) { fac = sg->get("diffuseFactor"); ti = sg->get("diffuseTexture"); }
            float f[4] = {1, 1, 1, 1};
            if (fac && fac->t == JV::Arr)
                for (int k = 0; k < 4; k++) {
                    const JV* x = fac->at(k);
                    if (x && x->t == JV::Num) f[k] = (float)x->n;
                }
            for (int k = 0; k < 3; k++) mi.base[k] = toDisp(f[k]);
            mi.base[3] = f[3];
            mi.img = texImage(ti);
            std::string am = jstr(m, "alphaMode");
            mi.mask = am == "MASK";
            mi.blend = am == "BLEND";
            mi.cutoff = (float)jnum(m, "alphaCutoff", 0.5);
            mi.unlit = ex && ex->get("KHR_materials_unlit");
        }
        imgs.assign(g.arrSize("images"), Tex());
    }
    Tex* getTex(int img) {
        if (img < 0 || img >= (int)imgs.size() || !dec) return nullptr;
        Tex& t = imgs[img];
        if (!t.tried) {
            t.tried = true;
            const JV* im = g.arr("images", img);
            const JV* bv = g.arr("bufferViews", jint(im, "bufferView", -1));
            if (bv && g.bin && jint(bv, "buffer", 0) == 0) {
                double o = jnum(bv, "byteOffset", 0), l = jnum(bv, "byteLength", 0);
                if (o >= 0 && l > 0 && o + l <= (double)g.binLen)
                    t.ok = dec(g.bin + (size_t)o, (size_t)l, t.w, t.h, t.px) && t.w > 0 && t.h > 0 &&
                           t.px.size() >= (size_t)t.w * (size_t)t.h;
            }
        }
        return t.ok ? &t : nullptr;
    }

    // --- scène
    void walk(int ni, const float* parent, int depth) {
        if (depth > 64 || ++steps > 200000) return;
        const JV* n = g.arr("nodes", ni);
        if (!n) return;
        float local[16], world[16];
        const JV* mx = n->get("matrix");
        if (mx && mx->t == JV::Arr && mx->a.size() == 16) {
            for (int i = 0; i < 16; i++) local[i] = (float)(mx->a[i].t == JV::Num ? mx->a[i].n : 0);
        } else {
            float t[3] = {0, 0, 0}, q[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
            auto rdv = [&](const char* k, float* o, int c) {
                const JV* a = n->get(k);
                if (a && a->t == JV::Arr)
                    for (int i = 0; i < c; i++) {
                        const JV* x = a->at(i);
                        if (x && x->t == JV::Num) o[i] = (float)x->n;
                    }
            };
            rdv("translation", t, 3); rdv("rotation", q, 4); rdv("scale", s, 3);
            matTRS(t, q, s, local);
        }
        matMul(parent, local, world);
        int mesh = jint(n, "mesh", -1);
        if (mesh >= 0) {
            Item it;
            it.mesh = mesh;
            if (n->get("skin")) matIdentity(it.M);  // maillage skinné : transform du noeud ignoré (spec)
            else memcpy(it.M, world, 64);
            items.push_back(it);
        }
        const JV* ch = n->get("children");
        if (ch && ch->t == JV::Arr)
            for (auto& c : ch->a)
                if (c.t == JV::Num) walk((int)c.n, world, depth + 1);
    }
    void collectItems() {
        float I[16];
        matIdentity(I);
        size_t nn = g.arrSize("nodes");
        const JV* scenes = g.root.get("scenes");
        if (scenes && scenes->t == JV::Arr && !scenes->a.empty() && nn) {
            const JV* sc = g.arr("scenes", jint(&g.root, "scene", 0));
            if (!sc) sc = scenes->at(0);
            const JV* roots = sc ? sc->get("nodes") : nullptr;
            if (roots && roots->t == JV::Arr)
                for (auto& r : roots->a)
                    if (r.t == JV::Num) walk((int)r.n, I, 0);
        } else if (nn) {
            std::vector<char> isChild(nn, 0);
            for (size_t i = 0; i < nn; i++) {
                const JV* ch = g.arr("nodes", (int)i)->get("children");
                if (ch && ch->t == JV::Arr)
                    for (auto& c : ch->a)
                        if (c.t == JV::Num && c.n >= 0 && c.n < (double)nn) isChild[(size_t)c.n] = 1;
            }
            for (size_t i = 0; i < nn; i++)
                if (!isChild[i]) walk((int)i, I, 0);
        }
        if (items.empty()) {  // pas de noeuds : on prend tous les meshes
            for (size_t m = 0; m < g.arrSize("meshes"); m++) {
                Item it; it.mesh = (int)m; matIdentity(it.M); items.push_back(it);
            }
        }
    }

    // --- primitives
    bool loadPrim(const JV* p, Prim& o) const {
        const JV* ex = p->get("extensions");
        if (ex && ex->get("KHR_draco_mesh_compression")) return false;
        if (ex && ex->get("EXT_meshopt_compression")) return false;
        int mode = jint(p, "mode", 4);
        if (mode != 4 && mode != 5 && mode != 6) return false;
        const JV* at = p->get("attributes");
        if (!at) return false;
        int ap = jint(at, "POSITION", -1);
        if (ap < 0 || !g.readFloats(ap, 3, 0.f, o.pos)) return false;
        size_t vc = o.pos.size() / 3;
        if (vc < 3) return false;
        int an = jint(at, "NORMAL", -1), au = jint(at, "TEXCOORD_0", -1), ac = jint(at, "COLOR_0", -1);
        if (an >= 0 && g.readFloats(an, 3, 0.f, o.nrm) && o.nrm.size() == vc * 3) o.hasN = true;
        if (au >= 0 && g.readFloats(au, 2, 0.f, o.uv) && o.uv.size() == vc * 2) o.hasUV = true;
        if (ac >= 0 && g.readFloats(ac, 4, 1.f, o.col) && o.col.size() == vc * 4) o.hasCol = true;
        std::vector<uint32_t> raw;
        int ai = jint(p, "indices", -1);
        if (ai >= 0) { if (!g.readIndices(ai, raw)) return false; }
        else { raw.resize(vc); for (size_t i = 0; i < vc; i++) raw[i] = (uint32_t)i; }
        if (mode == 4) {
            raw.resize(raw.size() / 3 * 3);
            o.idx.swap(raw);
        } else if (mode == 5) {
            for (size_t i = 2; i < raw.size(); i++) {
                if (i & 1) { o.idx.push_back(raw[i - 1]); o.idx.push_back(raw[i - 2]); }
                else { o.idx.push_back(raw[i - 2]); o.idx.push_back(raw[i - 1]); }
                o.idx.push_back(raw[i]);
            }
        } else {
            for (size_t i = 2; i < raw.size(); i++) {
                o.idx.push_back(raw[0]); o.idx.push_back(raw[i - 1]); o.idx.push_back(raw[i]);
            }
        }
        o.mat = jint(p, "material", -1);
        return !o.idx.empty();
    }
    template <class F>
    void forEachPrim(F&& fn) const {
        for (auto& it : items) {
            const JV* mesh = g.arr("meshes", it.mesh);
            const JV* prims = mesh ? mesh->get("primitives") : nullptr;
            if (!prims || prims->t != JV::Arr) continue;
            for (auto& pj : prims->a) {
                Prim pr;
                if (loadPrim(&pj, pr)) fn(it, pr);
            }
        }
    }

    // --- framebuffer
    int S = 0;
    std::vector<uint32_t> cbuf;
    std::vector<float> zbuf;

    static void norm3(float& x, float& y, float& z) {
        float l = std::sqrt(x * x + y * y + z * z);
        if (l > 1e-20f) { x /= l; y /= l; z /= l; }
    }

    void rasterTri(const SV& a, const SV& b, const SV& c, const MatInfo& m, const Tex* tex, bool flat) {
        float minx = std::min({a.sx, b.sx, c.sx}), maxx = std::max({a.sx, b.sx, c.sx});
        float miny = std::min({a.sy, b.sy, c.sy}), maxy = std::max({a.sy, b.sy, c.sy});
        if (!(maxx >= 0 && maxy >= 0 && minx <= S && miny <= S)) return;
        int x0 = std::max(0, (int)std::floor(minx)), x1 = std::min(S - 1, (int)std::floor(maxx));
        int y0 = std::max(0, (int)std::floor(miny)), y1 = std::min(S - 1, (int)std::floor(maxy));
        float area = (b.sx - a.sx) * (c.sy - a.sy) - (b.sy - a.sy) * (c.sx - a.sx);
        if (std::fabs(area) < 1e-9f) return;
        float inv = 1.f / area;

        float fnx = 0, fny = 0, fnz = 1;
        if (flat) {
            float e1x = b.vx - a.vx, e1y = b.vy - a.vy, e1z = b.z - a.z;
            float e2x = c.vx - a.vx, e2y = c.vy - a.vy, e2z = c.z - a.z;
            fnx = e1y * e2z - e1z * e2y; fny = e1z * e2x - e1x * e2z; fnz = e1x * e2y - e1y * e2x;
            norm3(fnx, fny, fnz);
        }
        static const float L1[3] = {-0.40f, 0.62f, 0.68f}, L2[3] = {0.62f, -0.25f, 0.74f};
        static const float H[3] = {-0.20f, 0.31f, 0.93f};

        for (int y = y0; y <= y1; y++) {
            float py = y + 0.5f;
            for (int x = x0; x <= x1; x++) {
                float px = x + 0.5f;
                float w0 = ((b.sx - px) * (c.sy - py) - (b.sy - py) * (c.sx - px)) * inv;
                float w1 = ((c.sx - px) * (a.sy - py) - (c.sy - py) * (a.sx - px)) * inv;
                float w2 = 1.f - w0 - w1;
                if (w0 < -1e-5f || w1 < -1e-5f || w2 < -1e-5f) continue;
                float z = w0 * a.z + w1 * b.z + w2 * c.z;
                size_t i = (size_t)y * S + x;
                if (z <= zbuf[i]) continue;

                float r = m.base[0], gg = m.base[1], bb = m.base[2], al = m.base[3];
                r *= w0 * a.c[0] + w1 * b.c[0] + w2 * c.c[0];
                gg *= w0 * a.c[1] + w1 * b.c[1] + w2 * c.c[1];
                bb *= w0 * a.c[2] + w1 * b.c[2] + w2 * c.c[2];
                al *= w0 * a.c[3] + w1 * b.c[3] + w2 * c.c[3];
                if (tex) {
                    float u = w0 * a.u + w1 * b.u + w2 * c.u, v = w0 * a.v + w1 * b.v + w2 * c.v;
                    u -= std::floor(u); v -= std::floor(v);
                    if (!(u >= 0 && u < 1)) u = 0;
                    if (!(v >= 0 && v < 1)) v = 0;
                    int tx = std::min(tex->w - 1, (int)(u * tex->w)), ty = std::min(tex->h - 1, (int)(v * tex->h));
                    uint32_t t = tex->px[(size_t)ty * tex->w + tx];
                    r *= ((t >> 16) & 255) / 255.f; gg *= ((t >> 8) & 255) / 255.f; bb *= (t & 255) / 255.f;
                    al *= (t >> 24) / 255.f;
                }
                if (m.mask && al < m.cutoff) continue;
                if (m.blend && al < 0.08f) continue;

                float shade = 1.f, spec = 0.f;
                if (!m.unlit) {
                    float nx, ny, nz;
                    if (flat) { nx = fnx; ny = fny; nz = fnz; }
                    else {
                        nx = w0 * a.nx + w1 * b.nx + w2 * c.nx;
                        ny = w0 * a.ny + w1 * b.ny + w2 * c.ny;
                        nz = w0 * a.nz + w1 * b.nz + w2 * c.nz;
                        norm3(nx, ny, nz);
                    }
                    if (nz < 0) { nx = -nx; ny = -ny; nz = -nz; }  // double face
                    float d1 = std::max(0.f, nx * L1[0] + ny * L1[1] + nz * L1[2]);
                    float d2 = std::max(0.f, nx * L2[0] + ny * L2[1] + nz * L2[2]);
                    shade = 0.30f + 0.10f * ny + 0.64f * d1 + 0.22f * d2;
                    float nh = std::max(0.f, nx * H[0] + ny * H[1] + nz * H[2]);
                    float s2 = nh * nh; s2 *= s2; s2 *= s2; s2 *= s2;  // ^16
                    spec = 0.10f * s2;
                }
                auto cv = [&](float v) { v = v * shade + spec; return (uint32_t)(v <= 0 ? 0 : v >= 1 ? 255 : v * 255.f + 0.5f); };
                zbuf[i] = z;
                cbuf[i] = 0xFF000000u | (cv(r) << 16) | (cv(gg) << 8) | cv(bb);
            }
        }
    }

    // --- point d'entrée
    bool render(const uint8_t* data, size_t len, int size, std::vector<uint32_t>& out) {
        if (!g.load(data, len)) return false;
        loadMaterials();
        collectItems();
        if (items.empty()) return false;

        // Caméra : vue 3/4 (de dessus, côté droit), orthographique.
        const float yaw = -35.f * 3.14159265f / 180.f, pitch = 22.f * 3.14159265f / 180.f;
        float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
        const float R[3][3] = {{cy, 0, sy}, {sp * sy, cp, -sp * cy}, {-cp * sy, sp, cp * cy}};

        // Passe A : bornes en espace vue + nombre de triangles.
        float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
        double triTotal = 0;
        forEachPrim([&](const Item& it, const Prim& p) {
            triTotal += p.idx.size() / 3;
            size_t vc = p.pos.size() / 3;
            for (size_t i = 0; i < vc; i++) {
                const float* q = &p.pos[i * 3];
                float w[3];
                for (int r = 0; r < 3; r++) w[r] = it.M[r] * q[0] + it.M[4 + r] * q[1] + it.M[8 + r] * q[2] + it.M[12 + r];
                for (int r = 0; r < 3; r++) {
                    float v = R[r][0] * w[0] + R[r][1] * w[1] + R[r][2] * w[2];
                    if (!std::isfinite(v)) continue;
                    mn[r] = std::min(mn[r], v); mx[r] = std::max(mx[r], v);
                }
            }
        });
        if (triTotal < 1 || mn[0] > mx[0]) return false;
        float bw = mx[0] - mn[0], bh = mx[1] - mn[1];
        float ext = std::max(bw, bh);
        if (!(ext > 1e-12f)) return false;

        int ss = size <= 128 ? 3 : (size <= 1024 ? 2 : 1);
        S = size * ss;
        float scale = S * 0.88f / ext;
        float cxm = (mn[0] + mx[0]) * 0.5f, cym = (mn[1] + mx[1]) * 0.5f;
        cbuf.assign((size_t)S * S, 0);
        zbuf.assign((size_t)S * S, -1e30f);
        int step = std::max(1, (int)(triTotal / 2500000.0));

        // Passe B : rastérisation.
        forEachPrim([&](const Item& it, const Prim& p) {
            // matrice des normales (cofactors) * R
            const float* M = it.M;
            float a00 = M[0], a10 = M[1], a20 = M[2], a01 = M[4], a11 = M[5], a21 = M[6], a02 = M[8], a12 = M[9], a22 = M[10];
            float C[3][3] = {
                {a11 * a22 - a12 * a21, -(a10 * a22 - a12 * a20), a10 * a21 - a11 * a20},
                {-(a01 * a22 - a02 * a21), a00 * a22 - a02 * a20, -(a00 * a21 - a01 * a20)},
                {a01 * a12 - a02 * a11, -(a00 * a12 - a02 * a10), a00 * a11 - a01 * a10}};
            // NB: C[r][c] est la cofactor-matrice de A (A[r][c] = M[c*4+r]) ; n' = C*n
            size_t vc = p.pos.size() / 3;
            std::vector<SV> sv(vc);
            for (size_t i = 0; i < vc; i++) {
                const float* q = &p.pos[i * 3];
                float w[3], v[3];
                for (int r = 0; r < 3; r++) w[r] = M[r] * q[0] + M[4 + r] * q[1] + M[8 + r] * q[2] + M[12 + r];
                for (int r = 0; r < 3; r++) v[r] = R[r][0] * w[0] + R[r][1] * w[1] + R[r][2] * w[2];
                SV& s = sv[i];
                s.vx = v[0]; s.vy = v[1]; s.z = v[2];
                s.sx = (v[0] - cxm) * scale + S * 0.5f;
                s.sy = S * 0.5f - (v[1] - cym) * scale;
                s.nx = s.ny = 0; s.nz = 1;
                if (p.hasN) {
                    const float* n = &p.nrm[i * 3];
                    float t[3], o[3];
                    for (int r = 0; r < 3; r++) t[r] = C[r][0] * n[0] + C[r][1] * n[1] + C[r][2] * n[2];
                    for (int r = 0; r < 3; r++) o[r] = R[r][0] * t[0] + R[r][1] * t[1] + R[r][2] * t[2];
                    norm3(o[0], o[1], o[2]);
                    s.nx = o[0]; s.ny = o[1]; s.nz = o[2];
                }
                s.u = p.hasUV ? p.uv[i * 2] : 0; s.v = p.hasUV ? p.uv[i * 2 + 1] : 0;
                for (int k = 0; k < 4; k++) s.c[k] = p.hasCol ? p.col[i * 4 + k] : 1.f;
            }
            const MatInfo& mi = (p.mat >= 0 && p.mat < (int)mats.size()) ? mats[p.mat] : defMat;
            const Tex* tex = mi.img >= 0 ? getTex(mi.img) : nullptr;
            if (tex && !p.hasUV) tex = nullptr;
            size_t nt = p.idx.size() / 3;
            for (size_t t = 0; t < nt; t += step) {
                uint32_t i0 = p.idx[t * 3], i1 = p.idx[t * 3 + 1], i2 = p.idx[t * 3 + 2];
                if (i0 >= vc || i1 >= vc || i2 >= vc) continue;
                const SV &A = sv[i0], &B = sv[i1], &Cc = sv[i2];
                if (!std::isfinite(A.sx + A.sy + B.sx + B.sy + Cc.sx + Cc.sy)) continue;
                rasterTri(A, B, Cc, mi, tex, !p.hasN);
            }
        });

        // Réduction (anti-aliasing) -> BGRA prémultiplié.
        out.assign((size_t)size * size, 0);
        int n2 = ss * ss;
        bool any = false;
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++) {
                unsigned sr = 0, sg = 0, sb = 0, cov = 0;
                for (int j = 0; j < ss; j++)
                    for (int i = 0; i < ss; i++) {
                        uint32_t c = cbuf[(size_t)(y * ss + j) * S + (x * ss + i)];
                        if (c >> 24) { sr += (c >> 16) & 255; sg += (c >> 8) & 255; sb += c & 255; cov++; }
                    }
                if (!cov) continue;
                any = true;
                out[(size_t)y * size + x] = ((cov * 255 / n2) << 24) | ((sr / n2) << 16) | ((sg / n2) << 8) | (sb / n2);
            }
        return any;
    }
};

// API simple. Sortie : size*size pixels BGRA (0xAARRGGBB) PRÉMULTIPLIÉS.
inline bool RenderGlbThumbnail(const uint8_t* data, size_t len, int size, TexDecoder dec, std::vector<uint32_t>& out) {
    Renderer r;
    r.dec = std::move(dec);
    return r.render(data, len, size, out);
}

}  // namespace glbr
