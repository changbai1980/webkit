/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "SVGToOTFFontConversion.h"

#include "CSSStyleDeclaration.h"
#include "ElementChildIterator.h"
#include "SVGFontElement.h"
#include "SVGFontFaceElement.h"
#include "SVGGlyphElement.h"
#include "SVGHKernElement.h"
#include "SVGMissingGlyphElement.h"
#include "SVGPathBuilder.h"
#include "SVGPathParser.h"
#include "SVGPathStringSource.h"
#include "SVGVKernElement.h"

namespace WebCore {

static inline void write32(Vector<char>& vector, uint32_t value)
{
    vector.append(value >> 24);
    vector.append(value >> 16);
    vector.append(value >> 8);
    vector.append(value);
}

static inline void write16(Vector<char>& vector, uint16_t value)
{
    vector.append(value >> 8);
    vector.append(value);
}

static inline void overwrite32(Vector<char>& vector, unsigned location, uint32_t value)
{
    ASSERT(vector.size() >= location + 4);
    *(vector.data() + location) = value >> 24;
    *(vector.data() + location + 1) = value >> 16;
    *(vector.data() + location + 2) = value >> 8;
    *(vector.data() + location + 3) = value;
}

class SVGToOTFFontConverter {
public:
    SVGToOTFFontConverter(const SVGFontElement&);
    Vector<char> convertSVGToOTFFont();

private:
    typedef uint16_t SID; // String ID
    typedef UChar Codepoint; // FIXME: Only support BMP for now
    struct GlyphData {
        GlyphData(Vector<char> charString, const SVGGlyphElement* glyphElement, float horizontalAdvance, float verticalAdvance, FloatRect boundingBox, Codepoint codepoint)
            : boundingBox(boundingBox)
            , charString(charString)
            , glyphElement(glyphElement)
            , horizontalAdvance(horizontalAdvance)
            , verticalAdvance(verticalAdvance)
            , codepoint(codepoint)
        {
        }
        FloatRect boundingBox;
        Vector<char> charString;
        const SVGGlyphElement* glyphElement;
        float horizontalAdvance;
        float verticalAdvance;
        Codepoint codepoint;
    };

    struct KerningData {
        KerningData(uint16_t glyph1, uint16_t glyph2, int16_t adjustment)
            : glyph1(glyph1)
            , glyph2(glyph2)
            , adjustment(adjustment)
        {
        }
        bool operator<(const KerningData& other) const
        {
            return glyph1 < other.glyph1 || (glyph1 == other.glyph1 && glyph2 < other.glyph2);
        }
        uint16_t glyph1;
        uint16_t glyph2;
        int16_t adjustment;
    };

    static const size_t kSNFTHeaderSize = 12;
    static const size_t kDirectoryEntrySize = 16;


    template <typename T>
    void appendGlyphData(const Vector<char>& path, const T* element, float horizontalAdvance, float verticalAdvance, const FloatRect& boundingBox, Codepoint codepoint);
    template <typename T>
    void processGlyphElement(const T& element, float defaultHorizontalAdvance, float defaultVerticalAdvance, Codepoint, bool& initialGlyph);

    typedef void (SVGToOTFFontConverter::*FontAppendingFunction)(Vector<char> &) const;
    void appendTable(const char identifier[4], Vector<char>&, FontAppendingFunction);
    void appendCMAPTable(Vector<char>&) const;
    void appendHEADTable(Vector<char>&) const;
    void appendHHEATable(Vector<char>&) const;
    void appendHMTXTable(Vector<char>&) const;
    void appendVHEATable(Vector<char>&) const;
    void appendVMTXTable(Vector<char>&) const;
    void appendKERNTable(Vector<char>&) const;
    void appendMAXPTable(Vector<char>&) const;
    void appendNAMETable(Vector<char>&) const;
    void appendOS2Table(Vector<char>&) const;
    void appendPOSTTable(Vector<char>&) const;
    void appendCFFTable(Vector<char>&) const;
    void appendVORGTable(Vector<char>&) const;

    void addCodepointRanges(const UnicodeRanges&, HashSet<uint16_t>& glyphSet) const;
    void addCodepoints(const HashSet<String>& codepoints, HashSet<uint16_t>& glyphSet) const;
    void addGlyphNames(const HashSet<String>& glyphNames, HashSet<uint16_t>& glyphSet) const;
    template <typename T>
    Vector<KerningData> computeKerningData(bool (T::*buildKerningPair)(SVGKerningPair&) const) const;
    template <typename T>
    size_t appendKERNSubtable(Vector<char>& result, bool (T::*buildKerningPair)(SVGKerningPair&) const, uint16_t coverage) const;

    Vector<GlyphData> m_glyphs;
    HashMap<String, Glyph> m_glyphNameToIndexMap; // SVG 1.1: "It is recommended that glyph names be unique within a font."
    HashMap<Codepoint, Glyph> m_codepointToIndexMap; // FIXME: There might be many glyphs that map to a single codepoint.
    FloatRect m_boundingBox;
    const SVGFontElement& m_fontElement;
    const SVGFontFaceElement* m_fontFaceElement;
    const SVGMissingGlyphElement* m_missingGlyphElement;
    String m_fontFamily;
    float m_advanceWidthMax;
    float m_advanceHeightMax;
    float m_minRightSideBearing;
    unsigned m_unitsPerEm;
    int m_tablesAppendedCount;
    char m_weight;
    bool m_italic;
};

static uint16_t roundDownToPowerOfTwo(uint16_t x)
{
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    return (x >> 1) + 1;
}

static uint16_t integralLog2(uint16_t x) {
    uint16_t result = 0;
    while (x >>= 1)
        ++result;
    return result;
}

void SVGToOTFFontConverter::appendCMAPTable(Vector<char>& result) const
{
    auto startingOffset = result.size();
    write16(result, 0);
    write16(result, 1); // Number subtables

    write16(result, 0); // Unicode
    write16(result, 3); // Unicode version 2.2+
    write32(result, result.size() - startingOffset + sizeof(uint32_t)); // Byte offset of subtable

    // Braindead scheme: One segment for each character
    ASSERT(m_glyphs.size() < 0xFFFF);
    uint16_t segCount = m_glyphs.size() + 1;
    write16(result, 4); // Format: Only support the Basic Multilingual Plane for now
    write16(result, 22 + m_glyphs.size() * 2); // length
    write16(result, 0); // Language independent
    write16(result, 2 * segCount); // segCountX2: "2 x segCount"
    uint16_t originalSearchRange = roundDownToPowerOfTwo(segCount);
    uint16_t searchRange = 2 * originalSearchRange; // searchRange: "2 x (2**floor(log2(segCount)))"
    write16(result, searchRange);
    write16(result, integralLog2(originalSearchRange)); // entrySelector: "log2(searchRange/2)"
    write16(result, (2 * segCount) - searchRange); // rangeShift: "2 x segCount - searchRange"

    for (auto& glyph : m_glyphs)
        write16(result, glyph.codepoint); // EndCode
    write16(result, 0xFFFF); // "To ensure that the search will terminate, the final endCode value must be 0xFFFF."
    write16(result, 0); // Reserved
    for (auto& glyph : m_glyphs)
        write16(result, glyph.codepoint); // startCode
    write16(result, 0xFFFF);
    for (unsigned i = 0; i < m_glyphs.size(); ++i) {
        // Note that this value can be "negative," but that is okay because wrapping is defined and expected here
        write16(result, static_cast<uint16_t>(i) - m_glyphs[i].codepoint); // idDelta
    }
    write16(result, 1);
    for (unsigned i = 0; i < m_glyphs.size(); ++i)
        write16(result, 0); // idRangeOffset
    write16(result, 0);
}

void SVGToOTFFontConverter::appendHEADTable(Vector<char>& result) const
{
    write32(result, 0x00010000); // Version
    write32(result, 0x00010000); // Revision
    write32(result, 0); // Checksum adjustment
    // Magic number. "Set to 0x5F0F3CF5"
    result.append(0x5F);
    result.append(0x0F);
    result.append(0x3C);
    result.append(-0x0B); // Wraparound
    write16(result, (1 << 9) | 1);

    write16(result, m_unitsPerEm);
    write32(result, 0); // First half of creation date
    write32(result, 0); // Last half of creation date
    write32(result, 0); // First half of modification date
    write32(result, 0); // Last half of modification date
    write16(result, std::numeric_limits<int16_t>::min()); // Minimum X
    write16(result, std::numeric_limits<int16_t>::min()); // Minimum Y
    write16(result, std::numeric_limits<int16_t>::max()); // Maximum X
    write16(result, std::numeric_limits<int16_t>::max()); // Maximum Y
    write16(result, (m_italic ? 1 << 1 : 0) | (m_weight >= 7 ? 1 : 0));
    write16(result, 3); // Smallest readable size in pixels
    write16(result, 0); // Might contain LTR or RTL glyphs
    write16(result, 0); // Short offsets in the 'loca' table. However, OTF fonts don't have a 'loca' table so this is irrelevant
    write16(result, 0); // Glyph data format
}

// Assumption: T2 can hold every value that a T1 can hold
template <typename T1, typename T2>
static inline T1 clampTo(T2 x)
{
    x = std::min(x, static_cast<T2>(std::numeric_limits<T1>::max()));
    x = std::max(x, static_cast<T2>(std::numeric_limits<T1>::min()));
    return static_cast<T1>(x);
}

void SVGToOTFFontConverter::appendHHEATable(Vector<char>& result) const
{
    int16_t ascent = std::numeric_limits<int16_t>::max();
    int16_t descent = std::numeric_limits<int16_t>::max();
    if (m_fontFaceElement) {
        ascent = m_fontFaceElement->ascent();
        descent = m_fontFaceElement->descent();
    }

    // Many platforms will assume that a 0 ascent or descent means that the platform should synthesize a font
    // based on a heuristic. However, many SVG fonts legitimitely have a 0 ascent or descent. Therefore,
    // we should specify a single FUnit instead, which is as close as we can get to 0 without actually being
    // it.
    if (!ascent)
        ascent = 1;
    if (!descent)
        descent = 1;

    write32(result, 0x00010000); // Version
    write16(result, ascent);
    write16(result, descent);
    // WebKit's SVG codepath hardcodes the line gap to be 1/10th of the font size (see r29719). Matching that
    // allows us to have consistent renderings between the two paths.
    write16(result, m_unitsPerEm / 10); // Line gap
    write16(result, clampTo<uint16_t>(m_advanceWidthMax));
    write16(result, clampTo<int16_t>(m_boundingBox.x())); // Minimum left side bearing
    write16(result, clampTo<int16_t>(m_minRightSideBearing)); // Minimum right side bearing
    write16(result, clampTo<int16_t>(m_boundingBox.maxX())); // X maximum extent
    // WebKit draws the caret
    write16(result, 1); // Vertical caret
    write16(result, 0); // Vertical caret
    write16(result, 0); // "Set value to 0 for non-slanted fonts"
    write32(result, 0); // Reserved
    write32(result, 0); // Reserved
    write16(result, 0); // Current format
    write16(result, m_glyphs.size()); // Number of advance widths in HMTX table
}

void SVGToOTFFontConverter::appendHMTXTable(Vector<char>& result) const
{
    for (auto& glyph : m_glyphs) {
        write16(result, clampTo<uint16_t>(glyph.horizontalAdvance));
        write16(result, clampTo<int16_t>(glyph.boundingBox.x()));
    }
}

void SVGToOTFFontConverter::appendMAXPTable(Vector<char>& result) const
{
    write32(result, 0x00010000); // Version
    write16(result, m_glyphs.size());
    write16(result, 0xFFFF); // Maximum number of points in non-compound glyph
    write16(result, 0xFFFF); // Maximum number of contours in non-compound glyph
    write16(result, 0xFFFF); // Maximum number of points in compound glyph
    write16(result, 0xFFFF); // Maximum number of contours in compound glyph
    write16(result, 2); // Maximum number of zones
    write16(result, 0); // Maximum number of points used in zone 0
    write16(result, 0); // Maximum number of storage area locations
    write16(result, 0); // Maximum number of function definitions
    write16(result, 0); // Maximum number of instruction definitions
    write16(result, 0); // Maximum stack depth
    write16(result, 0); // Maximum size of instructions
    write16(result, m_glyphs.size()); // Maximum number of glyphs referenced at top level
    write16(result, 0); // No compound glyphs
}

void SVGToOTFFontConverter::appendNAMETable(Vector<char>& result) const
{
    write16(result, 0); // Format selector
    write16(result, 1); // Number of name records in table
    write16(result, 18); // Offset in bytes to the beginning of name character strings

    write16(result, 0); // Unicode
    write16(result, 3); // Unicode version 2.0 or later
    write16(result, 0); // Language
    write16(result, 1); // Name identifier. 1 = Font family
    write16(result, m_fontFamily.length());
    write16(result, 0); // Offset into name data

    for (unsigned i = 0; i < m_fontFamily.length(); ++i)
        write16(result, m_fontFamily[i]);
}

void SVGToOTFFontConverter::appendOS2Table(Vector<char>& result) const
{
    int16_t averageAdvance = m_unitsPerEm;
    auto& attribute = m_fontElement.fastGetAttribute(SVGNames::horiz_adv_xAttr);
    bool ok;
    int value = attribute.toInt(&ok);
    if (ok)
        averageAdvance = clampTo<int16_t>(value);
    else if (m_missingGlyphElement) {
        int value = m_missingGlyphElement->fastGetAttribute(SVGNames::horiz_adv_xAttr).toInt(&ok);
        if (ok)
            averageAdvance = clampTo<int16_t>(value);
    }

    write16(result, 0); // Version
    write16(result, averageAdvance);
    write16(result, m_weight); // Weight class
    write16(result, 5); // Width class
    write16(result, 0); // Protected font
    // WebKit handles these superscripts and subscripts
    write16(result, 0); // Subscript X Size
    write16(result, 0); // Subscript Y Size
    write16(result, 0); // Subscript X Offset
    write16(result, 0); // Subscript Y Offset
    write16(result, 0); // Superscript X Size
    write16(result, 0); // Superscript Y Size
    write16(result, 0); // Superscript X Offset
    write16(result, 0); // Superscript Y Offset
    write16(result, 0); // Strikeout width
    write16(result, 0); // Strikeout Position
    write16(result, 0); // No classification

    Vector<unsigned char> specifiedPanose;
    if (m_fontFaceElement) {
        auto& attribute = m_fontFaceElement->fastGetAttribute(SVGNames::panose_1Attr);
        Vector<String> split;
        String(attribute).split(" ", split);
        if (split.size() == 10) {
            for (auto& s : split) {
                bool ok = true;
                int value = s.toInt(&ok);
                if (!ok || value < 0 || value > 0xFF) {
                    specifiedPanose.clear();
                    break;
                }
                specifiedPanose.append(static_cast<unsigned char>(value));
            }
        }
    }

    if (specifiedPanose.size() == 10) {
        for (char c : specifiedPanose)
            result.append(c);
    } else {
        for (int i = 0; i < 10; ++i)
            result.append(0); // PANOSE: Any
    }

    for (int i = 0; i < 4; ++i)
        write32(result, 0); // "Bit assignments are pending. Set to 0"
    write32(result, 0x544B4257); // Font Vendor. "WBKT"
    write16(result, (m_weight >= 7 ? 1 << 5 : 0) | (m_italic ? 1 : 0)); // Font Patterns.
    write16(result, m_glyphs[0].codepoint); // First unicode index
    write16(result, m_glyphs[m_glyphs.size() - 1].codepoint); // Last unicode index
}

void SVGToOTFFontConverter::appendPOSTTable(Vector<char>& result) const
{
    write32(result, 0x00030000); // Format. Printing is undefined
    write32(result, 0); // Italic angle in degrees
    write16(result, 0); // Underline position
    write16(result, 0); // Underline thickness
    write32(result, 0); // Monospaced
    write32(result, 0); // "Minimum memory usage when a TrueType font is downloaded as a Type 42 font"
    write32(result, 0); // "Maximum memory usage when a TrueType font is downloaded as a Type 42 font"
    write32(result, 0); // "Minimum memory usage when a TrueType font is downloaded as a Type 1 font"
    write32(result, 0); // "Maximum memory usage when a TrueType font is downloaded as a Type 1 font"
}

static bool isValidStringForCFF(const String& string)
{
    for (unsigned i = 0; i < string.length(); ++i) {
        if (string[i] < 33 || string[i] > 126)
            return false;
    }
    return true;
}

static void appendCFFValidString(Vector<char>& output, const String& string)
{
    ASSERT(isValidStringForCFF(string));
    for (unsigned i = 0; i < string.length(); ++i)
        output.append(string[i]);
}

void SVGToOTFFontConverter::appendCFFTable(Vector<char>& result) const
{
    auto startingOffset = result.size();
    // Header
    result.append(1); // Major version
    result.append(0); // Minor version
    result.append(4); // Header size
    result.append(4); // Offsets within CFF table are 4 bytes long

    // Name INDEX
    String fontName;
    if (m_fontFaceElement) {
        // FIXME: fontFamily() here might not be quite what I want
        String potentialFontName = m_fontFamily;
        if (isValidStringForCFF(potentialFontName))
            fontName = potentialFontName;
    }
    write16(result, 1); // INDEX contains 1 element
    result.append(4); // Offsets in this INDEX are 4 bytes long
    write32(result, 1); // 1-index offset of name data
    write32(result, fontName.length() + 1); // 1-index offset just past end of name data
    appendCFFValidString(result, fontName);

    String weight;
    if (m_fontFaceElement) {
        auto& potentialWeight = m_fontFaceElement->fastGetAttribute(SVGNames::font_weightAttr);
        if (isValidStringForCFF(potentialWeight))
            weight = potentialWeight;
    }

    const char operand32Bit = 29;
    const char fullNameKey = 2;
    const char familyNameKey = 3;
    const char weightKey = 4;
    const char fontBBoxKey = 5;
    const char charsetIndexKey = 15;
    const char charstringsIndexKey = 17;
    const uint32_t userDefinedStringStartIndex = 391;
    const unsigned sizeOfTopIndex = 45 + (weight.isEmpty() ? 0 : 6);

    // Top DICT INDEX.
    write16(result, 1); // INDEX contains 1 element
    result.append(4); // Offsets in this INDEX are 4 bytes long
    write32(result, 1); // 1-index offset of DICT data
    write32(result, 1 + sizeOfTopIndex); // 1-index offset just past end of DICT data

    // DICT information
#if !ASSERT_DISABLED
    unsigned topDictStart = result.size();
#endif
    result.append(operand32Bit);
    write32(result, userDefinedStringStartIndex);
    result.append(fullNameKey);
    result.append(operand32Bit);
    write32(result, userDefinedStringStartIndex);
    result.append(familyNameKey);
    if (!weight.isEmpty()) {
        result.append(operand32Bit);
        write32(result, userDefinedStringStartIndex + 1);
        result.append(weightKey);
    }
    result.append(operand32Bit);
    write32(result, clampTo<int32_t>(m_boundingBox.x()));
    result.append(operand32Bit);
    write32(result, clampTo<int32_t>(m_boundingBox.maxX()));
    result.append(operand32Bit);
    write32(result, clampTo<int32_t>(m_boundingBox.y()));
    result.append(operand32Bit);
    write32(result, clampTo<int32_t>(m_boundingBox.maxY()));
    result.append(fontBBoxKey);
    result.append(operand32Bit);
    unsigned charsetOffsetLocation = result.size();
    write32(result, 0); // Offset of Charset info. Will be overwritten later.
    result.append(charsetIndexKey);
    result.append(operand32Bit);
    unsigned charstringsOffsetLocation = result.size();
    write32(result, 0); // Offset of CharStrings INDEX. Will be overwritten later.
    result.append(charstringsIndexKey);
    ASSERT(result.size() == topDictStart + sizeOfTopIndex);

    // String INDEX
    write16(result, 1 + (weight.isEmpty() ? 0 : 1)); // Number of elements in INDEX
    result.append(4); // Offsets in this INDEX are 4 bytes long
    uint32_t offset = 1;
    write32(result, offset);
    offset += fontName.length();
    write32(result, offset);
    if (!weight.isEmpty()) {
        offset += weight.length();
        write32(result, offset);
    }
    appendCFFValidString(result, fontName);
    appendCFFValidString(result, weight);

    write16(result, 0); // Empty subroutine INDEX

    // Charset info
    overwrite32(result, charsetOffsetLocation, result.size() - startingOffset);
    result.append(0);
    for (unsigned i = 1; i < m_glyphs.size(); ++i)
        write16(result, i);

    // CharStrings INDEX
    overwrite32(result, charstringsOffsetLocation, result.size() - startingOffset);
    write16(result, m_glyphs.size());
    result.append(4); // Offsets in this INDEX are 4 bytes long
    offset = 1;
    write32(result, offset);
    for (auto& glyph : m_glyphs) {
        offset += glyph.charString.size();
        write32(result, offset);
    }
    for (auto& glyph : m_glyphs)
        result.appendVector(glyph.charString);
}

void SVGToOTFFontConverter::appendVORGTable(Vector<char>& result) const
{
    write16(result, 1); // Major version
    write16(result, 0); // Minor version

    bool ok;
    int16_t defaultVerticalOriginY = clampTo<int16_t>(m_fontElement.fastGetAttribute(SVGNames::vert_origin_yAttr).toInt(&ok));
    if (!ok && m_missingGlyphElement)
        defaultVerticalOriginY = clampTo<int16_t>(m_missingGlyphElement->fastGetAttribute(SVGNames::vert_origin_yAttr).toInt());
    write16(result, defaultVerticalOriginY);

    Vector<std::pair<uint16_t, int16_t>> origins;
    for (uint16_t i = 0; i < m_glyphs.size(); ++i) {
        if (m_glyphs[i].glyphElement) {
            auto& attribute = m_glyphs[i].glyphElement->fastGetAttribute(SVGNames::vert_origin_yAttr);
            if (attribute != nullAtom && attribute.is8Bit()) {
                bool ok;
                int16_t verticalOriginY = attribute.toInt(&ok);
                if (ok && verticalOriginY)
                    origins.append(std::make_pair(i, verticalOriginY));
            }
        }
    }
    write16(result, origins.size());

    for (auto& p : origins) {
        write16(result, p.first);
        write16(result, p.second);
    }
}

void SVGToOTFFontConverter::appendVHEATable(Vector<char>& result) const
{
    write32(result, 0x00011000); // Version
    write16(result, m_unitsPerEm / 2); // Vertical typographic ascender (vertical baseline to the right)
    write16(result, clampTo<int16_t>(-static_cast<int>(m_unitsPerEm / 2))); // Vertical typographic descender
    write16(result, m_unitsPerEm / 10); // Vertical typographic line gap
    write16(result, clampTo<int16_t>(m_advanceHeightMax));
    write16(result, clampTo<int16_t>(m_unitsPerEm - m_boundingBox.maxY())); // Minimum top side bearing
    write16(result, clampTo<int16_t>(m_boundingBox.y())); // Minimum bottom side bearing
    write16(result, clampTo<int16_t>(m_unitsPerEm - m_boundingBox.y())); // Y maximum extent
    // WebKit draws the caret
    write16(result, 1); // Vertical caret
    write16(result, 0); // Vertical caret
    write16(result, 0); // "Set value to 0 for non-slanted fonts"
    write32(result, 0); // Reserved
    write32(result, 0); // Reserved
    write16(result, 0); // "Set to 0"
    write16(result, m_glyphs.size()); // Number of advance heights in VMTX table
}

void SVGToOTFFontConverter::appendVMTXTable(Vector<char>& result) const
{
    for (auto& glyph : m_glyphs) {
        write16(result, clampTo<uint16_t>(glyph.verticalAdvance));
        write16(result, clampTo<int16_t>(m_unitsPerEm - glyph.boundingBox.maxY())); // top side bearing
    }
}

void SVGToOTFFontConverter::addCodepointRanges(const UnicodeRanges& unicodeRanges, HashSet<Glyph>& glyphSet) const
{
    for (auto& unicodeRange : unicodeRanges) {
        for (auto codepoint = unicodeRange.first; codepoint < unicodeRange.second; ++codepoint) {
            if (!codepoint || codepoint >= std::numeric_limits<Codepoint>::max())
                continue;
            auto iterator = m_codepointToIndexMap.find(codepoint);
            if (iterator != m_codepointToIndexMap.end())
                glyphSet.add(iterator->value);
        }
    }
}

void SVGToOTFFontConverter::addCodepoints(const HashSet<String>& codepoints, HashSet<Glyph>& glyphSet) const
{
    for (auto& codepointString : codepoints) {
        auto codepointStringLength = codepointString.length();
        unsigned i = 0;
        while (i < codepointStringLength) {
            // FIXME: Canonicalization might be necessary
            UChar32 codepoint;
            if (codepointString.is8Bit())
                codepoint = codepointString.characters8()[i++];
            else
                U16_NEXT(codepointString.characters16(), i, codepointStringLength, codepoint);

            if (!codepoint || codepoint >= std::numeric_limits<Codepoint>::max())
                continue;
            auto indexIter = m_codepointToIndexMap.find(codepoint);
            if (indexIter != m_codepointToIndexMap.end())
                glyphSet.add(indexIter->value);
        }
    }
}

void SVGToOTFFontConverter::addGlyphNames(const HashSet<String>& glyphNames, HashSet<uint16_t>& glyphSet) const
{
    for (auto& glyphName : glyphNames) {
        auto indexIter = m_glyphNameToIndexMap.find(glyphName);
        if (indexIter != m_glyphNameToIndexMap.end())
            glyphSet.add(indexIter->value);
    }
}

template <typename T>
auto SVGToOTFFontConverter::computeKerningData(bool (T::*buildKerningPair)(SVGKerningPair&) const) const -> Vector<KerningData>
{
    Vector<KerningData> result;

    for (auto& kernElement : childrenOfType<T>(m_fontElement)) {
        SVGKerningPair kerningPair;
        if ((kernElement.*buildKerningPair)(kerningPair)) {
            HashSet<Glyph> glyphSet1;
            HashSet<Glyph> glyphSet2;

            addCodepointRanges(kerningPair.unicodeRange1, glyphSet1);
            addCodepointRanges(kerningPair.unicodeRange2, glyphSet2);
            addGlyphNames(kerningPair.glyphName1, glyphSet1);
            addGlyphNames(kerningPair.glyphName2, glyphSet2);
            addCodepoints(kerningPair.unicodeName1, glyphSet1);
            addCodepoints(kerningPair.unicodeName2, glyphSet1);

            // FIXME: Use table format 2 so we don't have to append each of these one by one
            for (auto& glyph1 : glyphSet1) {
                for (auto& glyph2 : glyphSet2)
                    result.append(KerningData(glyph1, glyph2, clampTo<int16_t>(-kerningPair.kerning)));
            }
        }
    }

    return result;
}

template <typename T>
size_t SVGToOTFFontConverter::appendKERNSubtable(Vector<char>& result, bool (T::*buildKerningPair)(SVGKerningPair&) const, uint16_t coverage) const
{
    Vector<KerningData> kerningData = computeKerningData<T>(buildKerningPair);
    std::sort(kerningData.begin(), kerningData.end());
    size_t sizeOfKerningDataTable = 14 + 6 * kerningData.size();
    if (sizeOfKerningDataTable > std::numeric_limits<uint16_t>::max()) {
        kerningData.clear();
        sizeOfKerningDataTable = 14;
    }

    write16(result, 0); // Version of subtable
    write16(result, sizeOfKerningDataTable); // Length of this subtable
    write16(result, coverage); // Table coverage bitfield

    uint16_t roundedNumKerningPairs = roundDownToPowerOfTwo(kerningData.size());

    write16(result, kerningData.size());
    write16(result, roundedNumKerningPairs * 6); // searchRange: "The largest power of two less than or equal to the value of nPairs, multiplied by the size in bytes of an entry in the table."
    write16(result, integralLog2(roundedNumKerningPairs)); // entrySelector: "log2 of the largest power of two less than or equal to the value of nPairs."
    write16(result, (kerningData.size() - roundedNumKerningPairs) * 6); // rangeShift: "The value of nPairs minus the largest power of two less than or equal to nPairs,
                                                                        // and then multiplied by the size in bytes of an entry in the table."

    for (auto& kerningData : kerningData) {
        write16(result, kerningData.glyph1);
        write16(result, kerningData.glyph2);
        write16(result, kerningData.adjustment);
    }

    return sizeOfKerningDataTable;
}

void SVGToOTFFontConverter::appendKERNTable(Vector<char>& result) const
{
    write16(result, 0); // Version
    write16(result, 2); // Number of subtables

#if !ASSERT_DISABLED
    auto subtablesOffset = result.size();
#endif

    size_t sizeOfHorizontalSubtable = appendKERNSubtable<SVGHKernElement>(result, &SVGHKernElement::buildHorizontalKerningPair, 1);
    ASSERT_UNUSED(sizeOfHorizontalSubtable, subtablesOffset + sizeOfHorizontalSubtable == result.size());
    size_t sizeOfVerticalSubtable = appendKERNSubtable<SVGVKernElement>(result, &SVGVKernElement::buildVerticalKerningPair, 0);
    ASSERT_UNUSED(sizeOfVerticalSubtable, subtablesOffset + sizeOfHorizontalSubtable + sizeOfVerticalSubtable == result.size());

    // Work around a bug in Apple's font parser by adding some padding bytes. <rdar://problem/18401901>
    for (int i = 0; i < 6; ++i)
        result.append(0);
}

static void writeCFFEncodedNumber(Vector<char>& vector, float number)
{
    int raw = number * powf(2, 16);
    vector.append(-1); // 0xFF
    write32(vector, raw);
}

static const char rLineTo = 0x05;
static const char rrCurveTo = 0x08;
static const char endChar = 0x0e;
static const char rMoveTo = 0x15;

class CFFBuilder : public SVGPathBuilder {
public:
    CFFBuilder(Vector<char>& cffData, float width, FloatPoint origin)
        : m_cffData(cffData)
        , m_firstPoint(true)
    {
        writeCFFEncodedNumber(m_cffData, width);
        writeCFFEncodedNumber(m_cffData, origin.x());
        writeCFFEncodedNumber(m_cffData, origin.y());
        m_cffData.append(rMoveTo);
    }

    void updateForConstituentPoint(FloatPoint x)
    {
        if (m_firstPoint)
            m_boundingBox = FloatRect(x, FloatSize());
        else
            m_boundingBox.extend(x);
        m_firstPoint = false;
    }

    void moveTo(const FloatPoint& targetPoint, bool closed, PathCoordinateMode mode) override
    {
        FloatPoint destination = mode == AbsoluteCoordinates ? targetPoint : m_current + targetPoint;
        updateForConstituentPoint(destination);
        FloatSize delta = destination - m_current;

        if (closed && m_cffData.size())
            closePath();

        writeCFFEncodedNumber(m_cffData, delta.width());
        writeCFFEncodedNumber(m_cffData, delta.height());
        m_cffData.append(rMoveTo);

        m_current = destination;
        m_startingPoint = m_current;
    }

    void lineTo(const FloatPoint& targetPoint, PathCoordinateMode mode) override
    {
        FloatPoint destination = mode == AbsoluteCoordinates ? targetPoint : m_current + targetPoint;
        updateForConstituentPoint(destination);
        FloatSize delta = destination - m_current;

        writeCFFEncodedNumber(m_cffData, delta.width());
        writeCFFEncodedNumber(m_cffData, delta.height());
        m_cffData.append(rLineTo);

        m_current = destination;
    }

    void curveToCubic(const FloatPoint& point1, const FloatPoint& point2, const FloatPoint& targetPoint, PathCoordinateMode mode) override
    {
        // FIXME: This can be made way faster
        FloatPoint destination1 = point1;
        FloatPoint destination2 = point2;
        FloatPoint destination3 = targetPoint;
        if (mode == RelativeCoordinates) {
            destination1 += m_current;
            destination2 += m_current;
            destination3 += m_current;
        }
        updateForConstituentPoint(destination1);
        updateForConstituentPoint(destination2);
        updateForConstituentPoint(destination3);
        FloatSize delta3 = destination3 - destination2;
        FloatSize delta2 = destination2 - destination1;
        FloatSize delta1 = destination1 - m_current;

        writeCFFEncodedNumber(m_cffData, delta1.width());
        writeCFFEncodedNumber(m_cffData, delta1.height());
        writeCFFEncodedNumber(m_cffData, delta2.width());
        writeCFFEncodedNumber(m_cffData, delta2.height());
        writeCFFEncodedNumber(m_cffData, delta3.width());
        writeCFFEncodedNumber(m_cffData, delta3.height());
        m_cffData.append(rrCurveTo);

        m_current = destination3;
    }

    void closePath() override
    {
        if (m_current != m_startingPoint)
            lineTo(m_startingPoint, AbsoluteCoordinates);
    }

    FloatRect boundingBox()
    {
        return m_boundingBox;
    }

private:
    Vector<char>& m_cffData;
    FloatPoint m_startingPoint;
    FloatRect m_boundingBox;
    bool m_firstPoint;
};

template <typename T>
static Vector<char> transcodeGlyphPaths(float width, const T& glyphElement, FloatRect& boundingBox)
{
    Vector<char> result;
    auto& dAttribute = glyphElement.fastGetAttribute(SVGNames::dAttr);
    if (dAttribute.isEmpty()) {
        writeCFFEncodedNumber(result, width);
        writeCFFEncodedNumber(result, 0);
        writeCFFEncodedNumber(result, 0);
        result.append(rMoveTo);
        result.append(endChar);
        return result;
    }

    // FIXME: If we are vertical, use vert_origin_x and vert_origin_y
    bool ok;
    float horizontalOriginX = glyphElement.fastGetAttribute(SVGNames::horiz_origin_xAttr).toFloat(&ok);
    if (!ok)
        horizontalOriginX = 0;
    float horizontalOriginY = glyphElement.fastGetAttribute(SVGNames::horiz_origin_yAttr).toFloat(&ok);
    if (!ok)
        horizontalOriginY = 0;

    CFFBuilder builder(result, width, FloatPoint(horizontalOriginX, horizontalOriginY));
    SVGPathStringSource source(dAttribute);
    SVGPathParser parser;
    parser.setCurrentSource(&source);
    parser.setCurrentConsumer(&builder);

    ok = parser.parsePathDataFromSource(NormalizedParsing);
    parser.cleanup();

    if (!ok)
        result.clear();

    boundingBox = builder.boundingBox();

    result.append(endChar);
    return result;
}

template <typename T>
void SVGToOTFFontConverter::appendGlyphData(const Vector<char>& path, const T*, float horizontalAdvance, float verticalAdvance, const FloatRect& boundingBox, Codepoint codepoint)
{
    m_glyphs.append(GlyphData(path, nullptr, horizontalAdvance, verticalAdvance, boundingBox, codepoint));
}

template <>
void SVGToOTFFontConverter::appendGlyphData(const Vector<char>& path, const SVGGlyphElement* element, float horizontalAdvance, float verticalAdvance, const FloatRect& boundingBox, Codepoint codepoint)
{
    m_glyphs.append(GlyphData(path, element, horizontalAdvance, verticalAdvance, boundingBox, codepoint));
}

template <typename T>
void SVGToOTFFontConverter::processGlyphElement(const T& element, float defaultHorizontalAdvance, float defaultVerticalAdvance, Codepoint codepoint, bool& initialGlyph)
{
    bool ok;
    float horizontalAdvance = element.fastGetAttribute(SVGNames::horiz_adv_xAttr).toFloat(&ok);
    if (!ok)
        horizontalAdvance = defaultHorizontalAdvance;
    m_advanceWidthMax = std::max(m_advanceWidthMax, horizontalAdvance);
    float verticalAdvance = element.fastGetAttribute(SVGNames::vert_adv_yAttr).toFloat(&ok);
    if (!ok)
        verticalAdvance = defaultVerticalAdvance;
    m_advanceHeightMax = std::max(m_advanceHeightMax, verticalAdvance);

    FloatRect glyphBoundingBox;
    auto path = transcodeGlyphPaths(horizontalAdvance, element, glyphBoundingBox);
    if (initialGlyph)
        m_boundingBox = glyphBoundingBox;
    else
        m_boundingBox.unite(glyphBoundingBox);
    m_minRightSideBearing = std::min(m_minRightSideBearing, horizontalAdvance - glyphBoundingBox.maxX());
    initialGlyph = false;

    appendGlyphData(path, &element, horizontalAdvance, verticalAdvance, glyphBoundingBox, codepoint);
}

SVGToOTFFontConverter::SVGToOTFFontConverter(const SVGFontElement& fontElement)
    : m_fontElement(fontElement)
    , m_fontFaceElement(childrenOfType<SVGFontFaceElement>(m_fontElement).first())
    , m_missingGlyphElement(childrenOfType<SVGMissingGlyphElement>(m_fontElement).first())
    , m_advanceWidthMax(0)
    , m_advanceHeightMax(0)
    , m_minRightSideBearing(std::numeric_limits<float>::max())
    , m_unitsPerEm(0)
    , m_tablesAppendedCount(0)
    , m_weight(5)
    , m_italic(false)
{
    float defaultHorizontalAdvance = m_fontFaceElement ? m_fontFaceElement->horizontalAdvanceX() : 0;
    float defaultVerticalAdvance = m_fontFaceElement ? m_fontFaceElement->verticalAdvanceY() : 0;
    bool initialGlyph = true;

    if (m_fontFaceElement)
        m_unitsPerEm = m_fontFaceElement->unitsPerEm();

    if (m_missingGlyphElement)
        processGlyphElement(*m_missingGlyphElement, defaultHorizontalAdvance, defaultVerticalAdvance, 0, initialGlyph);
    else {
        Vector<char> notdefCharString;
        writeCFFEncodedNumber(notdefCharString, m_unitsPerEm);
        writeCFFEncodedNumber(notdefCharString, 0);
        writeCFFEncodedNumber(notdefCharString, 0);
        notdefCharString.append(rMoveTo);
        notdefCharString.append(endChar);
        m_glyphs.append(GlyphData(notdefCharString, nullptr, m_unitsPerEm, m_unitsPerEm, FloatRect(), 0));
    }

    for (auto& glyphElement : childrenOfType<SVGGlyphElement>(m_fontElement)) {
        auto& unicodeAttribute = glyphElement.fastGetAttribute(SVGNames::unicodeAttr);
        // Only support Basic Multilingual Plane w/o ligatures for now
        if (unicodeAttribute.length() == 1)
            processGlyphElement(glyphElement, defaultHorizontalAdvance, defaultVerticalAdvance, unicodeAttribute[0], initialGlyph);
    }

    if (m_glyphs.size() > std::numeric_limits<Glyph>::max()) {
        // This will break all sorts of things. Bail early instead.
        m_glyphs.clear();
        return;
    }

    std::sort(m_glyphs.begin(), m_glyphs.end(), [](const GlyphData& data1, const GlyphData& data2) {
        return data1.codepoint < data2.codepoint;
    });

    for (Glyph i = 0; i < m_glyphs.size(); ++i) {
        GlyphData& glyph = m_glyphs[i];
        if (glyph.glyphElement) {
            auto& glyphName = glyph.glyphElement->fastGetAttribute(SVGNames::glyph_nameAttr);
            if (!glyphName.isEmpty())
                m_glyphNameToIndexMap.add(glyphName, i);
        }
        if (glyph.codepoint)
            m_codepointToIndexMap.add(glyph.codepoint, i);
    }

    // FIXME: Handle commas
    if (m_fontFaceElement) {
        auto& fontWeightAttribute = m_fontFaceElement->fastGetAttribute(SVGNames::font_weightAttr);
        Vector<String> split;
        fontWeightAttribute.string().split(" ", split);
        for (auto& segment : split) {
            if (segment == "bold")
                m_weight = 7;
            bool ok;
            int value = segment.toInt(&ok);
            if (ok && value >= 0 && value < 1000)
                m_weight = value / 100;
        }
        auto& fontStyleAttribute = m_fontFaceElement->fastGetAttribute(SVGNames::font_weightAttr);
        split.clear();
        String(fontStyleAttribute).split(" ", split);
        for (auto& s : split) {
            if (s == "italic" || s == "oblique")
                m_italic = true;
        }
    }

    // Might not be quite what I want
    if (m_fontFaceElement)
        m_fontFamily = m_fontFaceElement->fontFamily();
}

static inline bool isFourByteAligned(size_t x)
{
    return !(x & sizeof(uint32_t)-1);
}

static uint32_t calculateChecksum(const Vector<char>& table, size_t startingOffset, size_t endingOffset)
{
    ASSERT(isFourByteAligned(endingOffset - startingOffset));
    uint32_t sum = 0;
    for (; startingOffset < endingOffset; startingOffset += 4) {
        // The spec is unclear whether this is a little-endian sum or a big-endian sum. Choose little endian.
        sum += (static_cast<unsigned char>(table[startingOffset + 3]) << 24)
            | (static_cast<unsigned char>(table[startingOffset + 2]) << 16)
            | (static_cast<unsigned char>(table[startingOffset + 1]) << 8)
            | static_cast<unsigned char>(table[startingOffset]);
    }
    return sum;
}

void SVGToOTFFontConverter::appendTable(const char identifier[4], Vector<char>& output, FontAppendingFunction appendingFunction)
{
    size_t offset = output.size();
    ASSERT(isFourByteAligned(offset));
    (this->*appendingFunction)(output);
    size_t unpaddedSize = output.size() - offset;
    while (!isFourByteAligned(output.size()))
        output.append(0);
    ASSERT(isFourByteAligned(output.size()));
    size_t directoryEntryOffset = kSNFTHeaderSize + m_tablesAppendedCount * kDirectoryEntrySize;
    output[directoryEntryOffset] = identifier[0];
    output[directoryEntryOffset + 1] = identifier[1];
    output[directoryEntryOffset + 2] = identifier[2];
    output[directoryEntryOffset + 3] = identifier[3];
    overwrite32(output, directoryEntryOffset + 4, calculateChecksum(output, offset, output.size()));
    overwrite32(output, directoryEntryOffset + 8, offset);
    overwrite32(output, directoryEntryOffset + 12, unpaddedSize);
    ++m_tablesAppendedCount;
}

Vector<char> SVGToOTFFontConverter::convertSVGToOTFFont()
{
    Vector<char> result;
    if (!m_glyphs.size())
        return result;

    uint16_t numTables = 13;
    uint16_t roundedNumTables = roundDownToPowerOfTwo(numTables);
    uint16_t searchRange = roundedNumTables * 16; // searchRange: "(Maximum power of 2 <= numTables) x 16."

    result.append('O');
    result.append('T');
    result.append('T');
    result.append('O');
    write16(result, numTables);
    write16(result, searchRange);
    write16(result, integralLog2(roundedNumTables)); // entrySelector: "Log2(maximum power of 2 <= numTables)."
    write16(result, numTables * 16 - searchRange); // rangeShift: "NumTables x 16-searchRange."

    ASSERT(result.size() == kSNFTHeaderSize);

    // Leave space for the Directory Entries
    for (size_t i = 0; i < kDirectoryEntrySize * numTables; ++i)
        result.append(0);

    // FIXME: Implement more tables, like vhea and vmtx (and kern!)
    appendTable("CFF ", result, &SVGToOTFFontConverter::appendCFFTable);
    appendTable("OS/2", result, &SVGToOTFFontConverter::appendOS2Table);
    appendTable("VORG", result, &SVGToOTFFontConverter::appendVORGTable);
    appendTable("cmap", result, &SVGToOTFFontConverter::appendCMAPTable);
    auto headTableOffset = result.size();
    appendTable("head", result, &SVGToOTFFontConverter::appendHEADTable);
    appendTable("hhea", result, &SVGToOTFFontConverter::appendHHEATable);
    appendTable("hmtx", result, &SVGToOTFFontConverter::appendHMTXTable);
    appendTable("kern", result, &SVGToOTFFontConverter::appendKERNTable);
    appendTable("maxp", result, &SVGToOTFFontConverter::appendMAXPTable);
    appendTable("name", result, &SVGToOTFFontConverter::appendNAMETable);
    appendTable("post", result, &SVGToOTFFontConverter::appendPOSTTable);
    appendTable("vhea", result, &SVGToOTFFontConverter::appendVHEATable);
    appendTable("vmtx", result, &SVGToOTFFontConverter::appendVMTXTable);

    ASSERT(numTables == m_tablesAppendedCount);

    // checkSumAdjustment: "To compute: set it to 0, calculate the checksum for the 'head' table and put it in the table directory,
    // sum the entire font as uint32, then store B1B0AFBA - sum. The checksum for the 'head' table will now be wrong. That is OK."
    uint32_t checksumAdjustment = 0xB1B0AFBAU - calculateChecksum(result, 0, result.size());
    overwrite32(result, headTableOffset + 8, checksumAdjustment);

    return result;
}

Vector<char> convertSVGToOTFFont(const SVGFontElement& element)
{
    return SVGToOTFFontConverter(element).convertSVGToOTFFont();
}

}
