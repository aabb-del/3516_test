#ifndef OSD_FONT_H
#define OSD_FONT_H

#include <string>
#include <cstdint>
#include <memory>

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

class OsdFont {
public:
    OsdFont(const std::string& family = "Builtin", 
            int pointSize = 24,
            int style = FontStyleNormal,
            PixelFormat format = PixelFormat::ARGB1555,
            uint32_t colorARGB = 0xFFFFFFFF);
    ~OsdFont();

    void setFamily(const std::string& family);
    std::string family() const;

    void setPointSize(int size);
    int pointSize() const;

    void setStyle(int style);
    int style() const;

    void setPixelFormat(PixelFormat fmt);
    PixelFormat pixelFormat() const;

    void setColor(uint32_t argb);
    uint32_t color() const;

    int height() const;          // 行高（推荐行间距，约等于 ascent - descent + 内部行距）
    int ascent() const;          // 从基线到顶部的像素数（正值）
    int descent() const;         // 从基线到底部的像素数（负值，如返回 -4 表示下降 4 像素）
    int lineSkip() const;        // 等同于 height()，提供更明确的命名（可选）

    bool operator<(const OsdFont& other) const;

    struct Private;
    std::shared_ptr<Private> d;
};

class OsdPainter {
public:
    static OsdPainter& getInstance();

    // 静态绘制方法
    static void drawText(void* canvasAddr, uint32_t stride,
                         int canvasW, int canvasH,
                         const std::string& text,
                         int x, int y,
                         const OsdFont& font,
                         int maxWidth = 0,
                         const std::string& placeholder = "□");

    static int textWidth(const std::string& text, const OsdFont& font);

private:
    OsdPainter() = default;
    ~OsdPainter() = default;
    OsdPainter(const OsdPainter&) = delete;
    OsdPainter& operator=(const OsdPainter&) = delete;

    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace osd
} // namespace hisi

#endif