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
#include <unordered_map>
#include <unordered_set>
#include <list>
#include <mutex>
#include <vector>
#include <functional>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <map>
#include <tuple>

// 内置字体头文件
#include "SourceHanSerifCN-Regular-1-subset.h"

namespace hisi {
namespace osd {

// ========== 内部辅助函数 ==========
static int getBytesPerPixel(PixelFormat fmt) {
    return (fmt == PixelFormat::ARGB8888) ? 4 : 2;
}

// 颜色 alpha 调制：将字形 Alpha 与颜色 Alpha 相乘，归一化到 [0,255]
// 新增参数 bgColor16（用于 RGB565/ARGB1555）或 bgColor32（用于 ARGB8888）
static void convertToTarget(const uint8_t* src, uint8_t* dst, int pixels,
                            PixelFormat fmt, uint8_t colorAlpha,
                            uint16_t bgColor16 = 0, uint32_t bgColor32 = 0) {
    for (int i = 0; i < pixels; ++i) {
        uint8_t r = src[0], g = src[1], b = src[2];
        uint8_t glyphA = src[3];
        uint8_t finalA = (glyphA * colorAlpha) / 255;

        // 如果字形完全透明，直接用背景色，跳过颜色混合
        if (finalA == 0) {
            if (fmt == PixelFormat::ARGB8888) {
                *reinterpret_cast<uint32_t*>(dst + i*4) = bgColor32;
            } else {
                *reinterpret_cast<uint16_t*>(dst + i*2) = bgColor16;
            }
        } else {
            // 不透明或半透明，按正常逻辑写入
            if (fmt == PixelFormat::RGB565) {
                // 没有 alpha 通道，必须用背景色混合（或直接当前景）
                // 这里简单处理：直接覆盖颜色（无半透明）
                uint16_t v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
                *reinterpret_cast<uint16_t*>(dst + i*2) = v;
            } else if (fmt == PixelFormat::ARGB1555) {
                uint16_t v = ((finalA > 128 ? 1 : 0) << 15) |
                             ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
                *reinterpret_cast<uint16_t*>(dst + i*2) = v;
            } else { // ARGB8888
                dst[i*4+0] = r;
                dst[i*4+1] = g;
                dst[i*4+2] = b;
                dst[i*4+3] = finalA;
            }
        }
        src += 4;
    }
}




static std::u32string utf8To32(const std::string& s) {
    std::u32string out;
    for (size_t i = 0; i < s.size(); ) {
        uint32_t cp = 0;
        unsigned char c = s[i];
        if ((c & 0x80) == 0) {
            cp = c; i += 1;
        } else if ((c & 0xE0) == 0xC0 && i+1 < s.size()) {
            cp = ((c & 0x1F) << 6) | (s[i+1] & 0x3F); i += 2;
        } else if ((c & 0xF0) == 0xE0 && i+2 < s.size()) {
            cp = ((c & 0x0F) << 12) | ((s[i+1] & 0x3F) << 6) | (s[i+2] & 0x3F); i += 3;
        } else if ((c & 0xF8) == 0xF0 && i+3 < s.size()) {
            cp = ((c & 0x07) << 18) | ((s[i+1] & 0x3F) << 12) |
                 ((s[i+2] & 0x3F) << 6) | (s[i+3] & 0x3F); i += 4;
        } else { i += 1; }
        out.push_back(cp);
    }
    return out;
}

static void scanDirectoryRecursive(const std::string& dir, std::function<void(const std::string&)> callback) {
    DIR* dp = opendir(dir.c_str());
    if (!dp) return;
    struct dirent* entry;
    while ((entry = readdir(dp)) != nullptr) {
        if (entry->d_name[0] == '.' && (entry->d_name[1] == '\0' ||
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

// ========== 内部字体管理器 ==========
class InternalFontManager {
public:
    static InternalFontManager& instance() {
        static InternalFontManager inst;
        return inst;
    }

    bool isFamilyAvailable(const std::string& family) const {
        auto fonts = getAvailableFonts();
        return std::find(fonts.begin(), fonts.end(), family) != fonts.end();
    }

    std::vector<std::string> getAvailableFonts() const {
        const_cast<InternalFontManager*>(this)->scan();
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> res;
        res.reserve(externalFonts_.size() + builtinFonts_.size());
        res.insert(res.end(), externalFonts_.begin(), externalFonts_.end());
        res.insert(res.end(), builtinFonts_.begin(), builtinFonts_.end());
        return res;
    }

    TTF_Font* openFont(const std::string& family, int size, int style) {
        scan();
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = fontSources_.find(family);
        if (it == fontSources_.end()) return nullptr;
        const auto& src = it->second;
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
        if (TTF_WasInit() == 0 && TTF_Init() == -1) {
            std::cerr << "TTF_Init failed: " << TTF_GetError() << std::endl;
        }
        scanPaths_ = {
            "/usr/share/fonts/",
            "/usr/share/fonts/truetype/",
            "/usr/share/fonts/opentype/",
            "/mnt/TF/fonts/",
            "./fonts/"
        };
        // 自动注册内置字体
        registerBuiltin("Builtin", SourceHanSerifCN_Regular_1_subset_otf,
                        SourceHanSerifCN_Regular_1_subset_otf_len);
    }

    void registerBuiltin(const std::string& name, const unsigned char* data, size_t size) {
        std::lock_guard<std::mutex> lock(mutex_);
        FontSource src;
        src.type = FontSource::Builtin;
        src.data = data;
        src.dataSize = size;
        fontSources_[name] = src;
        builtinFonts_.push_back(name);
    }

    void scan() {
        if (scanned_) return;
        std::lock_guard<std::mutex> lock(mutex_);
        if (scanned_) return;

        std::unordered_set<std::string> added;
        auto testFile = [&](const std::string& filePath) {
            std::string ext;
            size_t dot = filePath.rfind('.');
            if (dot != std::string::npos) ext = filePath.substr(dot);
            if (ext != ".ttf" && ext != ".otf" && ext != ".ttc") return;
            size_t slash = filePath.rfind('/');
            std::string base = (slash == std::string::npos) ? filePath : filePath.substr(slash+1);
            std::string family = base.substr(0, base.rfind('.'));

            if (added.find(family) != added.end()) return;
            TTF_Font* font = TTF_OpenFont(filePath.c_str(), 16);
            if (font) {
                TTF_CloseFont(font);
                FontSource src;
                src.type = FontSource::External;
                src.path = filePath;
                fontSources_[family] = src;
                externalFonts_.push_back(family);
                added.insert(family);
            }
        };

        for (const auto& path : scanPaths_) {
            struct stat st;
            if (stat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;
            scanDirectoryRecursive(path, testFile);
        }
        for (const auto& pair : fontSources_) {
            std::cout << "Registered font: " << pair.first << std::endl;
        }
        scanned_ = true;
    }

    struct FontSource {
        enum Type { External, Builtin };
        Type type;
        std::string path;
        const unsigned char* data = nullptr;
        size_t dataSize = 0;
    };
    mutable std::mutex mutex_;
    bool scanned_ = false;
    std::vector<std::string> scanPaths_;
    std::unordered_map<std::string, FontSource> fontSources_;
    std::vector<std::string> externalFonts_;
    std::vector<std::string> builtinFonts_;
};

// ========== 字形缓存（扩展 bearing 和 advance） ==========
struct CachedGlyph {
    std::vector<uint8_t> bitmap;   // 像素数据，按目标像素格式存储
    int width = 0;                 // 位图宽度
    int height = 0;                // 位图高度
    int pitch = 0;                 // 每行字节数
    int bearingX = 0;              // 水平偏移（相对原点）
    int topOffset = 0;             // 从基线到字形顶部的偏移（正值向上）
    int advanceX = 0;              // 水平步进宽度
};

class GlyphCache {
public:
    GlyphCache(const std::string& family, int size, int style, PixelFormat format, uint32_t color)
        : family_(family), size_(size), style_(style), format_(format), color_(color) {
        bpp_ = getBytesPerPixel(format);
        if (TTF_WasInit() == 0 && TTF_Init() == -1) {
            std::cerr << "TTF_Init failed" << std::endl;
        }
        font_ = InternalFontManager::instance().openFont(family, size, style);
        if (!font_) {
            std::cerr << "Failed to open font: " << family << std::endl;
            return;
        }
        fontHeight_ = TTF_FontHeight(font_);
    }
    ~GlyphCache() {
        if (font_) TTF_CloseFont(font_);
    }

    const CachedGlyph* getGlyph(uint32_t ch) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(ch);
        if (it != cache_.end()) {
            lruList_.splice(lruList_.begin(), lruList_, it->second.second);
            return &it->second.first;
        }
        CachedGlyph g = renderGlyph(ch);
        // 即使位图为空（如空格）也缓存，以保留 advanceX 等度量
        while (cache_.size() >= 256) {
            uint32_t old = lruList_.back();
            lruList_.pop_back();
            cache_.erase(old);
        }
        lruList_.push_front(ch);
        cache_[ch] = std::make_pair(std::move(g), lruList_.begin());
        return &cache_[ch].first;
    }

    int bytesPerPixel() const { return bpp_; }

    int fontHeight() const { return fontHeight_; }  // 与 TTF_FontHeight 相同
    int ascent() const { return font_ ? TTF_FontAscent(font_) : 0; }
    int descent() const { return font_ ? TTF_FontDescent(font_) : 0; }
    int lineSkip() const { return fontHeight_; }    // 保持语义清晰

private:
    CachedGlyph renderGlyph(uint32_t ch) {
        CachedGlyph g;
        if (!font_) return g;

        // 获取字形度量（支持 32 位 Unicode，优先使用 32 位版本）
        int minx, maxx, miny, maxy, advance;
        bool metricsOk = false;
#if SDL_TTF_VERSION_ATLEAST(2,0,18)
        metricsOk = (TTF_GlyphMetrics32(font_, ch, &minx, &maxx, &miny, &maxy, &advance) == 0);
#else
        if (ch <= 0xFFFF) {
            metricsOk = (TTF_GlyphMetrics(font_, ch, &minx, &maxx, &miny, &maxy, &advance) == 0);
        }
#endif
        if (!metricsOk) {
            // 度量失败，用简单回退（宽度假定为字体高度的一半，偏移为 0）
            g.bearingX = 0;
            g.topOffset = fontHeight_ / 2;   // 粗略顶部偏移
            g.advanceX = fontHeight_ / 2;
        } else {
            g.bearingX = minx;
            g.topOffset = maxy;               // maxy 是从基线向上的距离
            g.advanceX = advance;
        }

        // 渲染字形像素
        uint8_t colorR = (color_ >> 16) & 0xFF;
        uint8_t colorG = (color_ >> 8) & 0xFF;
        uint8_t colorB = color_ & 0xFF;
        uint8_t colorA = (color_ >> 24) & 0xFF;  // 全局颜色 Alpha
        SDL_Color col = { colorR, colorG, colorB, 0 };  // Alpha 在之后调制
        SDL_Surface* surf = TTF_RenderGlyph_Blended(font_, ch, col);
        if (!surf) return g;

        g.width = surf->w;
        g.height = surf->h;
        g.pitch = g.width * bpp_;
        g.bitmap.resize(g.height * g.pitch);

        const uint8_t* srcRow = (const uint8_t*)surf->pixels;
        uint8_t* dstRow = g.bitmap.data();
        for (int y = 0; y < g.height; ++y) {
            convertToTarget(srcRow, dstRow, g.width, format_, colorA, 0x0000, 0x00000000);
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
    TTF_Font* font_ = nullptr;
    std::mutex mutex_;
    std::list<uint32_t> lruList_;
    std::unordered_map<uint32_t, std::pair<CachedGlyph, std::list<uint32_t>::iterator>> cache_;
};

// ========== OsdFont 实现 ==========
struct OsdFont::Private {
    std::string family;
    int size;
    int style;
    PixelFormat format;
    uint32_t color;
    // 不再缓存 GlyphCache，交给 OsdPainter 统一管理
};

OsdFont::OsdFont(const std::string& family, int pointSize, int style,
                 PixelFormat format, uint32_t colorARGB)
    : d(std::make_shared<Private>()) {
    std::string actualFamily = family;
    auto& mgr = InternalFontManager::instance();
    if (!mgr.isFamilyAvailable(actualFamily) && actualFamily != "Builtin") {
        auto available = mgr.getAvailableFonts();
        if (!available.empty()) {
            actualFamily = available[0];
            std::cerr << "Warning: Font family '" << family << "' not found, fallback to '" << actualFamily << "'" << std::endl;
        } else {
            actualFamily = "Builtin";
            std::cerr << "Warning: Font family '" << family << "' not found, fallback to builtin font" << std::endl;
        }
    }
    d->family = actualFamily;
    d->size = pointSize;
    d->style = style;
    d->format = format;
    d->color = colorARGB;
}

OsdFont::~OsdFont() = default;

void OsdFont::setFamily(const std::string& family) {
    std::string actualFamily = family;
    auto& mgr = InternalFontManager::instance();
    if (!mgr.isFamilyAvailable(actualFamily) && actualFamily != "Builtin") {
        auto available = mgr.getAvailableFonts();
        if (!available.empty()) {
            actualFamily = available[0];
        } else {
            actualFamily = "Builtin";
        }
    }
    d->family = actualFamily;
}

std::string OsdFont::family() const { return d->family; }
void OsdFont::setPointSize(int size) { d->size = size; }
int OsdFont::pointSize() const { return d->size; }
void OsdFont::setStyle(int style) { d->style = style; }
int OsdFont::style() const { return d->style; }
void OsdFont::setPixelFormat(PixelFormat fmt) { d->format = fmt; }
PixelFormat OsdFont::pixelFormat() const { return d->format; }
void OsdFont::setColor(uint32_t argb) { d->color = argb; }
uint32_t OsdFont::color() const { return d->color; }

int OsdFont::height() const {
    // 创建临时缓存获取高度
    auto tempCache = std::make_shared<GlyphCache>(d->family, d->size, d->style, d->format, d->color);
    return tempCache->fontHeight();
}


int OsdFont::ascent() const {
    auto tempCache = std::make_shared<GlyphCache>(d->family, d->size, d->style, d->format, d->color);
    return tempCache->ascent();
}

int OsdFont::descent() const {
    auto tempCache = std::make_shared<GlyphCache>(d->family, d->size, d->style, d->format, d->color);
    return tempCache->descent();
}

int OsdFont::lineSkip() const {
    return height();   // 重用 height()
}





bool OsdFont::operator<(const OsdFont& other) const {
    if (d->family != other.d->family) return d->family < other.d->family;
    if (d->size != other.d->size) return d->size < other.d->size;
    if (d->style != other.d->style) return d->style < other.d->style;
    if (d->format != other.d->format) return d->format < other.d->format;
    return d->color < other.d->color;
}

// ========== OsdPainter 实现 ==========
// 使用属性元组作为缓存键，彻底解决属性修改后缓存不变的问题
using FontKey = std::tuple<std::string, int, int, PixelFormat, uint32_t>;

struct OsdPainter::Private {
    std::map<FontKey, std::shared_ptr<GlyphCache>> caches;
    std::mutex mutex;
};

OsdPainter& OsdPainter::getInstance() {
    static OsdPainter instance;
    static std::once_flag initFlag;
    std::call_once(initFlag, []() {
        instance.d = std::make_unique<Private>();
    });
    return instance;
}

// 辅助：在画布上安全地绘制一个位图（带裁剪）
static void blitClipped(uint8_t* canvas, uint32_t stride,
                        int canvasW, int canvasH,
                        int dstX, int dstY,
                        const uint8_t* src, int srcW, int srcH, int srcPitch, int bpp) {
    if (dstX >= canvasW || dstY >= canvasH || dstX + srcW <= 0 || dstY + srcH <= 0)
        return;
    // 计算裁剪区域
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
                          const std::string& placeholder) {
    auto& painter = getInstance();
    auto& d = *painter.d;

    // 构造缓存键
    FontKey key = std::make_tuple(font.d->family, font.d->size, font.d->style,
                                  font.d->format, font.d->color);
    std::shared_ptr<GlyphCache> cache;
    {
        std::lock_guard<std::mutex> lock(d.mutex);
        auto it = d.caches.find(key);
        if (it != d.caches.end()) {
            cache = it->second;
        } else {
            cache = std::make_shared<GlyphCache>(font.d->family, font.d->size,
                                                 font.d->style, font.d->format,
                                                 font.d->color);
            d.caches[key] = cache;
        }
    }
    if (!cache || cache->fontHeight() == 0) return;

    // 准备占位符
    std::u32string ph32 = utf8To32(placeholder.empty() ? "?" : placeholder);
    const CachedGlyph* placeholdGlyph = nullptr;
    if (!ph32.empty()) placeholdGlyph = cache->getGlyph(ph32[0]);

    // 逐字符绘制
    std::u32string u32 = utf8To32(text);
    int curX = x, curY = y;          // curY 为基线位置
    int fontH = cache->fontHeight();
    int bpp = cache->bytesPerPixel();

    for (uint32_t ch : u32) {
        if (ch == '\n') {
            curX = x;
            curY += fontH + 2;
            continue;
        }
        // 尝试获取字形，失败时才用占位符
        const CachedGlyph* g = cache->getGlyph(ch);
        if (!g && placeholdGlyph) {
            g = placeholdGlyph;
        }
        if (!g) continue;   // 完全无法显示则跳过

        int adv = g->advanceX;
        if (maxWidth > 0 && curX + adv > x + maxWidth) {
            curX = x;
            curY += fontH + 2;
        }

        // 仅当字形有实际像素时才绘制，否则只步进
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
    auto& painter = getInstance();
    auto& d = *painter.d;

    FontKey key = std::make_tuple(font.d->family, font.d->size, font.d->style,
                                  font.d->format, font.d->color);
    std::shared_ptr<GlyphCache> cache;
    {
        std::lock_guard<std::mutex> lock(d.mutex);
        auto it = d.caches.find(key);
        if (it != d.caches.end()) {
            cache = it->second;
        } else {
            cache = std::make_shared<GlyphCache>(font.d->family, font.d->size,
                                                 font.d->style, font.d->format,
                                                 font.d->color);
            d.caches[key] = cache;
        }
    }
    if (!cache) return 0;

    std::u32string u32 = utf8To32(text);
    int total = 0;
    for (uint32_t ch : u32) {
        if (ch == '\n') continue;
        const CachedGlyph* g = cache->getGlyph(ch);
        if (g) total += g->advanceX;
        else total += cache->fontHeight() / 2;  // 回退宽度
    }
    return total;
}

} // namespace osd
} // namespace hisi