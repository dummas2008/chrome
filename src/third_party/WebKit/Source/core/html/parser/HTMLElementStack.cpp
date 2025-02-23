/*
 * Copyright (C) 2010 Google, Inc. All Rights Reserved.
 * Copyright (C) 2011 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY GOOGLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL GOOGLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/html/parser/HTMLElementStack.h"

#include "core/HTMLNames.h"
#include "core/MathMLNames.h"
#include "core/SVGNames.h"
#include "core/dom/Element.h"
#include "core/html/HTMLElement.h"

namespace blink {

using namespace HTMLNames;


namespace {

inline bool isRootNode(HTMLStackItem* item)
{
    return item->isDocumentFragmentNode()
        || item->hasTagName(htmlTag);
}

inline bool isScopeMarker(HTMLStackItem* item)
{
    return item->hasTagName(appletTag)
        || item->hasTagName(captionTag)
        || item->hasTagName(marqueeTag)
        || item->hasTagName(objectTag)
        || item->hasTagName(tableTag)
        || item->hasTagName(tdTag)
        || item->hasTagName(thTag)
        || item->hasTagName(MathMLNames::miTag)
        || item->hasTagName(MathMLNames::moTag)
        || item->hasTagName(MathMLNames::mnTag)
        || item->hasTagName(MathMLNames::msTag)
        || item->hasTagName(MathMLNames::mtextTag)
        || item->hasTagName(MathMLNames::annotation_xmlTag)
        || item->hasTagName(SVGNames::foreignObjectTag)
        || item->hasTagName(SVGNames::descTag)
        || item->hasTagName(SVGNames::titleTag)
        || item->hasTagName(templateTag)
        || isRootNode(item);
}

inline bool isListItemScopeMarker(HTMLStackItem* item)
{
    return isScopeMarker(item)
        || item->hasTagName(olTag)
        || item->hasTagName(ulTag);
}

inline bool isTableScopeMarker(HTMLStackItem* item)
{
    return item->hasTagName(tableTag)
        || item->hasTagName(templateTag)
        || isRootNode(item);
}

inline bool isTableBodyScopeMarker(HTMLStackItem* item)
{
    return item->hasTagName(tbodyTag)
        || item->hasTagName(tfootTag)
        || item->hasTagName(theadTag)
        || item->hasTagName(templateTag)
        || isRootNode(item);
}

inline bool isTableRowScopeMarker(HTMLStackItem* item)
{
    return item->hasTagName(trTag)
        || item->hasTagName(templateTag)
        || isRootNode(item);
}

inline bool isForeignContentScopeMarker(HTMLStackItem* item)
{
    return HTMLElementStack::isMathMLTextIntegrationPoint(item)
        || HTMLElementStack::isHTMLIntegrationPoint(item)
        || item->isInHTMLNamespace();
}

inline bool isButtonScopeMarker(HTMLStackItem* item)
{
    return isScopeMarker(item)
        || item->hasTagName(buttonTag);
}

inline bool isSelectScopeMarker(HTMLStackItem* item)
{
    return !item->hasTagName(optgroupTag)
        && !item->hasTagName(optionTag);
}

} // namespace

HTMLElementStack::ElementRecord::ElementRecord(HTMLStackItem* item, ElementRecord* next)
    : m_item(item)
    , m_next(next)
{
    ASSERT(m_item);
}

#if !ENABLE(OILPAN)
HTMLElementStack::ElementRecord::~ElementRecord()
{
}
#endif

void HTMLElementStack::ElementRecord::replaceElement(HTMLStackItem* item)
{
    ASSERT(item);
    ASSERT(!m_item || m_item->isElementNode());
    // FIXME: Should this call finishParsingChildren?
    m_item = item;
}

bool HTMLElementStack::ElementRecord::isAbove(ElementRecord* other) const
{
    for (ElementRecord* below = next(); below; below = below->next()) {
        if (below == other)
            return true;
    }
    return false;
}

DEFINE_TRACE(HTMLElementStack::ElementRecord)
{
    visitor->trace(m_item);
    visitor->trace(m_next);
}

HTMLElementStack::HTMLElementStack()
    : m_rootNode(nullptr)
    , m_headElement(nullptr)
    , m_bodyElement(nullptr)
    , m_stackDepth(0)
{
}

HTMLElementStack::~HTMLElementStack()
{
}

bool HTMLElementStack::hasOnlyOneElement() const
{
    return !topRecord()->next();
}

bool HTMLElementStack::secondElementIsHTMLBodyElement() const
{
    // This is used the fragment case of <body> and <frameset> in the "in body"
    // insertion mode.
    // http://www.whatwg.org/specs/web-apps/current-work/multipage/tokenization.html#parsing-main-inbody
    ASSERT(m_rootNode);
    // If we have a body element, it must always be the second element on the
    // stack, as we always start with an html element, and any other element
    // would cause the implicit creation of a body element.
    return !!m_bodyElement;
}

void HTMLElementStack::popHTMLHeadElement()
{
    ASSERT(top() == m_headElement);
    m_headElement = nullptr;
    popCommon();
}

void HTMLElementStack::popHTMLBodyElement()
{
    ASSERT(top() == m_bodyElement);
    m_bodyElement = nullptr;
    popCommon();
}

void HTMLElementStack::popAll()
{
    m_rootNode = nullptr;
    m_headElement = nullptr;
    m_bodyElement = nullptr;
    m_stackDepth = 0;
    while (m_top) {
        Node& node = *topNode();
        if (node.isElementNode())
            toElement(node).finishParsingChildren();
        m_top = m_top->releaseNext();
    }
}

void HTMLElementStack::pop()
{
    ASSERT(!topStackItem()->hasTagName(HTMLNames::headTag));
    popCommon();
}

void HTMLElementStack::popUntil(const AtomicString& tagName)
{
    while (!topStackItem()->matchesHTMLTag(tagName)) {
        // pop() will ASSERT if a <body>, <head> or <html> will be popped.
        pop();
    }
}

void HTMLElementStack::popUntilPopped(const AtomicString& tagName)
{
    popUntil(tagName);
    pop();
}

void HTMLElementStack::popUntilNumberedHeaderElementPopped()
{
    while (!topStackItem()->isNumberedHeaderElement())
        pop();
    pop();
}

void HTMLElementStack::popUntil(Element* element)
{
    while (top() != element)
        pop();
}

void HTMLElementStack::popUntilPopped(Element* element)
{
    popUntil(element);
    pop();
}

void HTMLElementStack::popUntilTableScopeMarker()
{
    // http://www.whatwg.org/specs/web-apps/current-work/multipage/tokenization.html#clear-the-stack-back-to-a-table-context
    while (!isTableScopeMarker(topStackItem()))
        pop();
}

void HTMLElementStack::popUntilTableBodyScopeMarker()
{
    // http://www.whatwg.org/specs/web-apps/current-work/multipage/tokenization.html#clear-the-stack-back-to-a-table-body-context
    while (!isTableBodyScopeMarker(topStackItem()))
        pop();
}

void HTMLElementStack::popUntilTableRowScopeMarker()
{
    // http://www.whatwg.org/specs/web-apps/current-work/multipage/tokenization.html#clear-the-stack-back-to-a-table-row-context
    while (!isTableRowScopeMarker(topStackItem()))
        pop();
}

// http://www.whatwg.org/specs/web-apps/current-work/multipage/tree-construction.html#mathml-text-integration-point
bool HTMLElementStack::isMathMLTextIntegrationPoint(HTMLStackItem* item)
{
    if (!item->isElementNode())
        return false;
    return item->hasTagName(MathMLNames::miTag)
        || item->hasTagName(MathMLNames::moTag)
        || item->hasTagName(MathMLNames::mnTag)
        || item->hasTagName(MathMLNames::msTag)
        || item->hasTagName(MathMLNames::mtextTag);
}

// http://www.whatwg.org/specs/web-apps/current-work/multipage/tree-construction.html#html-integration-point
bool HTMLElementStack::isHTMLIntegrationPoint(HTMLStackItem* item)
{
    if (!item->isElementNode())
        return false;
    if (item->hasTagName(MathMLNames::annotation_xmlTag)) {
        Attribute* encodingAttr = item->getAttributeItem(MathMLNames::encodingAttr);
        if (encodingAttr) {
            const String& encoding = encodingAttr->value();
            return equalIgnoringCase(encoding, "text/html")
                || equalIgnoringCase(encoding, "application/xhtml+xml");
        }
        return false;
    }
    return item->hasTagName(SVGNames::foreignObjectTag)
        || item->hasTagName(SVGNames::descTag)
        || item->hasTagName(SVGNames::titleTag);
}

void HTMLElementStack::popUntilForeignContentScopeMarker()
{
    while (!isForeignContentScopeMarker(topStackItem()))
        pop();
}

void HTMLElementStack::pushRootNode(HTMLStackItem* rootItem)
{
    ASSERT(rootItem->isDocumentFragmentNode());
    pushRootNodeCommon(rootItem);
}

void HTMLElementStack::pushHTMLHtmlElement(HTMLStackItem* item)
{
    ASSERT(item->hasTagName(htmlTag));
    pushRootNodeCommon(item);
}

void HTMLElementStack::pushRootNodeCommon(HTMLStackItem* rootItem)
{
    ASSERT(!m_top);
    ASSERT(!m_rootNode);
    m_rootNode = rootItem->node();
    pushCommon(rootItem);
}

void HTMLElementStack::pushHTMLHeadElement(HTMLStackItem* item)
{
    ASSERT(item->hasTagName(HTMLNames::headTag));
    ASSERT(!m_headElement);
    m_headElement = item->element();
    pushCommon(item);
}

void HTMLElementStack::pushHTMLBodyElement(HTMLStackItem* item)
{
    ASSERT(item->hasTagName(HTMLNames::bodyTag));
    ASSERT(!m_bodyElement);
    m_bodyElement = item->element();
    pushCommon(item);
}

void HTMLElementStack::push(HTMLStackItem* item)
{
    ASSERT(!item->hasTagName(htmlTag));
    ASSERT(!item->hasTagName(headTag));
    ASSERT(!item->hasTagName(bodyTag));
    ASSERT(m_rootNode);
    pushCommon(item);
}

void HTMLElementStack::insertAbove(HTMLStackItem* item, ElementRecord* recordBelow)
{
    ASSERT(item);
    ASSERT(recordBelow);
    ASSERT(m_top);
    ASSERT(!item->hasTagName(htmlTag));
    ASSERT(!item->hasTagName(headTag));
    ASSERT(!item->hasTagName(bodyTag));
    ASSERT(m_rootNode);
    if (recordBelow == m_top) {
        push(item);
        return;
    }

    for (ElementRecord* recordAbove = m_top.get(); recordAbove; recordAbove = recordAbove->next()) {
        if (recordAbove->next() != recordBelow)
            continue;

        m_stackDepth++;
        recordAbove->setNext(new ElementRecord(item, recordAbove->releaseNext()));
        recordAbove->next()->element()->beginParsingChildren();
        return;
    }
    ASSERT_NOT_REACHED();
}

HTMLElementStack::ElementRecord* HTMLElementStack::topRecord() const
{
    ASSERT(m_top);
    return m_top.get();
}

HTMLStackItem* HTMLElementStack::oneBelowTop() const
{
    // We should never call this if there are fewer than 2 elements on the stack.
    ASSERT(m_top);
    ASSERT(m_top->next());
    if (m_top->next()->stackItem()->isElementNode())
        return m_top->next()->stackItem();
    return nullptr;
}

void HTMLElementStack::removeHTMLHeadElement(Element* element)
{
    ASSERT(m_headElement == element);
    if (m_top->element() == element) {
        popHTMLHeadElement();
        return;
    }
    m_headElement = nullptr;
    removeNonTopCommon(element);
}

void HTMLElementStack::remove(Element* element)
{
    ASSERT(!isHTMLHeadElement(element));
    if (m_top->element() == element) {
        pop();
        return;
    }
    removeNonTopCommon(element);
}

HTMLElementStack::ElementRecord* HTMLElementStack::find(Element* element) const
{
    for (ElementRecord* pos = m_top.get(); pos; pos = pos->next()) {
        if (pos->node() == element)
            return pos;
    }
    return nullptr;
}

HTMLElementStack::ElementRecord* HTMLElementStack::topmost(const AtomicString& tagName) const
{
    for (ElementRecord* pos = m_top.get(); pos; pos = pos->next()) {
        if (pos->stackItem()->matchesHTMLTag(tagName))
            return pos;
    }
    return nullptr;
}

bool HTMLElementStack::contains(Element* element) const
{
    return !!find(element);
}

bool HTMLElementStack::contains(const AtomicString& tagName) const
{
    return !!topmost(tagName);
}

template <bool isMarker(HTMLStackItem*)>
bool inScopeCommon(HTMLElementStack::ElementRecord* top, const AtomicString& targetTag)
{
    for (HTMLElementStack::ElementRecord* pos = top; pos; pos = pos->next()) {
        HTMLStackItem* item = pos->stackItem();
        if (item->matchesHTMLTag(targetTag))
            return true;
        if (isMarker(item))
            return false;
    }
    ASSERT_NOT_REACHED(); // <html> is always on the stack and is a scope marker.
    return false;
}

bool HTMLElementStack::hasNumberedHeaderElementInScope() const
{
    for (ElementRecord* record = m_top.get(); record; record = record->next()) {
        HTMLStackItem* item = record->stackItem();
        if (item->isNumberedHeaderElement())
            return true;
        if (isScopeMarker(item))
            return false;
    }
    ASSERT_NOT_REACHED(); // <html> is always on the stack and is a scope marker.
    return false;
}

bool HTMLElementStack::inScope(Element* targetElement) const
{
    for (ElementRecord* pos = m_top.get(); pos; pos = pos->next()) {
        HTMLStackItem* item = pos->stackItem();
        if (item->node() == targetElement)
            return true;
        if (isScopeMarker(item))
            return false;
    }
    ASSERT_NOT_REACHED(); // <html> is always on the stack and is a scope marker.
    return false;
}

bool HTMLElementStack::inScope(const AtomicString& targetTag) const
{
    return inScopeCommon<isScopeMarker>(m_top.get(), targetTag);
}

bool HTMLElementStack::inScope(const QualifiedName& tagName) const
{
    return inScope(tagName.localName());
}

bool HTMLElementStack::inListItemScope(const AtomicString& targetTag) const
{
    return inScopeCommon<isListItemScopeMarker>(m_top.get(), targetTag);
}

bool HTMLElementStack::inListItemScope(const QualifiedName& tagName) const
{
    return inListItemScope(tagName.localName());
}

bool HTMLElementStack::inTableScope(const AtomicString& targetTag) const
{
    return inScopeCommon<isTableScopeMarker>(m_top.get(), targetTag);
}

bool HTMLElementStack::inTableScope(const QualifiedName& tagName) const
{
    return inTableScope(tagName.localName());
}

bool HTMLElementStack::inButtonScope(const AtomicString& targetTag) const
{
    return inScopeCommon<isButtonScopeMarker>(m_top.get(), targetTag);
}

bool HTMLElementStack::inButtonScope(const QualifiedName& tagName) const
{
    return inButtonScope(tagName.localName());
}

bool HTMLElementStack::inSelectScope(const AtomicString& targetTag) const
{
    return inScopeCommon<isSelectScopeMarker>(m_top.get(), targetTag);
}

bool HTMLElementStack::inSelectScope(const QualifiedName& tagName) const
{
    return inSelectScope(tagName.localName());
}

bool HTMLElementStack::hasTemplateInHTMLScope() const
{
    return inScopeCommon<isRootNode>(m_top.get(), templateTag.localName());
}

Element* HTMLElementStack::htmlElement() const
{
    ASSERT(m_rootNode);
    return toElement(m_rootNode);
}

Element* HTMLElementStack::headElement() const
{
    ASSERT(m_headElement);
    return m_headElement;
}

Element* HTMLElementStack::bodyElement() const
{
    ASSERT(m_bodyElement);
    return m_bodyElement;
}

ContainerNode* HTMLElementStack::rootNode() const
{
    ASSERT(m_rootNode);
    return m_rootNode;
}

void HTMLElementStack::pushCommon(HTMLStackItem* item)
{
    ASSERT(m_rootNode);

    m_stackDepth++;
    m_top = new ElementRecord(item, m_top.release());
}

void HTMLElementStack::popCommon()
{
    ASSERT(!topStackItem()->hasTagName(htmlTag));
    ASSERT(!topStackItem()->hasTagName(headTag) || !m_headElement);
    ASSERT(!topStackItem()->hasTagName(bodyTag) || !m_bodyElement);
    top()->finishParsingChildren();
    m_top = m_top->releaseNext();

    m_stackDepth--;
}

void HTMLElementStack::removeNonTopCommon(Element* element)
{
    ASSERT(!isHTMLHtmlElement(element));
    ASSERT(!isHTMLBodyElement(element));
    ASSERT(top() != element);
    for (ElementRecord* pos = m_top.get(); pos; pos = pos->next()) {
        if (pos->next()->element() == element) {
            // FIXME: Is it OK to call finishParsingChildren()
            // when the children aren't actually finished?
            element->finishParsingChildren();
            pos->setNext(pos->next()->releaseNext());
            m_stackDepth--;
            return;
        }
    }
    ASSERT_NOT_REACHED();
}

HTMLElementStack::ElementRecord* HTMLElementStack::furthestBlockForFormattingElement(Element* formattingElement) const
{
    ElementRecord* furthestBlock = 0;
    for (ElementRecord* pos = m_top.get(); pos; pos = pos->next()) {
        if (pos->element() == formattingElement)
            return furthestBlock;
        if (pos->stackItem()->isSpecialNode())
            furthestBlock = pos;
    }
    ASSERT_NOT_REACHED();
    return nullptr;
}

DEFINE_TRACE(HTMLElementStack)
{
    visitor->trace(m_top);
    visitor->trace(m_rootNode);
    visitor->trace(m_headElement);
    visitor->trace(m_bodyElement);
}

#ifndef NDEBUG

void HTMLElementStack::show()
{
    for (ElementRecord* record = m_top.get(); record; record = record->next())
        record->element()->showNode();
}

#endif

} // namespace blink
