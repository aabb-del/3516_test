// 兼容 SDL_ttf 版本检查宏
#ifndef SDL_TTF_VERSION_ATLEAST
#ifdef TTF_MAJOR_VERSION
#define SDL_TTF_VERSION_ATLEAST(major, minor, patch) \
    ((TTF_MAJOR_VERSION > (major)) || \
     (TTF_MAJOR_VERSION == (major) && TTF_MINOR_VERSION > (minor)) || \
     (TTF_MAJOR_VERSION == (major) && TTF_MINOR_VERSION == (minor) && TTF_PATCHLEVEL >= (patch)))
#else
#define SDL_TTF_VERSION_ATLEAST(major, minor, patch) 0
#endif
#endif

#include "osd_font.h"

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>
#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstring>
#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "SourceHanSerifCN-Regular-1-subset.h"

namespace hisi {
namespace osd {

// ============================================================
// ★ SDL_ttf / FreeType 全局锁
//
// 旧版 SDL_ttf（< 2.0.14）内部有全局状态（FreeType library、
// 字形缓存等），多个线程同时调用 TTF_* 函数会破坏内部数据，
// 导致 SIGSEGV（跳到垃圾地址）。
//
// 所有 TTF_* 调用都必须通过此锁串行化。
// ============================================================
static std::mutex g_ttfMutex;

class TtfLock {
public:
    TtfLock() : lock_(g_ttfMutex) {}
private:
    std::lock_guard<std::mutex> lock_;
};

// ============================================================
// 1) 内部辅助函数
// ============================================================
static int getBytesPerPixel(PixelFormat fmt) {
    return (fmt == PixelFormat::ARGB8888) ? 4 : 2;
}

static void convertToTarget(const uint8_t* src, uint8_t* dst, int pixels,
                            PixelFormat fmt, uint8_t colorAlpha,
                            uint16_t bgColor16 = 0,
                            uint32_t bgColor32 = 0)
{
    for (int i = 0; i < pixels; ++i) {
        uint8_t r = src[0], g = src[1], b = src[2];
        uint8_t glyphA = src[3];
        uint8_t finalA = (glyphA * colorAlpha) / 255;

        if (finalA == 0) {
            if (fmt == PixelFormat::ARGB8888) {
                *reinterpret_cast<uint32_t*>(dst + i * 4) = bgColor32;
            } else {
                *reinterpret_cast<uint16_t*>(dst + i * 2) = bgColor16;
            }
        } else {
            if (fmt == PixelFormat::RGB565) {
                uint16_t v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
                *reinterpret_cast<uint16_t*>(dst + i * 2) = v;
            } else if (fmt == PixelFormat::ARGB1555) {
                uint16_t v = ((finalA > 128 ? 1 : 0) << 15) |
                             ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
                *reinterpret_cast<uint16_t*>(dst + i * 2) = v;
            } else { // ARGB8888
                dst[i * 4 + 0] = r;
                dst[i * 4 + 1] = g;
                dst[i * 4 + 2] = b;
                dst[i * 4 + 3] = finalA;
            }
        }
        src += 4;
    }
}

static std::u32string utf8To32(const std::string& s) {
    std::u32string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ) {
        uint32_t cp = 0;
        unsigned char c = s[i];
        if ((c & 0x80) == 0) {
            cp = c; i += 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            cp = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F); i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
            cp = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F); i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
            cp = ((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) |
                 ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F); i += 4;
        } else {
            i += 1;
        }
        out.push_back(cp);
    }
    return out;
}

static void scanDirectoryRecursive(const std::string& dir,
                                   std::function<void(const std::string&)> callback)
{
    DIR* dp = opendir(dir.c_str());
    if (!dp) return;

    struct dirent* entry;
    while ((entry = readdir(dp)) != nullptr) {
        if (entry->d_name[0] == '.' &&
            (entry->d_name[1] == '\0' ||
             (entry->d_name[1] == '.' && entry->d_name[2] == '\0')))
            continue;

        std::string fullPath = dir;
        if (fullPath.back() != '/') fullPath += '/';
        fullPath += entry->d_name;

        struct stat st;
        if (stat(fullPath.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                scanDirectoryRecursive(fullPath, callback);
            } else {
                callback(fullPath);
            }
        }
    }
    closedir(dp);
}

// ============================================================
// 2) InternalFontManager
// ============================================================
class InternalFontManager {
public:
    static InternalFontManager& instance() {
        static InternalFontManager inst;
        return inst;
    }

    bool isFamilyAvailable(const std::string& family) const {
        ensureScanned();
        std::lock_guard<std::mutex> lock(mutex_);
        return fontSources_.find(family) != fontSources_.end();
    }

    const std::vector<std::string>& getAvailableFonts() const {
        ensureScanned();
        std::lock_guard<std::mutex> lock(mutex_);
        return allFonts_;
    }

    // ★ 所有 TTF_* 调用都在 TtfLock 保护下
    TTF_Font* openFont(const std::string& family, int size, int style) {
        ensureScanned();

        FontSource src;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = fontSources_.find(family);
            if (it == fontSources_.end()) return nullptr;
            src = it->second;
        }

        TtfLock ttfLock;

        TTF_Font* font = nullptr;
        if (src.type == FontSource::External) {
            font = TTF_OpenFont(src.path.c_str(), size);
        } else if (src.type == FontSource::Builtin) {
            SDL_RWops* rw = SDL_RWFromConstMem(src.data, src.dataSize);
            if (rw) font = TTF_OpenFontRW(rw, 1, size);
        }
        if (font) TTF_SetFontStyle(font, style);
        return font;
    }

private:
    InternalFontManager() {
        {
            TtfLock ttfLock;
            if (TTF_WasInit() == 0 && TTF_Init() == -1) {
                std::cerr << "TTF_Init failed: " << TTF_GetError() << std::endl;
            }
        }

        scanPaths_ = {
            "/usr/share/fonts/",
            "/usr/share/fonts/truetype/",
            "/usr/share/fonts/opentype/",
            "/mnt/TF/fonts/",
            "./fonts/"
        };
        registerBuiltin("Builtin",
                        SourceHanSerifCN_Regular_1_subset_otf,
                        SourceHanSerifCN_Regular_1_subset_otf_len);

        // ★ 直接同步扫描，不用 ensureScanned
        scan();   // scan 内部会 print "Registered font"
    }

    void registerBuiltin(const std::string& name,
                         const unsigned char* data, size_t size)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        FontSource src;
        src.type     = FontSource::Builtin;
        src.data     = data;
        src.dataSize = size;
        fontSources_[name] = src;
        allFonts_.push_back(name);
    }

    void ensureScanned() const {
    }

    void scan() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (scanned_.load(std::memory_order_relaxed)) return;
        }

        // 收集候选文件（不加锁）
        std::vector<std::string> candidates;
        std::unordered_set<std::string> added;
        auto testFile = [&](const std::string& filePath) {
            std::string ext;
            size_t dot = filePath.rfind('.');
            if (dot != std::string::npos) ext = filePath.substr(dot);
            if (ext != ".ttf" && ext != ".otf" && ext != ".ttc") return;

            size_t slash = filePath.rfind('/');
            std::string base = (slash == std::string::npos)
                                   ? filePath : filePath.substr(slash + 1);
            std::string family = base.substr(0, base.rfind('.'));
            if (added.count(family)) return;
            added.insert(family);
            candidates.push_back(filePath);
        };

        for (const auto& path : scanPaths_) {
            struct stat st;
            if (stat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;
            scanDirectoryRecursive(path, testFile);
        }

        // 测试每个候选（用 TtfLock 保护 TTF_OpenFont）
        for (const auto& filePath : candidates) {
            bool ok = false;
            {
                TtfLock ttfLock;
                TTF_Font* font = TTF_OpenFont(filePath.c_str(), 16);
                if (font) {
                    TTF_CloseFont(font);
                    ok = true;
                }
            }
            if (!ok) continue;

            size_t slash = filePath.rfind('/');
            std::string base = (slash == std::string::npos)
                                   ? filePath : filePath.substr(slash + 1);
            std::string family = base.substr(0, base.rfind('.'));

            std::lock_guard<std::mutex> lk(mutex_);
            FontSource src;
            src.type = FontSource::External;
            src.path = filePath;
            fontSources_[family] = src;
            externalFonts_.push_back(family);
            allFonts_.push_back(family);
        }

        {
            std::lock_guard<std::mutex> lk(mutex_);
            for (const auto& pair : fontSources_) {
                std::cout << "Registered font: " << pair.first << std::endl;
            }
            scanned_.store(true, std::memory_order_release);
        }
    }

    struct FontSource {
        enum Type { External, Builtin };
        Type type = External;
        std::string path;
        const unsigned char* data = nullptr;
        size_t dataSize = 0;
    };

    mutable std::mutex          mutex_;
    mutable std::atomic<bool>   scanned_{false};
    std::vector<std::string>    scanPaths_;
    std::unordered_map<std::string, FontSource> fontSources_;
    std::vector<std::string>    externalFonts_;
    std::vector<std::string>    builtinFonts_;
    std::vector<std::string>    allFonts_;
};

// ============================================================
// 3) CachedGlyph / GlyphCache
//    - getGlyph 返回 shared_ptr，锁外安全
//    - 所有 TTF_* 调用在 TtfLock 保护下
// ============================================================
struct CachedGlyph {
    std::vector<uint8_t> bitmap;
    int width     = 0;
    int height    = 0;
    int pitch     = 0;
    int bearingX  = 0;
    int topOffset = 0;
    int advanceX  = 0;
};

class GlyphCache {
public:
    GlyphCache(const std::string& family, int size, int style,
               PixelFormat format, uint32_t color)
        : family_(family), size_(size), style_(style),
          format_(format), color_(color)
    {
        bpp_ = getBytesPerPixel(format);

        {
            TtfLock ttfLock;
            if (TTF_WasInit() == 0 && TTF_Init() == -1) {
                std::cerr << "TTF_Init failed" << std::endl;
            }
        }

        font_ = InternalFontManager::instance().openFont(family, size, style);
        if (!font_) {
            std::cerr << "Failed to open font: " << family << std::endl;
            return;
        }

        {
            TtfLock ttfLock;
            fontHeight_ = TTF_FontHeight(font_);
            ascent_     = TTF_FontAscent(font_);
            descent_    = TTF_FontDescent(font_);
        }

        cache_.reserve(300);
    }

    ~GlyphCache() {
        if (font_) {
            TtfLock ttfLock;
            TTF_CloseFont(font_);
            font_ = nullptr;
        }
    }

    GlyphCache(const GlyphCache&)            = delete;
    GlyphCache& operator=(const GlyphCache&) = delete;

    // ★ 返回 shared_ptr，锁外使用安全
    std::shared_ptr<CachedGlyph> getGlyph(uint32_t ch) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = cache_.find(ch);
        if (it != cache_.end()) {
            if (it->second.iter != lruList_.begin()) {
                lruList_.splice(lruList_.begin(), lruList_, it->second.iter);
            }
            return it->second.glyph;
        }

        auto g = std::make_shared<CachedGlyph>(renderGlyph(ch));

        while (cache_.size() >= 256) {
            uint32_t old = lruList_.back();
            lruList_.pop_back();
            cache_.erase(old);
        }

        lruList_.push_front(ch);
        auto iter = lruList_.begin();
        cache_[ch] = GlyphEntry{ g, iter };
        return g;
    }

    int bytesPerPixel() const { return bpp_; }
    int fontHeight()    const { return fontHeight_; }
    int ascent()        const { return ascent_; }
    int descent()       const { return descent_; }
    int lineSkip()      const { return fontHeight_; }

private:
    struct GlyphEntry {
        std::shared_ptr<CachedGlyph> glyph;
        std::list<uint32_t>::iterator iter;
    };

    CachedGlyph renderGlyph(uint32_t ch) {
        CachedGlyph g;
        if (!font_) return g;

        // ★ 所有 TTF_* 调用在全局锁里
        TtfLock ttfLock;

        int minx, maxx, miny, maxy, advance;
        bool metricsOk = false;
#if SDL_TTF_VERSION_ATLEAST(2,0,18)
        metricsOk = (TTF_GlyphMetrics32(font_, ch,
                                        &minx, &maxx, &miny, &maxy, &advance) == 0);
#else
        if (ch <= 0xFFFF) {
            metricsOk = (TTF_GlyphMetrics(font_, ch,
                                          &minx, &maxx, &miny, &maxy, &advance) == 0);
        }
#endif
        if (!metricsOk) {
            g.bearingX  = 0;
            g.topOffset = fontHeight_ / 2;
            g.advanceX  = fontHeight_ / 2;
        } else {
            g.bearingX  = minx;
            g.topOffset = maxy;
            g.advanceX  = advance;
        }

        uint8_t colorR = (color_ >> 16) & 0xFF;
        uint8_t colorG = (color_ >> 8)  & 0xFF;
        uint8_t colorB = (color_)       & 0xFF;
        uint8_t colorA = (color_ >> 24) & 0xFF;

        SDL_Color col = { colorR, colorG, colorB, 0 };
        SDL_Surface* surf = TTF_RenderGlyph_Blended(font_, ch, col);
        if (!surf) return g;

        g.width  = surf->w;
        g.height = surf->h;
        g.pitch  = g.width * bpp_;
        g.bitmap.resize(g.height * g.pitch);

        const uint8_t* srcRow = (const uint8_t*)surf->pixels;
        uint8_t* dstRow = g.bitmap.data();
        for (int y = 0; y < g.height; ++y) {
            convertToTarget(srcRow, dstRow, g.width, format_, colorA,
                            0x0000, 0x00000000);
            srcRow += surf->pitch;
            dstRow += g.width * bpp_;
        }
        SDL_FreeSurface(surf);
        return g;
    }

    std::string family_;
    int size_, style_;
    PixelFormat format_;
    uint32_t color_;
    int bpp_, fontHeight_ = 0;
    int ascent_ = 0, descent_ = 0;
    TTF_Font* font_ = nullptr;
    std::mutex mutex_;
    std::list<uint32_t> lruList_;
    std::unordered_map<uint32_t, GlyphEntry> cache_;
};

// ============================================================
// 4) FontKey / FontKeyHash
// ============================================================
struct FontKey {
    std::string family;
    int         size   = 0;
    int         style  = 0;
    PixelFormat format = PixelFormat::ARGB1555;
    uint32_t    color  = 0;

    bool operator==(const FontKey& o) const {
        return family == o.family && size == o.size && style == o.style
            && format == o.format && color == o.color;
    }
};

struct FontKeyHash {
    std::size_t operator()(const FontKey& k) const {
        std::size_t h = std::hash<std::string>()(k.family);
        h ^= std::hash<int>()(k.size)              + 0x9e3779b9;
        h ^= std::hash<int>()(k.style)             + 0x9e3779b9;
        h ^= std::hash<int>()((int)k.format)       + 0x9e3779b9;
        h ^= std::hash<uint32_t>()(k.color)        + 0x9e3779b9;
        return h;
    }
};

// ============================================================
// 5) OsdFont::Private
// ============================================================
struct OsdFont::Private {
    std::string family;
    int         size   = 0;
    int         style  = 0;
    PixelFormat format = PixelFormat::ARGB1555;
    uint32_t    color  = 0xFFFFFFFF;
};

// ============================================================
// 6) OsdFont 构造 / 析构 / setter / getter
// ============================================================
OsdFont::OsdFont(const std::string& family, int pointSize, int style,
                 PixelFormat format, uint32_t colorARGB)
    : d(std::make_shared<Private>())
{
    std::string actualFamily = family;
    auto& mgr = InternalFontManager::instance();
    if (!mgr.isFamilyAvailable(actualFamily) && actualFamily != "Builtin") {
        const auto& available = mgr.getAvailableFonts();
        if (!available.empty()) {
            actualFamily = available[0];
            std::cerr << "Warning: Font family '" << family
                      << "' not found, fallback to '" << actualFamily << "'"
                      << std::endl;
        } else {
            actualFamily = "Builtin";
            std::cerr << "Warning: Font family '" << family
                      << "' not found, fallback to builtin font" << std::endl;
        }
    }
    d->family = actualFamily;
    d->size   = pointSize;
    d->style  = style;
    d->format = format;
    d->color  = colorARGB;
}

OsdFont::~OsdFont() = default;

void OsdFont::setFamily(const std::string& family) {
    std::string actualFamily = family;
    auto& mgr = InternalFontManager::instance();
    if (!mgr.isFamilyAvailable(actualFamily) && actualFamily != "Builtin") {
        const auto& available = mgr.getAvailableFonts();
        if (!available.empty()) actualFamily = available[0];
        else                    actualFamily = "Builtin";
    }
    d->family = actualFamily;
}

void OsdFont::setPointSize(int size)   { d->size   = size; }
void OsdFont::setStyle(int style)      { d->style  = style; }
void OsdFont::setPixelFormat(PixelFormat fmt) { d->format = fmt; }
void OsdFont::setColor(uint32_t argb)  { d->color  = argb; }

std::string OsdFont::family() const       { return d->family; }
int         OsdFont::pointSize() const    { return d->size;   }
int         OsdFont::style() const        { return d->style;  }
PixelFormat OsdFont::pixelFormat() const  { return d->format; }
uint32_t    OsdFont::color() const        { return d->color;  }

bool OsdFont::operator<(const OsdFont& other) const {
    if (d->family != other.d->family) return d->family < other.d->family;
    if (d->size   != other.d->size)   return d->size   < other.d->size;
    if (d->style  != other.d->style)  return d->style  < other.d->style;
    if (d->format != other.d->format) return d->format < other.d->format;
    return d->color < other.d->color;
}

// ============================================================
// 7) OsdPainter::Private
// ============================================================
struct OsdPainter::Private {
    std::unordered_map<FontKey, std::shared_ptr<GlyphCache>, FontKeyHash> caches;
    std::mutex mutex;

    std::shared_ptr<GlyphCache> getOrCreate(const OsdFont& font) {
        FontKey key {
            font.d->family,
            font.d->size,
            font.d->style,
            font.d->format,
            font.d->color
        };

        std::lock_guard<std::mutex> lock(mutex);
        auto it = caches.find(key);
        if (it != caches.end()) return it->second;

        auto cache = std::make_shared<GlyphCache>(
            font.d->family, font.d->size, font.d->style,
            font.d->format, font.d->color);
        caches[key] = cache;
        return cache;
    }
};

// ============================================================
// 8) OsdPainter 构造 / 单例 / 静态接口
// ============================================================
OsdPainter::OsdPainter() {
    d = std::make_unique<Private>();
}

OsdPainter::~OsdPainter() = default;

OsdPainter& OsdPainter::getInstance() {
    // ★ C++11 magic static：线程安全，构造完成才返回
    static OsdPainter instance;
    return instance;
}

static void blitClipped(uint8_t* canvas, uint32_t stride,
                        int canvasW, int canvasH,
                        int dstX, int dstY,
                        const uint8_t* src, int srcW, int srcH,
                        int srcPitch, int bpp)
{
    if (dstX >= canvasW || dstY >= canvasH ||
        dstX + srcW <= 0 || dstY + srcH <= 0)
        return;

    int copyX = 0, copyY = 0;
    int copyW = srcW, copyH = srcH;
    if (dstX < 0) { copyX = -dstX; copyW -= copyX; dstX = 0; }
    if (dstY < 0) { copyY = -dstY; copyH -= copyY; dstY = 0; }
    if (dstX + copyW > canvasW) copyW = canvasW - dstX;
    if (dstY + copyH > canvasH) copyH = canvasH - dstY;
    if (copyW <= 0 || copyH <= 0) return;

    const uint8_t* srcLine = src + copyY * srcPitch + copyX * bpp;
    uint8_t* dstLine = canvas + dstY * stride + dstX * bpp;
    for (int i = 0; i < copyH; ++i) {
        memcpy(dstLine, srcLine, copyW * bpp);
        srcLine += srcPitch;
        dstLine += stride;
    }
}

void OsdPainter::drawText(void* canvasAddr, uint32_t stride,
                          int canvasW, int canvasH,
                          const std::string& text,
                          int x, int y,
                          const OsdFont& font,
                          int maxWidth,
                          const std::string& placeholder)
{
    auto cache = getInstance().d->getOrCreate(font);
    if (!cache || cache->fontHeight() == 0) return;

    std::u32string ph32 = utf8To32(placeholder.empty() ? "?" : placeholder);
    std::shared_ptr<CachedGlyph> placeholdGlyph;
    if (!ph32.empty()) placeholdGlyph = cache->getGlyph(ph32[0]);

    std::u32string u32 = utf8To32(text);
    int curX = x, curY = y;
    int fontH = cache->fontHeight();
    int bpp   = cache->bytesPerPixel();

    for (uint32_t ch : u32) {
        if (ch == '\n') {
            curX = x;
            curY += fontH + 2;
            continue;
        }

        std::shared_ptr<CachedGlyph> g = cache->getGlyph(ch);
        if (!g && placeholdGlyph) g = placeholdGlyph;
        if (!g) continue;

        int adv = g->advanceX;
        if (maxWidth > 0 && curX + adv > x + maxWidth) {
            curX = x;
            curY += fontH + 2;
        }

        if (g->width > 0 && g->height > 0) {
            int blitX = curX + g->bearingX;
            int blitY = curY - g->topOffset;
            blitClipped((uint8_t*)canvasAddr, stride, canvasW, canvasH,
                        blitX, blitY,
                        g->bitmap.data(), g->width, g->height, g->pitch, bpp);
        }
        curX += adv;
    }
}

int OsdPainter::textWidth(const std::string& text, const OsdFont& font) {
    auto cache = getInstance().d->getOrCreate(font);
    if (!cache) return 0;

    std::u32string u32 = utf8To32(text);
    int total = 0;
    for (uint32_t ch : u32) {
        if (ch == '\n') continue;
        std::shared_ptr<CachedGlyph> g = cache->getGlyph(ch);
        if (g) total += g->advanceX;
        else   total += cache->fontHeight() / 2;
    }
    return total;
}

void OsdPainter::clearCache() {
    auto& painter = getInstance();
    std::lock_guard<std::mutex> lock(painter.d->mutex);
    painter.d->caches.clear();
}

// ============================================================
// 9) 度量桥接方法
// ============================================================
int OsdPainter::fontAscent(const OsdFont& font) {
    auto cache = getInstance().d->getOrCreate(font);
    return cache ? cache->ascent() : 0;
}

int OsdPainter::fontDescent(const OsdFont& font) {
    auto cache = getInstance().d->getOrCreate(font);
    return cache ? cache->descent() : 0;
}

int OsdPainter::fontHeight(const OsdFont& font) {
    auto cache = getInstance().d->getOrCreate(font);
    return cache ? cache->fontHeight() : 0;
}

int OsdPainter::fontLineSkip(const OsdFont& font) {
    return fontHeight(font);
}

// ============================================================
// 10) OsdFont 度量实现
// ============================================================
int OsdFont::height()  const { return OsdPainter::fontHeight(*this);  }
int OsdFont::ascent()  const { return OsdPainter::fontAscent(*this);  }
int OsdFont::descent() const { return OsdPainter::fontDescent(*this); }
int OsdFont::lineSkip()const { return OsdPainter::fontLineSkip(*this); }

} // namespace osd
} // namespace hisi