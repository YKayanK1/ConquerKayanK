// ============================================================================
// Conquer Kayank Engine
// ============================================================================
#pragma once
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <cctype>

namespace Resource {

    struct BinaryReader {
        const std::vector<uint8_t>& m_data; size_t m_offset = 0;
        BinaryReader(const std::vector<uint8_t>& data) : m_data(data) {}
        bool CanRead(size_t bytes) const { return m_offset + bytes <= m_data.size(); }
        size_t GetPosition() const { return m_offset; }
        void SetPosition(size_t pos) { m_offset = pos; }
        void Skip(size_t bytes) { m_offset += bytes; }
        void SkipBack(size_t bytes) { if (m_offset >= bytes) m_offset -= bytes; else m_offset = 0; }

        template<typename T> T Read() {
            if (!CanRead(sizeof(T))) return T(); T val;
            std::memcpy(&val, m_data.data() + m_offset, sizeof(T)); m_offset += sizeof(T); return val;
        }

        std::string ReadString(size_t length) {
            if (!CanRead(length)) return "";
            std::string str(reinterpret_cast<const char*>(m_data.data() + m_offset), length);
            m_offset += length;

            size_t nullPos = str.find('\0');
            if (nullPos != std::string::npos) {
                str = str.substr(0, nullPos);
            }
            return str;
        }
    };

    // Le apenas as dimensoes de imagem (largura/altura em pixels) sem decodificar todo o conteudo.
    // Suporta DDS, TGA e BMP. Usado para corrigir o tamanho de sprites cujo width/height do .dmap
    // na verdade representa a area em tiles (footprint), nao o tamanho em pixels da textura.
    static bool ReadImagePixelSize(const std::vector<uint8_t>& data, int& outWidth, int& outHeight) {
        if (data.size() > 128 && data[0] == 'D' && data[1] == 'D' && data[2] == 'S' && data[3] == ' ') {
            outHeight = (int)(data[12] | (data[13] << 8) | (data[14] << 16) | (data[15] << 24));
            outWidth = (int)(data[16] | (data[17] << 8) | (data[18] << 16) | (data[19] << 24));
            return outWidth > 0 && outHeight > 0;
        }
        if (data.size() > 54 && data[0] == 'B' && data[1] == 'M') {
            outWidth = (int)(data[18] | (data[19] << 8) | (data[20] << 16) | (data[21] << 24));
            outHeight = (int)(data[22] | (data[23] << 8) | (data[24] << 16) | (data[25] << 24));
            if (outHeight < 0) outHeight = -outHeight;
            return outWidth > 0 && outHeight > 0;
        }
        if (data.size() > 18 && data[2] == 2) {
            outWidth = data[12] | (data[13] << 8);
            outHeight = data[14] | (data[15] << 8);
            return outWidth > 0 && outHeight > 0;
        }
        return false;
    }

    static uint32_t HashFilename(const std::string& filename) {
        std::string str = filename;
        for (char& c : str) {
            c = std::tolower((unsigned char)c);
            if (c == '/') c = '\\';
        }

        int pad = (str.length() % 4 != 0) ? (4 - (str.length() % 4)) : 0;
        for (int i = 0; i < pad; i++) str.push_back('\0');

        std::vector<uint32_t> array(70, 0);
        std::memcpy(array.data(), str.data(), str.length());
        int i = str.length() / 4;
        array[i++] = 2615624776u;
        array[i++] = 1727278152u;

        uint32_t num = 4110059816u, num2 = 0, num3 = 0, num4 = 933775118u, num5 = 2002301995u, num6 = 0;

        for (int num7 = 0; num7 < i; num7++) {
            num6 = 645597969u;
            num = (num << 1) | (num >> 31);
            num6 ^= num;
            num2 = array[num7];
            num4 ^= num2;
            num5 ^= num2;
            num3 = num6 + num5;
            num3 |= 0x2040801u;
            num3 &= 0xBFEF7FDFu;
            uint64_t num8 = (uint64_t)num3 * num4;
            num2 = (uint32_t)num8;
            num3 = (uint32_t)(num8 >> 32);
            if (num3 != 0) num2++;
            num8 = (uint64_t)num2 + num3;
            num2 = (uint32_t)num8;
            if ((uint32_t)(num8 >> 32) != 0) num2++;
            num3 = num6 + num4;
            num3 |= 0x804021u;
            num3 &= 0x7DFEFBFFu;
            num4 = num2;
            num8 = (uint64_t)num5 * num3;
            num2 = (uint32_t)num8;
            num3 = (uint32_t)(num8 >> 32);
            num8 = (uint64_t)num3 + num3;
            num3 = (uint32_t)num8;
            if ((uint32_t)(num8 >> 32) != 0) num2++;
            num8 = (uint64_t)num2 + num3;
            num2 = (uint32_t)num8;
            if ((uint32_t)(num8 >> 32) != 0) num2 += 2;
            num5 = num2;
        }
        return num4 ^ num5;
    }
}