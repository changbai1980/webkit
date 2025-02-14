/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 * Copyright (C) 2003, 2004, 2005, 2006, 2008, 2009 Apple Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef CharacterData_h
#define CharacterData_h

#include "Node.h"
#include <wtf/text/WTFString.h>

namespace WebCore {

class CharacterData : public Node {
public:
    const String& data() const { return m_data; }
    static ptrdiff_t dataMemoryOffset() { return OBJECT_OFFSETOF(CharacterData, m_data); }

    WEBCORE_EXPORT void setData(const String&, ExceptionCode&);
    unsigned length() const { return m_data.length(); }
    String substringData(unsigned offset, unsigned count, ExceptionCode&);
    void appendData(const String&, ExceptionCode&);
    void insertData(unsigned offset, const String&, ExceptionCode&);
    void deleteData(unsigned offset, unsigned count, ExceptionCode&);
    void replaceData(unsigned offset, unsigned count, const String&, ExceptionCode&);

    bool containsOnlyWhitespace() const;

    StringImpl* dataImpl() { return m_data.impl(); }

    // Like appendData, but optimized for the parser (e.g., no mutation events).
    // Returns how much could be added before length limit was met.
    unsigned parserAppendData(const String& string, unsigned offset, unsigned lengthLimit);

protected:
    CharacterData(Document& document, const String& text, ConstructionType type)
        : Node(document, type)
        , m_data(!text.isNull() ? text : emptyString())
    {
        ASSERT(type == CreateOther || type == CreateText || type == CreateEditingText);
    }

    void setDataWithoutUpdate(const String& data)
    {
        ASSERT(!data.isNull());
        m_data = data;
    }
    void dispatchModifiedEvent(const String& oldValue);

private:
    virtual String nodeValue() const override final;
    virtual void setNodeValue(const String&, ExceptionCode&) override final;
    virtual bool isCharacterDataNode() const override final { return true; }
    virtual int maxCharacterOffset() const override final;
    virtual bool offsetInCharacters() const override final;
    void setDataAndUpdate(const String&, unsigned offsetOfReplacedData, unsigned oldLength, unsigned newLength);
    void checkCharDataOperation(unsigned offset, ExceptionCode&);

    String m_data;
};

inline bool isCharacterData(const Node& node) { return node.isCharacterDataNode(); }
void isCharacterData(const CharacterData&); // Catch unnecessary runtime check of type known at compile time.
void isCharacterData(const ContainerNode&); // Catch unnecessary runtime check of type known at compile time.

NODE_TYPE_CASTS(CharacterData)

} // namespace WebCore

#endif // CharacterData_h
