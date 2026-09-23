#ifndef OSD_FONT_H
#define OSD_FONT_H

#include <cstdint>
#include <memory>
#include <string>

namespace hisi {
namespace osd {

enum class PixelFormat {
    RGB565,
    ARGB1555,
    ARGB8888
};

enum FontStyle {
    FontStyleNormal    = 0,
    FontStyleBold      = 1 << 0,
    FontStyleItalic    = 1 << 1,
    FontStyleUnderline = 1 << 2
};

// ============================================================
// OsdFont
//   - 构造只保存属性，不加载字体
//   - 真正的 TTF_OpenFont 由 OsdPainter 按 FontKey 全局缓存
//   - colorARGB 始终是 ARGB8888（0xAARRGGBB）
// ============================================================
class OsdFont {
public:
    OsdFont(const std::string& family = "Builtin",
            int pointSize          = 24,
            int style              = FontStyleNormal,
            PixelFormat format     = PixelFormat::ARGB1555,
            uint32_t    colorARGB  = 0xFFFFFFFF);
    ~OsdFont();

    OsdFont(const OsdFont&)            = default;
    OsdFont& operator=(const OsdFont&) = default;
    OsdFont(OsdFont&&)                 = default;
    OsdFont& operator=(OsdFont&&)      = default;

    // 属性
    void        setFamily(const std::string& family);
    std::string family() const;

    void setPointSize(int size);
    int  pointSize() const;

    void setStyle(int style);
    int  style() const;

    void        setPixelFormat(PixelFormat fmt);
    PixelFormat pixelFormat() const;

    void     setColor(uint32_t argb);
    uint32_t color() const;

    // 度量（走 OsdPainter 全局缓存，不触发 TTF_OpenFont）
    int height()  const;
    int ascent()  const;
    int descent() const;
    int lineSkip() const;

    bool operator<(const OsdFont& other) const;

    struct Private;
    std::shared_ptr<Private> d;
};

// ============================================================
// OsdPainter 单例
// ============================================================
class OsdPainter {
public:
    static OsdPainter& getInstance();

    static void drawText(void* canvasAddr, uint32_t stride,
                         int canvasW, int canvasH,
                         const std::string& text,
                         int x, int y,
                         const OsdFont& font,
                         int maxWidth = 0,
                         const std::string& placeholder = "□");

    static int textWidth(const std::string& text, const OsdFont& font);

    static void clearCache();

    // 度量查询（供 OsdFont 内部使用）
    static int fontAscent (const OsdFont& font);
    static int fontDescent(const OsdFont& font);
    static int fontHeight (const OsdFont& font);
    static int fontLineSkip(const OsdFont& font);

private:
    OsdPainter();
    ~OsdPainter();
    OsdPainter(const OsdPainter&)            = delete;
    OsdPainter& operator=(const OsdPainter&) = delete;

    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace osd
} // namespace hisi

#endif // OSD_FONT_H