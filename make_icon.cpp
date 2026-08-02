// ============================================================================
//  Syspect — .ico ureteci (gelistirme araci)
//  ----------------------------------------------------------------------
//  logo.cpp'deki ayni cizim koduyla simge dosyasini uretir. Boylece pencere
//  basligindaki isaret ile gorev cubugundaki simge ASLA birbirinden
//  ayrilmaz; birini degistirince digeri de degisir.
//
//  Calistirma:  ss_makeicon syspect.ico
//
//  Bicim: her boyut PNG olarak gomuluyor (Vista+ destekler). BMP/DIB yerine
//  PNG secilmesinin sebebi alfa kanalinin sorunsuz tasinmasi — DIB yolunda
//  ayrica AND maskesi uretmek gerekiyor ve kenar yumusatma bozuluyor.
// ============================================================================
#ifdef _WIN32

#include "logo.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace Gdiplus;

namespace {

#pragma pack(push, 1)
struct IconDir      { uint16_t reserved, type, count; };
struct IconDirEntry {
    uint8_t  width, height, colorCount, reserved;
    uint16_t planes, bitCount;
    uint32_t bytesInRes, imageOffset;
};
#pragma pack(pop)

bool pngEncoderClsid(CLSID& out) {
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return false;
    std::vector<uint8_t> buf(size);
    auto* codecs = reinterpret_cast<ImageCodecInfo*>(buf.data());
    GetImageEncoders(num, size, codecs);
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0) {
            out = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

// Iki renk semasi. Ikisi de her zeminde okunur cunku halka ici DOLU.
//   dark  — koyu dolgu, beyaz halka ve S   (VARSAYILAN)
//   light — beyaz dolgu, koyu halka ve S   (referans tasarim)
//
// Varsayilan koyu: yan yana karsilastirildiginda beyaz dolgulu surum koyu
// gorev cubugunda parlayan bir leke gibi duruyor, koyu surum ise hem acik
// hem koyu zeminde formunu koruyor.
struct Scheme { Color body, accent, backdrop; };

const Scheme kLight{
    Color(255, 0x1B, 0x1B, 0x1F),
    Color(255, 0xEE, 0x77, 0x2A),
    Color(255, 0xFF, 0xFF, 0xFF)
};
const Scheme kDark{
    Color(255, 0xFF, 0xFF, 0xFF),
    Color(255, 0xEE, 0x77, 0x2A),
    Color(255, 0x1B, 0x1B, 0x1F)
};

// Tek bir boyutu cizip PNG baytlarini dondurur.
std::vector<uint8_t> renderPng(int size, const CLSID& png, const Scheme& s) {
    Bitmap bmp(size, size, PixelFormat32bppARGB);
    Graphics g(&bmp);
    g.Clear(Color(0, 0, 0, 0));

    const float inset = size * 0.03f;
    sslogo::draw(g, RectF(inset, inset, size - inset * 2, size - inset * 2),
                 s.body, s.accent, s.backdrop);

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) != S_OK) return {};
    bmp.Save(stream, &png, nullptr);

    HGLOBAL mem = nullptr;
    GetHGlobalFromStream(stream, &mem);
    const SIZE_T bytes = GlobalSize(mem);
    std::vector<uint8_t> out(bytes);
    if (void* p = GlobalLock(mem)) {
        memcpy(out.data(), p, bytes);
        GlobalUnlock(mem);
    }
    stream->Release();
    return out;
}

} // namespace

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : "syspect.ico";
    const bool  light = (argc > 2) && std::string(argv[2]) == "light";
    const Scheme& scheme = light ? kLight : kDark;

    ULONG_PTR token = 0;
    GdiplusStartupInput input;
    if (GdiplusStartup(&token, &input, nullptr) != Ok) {
        std::printf("GDI+ baslatilamadi\n");
        return 1;
    }

    CLSID png;
    if (!pngEncoderClsid(png)) {
        std::printf("PNG kodlayicisi bulunamadi\n");
        GdiplusShutdown(token);
        return 1;
    }

    // 16..256: Explorer, gorev cubugu, Alt+Tab ve kisayol simgeleri farkli
    // boyutlar ister; hepsini gommezsek Windows olceklendirir ve bulaniklasir.
    const int sizes[] = {16, 20, 24, 32, 40, 48, 64, 96, 128, 256};
    const int count = static_cast<int>(sizeof(sizes) / sizeof(sizes[0]));

    std::vector<std::vector<uint8_t>> images;
    for (const int s : sizes) {
        images.push_back(renderPng(s, png, scheme));
        if (images.back().empty()) {
            std::printf("%d px cizilemedi\n", s);
            GdiplusShutdown(token);
            return 1;
        }
    }

    FILE* f = std::fopen(path, "wb");
    if (!f) {
        std::printf("Dosya acilamadi: %s\n", path);
        GdiplusShutdown(token);
        return 1;
    }

    IconDir dir{0, 1, static_cast<uint16_t>(count)};
    std::fwrite(&dir, sizeof(dir), 1, f);

    uint32_t offset = static_cast<uint32_t>(sizeof(IconDir) +
                                            sizeof(IconDirEntry) * count);
    for (int i = 0; i < count; ++i) {
        IconDirEntry e{};
        // 256 px, bayt alanina 0 olarak yazilir — bicimin kurali bu.
        e.width       = static_cast<uint8_t>(sizes[i] >= 256 ? 0 : sizes[i]);
        e.height      = e.width;
        e.planes      = 1;
        e.bitCount    = 32;
        e.bytesInRes  = static_cast<uint32_t>(images[i].size());
        e.imageOffset = offset;
        std::fwrite(&e, sizeof(e), 1, f);
        offset += e.bytesInRes;
    }
    for (const auto& img : images)
        std::fwrite(img.data(), 1, img.size(), f);

    std::fclose(f);
    GdiplusShutdown(token);

    std::printf("%s yazildi (%d boyut)\n", path, count);
    return 0;
}

#endif // _WIN32
