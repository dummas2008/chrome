// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/css/parser/CSSPropertyParser.h"

#include "core/StylePropertyShorthand.h"
#include "core/css/CSSBasicShapeValues.h"
#include "core/css/CSSBorderImage.h"
#include "core/css/CSSContentDistributionValue.h"
#include "core/css/CSSCounterValue.h"
#include "core/css/CSSCrossfadeValue.h"
#include "core/css/CSSCursorImageValue.h"
#include "core/css/CSSCustomIdentValue.h"
#include "core/css/CSSFontFaceSrcValue.h"
#include "core/css/CSSFontFeatureValue.h"
#include "core/css/CSSFunctionValue.h"
#include "core/css/CSSGradientValue.h"
#include "core/css/CSSGridAutoRepeatValue.h"
#include "core/css/CSSGridLineNamesValue.h"
#include "core/css/CSSImageSetValue.h"
#include "core/css/CSSPaintValue.h"
#include "core/css/CSSPathValue.h"
#include "core/css/CSSPrimitiveValueMappings.h"
#include "core/css/CSSQuadValue.h"
#include "core/css/CSSReflectValue.h"
#include "core/css/CSSSVGDocumentValue.h"
#include "core/css/CSSShadowValue.h"
#include "core/css/CSSStringValue.h"
#include "core/css/CSSTimingFunctionValue.h"
#include "core/css/CSSURIValue.h"
#include "core/css/CSSUnicodeRangeValue.h"
#include "core/css/CSSValuePair.h"
#include "core/css/CSSValuePool.h"
#include "core/css/CSSVariableReferenceValue.h"
#include "core/css/FontFace.h"
#include "core/css/HashTools.h"
#include "core/css/parser/CSSParserFastPaths.h"
#include "core/css/parser/CSSParserValues.h"
#include "core/css/parser/CSSPropertyParserHelpers.h"
#include "core/css/parser/CSSVariableParser.h"
#include "core/frame/UseCounter.h"
#include "core/layout/LayoutTheme.h"
#include "core/svg/SVGPathUtilities.h"
#include "wtf/text/StringBuilder.h"

namespace blink {

using namespace CSSPropertyParserHelpers;

CSSPropertyParser::CSSPropertyParser(const CSSParserTokenRange& range,
    const CSSParserContext& context, HeapVector<CSSProperty, 256>* parsedProperties)
    : m_range(range)
    , m_context(context)
    , m_parsedProperties(parsedProperties)
    , m_inParseShorthand(0)
    , m_currentShorthand(CSSPropertyInvalid)
{
    m_range.consumeWhitespace();
}

static bool hasInvalidNumericValues(const CSSParserTokenRange& range)
{
    for (const CSSParserToken& token : range) {
        CSSParserTokenType type = token.type();
        if ((type == NumberToken || type == DimensionToken || type == PercentageToken)
            && !CSSPropertyParser::isValidNumericValue(token.numericValue()))
            return true;
    }
    return false;
}

bool CSSPropertyParser::parseValue(CSSPropertyID unresolvedProperty, bool important,
    const CSSParserTokenRange& range, const CSSParserContext& context,
    HeapVector<CSSProperty, 256>& parsedProperties, StyleRule::RuleType ruleType)
{
    if (hasInvalidNumericValues(range))
        return false;
    int parsedPropertiesSize = parsedProperties.size();

    CSSPropertyParser parser(range, context, &parsedProperties);
    CSSPropertyID resolvedProperty = resolveCSSPropertyID(unresolvedProperty);
    bool parseSuccess;

    if (ruleType == StyleRule::Viewport) {
        parseSuccess = (RuntimeEnabledFeatures::cssViewportEnabled() || isUASheetBehavior(context.mode()))
            && parser.parseViewportDescriptor(resolvedProperty, important);
    } else if (ruleType == StyleRule::FontFace) {
        parseSuccess = parser.parseFontFaceDescriptor(resolvedProperty);
    } else {
        parseSuccess = parser.parseValueStart(unresolvedProperty, important);
    }

    // This doesn't count UA style sheets
    if (parseSuccess && context.useCounter())
        context.useCounter()->count(context.mode(), unresolvedProperty);

    if (!parseSuccess)
        parsedProperties.shrink(parsedPropertiesSize);

    return parseSuccess;
}

CSSValue* CSSPropertyParser::parseSingleValue(
    CSSPropertyID property, const CSSParserTokenRange& range, const CSSParserContext& context)
{
    if (hasInvalidNumericValues(range))
        return nullptr;
    CSSPropertyParser parser(range, context, nullptr);
    CSSValue* value = parser.parseSingleValue(property);
    if (!value || !parser.m_range.atEnd())
        return nullptr;
    return value;
}

bool CSSPropertyParser::isValidNumericValue(double value)
{
    return std::isfinite(value)
        && value >= -std::numeric_limits<float>::max()
        && value <= std::numeric_limits<float>::max();
}

bool CSSPropertyParser::parseValueStart(CSSPropertyID unresolvedProperty, bool important)
{
    if (consumeCSSWideKeyword(unresolvedProperty, important))
        return true;

    CSSParserTokenRange originalRange = m_range;
    CSSPropertyID propertyId = resolveCSSPropertyID(unresolvedProperty);

    if (isShorthandProperty(propertyId)) {
        if (parseShorthand(unresolvedProperty, important))
            return true;
    } else {
        if (CSSValue* parsedValue = parseSingleValue(unresolvedProperty)) {
            if (m_range.atEnd()) {
                addProperty(propertyId, parsedValue, important);
                return true;
            }
        }
    }

    if (RuntimeEnabledFeatures::cssVariablesEnabled() && CSSVariableParser::containsValidVariableReferences(originalRange)) {
        // We don't expand the shorthand here because crazypants.
        CSSVariableReferenceValue* variable = CSSVariableReferenceValue::create(CSSVariableData::create(originalRange));
        addProperty(propertyId, variable, important);
        return true;
    }

    return false;
}

bool CSSPropertyParser::isColorKeyword(CSSValueID id)
{
    // Named colors and color keywords:
    //
    // <named-color>
    //   'aqua', 'black', 'blue', ..., 'yellow' (CSS3: "basic color keywords")
    //   'aliceblue', ..., 'yellowgreen'        (CSS3: "extended color keywords")
    //   'transparent'
    //
    // 'currentcolor'
    //
    // <deprecated-system-color>
    //   'ActiveBorder', ..., 'WindowText'
    //
    // WebKit proprietary/internal:
    //   '-webkit-link'
    //   '-webkit-activelink'
    //   '-internal-active-list-box-selection'
    //   '-internal-active-list-box-selection-text'
    //   '-internal-inactive-list-box-selection'
    //   '-internal-inactive-list-box-selection-text'
    //   '-webkit-focus-ring-color'
    //   '-internal-quirk-inherit'
    //
    return (id >= CSSValueAqua && id <= CSSValueInternalQuirkInherit)
        || (id >= CSSValueAliceblue && id <= CSSValueYellowgreen)
        || id == CSSValueMenu;
}

bool CSSPropertyParser::isSystemColor(CSSValueID id)
{
    return (id >= CSSValueActiveborder && id <= CSSValueWindowtext) || id == CSSValueMenu;
}

template <typename CharacterType>
static CSSPropertyID unresolvedCSSPropertyID(const CharacterType* propertyName, unsigned length)
{
    char buffer[maxCSSPropertyNameLength + 1]; // 1 for null character

    for (unsigned i = 0; i != length; ++i) {
        CharacterType c = propertyName[i];
        if (c == 0 || c >= 0x7F)
            return CSSPropertyInvalid; // illegal character
        buffer[i] = toASCIILower(c);
    }
    buffer[length] = '\0';

    const char* name = buffer;
    const Property* hashTableEntry = findProperty(name, length);
    if (!hashTableEntry)
        return CSSPropertyInvalid;
    CSSPropertyID property = static_cast<CSSPropertyID>(hashTableEntry->id);
    if (!CSSPropertyMetadata::isEnabledProperty(property))
        return CSSPropertyInvalid;
    return property;
}

CSSPropertyID unresolvedCSSPropertyID(const String& string)
{
    unsigned length = string.length();

    if (!length)
        return CSSPropertyInvalid;
    if (length > maxCSSPropertyNameLength)
        return CSSPropertyInvalid;

    return string.is8Bit() ? unresolvedCSSPropertyID(string.characters8(), length) : unresolvedCSSPropertyID(string.characters16(), length);
}

CSSPropertyID unresolvedCSSPropertyID(const CSSParserString& string)
{
    unsigned length = string.length();

    if (!length)
        return CSSPropertyInvalid;
    if (length > maxCSSPropertyNameLength)
        return CSSPropertyInvalid;

    return string.is8Bit() ? unresolvedCSSPropertyID(string.characters8(), length) : unresolvedCSSPropertyID(string.characters16(), length);
}

template <typename CharacterType>
static CSSValueID cssValueKeywordID(const CharacterType* valueKeyword, unsigned length)
{
    char buffer[maxCSSValueKeywordLength + 1]; // 1 for null character

    for (unsigned i = 0; i != length; ++i) {
        CharacterType c = valueKeyword[i];
        if (c == 0 || c >= 0x7F)
            return CSSValueInvalid; // illegal character
        buffer[i] = WTF::toASCIILower(c);
    }
    buffer[length] = '\0';

    const Value* hashTableEntry = findValue(buffer, length);
    return hashTableEntry ? static_cast<CSSValueID>(hashTableEntry->id) : CSSValueInvalid;
}

CSSValueID cssValueKeywordID(const CSSParserString& string)
{
    unsigned length = string.length();
    if (!length)
        return CSSValueInvalid;
    if (length > maxCSSValueKeywordLength)
        return CSSValueInvalid;

    return string.is8Bit() ? cssValueKeywordID(string.characters8(), length) : cssValueKeywordID(string.characters16(), length);
}

bool CSSPropertyParser::consumeCSSWideKeyword(CSSPropertyID unresolvedProperty, bool important)
{
    CSSParserTokenRange rangeCopy = m_range;
    CSSValueID id = rangeCopy.consumeIncludingWhitespace().id();
    if (!rangeCopy.atEnd())
        return false;

    CSSValue* value = nullptr;
    if (id == CSSValueInitial)
        value = cssValuePool().createExplicitInitialValue();
    else if (id == CSSValueInherit)
        value = cssValuePool().createInheritedValue();
    else if (id == CSSValueUnset)
        value = cssValuePool().createUnsetValue();
    else
        return false;

    addExpandedPropertyForValue(resolveCSSPropertyID(unresolvedProperty), value, important);
    m_range = rangeCopy;
    return true;
}

static CSSValueList* consumeTransformOrigin(CSSParserTokenRange& range, CSSParserMode cssParserMode, UnitlessQuirk unitless)
{
    CSSValue* resultX = nullptr;
    CSSValue* resultY = nullptr;
    if (consumeOneOrTwoValuedPosition(range, cssParserMode, unitless, resultX, resultY)) {
        CSSValueList* list = CSSValueList::createSpaceSeparated();
        list->append(resultX);
        list->append(resultY);
        CSSValue* resultZ = consumeLength(range, cssParserMode, ValueRangeAll);
        if (!resultZ)
            resultZ = cssValuePool().createValue(0, CSSPrimitiveValue::UnitType::Pixels);
        list->append(resultZ);
        return list;
    }
    return nullptr;
}

// Methods for consuming non-shorthand properties starts here.
static CSSValue* consumeWillChange(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueAuto)
        return consumeIdent(range);

    CSSValueList* values = CSSValueList::createCommaSeparated();
    // Every comma-separated list of identifiers is a valid will-change value,
    // unless the list includes an explicitly disallowed identifier.
    while (true) {
        if (range.peek().type() != IdentToken)
            return nullptr;
        CSSPropertyID unresolvedProperty = unresolvedCSSPropertyID(range.peek().value());
        if (unresolvedProperty) {
            ASSERT(CSSPropertyMetadata::isEnabledProperty(unresolvedProperty));
            // Now "all" is used by both CSSValue and CSSPropertyValue.
            // Need to return nullptr when currentValue is CSSPropertyAll.
            if (unresolvedProperty == CSSPropertyWillChange || unresolvedProperty == CSSPropertyAll)
                return nullptr;
            values->append(cssValuePool().createIdentifierValue(unresolvedProperty));
            range.consumeIncludingWhitespace();
        } else {
            switch (range.peek().id()) {
            case CSSValueNone:
            case CSSValueAll:
            case CSSValueAuto:
            case CSSValueDefault:
            case CSSValueInitial:
            case CSSValueInherit:
                return nullptr;
            case CSSValueContents:
            case CSSValueScrollPosition:
                values->append(consumeIdent(range));
                break;
            default:
                range.consumeIncludingWhitespace();
                break;
            }
        }

        if (range.atEnd())
            break;
        if (!consumeCommaIncludingWhitespace(range))
            return nullptr;
    }

    return values;
}

static CSSFontFeatureValue* consumeFontFeatureTag(CSSParserTokenRange& range)
{
    // Feature tag name consists of 4-letter characters.
    static const unsigned tagNameLength = 4;

    const CSSParserToken& token = range.consumeIncludingWhitespace();
    // Feature tag name comes first
    if (token.type() != StringToken)
        return nullptr;
    if (token.value().length() != tagNameLength)
        return nullptr;
    AtomicString tag = token.value();
    for (unsigned i = 0; i < tagNameLength; ++i) {
        // Limits the range of characters to 0x20-0x7E, following the tag name rules defiend in the OpenType specification.
        UChar character = tag[i];
        if (character < 0x20 || character > 0x7E)
            return nullptr;
    }

    int tagValue = 1;
    // Feature tag values could follow: <integer> | on | off
    if (range.peek().type() == NumberToken && range.peek().numericValueType() == IntegerValueType && range.peek().numericValue() >= 0) {
        tagValue = clampTo<int>(range.consumeIncludingWhitespace().numericValue());
        if (tagValue < 0)
            return nullptr;
    } else if (range.peek().id() == CSSValueOn || range.peek().id() == CSSValueOff) {
        tagValue = range.consumeIncludingWhitespace().id() == CSSValueOn;
    }
    return CSSFontFeatureValue::create(tag, tagValue);
}

static CSSValue* consumeFontFeatureSettings(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueNormal)
        return consumeIdent(range);
    CSSValueList* settings = CSSValueList::createCommaSeparated();
    do {
        CSSFontFeatureValue* fontFeatureValue = consumeFontFeatureTag(range);
        if (!fontFeatureValue)
            return nullptr;
        settings->append(fontFeatureValue);
    } while (consumeCommaIncludingWhitespace(range));
    return settings;
}

static CSSValue* consumePage(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueAuto)
        return consumeIdent(range);
    return consumeCustomIdent(range);
}

static CSSValue* consumeQuotes(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);
    CSSValueList* values = CSSValueList::createSpaceSeparated();
    while (!range.atEnd()) {
        CSSStringValue* parsedValue = consumeString(range);
        if (!parsedValue)
            return nullptr;
        values->append(parsedValue);
    }
    if (values->length() && values->length() % 2 == 0)
        return values;
    return nullptr;
}

static CSSValue* consumeWebkitHighlight(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);
    return consumeString(range);
}

static CSSValue* consumeFontVariantLigatures(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueNormal)
        return consumeIdent(range);
    CSSValueList* ligatureValues = CSSValueList::createSpaceSeparated();
    bool sawCommonLigaturesValue = false;
    bool sawDiscretionaryLigaturesValue = false;
    bool sawHistoricalLigaturesValue = false;
    bool sawContextualLigaturesValue = false;
    do {
        CSSValueID id = range.peek().id();
        switch (id) {
        case CSSValueNoCommonLigatures:
        case CSSValueCommonLigatures:
            if (sawCommonLigaturesValue)
                return nullptr;
            sawCommonLigaturesValue = true;
            break;
        case CSSValueNoDiscretionaryLigatures:
        case CSSValueDiscretionaryLigatures:
            if (sawDiscretionaryLigaturesValue)
                return nullptr;
            sawDiscretionaryLigaturesValue = true;
            break;
        case CSSValueNoHistoricalLigatures:
        case CSSValueHistoricalLigatures:
            if (sawHistoricalLigaturesValue)
                return nullptr;
            sawHistoricalLigaturesValue = true;
            break;
        case CSSValueNoContextual:
        case CSSValueContextual:
            if (sawContextualLigaturesValue)
                return nullptr;
            sawContextualLigaturesValue = true;
            break;
        default:
            return nullptr;
        }
        ligatureValues->append(consumeIdent(range));
    } while (!range.atEnd());

    return ligatureValues;
}

static CSSPrimitiveValue* consumeFontVariant(CSSParserTokenRange& range)
{
    return consumeIdent<CSSValueNormal, CSSValueSmallCaps>(range);
}

static CSSValue* consumeFontVariantList(CSSParserTokenRange& range)
{
    CSSValueList* values = CSSValueList::createCommaSeparated();
    do {
        if (range.peek().id() == CSSValueAll) {
            // FIXME: CSSPropertyParser::parseFontVariant() implements
            // the old css3 draft:
            // http://www.w3.org/TR/2002/WD-css3-webfonts-20020802/#font-variant
            // 'all' is only allowed in @font-face and with no other values.
            if (values->length())
                return nullptr;
            return consumeIdent(range);
        }
        CSSPrimitiveValue* fontVariant = consumeFontVariant(range);
        if (fontVariant)
            values->append(fontVariant);
    } while (consumeCommaIncludingWhitespace(range));

    if (values->length())
        return values;

    return nullptr;
}

static CSSPrimitiveValue* consumeFontWeight(CSSParserTokenRange& range)
{
    const CSSParserToken& token = range.peek();
    if (token.id() >= CSSValueNormal && token.id() <= CSSValueLighter)
        return consumeIdent(range);
    if (token.type() != NumberToken || token.numericValueType() != IntegerValueType)
        return nullptr;
    int weight = static_cast<int>(token.numericValue());
    if ((weight % 100) || weight < 100 || weight > 900)
        return nullptr;
    range.consumeIncludingWhitespace();
    return cssValuePool().createIdentifierValue(static_cast<CSSValueID>(CSSValue100 + weight / 100 - 1));
}

static String concatenateFamilyName(CSSParserTokenRange& range)
{
    StringBuilder builder;
    bool addedSpace = false;
    const CSSParserToken& firstToken = range.peek();
    while (range.peek().type() == IdentToken) {
        if (!builder.isEmpty()) {
            builder.append(' ');
            addedSpace = true;
        }
        builder.append(range.consumeIncludingWhitespace().value());
    }
    if (!addedSpace && isCSSWideKeyword(firstToken.id()))
        return String();
    return builder.toString();
}

static CSSValue* consumeFamilyName(CSSParserTokenRange& range)
{
    if (range.peek().type() == StringToken)
        return cssValuePool().createFontFamilyValue(range.consumeIncludingWhitespace().value());
    if (range.peek().type() != IdentToken)
        return nullptr;
    String familyName = concatenateFamilyName(range);
    if (familyName.isNull())
        return nullptr;
    return cssValuePool().createFontFamilyValue(familyName);
}

static CSSValue* consumeGenericFamily(CSSParserTokenRange& range)
{
    return consumeIdentRange(range, CSSValueSerif, CSSValueWebkitBody);
}

static CSSValueList* consumeFontFamily(CSSParserTokenRange& range)
{
    CSSValueList* list = CSSValueList::createCommaSeparated();
    do {
        CSSValue* parsedValue = consumeGenericFamily(range);
        if (parsedValue) {
            list->append(parsedValue);
        } else {
            parsedValue = consumeFamilyName(range);
            if (parsedValue) {
                list->append(parsedValue);
            } else {
                return nullptr;
            }
        }
    } while (consumeCommaIncludingWhitespace(range));
    return list;
}

static CSSValue* consumeSpacing(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    if (range.peek().id() == CSSValueNormal)
        return consumeIdent(range);
    // TODO(timloh): Don't allow unitless values, and allow <percentage>s in word-spacing.
    return consumeLength(range, cssParserMode, ValueRangeAll, UnitlessQuirk::Allow);
}

static CSSValue* consumeTabSize(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    CSSPrimitiveValue* parsedValue = consumeInteger(range, 0);
    if (parsedValue)
        return parsedValue;
    return consumeLength(range, cssParserMode, ValueRangeNonNegative);
}

static CSSValue* consumeFontSize(CSSParserTokenRange& range, CSSParserMode cssParserMode, UnitlessQuirk unitless = UnitlessQuirk::Forbid)
{
    if (range.peek().id() >= CSSValueXxSmall && range.peek().id() <= CSSValueLarger)
        return consumeIdent(range);
    return consumeLengthOrPercent(range, cssParserMode, ValueRangeNonNegative, unitless);
}

static CSSPrimitiveValue* consumeLineHeight(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    if (range.peek().id() == CSSValueNormal)
        return consumeIdent(range);

    CSSPrimitiveValue* lineHeight = consumeNumber(range, ValueRangeNonNegative);
    if (lineHeight)
        return lineHeight;
    return consumeLengthOrPercent(range, cssParserMode, ValueRangeNonNegative);
}

static CSSValueList* consumeRotation(CSSParserTokenRange& range)
{
    ASSERT(RuntimeEnabledFeatures::cssIndependentTransformPropertiesEnabled());
    CSSValueList* list = CSSValueList::createSpaceSeparated();

    CSSValue* rotation = consumeAngle(range);
    if (!rotation)
        return nullptr;
    list->append(rotation);

    if (range.atEnd())
        return list;

    for (unsigned i = 0; i < 3; i++) { // 3 dimensions of rotation
        CSSValue* dimension = consumeNumber(range, ValueRangeAll);
        if (!dimension)
            return nullptr;
        list->append(dimension);
    }

    return list;
}

static CSSValueList* consumeScale(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    ASSERT(RuntimeEnabledFeatures::cssIndependentTransformPropertiesEnabled());

    CSSValue* scale = consumeNumber(range, ValueRangeAll);
    if (!scale)
        return nullptr;
    CSSValueList* list = CSSValueList::createSpaceSeparated();
    list->append(scale);
    scale = consumeNumber(range, ValueRangeAll);
    if (scale) {
        list->append(scale);
        scale = consumeNumber(range, ValueRangeAll);
        if (scale)
            list->append(scale);
    }

    return list;
}

static CSSValueList* consumeTranslate(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    ASSERT(RuntimeEnabledFeatures::cssIndependentTransformPropertiesEnabled());
    CSSValue* translate = consumeLengthOrPercent(range, cssParserMode, ValueRangeAll);
    if (!translate)
        return nullptr;
    CSSValueList* list = CSSValueList::createSpaceSeparated();
    list->append(translate);
    translate = consumeLengthOrPercent(range, cssParserMode, ValueRangeAll);
    if (translate) {
        list->append(translate);
        translate = consumeLength(range, cssParserMode, ValueRangeAll);
        if (translate)
            list->append(translate);
    }

    return list;
}

static CSSValue* consumeCounter(CSSParserTokenRange& range, CSSParserMode cssParserMode, int defaultValue)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);

    CSSValueList* list = CSSValueList::createSpaceSeparated();
    do {
        CSSCustomIdentValue* counterName = consumeCustomIdent(range);
        if (!counterName)
            return nullptr;
        int i = defaultValue;
        if (CSSPrimitiveValue* counterValue = consumeInteger(range))
            i = clampTo<int>(counterValue->getDoubleValue());
        list->append(CSSValuePair::create(counterName,
            cssValuePool().createValue(i, CSSPrimitiveValue::UnitType::Integer),
            CSSValuePair::DropIdenticalValues));
    } while (!range.atEnd());
    return list;
}

static CSSValue* consumePageSize(CSSParserTokenRange& range)
{
    return consumeIdent<CSSValueA3, CSSValueA4, CSSValueA5, CSSValueB4, CSSValueB5, CSSValueLedger, CSSValueLegal, CSSValueLetter>(range);
}

static CSSValueList* consumeSize(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    CSSValueList* result = CSSValueList::createSpaceSeparated();

    if (range.peek().id() == CSSValueAuto) {
        result->append(consumeIdent(range));
        return result;
    }

    if (CSSValue* width = consumeLength(range, cssParserMode, ValueRangeNonNegative)) {
        CSSValue* height = consumeLength(range, cssParserMode, ValueRangeNonNegative);
        result->append(width);
        if (height)
            result->append(height);
        return result;
    }

    CSSValue* pageSize = consumePageSize(range);
    CSSValue* orientation = consumeIdent<CSSValuePortrait, CSSValueLandscape>(range);
    if (!pageSize)
        pageSize = consumePageSize(range);

    if (!orientation && !pageSize)
        return nullptr;
    if (pageSize)
        result->append(pageSize);
    if (orientation)
        result->append(orientation);
    return result;
}

static CSSValue* consumeSnapHeight(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    CSSPrimitiveValue* unit = consumeLength(range, cssParserMode, ValueRangeNonNegative);
    if (!unit)
        return nullptr;
    CSSValueList* list = CSSValueList::createSpaceSeparated();
    list->append(unit);

    if (CSSPrimitiveValue* position = consumePositiveInteger(range)) {
        if (position->getIntValue() > 100)
            return nullptr;
        list->append(position);
    }

    return list;
}

static CSSValue* consumeTextIndent(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    // [ <length> | <percentage> ] && hanging? && each-line?
    // Keywords only allowed when css3Text is enabled.
    CSSValueList* list = CSSValueList::createSpaceSeparated();

    bool hasLengthOrPercentage = false;
    bool hasEachLine = false;
    bool hasHanging = false;

    do {
        if (!hasLengthOrPercentage) {
            if (CSSValue* textIndent = consumeLengthOrPercent(range, cssParserMode, ValueRangeAll, UnitlessQuirk::Allow)) {
                list->append(textIndent);
                hasLengthOrPercentage = true;
                continue;
            }
        }

        if (RuntimeEnabledFeatures::css3TextEnabled()) {
            CSSValueID id = range.peek().id();
            if (!hasEachLine && id == CSSValueEachLine) {
                list->append(consumeIdent(range));
                hasEachLine = true;
                continue;
            }
            if (!hasHanging && id == CSSValueHanging) {
                list->append(consumeIdent(range));
                hasHanging = true;
                continue;
            }
        }
        return nullptr;
    } while (!range.atEnd());

    if (!hasLengthOrPercentage)
        return nullptr;

    return list;
}

static bool validWidthOrHeightKeyword(CSSValueID id, const CSSParserContext& context)
{
    if (id == CSSValueWebkitMinContent || id == CSSValueWebkitMaxContent || id == CSSValueWebkitFillAvailable || id == CSSValueWebkitFitContent
        || id == CSSValueMinContent || id == CSSValueMaxContent || id == CSSValueFitContent) {
        if (context.useCounter()) {
            switch (id) {
            case CSSValueWebkitMinContent:
                context.useCounter()->count(UseCounter::CSSValuePrefixedMinContent);
                break;
            case CSSValueWebkitMaxContent:
                context.useCounter()->count(UseCounter::CSSValuePrefixedMaxContent);
                break;
            case CSSValueWebkitFillAvailable:
                context.useCounter()->count(UseCounter::CSSValuePrefixedFillAvailable);
                break;
            case CSSValueWebkitFitContent:
                context.useCounter()->count(UseCounter::CSSValuePrefixedFitContent);
                break;
            default:
                break;
            }
        }
        return true;
    }
    return false;
}

static CSSValue* consumeMaxWidthOrHeight(CSSParserTokenRange& range, const CSSParserContext& context, UnitlessQuirk unitless = UnitlessQuirk::Forbid)
{
    if (range.peek().id() == CSSValueNone || validWidthOrHeightKeyword(range.peek().id(), context))
        return consumeIdent(range);
    return consumeLengthOrPercent(range, context.mode(), ValueRangeNonNegative, unitless);
}

static CSSValue* consumeWidthOrHeight(CSSParserTokenRange& range, const CSSParserContext& context, UnitlessQuirk unitless = UnitlessQuirk::Forbid)
{
    if (range.peek().id() == CSSValueAuto || validWidthOrHeightKeyword(range.peek().id(), context))
        return consumeIdent(range);
    return consumeLengthOrPercent(range, context.mode(), ValueRangeNonNegative, unitless);
}

static CSSValue* consumeMarginOrOffset(CSSParserTokenRange& range, CSSParserMode cssParserMode, UnitlessQuirk unitless)
{
    if (range.peek().id() == CSSValueAuto)
        return consumeIdent(range);
    return consumeLengthOrPercent(range, cssParserMode, ValueRangeAll, unitless);
}

static CSSPrimitiveValue* consumeClipComponent(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    if (range.peek().id() == CSSValueAuto)
        return consumeIdent(range);
    return consumeLength(range, cssParserMode, ValueRangeAll, UnitlessQuirk::Allow);
}

static CSSValue* consumeClip(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    if (range.peek().id() == CSSValueAuto)
        return consumeIdent(range);

    if (range.peek().functionId() != CSSValueRect)
        return nullptr;

    CSSParserTokenRange args = consumeFunction(range);
    // rect(t, r, b, l) || rect(t r b l)
    CSSPrimitiveValue* top = consumeClipComponent(args, cssParserMode);
    if (!top)
        return nullptr;
    bool needsComma = consumeCommaIncludingWhitespace(args);
    CSSPrimitiveValue* right = consumeClipComponent(args, cssParserMode);
    if (!right || (needsComma && !consumeCommaIncludingWhitespace(args)))
        return nullptr;
    CSSPrimitiveValue* bottom = consumeClipComponent(args, cssParserMode);
    if (!bottom || (needsComma && !consumeCommaIncludingWhitespace(args)))
        return nullptr;
    CSSPrimitiveValue* left = consumeClipComponent(args, cssParserMode);
    if (!left || !args.atEnd())
        return nullptr;
    return CSSQuadValue::create(top, right, bottom, left, CSSQuadValue::SerializeAsRect);
}

static bool consumePan(CSSParserTokenRange& range, CSSValue*& panX, CSSValue*& panY)
{
    CSSValueID id = range.peek().id();
    if ((id == CSSValuePanX || id == CSSValuePanRight || id == CSSValuePanLeft) && !panX) {
        if (id != CSSValuePanX && !RuntimeEnabledFeatures::cssTouchActionPanDirectionsEnabled())
            return false;
        panX = consumeIdent(range);
    } else if ((id == CSSValuePanY || id == CSSValuePanDown || id == CSSValuePanUp) && !panY) {
        if (id != CSSValuePanY && !RuntimeEnabledFeatures::cssTouchActionPanDirectionsEnabled())
            return false;
        panY = consumeIdent(range);
    } else {
        return false;
    }
    return true;
}

static CSSValue* consumeTouchAction(CSSParserTokenRange& range)
{
    CSSValueList* list = CSSValueList::createSpaceSeparated();
    CSSValueID id = range.peek().id();
    if (id == CSSValueAuto || id == CSSValueNone || id == CSSValueManipulation) {
        list->append(consumeIdent(range));
        return list;
    }

    CSSValue* panX = nullptr;
    CSSValue* panY = nullptr;
    if (!consumePan(range, panX, panY))
        return nullptr;
    if (!range.atEnd() && !consumePan(range, panX, panY))
        return nullptr;

    if (panX)
        list->append(panX);
    if (panY)
        list->append(panY);
    return list;
}

static CSSPrimitiveValue* consumeLineClamp(CSSParserTokenRange& range)
{
    if (range.peek().type() != PercentageToken && range.peek().type() != NumberToken)
        return nullptr;
    CSSPrimitiveValue* clampValue = consumePercent(range, ValueRangeNonNegative);
    if (clampValue)
        return clampValue;
    // When specifying number of lines, don't allow 0 as a valid value.
    return consumePositiveInteger(range);
}

static CSSValue* consumeLocale(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueAuto)
        return consumeIdent(range);
    return consumeString(range);
}

static CSSValue* consumeColumnWidth(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueAuto)
        return consumeIdent(range);
    // Always parse lengths in strict mode here, since it would be ambiguous otherwise when used in
    // the 'columns' shorthand property.
    CSSPrimitiveValue* columnWidth = consumeLength(range, HTMLStandardMode, ValueRangeNonNegative);
    if (!columnWidth || (!columnWidth->isCalculated() && columnWidth->getDoubleValue() == 0))
        return nullptr;
    return columnWidth;
}

static CSSValue* consumeColumnCount(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueAuto)
        return consumeIdent(range);
    return consumePositiveInteger(range);
}

static CSSValue* consumeColumnGap(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    if (range.peek().id() == CSSValueNormal)
        return consumeIdent(range);
    return consumeLength(range, cssParserMode, ValueRangeNonNegative);
}

static CSSValue* consumeColumnSpan(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    return consumeIdent<CSSValueAll, CSSValueNone>(range);
}

static CSSValue* consumeZoom(CSSParserTokenRange& range, const CSSParserContext& context)
{
    const CSSParserToken& token = range.peek();
    CSSPrimitiveValue* zoom = nullptr;
    if (token.type() == IdentToken) {
        zoom = consumeIdent<CSSValueNormal, CSSValueReset, CSSValueDocument>(range);
    } else {
        zoom = consumePercent(range, ValueRangeNonNegative);
        if (!zoom)
            zoom = consumeNumber(range, ValueRangeNonNegative);
    }
    if (zoom && context.useCounter()
        && !(token.id() == CSSValueNormal
            || (token.type() == NumberToken && zoom->getDoubleValue() == 1)
            || (token.type() == PercentageToken && zoom->getDoubleValue() == 100)))
        context.useCounter()->count(UseCounter::CSSZoomNotEqualToOne);
    return zoom;
}

static CSSValue* consumeAnimationIterationCount(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueInfinite)
        return consumeIdent(range);
    return consumeNumber(range, ValueRangeNonNegative);
}

static CSSValue* consumeAnimationName(CSSParserTokenRange& range, const CSSParserContext& context, bool allowQuotedName)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);

    if (allowQuotedName && range.peek().type() == StringToken) {
        // Legacy support for strings in prefixed animations.
        if (context.useCounter())
            context.useCounter()->count(UseCounter::QuotedAnimationName);

        const CSSParserToken& token = range.consumeIncludingWhitespace();
        if (token.valueEqualsIgnoringASCIICase("none"))
            return cssValuePool().createIdentifierValue(CSSValueNone);
        return CSSCustomIdentValue::create(token.value());
    }

    return consumeCustomIdent(range);
}

static CSSValue* consumeTransitionProperty(CSSParserTokenRange& range)
{
    const CSSParserToken& token = range.peek();
    if (token.type() != IdentToken)
        return nullptr;
    if (token.id() == CSSValueNone)
        return consumeIdent(range);

    if (CSSPropertyID property = token.parseAsUnresolvedCSSPropertyID()) {
        ASSERT(CSSPropertyMetadata::isEnabledProperty(property));
        range.consumeIncludingWhitespace();
        return cssValuePool().createIdentifierValue(property);
    }
    return consumeCustomIdent(range);
}

static CSSValue* consumeSteps(CSSParserTokenRange& range)
{
    ASSERT(range.peek().functionId() == CSSValueSteps);
    CSSParserTokenRange rangeCopy = range;
    CSSParserTokenRange args = consumeFunction(rangeCopy);

    CSSPrimitiveValue* steps = consumePositiveInteger(args);
    if (!steps)
        return nullptr;

    StepsTimingFunction::StepAtPosition position = StepsTimingFunction::End;
    if (consumeCommaIncludingWhitespace(args)) {
        switch (args.consumeIncludingWhitespace().id()) {
        case CSSValueMiddle:
            if (!RuntimeEnabledFeatures::webAnimationsAPIEnabled())
                return nullptr;
            position = StepsTimingFunction::Middle;
            break;
        case CSSValueStart:
            position = StepsTimingFunction::Start;
            break;
        case CSSValueEnd:
            position = StepsTimingFunction::End;
            break;
        default:
            return nullptr;
        }
    }

    if (!args.atEnd())
        return nullptr;

    range = rangeCopy;
    return CSSStepsTimingFunctionValue::create(steps->getIntValue(), position);
}

static CSSValue* consumeCubicBezier(CSSParserTokenRange& range)
{
    ASSERT(range.peek().functionId() == CSSValueCubicBezier);
    CSSParserTokenRange rangeCopy = range;
    CSSParserTokenRange args = consumeFunction(rangeCopy);

    double x1, y1, x2, y2;
    if (consumeNumberRaw(args, x1)
        && x1 >= 0 && x1 <= 1
        && consumeCommaIncludingWhitespace(args)
        && consumeNumberRaw(args, y1)
        && consumeCommaIncludingWhitespace(args)
        && consumeNumberRaw(args, x2)
        && x2 >= 0 && x2 <= 1
        && consumeCommaIncludingWhitespace(args)
        && consumeNumberRaw(args, y2)
        && args.atEnd()) {
        range = rangeCopy;
        return CSSCubicBezierTimingFunctionValue::create(x1, y1, x2, y2);
    }

    return nullptr;
}

static CSSValue* consumeAnimationTimingFunction(CSSParserTokenRange& range)
{
    CSSValueID id = range.peek().id();
    if (id == CSSValueEase || id == CSSValueLinear || id == CSSValueEaseIn
        || id == CSSValueEaseOut || id == CSSValueEaseInOut || id == CSSValueStepStart
        || id == CSSValueStepEnd || id == CSSValueStepMiddle)
        return consumeIdent(range);

    CSSValueID function = range.peek().functionId();
    if (function == CSSValueSteps)
        return consumeSteps(range);
    if (function == CSSValueCubicBezier)
        return consumeCubicBezier(range);
    return nullptr;
}

static CSSValue* consumeAnimationValue(CSSPropertyID property, CSSParserTokenRange& range, const CSSParserContext& context, bool useLegacyParsing)
{
    switch (property) {
    case CSSPropertyAnimationDelay:
    case CSSPropertyTransitionDelay:
        return consumeTime(range, ValueRangeAll);
    case CSSPropertyAnimationDirection:
        return consumeIdent<CSSValueNormal, CSSValueAlternate, CSSValueReverse, CSSValueAlternateReverse>(range);
    case CSSPropertyAnimationDuration:
    case CSSPropertyTransitionDuration:
        return consumeTime(range, ValueRangeNonNegative);
    case CSSPropertyAnimationFillMode:
        return consumeIdent<CSSValueNone, CSSValueForwards, CSSValueBackwards, CSSValueBoth>(range);
    case CSSPropertyAnimationIterationCount:
        return consumeAnimationIterationCount(range);
    case CSSPropertyAnimationName:
        return consumeAnimationName(range, context, useLegacyParsing);
    case CSSPropertyAnimationPlayState:
        return consumeIdent<CSSValueRunning, CSSValuePaused>(range);
    case CSSPropertyTransitionProperty:
        return consumeTransitionProperty(range);
    case CSSPropertyAnimationTimingFunction:
    case CSSPropertyTransitionTimingFunction:
        return consumeAnimationTimingFunction(range);
    default:
        ASSERT_NOT_REACHED();
        return nullptr;
    }
}

static bool isValidAnimationPropertyList(CSSPropertyID property, const CSSValueList& valueList)
{
    if (property != CSSPropertyTransitionProperty || valueList.length() < 2)
        return true;
    for (auto& value : valueList) {
        if (value->isPrimitiveValue() && toCSSPrimitiveValue(*value).isValueID()
            && toCSSPrimitiveValue(*value).getValueID() == CSSValueNone)
            return false;
    }
    return true;
}

static CSSValueList* consumeAnimationPropertyList(CSSPropertyID property, CSSParserTokenRange& range, const CSSParserContext& context, bool useLegacyParsing)
{
    CSSValueList* list = CSSValueList::createCommaSeparated();
    do {
        CSSValue* value = consumeAnimationValue(property, range, context, useLegacyParsing);
        if (!value)
            return nullptr;
        list->append(value);
    } while (consumeCommaIncludingWhitespace(range));
    if (!isValidAnimationPropertyList(property, *list))
        return nullptr;
    ASSERT(list->length());
    return list;
}

bool CSSPropertyParser::consumeAnimationShorthand(const StylePropertyShorthand& shorthand, bool useLegacyParsing, bool important)
{
    const unsigned longhandCount = shorthand.length();
    CSSValueList* longhands[8];
    ASSERT(longhandCount <= 8);
    for (size_t i = 0; i < longhandCount; ++i)
        longhands[i] = CSSValueList::createCommaSeparated();

    do {
        bool parsedLonghand[8] = { false };
        do {
            bool foundProperty = false;
            for (size_t i = 0; i < longhandCount; ++i) {
                if (parsedLonghand[i])
                    continue;

                if (CSSValue* value = consumeAnimationValue(shorthand.properties()[i], m_range, m_context, useLegacyParsing)) {
                    parsedLonghand[i] = true;
                    foundProperty = true;
                    longhands[i]->append(value);
                    break;
                }
            }
            if (!foundProperty)
                return false;
        } while (!m_range.atEnd() && m_range.peek().type() != CommaToken);

        // TODO(timloh): This will make invalid longhands, see crbug.com/386459
        for (size_t i = 0; i < longhandCount; ++i) {
            if (!parsedLonghand[i])
                longhands[i]->append(cssValuePool().createImplicitInitialValue());
            parsedLonghand[i] = false;
        }
    } while (consumeCommaIncludingWhitespace(m_range));

    for (size_t i = 0; i < longhandCount; ++i) {
        if (!isValidAnimationPropertyList(shorthand.properties()[i], *longhands[i]))
            return false;
    }

    for (size_t i = 0; i < longhandCount; ++i)
        addProperty(shorthand.properties()[i], longhands[i], important);

    return m_range.atEnd();
}

static CSSValue* consumeWidowsOrOrphans(CSSParserTokenRange& range)
{
    // Support for auto is non-standard and for backwards compatibility.
    if (range.peek().id() == CSSValueAuto)
        return consumeIdent(range);
    return consumePositiveInteger(range);
}

static CSSValue* consumeZIndex(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueAuto)
        return consumeIdent(range);
    return consumeInteger(range);
}

static CSSShadowValue* parseSingleShadow(CSSParserTokenRange& range, CSSParserMode cssParserMode, bool allowInset, bool allowSpread)
{
    CSSPrimitiveValue* style = nullptr;
    CSSValue* color = nullptr;

    if (range.atEnd())
        return nullptr;
    if (range.peek().id() == CSSValueInset) {
        if (!allowInset)
            return nullptr;
        style = consumeIdent(range);
    }
    color = consumeColor(range, cssParserMode);

    CSSPrimitiveValue* horizontalOffset = consumeLength(range, cssParserMode, ValueRangeAll);
    if (!horizontalOffset)
        return nullptr;

    CSSPrimitiveValue* verticalOffset = consumeLength(range, cssParserMode, ValueRangeAll);
    if (!verticalOffset)
        return nullptr;

    CSSPrimitiveValue* blurRadius = consumeLength(range, cssParserMode, ValueRangeAll);
    CSSPrimitiveValue* spreadDistance = nullptr;
    if (blurRadius) {
        // Blur radius must be non-negative.
        if (blurRadius->getDoubleValue() < 0)
            return nullptr;
        if (allowSpread)
            spreadDistance = consumeLength(range, cssParserMode, ValueRangeAll);
    }

    if (!range.atEnd()) {
        if (!color)
            color = consumeColor(range, cssParserMode);
        if (range.peek().id() == CSSValueInset) {
            if (!allowInset || style)
                return nullptr;
            style = consumeIdent(range);
        }
    }
    return CSSShadowValue::create(horizontalOffset, verticalOffset, blurRadius,
        spreadDistance, style, color);
}

static CSSValue* consumeShadow(CSSParserTokenRange& range, CSSParserMode cssParserMode, bool isBoxShadowProperty)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);

    CSSValueList* shadowValueList = CSSValueList::createCommaSeparated();
    do {
        if (CSSShadowValue* shadowValue = parseSingleShadow(range, cssParserMode, isBoxShadowProperty, isBoxShadowProperty))
            shadowValueList->append(shadowValue);
        else
            return nullptr;
    } while (consumeCommaIncludingWhitespace(range));
    return shadowValueList;
}

static CSSFunctionValue* consumeFilterFunction(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    CSSValueID filterType = range.peek().functionId();
    if (filterType < CSSValueInvert || filterType > CSSValueDropShadow)
        return nullptr;
    CSSParserTokenRange args = consumeFunction(range);
    CSSFunctionValue* filterValue = CSSFunctionValue::create(filterType);
    CSSValue* parsedValue = nullptr;

    if (filterType == CSSValueDropShadow) {
        parsedValue = parseSingleShadow(args, cssParserMode, false, false);
    } else {
        // TODO(timloh): Add UseCounters for empty filter arguments.
        if (args.atEnd())
            return filterValue;
        if (filterType == CSSValueBrightness) {
            // FIXME (crbug.com/397061): Support calc expressions like calc(10% + 0.5)
            parsedValue = consumePercent(args, ValueRangeAll);
            if (!parsedValue)
                parsedValue = consumeNumber(args, ValueRangeAll);
        } else if (filterType == CSSValueHueRotate) {
            parsedValue = consumeAngle(args);
        } else if (filterType == CSSValueBlur) {
            parsedValue = consumeLength(args, HTMLStandardMode, ValueRangeNonNegative);
        } else {
            // FIXME (crbug.com/397061): Support calc expressions like calc(10% + 0.5)
            parsedValue = consumePercent(args, ValueRangeNonNegative);
            if (!parsedValue)
                parsedValue = consumeNumber(args, ValueRangeNonNegative);
            if (parsedValue && filterType != CSSValueSaturate && filterType != CSSValueContrast) {
                double maxAllowed = toCSSPrimitiveValue(parsedValue)->isPercentage() ? 100.0 : 1.0;
                if (toCSSPrimitiveValue(parsedValue)->getDoubleValue() > maxAllowed)
                    return nullptr;
            }
        }
    }
    if (!parsedValue || !args.atEnd())
        return nullptr;
    filterValue->append(parsedValue);
    return filterValue;
}

static CSSValue* consumeFilter(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);

    CSSValueList* list = CSSValueList::createSpaceSeparated();
    do {
        String url = consumeUrl(range);
        CSSFunctionValue* filterValue = nullptr;
        if (!url.isNull()) {
            filterValue = CSSFunctionValue::create(CSSValueUrl);
            filterValue->append(CSSSVGDocumentValue::create(url));
        } else {
            filterValue = consumeFilterFunction(range, cssParserMode);
            if (!filterValue)
                return nullptr;
        }
        list->append(filterValue);
    } while (!range.atEnd());
    return list;
}

static CSSValue* consumeTextDecorationLine(CSSParserTokenRange& range)
{
    CSSValueID id = range.peek().id();
    if (id == CSSValueNone)
        return consumeIdent(range);

    CSSValueList* list = CSSValueList::createSpaceSeparated();
    while (true) {
        CSSPrimitiveValue* ident = consumeIdent<CSSValueBlink, CSSValueUnderline, CSSValueOverline, CSSValueLineThrough>(range);
        if (!ident)
            break;
        if (list->hasValue(ident))
            return nullptr;
        list->append(ident);
    }

    if (!list->length())
        return nullptr;
    return list;
}

// none | strict | [ layout || style || paint ]
static CSSValue* consumeContain(CSSParserTokenRange& range)
{
    CSSValueID id = range.peek().id();
    if (id == CSSValueNone)
        return consumeIdent(range);

    CSSValueList* list = CSSValueList::createSpaceSeparated();
    if (id == CSSValueStrict) {
        list->append(consumeIdent(range));
        return list;
    }
    while (true) {
        CSSPrimitiveValue* ident = consumeIdent<CSSValuePaint, CSSValueLayout, CSSValueStyle>(range);
        if (!ident)
            break;
        if (list->hasValue(ident))
            return nullptr;
        list->append(ident);
    }

    if (!list->length())
        return nullptr;
    return list;
}

static CSSValue* consumePath(CSSParserTokenRange& range)
{
    // FIXME: Add support for <url>, <basic-shape>, <geometry-box>.
    if (range.peek().functionId() != CSSValuePath)
        return nullptr;

    CSSParserTokenRange functionRange = range;
    CSSParserTokenRange functionArgs = consumeFunction(functionRange);

    if (functionArgs.peek().type() != StringToken)
        return nullptr;
    String pathString = functionArgs.consumeIncludingWhitespace().value();

    OwnPtr<SVGPathByteStream> byteStream = SVGPathByteStream::create();
    if (buildByteStreamFromString(pathString, *byteStream) != SVGParseStatus::NoError
        || !functionArgs.atEnd())
        return nullptr;

    range = functionRange;
    if (byteStream->isEmpty())
        return cssValuePool().createIdentifierValue(CSSValueNone);
    return CSSPathValue::create(byteStream.release());
}

static CSSValue* consumePathOrNone(CSSParserTokenRange& range)
{
    CSSValueID id = range.peek().id();
    if (id == CSSValueNone)
        return consumeIdent(range);

    return consumePath(range);
}

static CSSValue* consumeMotionRotation(CSSParserTokenRange& range)
{
    CSSValue* angle = consumeAngle(range);
    CSSValue* keyword = consumeIdent<CSSValueAuto, CSSValueReverse>(range);
    if (!angle && !keyword)
        return nullptr;

    if (!angle)
        angle = consumeAngle(range);

    CSSValueList* list = CSSValueList::createSpaceSeparated();
    if (keyword)
        list->append(keyword);
    if (angle)
        list->append(angle);
    return list;
}

static CSSValue* consumeTextEmphasisStyle(CSSParserTokenRange& range)
{
    CSSValueID id = range.peek().id();
    if (id == CSSValueNone)
        return consumeIdent(range);

    if (CSSValue* textEmphasisStyle = consumeString(range))
        return textEmphasisStyle;

    CSSPrimitiveValue* fill = consumeIdent<CSSValueFilled, CSSValueOpen>(range);
    CSSPrimitiveValue* shape = consumeIdent<CSSValueDot, CSSValueCircle, CSSValueDoubleCircle, CSSValueTriangle, CSSValueSesame>(range);
    if (!fill)
        fill = consumeIdent<CSSValueFilled, CSSValueOpen>(range);
    if (fill && shape) {
        CSSValueList* parsedValues = CSSValueList::createSpaceSeparated();
        parsedValues->append(fill);
        parsedValues->append(shape);
        return parsedValues;
    }
    if (fill)
        return fill;
    if (shape)
        return shape;
    return nullptr;
}

static CSSValue* consumeOutlineColor(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    // Outline color has "invert" as additional keyword.
    // Also, we want to allow the special focus color even in HTML Standard parsing mode.
    if (range.peek().id() == CSSValueInvert || range.peek().id() == CSSValueWebkitFocusRingColor)
        return consumeIdent(range);
    return consumeColor(range, cssParserMode);
}

static CSSPrimitiveValue* consumeLineWidth(CSSParserTokenRange& range, CSSParserMode cssParserMode, UnitlessQuirk unitless)
{
    CSSValueID id = range.peek().id();
    if (id == CSSValueThin || id == CSSValueMedium || id == CSSValueThick)
        return consumeIdent(range);
    return consumeLength(range, cssParserMode, ValueRangeNonNegative, unitless);
}

static CSSPrimitiveValue* consumeBorderWidth(CSSParserTokenRange& range, CSSParserMode cssParserMode, UnitlessQuirk unitless)
{
    return consumeLineWidth(range, cssParserMode, unitless);
}

static CSSPrimitiveValue* consumeTextStrokeWidth(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    return consumeLineWidth(range, cssParserMode, UnitlessQuirk::Forbid);
}

static CSSPrimitiveValue* consumeColumnRuleWidth(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    return consumeLineWidth(range, cssParserMode, UnitlessQuirk::Forbid);
}

static bool consumeTranslate3d(CSSParserTokenRange& args, CSSParserMode cssParserMode, CSSFunctionValue*& transformValue)
{
    unsigned numberOfArguments = 2;
    CSSValue* parsedValue = nullptr;
    do {
        parsedValue = consumeLengthOrPercent(args, cssParserMode, ValueRangeAll);
        if (!parsedValue)
            return false;
        transformValue->append(parsedValue);
        if (!consumeCommaIncludingWhitespace(args))
            return false;
    } while (--numberOfArguments);
    parsedValue = consumeLength(args, cssParserMode, ValueRangeAll);
    if (!parsedValue)
        return false;
    transformValue->append(parsedValue);
    return true;
}

static bool consumeNumbers(CSSParserTokenRange& args, CSSFunctionValue*& transformValue, unsigned numberOfArguments)
{
    do {
        CSSValue* parsedValue = consumeNumber(args, ValueRangeAll);
        if (!parsedValue)
            return false;
        transformValue->append(parsedValue);
        if (--numberOfArguments && !consumeCommaIncludingWhitespace(args))
            return false;
    } while (numberOfArguments);
    return true;
}

static bool consumePerspective(CSSParserTokenRange& args, CSSParserMode cssParserMode, CSSFunctionValue*& transformValue, bool useLegacyParsing)
{
    CSSPrimitiveValue* parsedValue = consumeLength(args, cssParserMode, ValueRangeNonNegative);
    if (!parsedValue && useLegacyParsing) {
        double perspective;
        if (!consumeNumberRaw(args, perspective) || perspective < 0)
            return false;
        parsedValue = cssValuePool().createValue(perspective, CSSPrimitiveValue::UnitType::Pixels);
    }
    if (!parsedValue)
        return false;
    transformValue->append(parsedValue);
    return true;
}

static CSSValue* consumeTransformValue(CSSParserTokenRange& range, CSSParserMode cssParserMode, bool useLegacyParsing)
{
    CSSValueID functionId = range.peek().functionId();
    if (functionId == CSSValueInvalid)
        return nullptr;
    CSSParserTokenRange args = consumeFunction(range);
    if (args.atEnd())
        return nullptr;
    CSSFunctionValue* transformValue = CSSFunctionValue::create(functionId);
    CSSValue* parsedValue = nullptr;
    switch (functionId) {
    case CSSValueRotate:
    case CSSValueRotateX:
    case CSSValueRotateY:
    case CSSValueRotateZ:
    case CSSValueSkewX:
    case CSSValueSkewY:
    case CSSValueSkew:
        parsedValue = consumeAngle(args);
        if (!parsedValue)
            return nullptr;
        if (functionId == CSSValueSkew && consumeCommaIncludingWhitespace(args)) {
            transformValue->append(parsedValue);
            parsedValue = consumeAngle(args);
            if (!parsedValue)
                return nullptr;
        }
        break;
    case CSSValueScaleX:
    case CSSValueScaleY:
    case CSSValueScaleZ:
    case CSSValueScale:
        parsedValue = consumeNumber(args, ValueRangeAll);
        if (!parsedValue)
            return nullptr;
        if (functionId == CSSValueScale && consumeCommaIncludingWhitespace(args)) {
            transformValue->append(parsedValue);
            parsedValue = consumeNumber(args, ValueRangeAll);
            if (!parsedValue)
                return nullptr;
        }
        break;
    case CSSValuePerspective:
        if (!consumePerspective(args, cssParserMode, transformValue, useLegacyParsing))
            return nullptr;
        break;
    case CSSValueTranslateX:
    case CSSValueTranslateY:
    case CSSValueTranslate:
        parsedValue = consumeLengthOrPercent(args, cssParserMode, ValueRangeAll);
        if (!parsedValue)
            return nullptr;
        if (functionId == CSSValueTranslate && consumeCommaIncludingWhitespace(args)) {
            transformValue->append(parsedValue);
            parsedValue = consumeLengthOrPercent(args, cssParserMode, ValueRangeAll);
            if (!parsedValue)
                return nullptr;
        }
        break;
    case CSSValueTranslateZ:
        parsedValue = consumeLength(args, cssParserMode, ValueRangeAll);
        break;
    case CSSValueMatrix:
    case CSSValueMatrix3d:
        if (!consumeNumbers(args, transformValue, (functionId == CSSValueMatrix3d) ? 16 : 6))
            return nullptr;
        break;
    case CSSValueScale3d:
        if (!consumeNumbers(args, transformValue, 3))
            return nullptr;
        break;
    case CSSValueRotate3d:
        if (!consumeNumbers(args, transformValue, 3) || !consumeCommaIncludingWhitespace(args))
            return nullptr;
        parsedValue = consumeAngle(args);
        if (!parsedValue)
            return nullptr;
        break;
    case CSSValueTranslate3d:
        if (!consumeTranslate3d(args, cssParserMode, transformValue))
            return nullptr;
        break;
    default:
        return nullptr;
    }
    if (parsedValue)
        transformValue->append(parsedValue);
    if (!args.atEnd())
        return nullptr;
    return transformValue;
}

static CSSValue* consumeTransform(CSSParserTokenRange& range, CSSParserMode cssParserMode, bool useLegacyParsing)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);

    CSSValueList* list = CSSValueList::createSpaceSeparated();
    do {
        CSSValue* parsedTransformValue = consumeTransformValue(range, cssParserMode, useLegacyParsing);
        if (!parsedTransformValue)
            return nullptr;
        list->append(parsedTransformValue);
    } while (!range.atEnd());

    return list;
}

template <CSSValueID start, CSSValueID end>
static CSSValue* consumePositionLonghand(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    if (range.peek().type() == IdentToken) {
        CSSValueID id = range.peek().id();
        int percent;
        if (id == start)
            percent = 0;
        else if (id == CSSValueCenter)
            percent = 50;
        else if (id == end)
            percent = 100;
        else
            return nullptr;
        range.consumeIncludingWhitespace();
        return cssValuePool().createValue(percent, CSSPrimitiveValue::UnitType::Percentage);
    }
    return consumeLengthOrPercent(range, cssParserMode, ValueRangeAll);
}

static CSSValue* consumePositionX(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    return consumePositionLonghand<CSSValueLeft, CSSValueRight>(range, cssParserMode);
}

static CSSValue* consumePositionY(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    return consumePositionLonghand<CSSValueTop, CSSValueBottom>(range, cssParserMode);
}

static CSSValue* consumePaintStroke(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);
    String url = consumeUrl(range);
    if (!url.isNull()) {
        CSSValue* parsedValue = nullptr;
        if (range.peek().id() == CSSValueNone)
            parsedValue = consumeIdent(range);
        else
            parsedValue = consumeColor(range, cssParserMode);
        if (parsedValue) {
            CSSValueList* values = CSSValueList::createSpaceSeparated();
            values->append(CSSURIValue::create(url));
            values->append(parsedValue);
            return values;
        }
        return CSSURIValue::create(url);
    }
    return consumeColor(range, cssParserMode);
}

static CSSValue* consumePaintOrder(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueNormal)
        return consumeIdent(range);

    Vector<CSSValueID, 3> paintTypeList;
    CSSPrimitiveValue* fill = nullptr;
    CSSPrimitiveValue* stroke = nullptr;
    CSSPrimitiveValue* markers = nullptr;
    do {
        CSSValueID id = range.peek().id();
        if (id == CSSValueFill && !fill)
            fill = consumeIdent(range);
        else if (id == CSSValueStroke && !stroke)
            stroke = consumeIdent(range);
        else if (id == CSSValueMarkers && !markers)
            markers = consumeIdent(range);
        else
            return nullptr;
        paintTypeList.append(id);
    } while (!range.atEnd());

    // After parsing we serialize the paint-order list. Since it is not possible to
    // pop a last list items from CSSValueList without bigger cost, we create the
    // list after parsing.
    CSSValueID firstPaintOrderType = paintTypeList.at(0);
    CSSValueList* paintOrderList = CSSValueList::createSpaceSeparated();
    switch (firstPaintOrderType) {
    case CSSValueFill:
    case CSSValueStroke:
        paintOrderList->append(firstPaintOrderType == CSSValueFill ? fill : stroke);
        if (paintTypeList.size() > 1) {
            if (paintTypeList.at(1) == CSSValueMarkers)
                paintOrderList->append(markers);
        }
        break;
    case CSSValueMarkers:
        paintOrderList->append(markers);
        if (paintTypeList.size() > 1) {
            if (paintTypeList.at(1) == CSSValueStroke)
                paintOrderList->append(stroke);
        }
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    return paintOrderList;
}

static CSSValue* consumeNoneOrURI(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);

    String url = consumeUrl(range);
    if (url.isNull())
        return nullptr;
    return CSSURIValue::create(url);
}

static CSSValue* consumeFlexBasis(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    // FIXME: Support intrinsic dimensions too.
    if (range.peek().id() == CSSValueAuto)
        return consumeIdent(range);
    return consumeLengthOrPercent(range, cssParserMode, ValueRangeNonNegative);
}

static CSSValue* consumeStrokeDasharray(CSSParserTokenRange& range)
{
    CSSValueID id = range.peek().id();
    if (id == CSSValueNone)
        return consumeIdent(range);

    CSSValueList* dashes = CSSValueList::createCommaSeparated();
    do {
        CSSPrimitiveValue* dash = consumeLengthOrPercent(range, SVGAttributeMode, ValueRangeNonNegative, UnitlessQuirk::Allow);
        if (!dash || (consumeCommaIncludingWhitespace(range) && range.atEnd()))
            return nullptr;
        dashes->append(dash);
    } while (!range.atEnd());
    return dashes;
}

static CSSPrimitiveValue* consumeBaselineShift(CSSParserTokenRange& range)
{
    CSSValueID id = range.peek().id();
    if (id == CSSValueBaseline || id == CSSValueSub || id == CSSValueSuper)
        return consumeIdent(range);
    return consumeLengthOrPercent(range, SVGAttributeMode, ValueRangeAll);
}

static CSSValue* createCSSImageValue(const AtomicString& rawValue, const CSSParserContext& context)
{
    CSSValue* imageValue = CSSImageValue::create(rawValue, context.completeURL(rawValue));
    return imageValue;
}

static CSSValue* consumeImageSet(CSSParserTokenRange& range, const CSSParserContext& context)
{
    CSSParserTokenRange rangeCopy = range;
    CSSParserTokenRange args = consumeFunction(rangeCopy);
    CSSImageSetValue* imageSet = CSSImageSetValue::create();
    do {
        AtomicString urlValue(consumeUrl(args));
        if (urlValue.isNull())
            return nullptr;

        CSSValue* image = createCSSImageValue(urlValue, context);
        imageSet->append(image);

        const CSSParserToken& token = args.consumeIncludingWhitespace();
        if (token.type() != DimensionToken)
            return nullptr;
        if (String(token.value()) != "x")
            return nullptr;
        ASSERT(token.unitType() == CSSPrimitiveValue::UnitType::Unknown);
        double imageScaleFactor = token.numericValue();
        if (imageScaleFactor <= 0)
            return nullptr;
        imageSet->append(cssValuePool().createValue(imageScaleFactor, CSSPrimitiveValue::UnitType::Number));
    } while (consumeCommaIncludingWhitespace(args));
    if (!args.atEnd())
        return nullptr;
    range = rangeCopy;
    return imageSet;
}

static CSSValue* consumeCursor(CSSParserTokenRange& range, const CSSParserContext& context, bool inQuirksMode)
{
    CSSValueList* list = nullptr;
    while (true) {
        CSSValue* image = nullptr;
        AtomicString uri(consumeUrl(range));
        if (!uri.isNull()) {
            image = createCSSImageValue(uri, context);
        } else if (range.peek().type() == FunctionToken && range.peek().functionId() == CSSValueWebkitImageSet) {
            image = consumeImageSet(range, context);
            if (!image)
                return nullptr;
        } else {
            break;
        }

        double num;
        IntPoint hotSpot(-1, -1);
        bool hotSpotSpecified = false;
        if (consumeNumberRaw(range, num)) {
            hotSpot.setX(int(num));
            if (!consumeNumberRaw(range, num))
                return nullptr;
            hotSpot.setY(int(num));
            hotSpotSpecified = true;
        }

        if (!list)
            list = CSSValueList::createCommaSeparated();

        list->append(CSSCursorImageValue::create(image, hotSpotSpecified, hotSpot));
        if (!consumeCommaIncludingWhitespace(range))
            return nullptr;
    }

    CSSValueID id = range.peek().id();
    if (!range.atEnd() && context.useCounter()) {
        if (id == CSSValueWebkitZoomIn)
            context.useCounter()->count(UseCounter::PrefixedCursorZoomIn);
        else if (id == CSSValueWebkitZoomOut)
            context.useCounter()->count(UseCounter::PrefixedCursorZoomOut);
    }
    CSSValue* cursorType = nullptr;
    if (id == CSSValueHand) {
        if (!inQuirksMode) // Non-standard behavior
            return nullptr;
        cursorType = cssValuePool().createIdentifierValue(CSSValuePointer);
        range.consumeIncludingWhitespace();
    } else if ((id >= CSSValueAuto && id <= CSSValueWebkitZoomOut) || id == CSSValueCopy || id == CSSValueNone) {
        cursorType = consumeIdent(range);
    } else {
        return nullptr;
    }

    if (!list)
        return cursorType;
    list->append(cursorType);
    return list;
}

// This should go away once we drop support for -webkit-gradient
static CSSPrimitiveValue* consumeDeprecatedGradientPoint(CSSParserTokenRange& args, bool horizontal)
{
    if (args.peek().type() == IdentToken) {
        if ((horizontal && consumeIdent<CSSValueLeft>(args)) || (!horizontal && consumeIdent<CSSValueTop>(args)))
            return cssValuePool().createValue(0., CSSPrimitiveValue::UnitType::Percentage);
        if ((horizontal && consumeIdent<CSSValueRight>(args)) || (!horizontal && consumeIdent<CSSValueBottom>(args)))
            return cssValuePool().createValue(100., CSSPrimitiveValue::UnitType::Percentage);
        if (consumeIdent<CSSValueCenter>(args))
            return cssValuePool().createValue(50., CSSPrimitiveValue::UnitType::Percentage);
        return nullptr;
    }
    CSSPrimitiveValue* result = consumePercent(args, ValueRangeAll);
    if (!result)
        result = consumeNumber(args, ValueRangeAll);
    return result;
}

// Used to parse colors for -webkit-gradient(...).
static CSSValue* consumeDeprecatedGradientStopColor(CSSParserTokenRange& args, CSSParserMode cssParserMode)
{
    if (args.peek().id() == CSSValueCurrentcolor)
        return nullptr;
    return consumeColor(args, cssParserMode);
}

static bool consumeDeprecatedGradientColorStop(CSSParserTokenRange& range, CSSGradientColorStop& stop, CSSParserMode cssParserMode)
{
    CSSValueID id = range.peek().functionId();
    if (id != CSSValueFrom && id != CSSValueTo && id != CSSValueColorStop)
        return false;

    CSSParserTokenRange args = consumeFunction(range);
    double position;
    if (id == CSSValueFrom || id == CSSValueTo) {
        position = (id == CSSValueFrom) ? 0 : 1;
    } else {
        ASSERT(id == CSSValueColorStop);
        const CSSParserToken& arg = args.consumeIncludingWhitespace();
        if (arg.type() == PercentageToken)
            position = arg.numericValue() / 100.0;
        else if (arg.type() == NumberToken)
            position = arg.numericValue();
        else
            return false;

        if (!consumeCommaIncludingWhitespace(args))
            return false;
    }

    stop.m_position = cssValuePool().createValue(position, CSSPrimitiveValue::UnitType::Number);
    stop.m_color = consumeDeprecatedGradientStopColor(args, cssParserMode);
    return stop.m_color && args.atEnd();
}

static CSSValue* consumeDeprecatedGradient(CSSParserTokenRange& args, CSSParserMode cssParserMode)
{
    CSSGradientValue* result = nullptr;
    CSSValueID id = args.consumeIncludingWhitespace().id();
    bool isDeprecatedRadialGradient = (id == CSSValueRadial);
    if (isDeprecatedRadialGradient)
        result = CSSRadialGradientValue::create(NonRepeating, CSSDeprecatedRadialGradient);
    else if (id == CSSValueLinear)
        result = CSSLinearGradientValue::create(NonRepeating, CSSDeprecatedLinearGradient);
    if (!result || !consumeCommaIncludingWhitespace(args))
        return nullptr;

    CSSPrimitiveValue* point = consumeDeprecatedGradientPoint(args, true);
    if (!point)
        return nullptr;
    result->setFirstX(point);
    point = consumeDeprecatedGradientPoint(args, false);
    if (!point)
        return nullptr;
    result->setFirstY(point);

    if (!consumeCommaIncludingWhitespace(args))
        return nullptr;

    // For radial gradients only, we now expect a numeric radius.
    if (isDeprecatedRadialGradient) {
        CSSPrimitiveValue* radius = consumeNumber(args, ValueRangeAll);
        if (!radius || !consumeCommaIncludingWhitespace(args))
            return nullptr;
        toCSSRadialGradientValue(result)->setFirstRadius(radius);
    }

    point = consumeDeprecatedGradientPoint(args, true);
    if (!point)
        return nullptr;
    result->setSecondX(point);
    point = consumeDeprecatedGradientPoint(args, false);
    if (!point)
        return nullptr;
    result->setSecondY(point);

    // For radial gradients only, we now expect the second radius.
    if (isDeprecatedRadialGradient) {
        if (!consumeCommaIncludingWhitespace(args))
            return nullptr;
        CSSPrimitiveValue* radius = consumeNumber(args, ValueRangeAll);
        if (!radius)
            return nullptr;
        toCSSRadialGradientValue(result)->setSecondRadius(radius);
    }

    CSSGradientColorStop stop;
    while (consumeCommaIncludingWhitespace(args)) {
        if (!consumeDeprecatedGradientColorStop(args, stop, cssParserMode))
            return nullptr;
        result->addStop(stop);
    }

    return result;
}

static bool consumeGradientColorStops(CSSParserTokenRange& range, CSSParserMode cssParserMode, CSSGradientValue* gradient)
{
    bool supportsColorHints = gradient->gradientType() == CSSLinearGradient || gradient->gradientType() == CSSRadialGradient;

    // The first color stop cannot be a color hint.
    bool previousStopWasColorHint = true;
    do {
        CSSGradientColorStop stop;
        stop.m_color = consumeColor(range, cssParserMode);
        // Two hints in a row are not allowed.
        if (!stop.m_color && (!supportsColorHints || previousStopWasColorHint))
            return false;
        previousStopWasColorHint = !stop.m_color;
        stop.m_position = consumeLengthOrPercent(range, cssParserMode, ValueRangeAll);
        if (!stop.m_color && !stop.m_position)
            return false;
        gradient->addStop(stop);
    } while (consumeCommaIncludingWhitespace(range));

    // The last color stop cannot be a color hint.
    if (previousStopWasColorHint)
        return false;

    // Must have 2 or more stops to be valid.
    return gradient->stopCount() >= 2;
}

static CSSValue* consumeDeprecatedRadialGradient(CSSParserTokenRange& args, CSSParserMode cssParserMode, CSSGradientRepeat repeating)
{
    CSSRadialGradientValue* result = CSSRadialGradientValue::create(repeating, CSSPrefixedRadialGradient);
    CSSValue* centerX = nullptr;
    CSSValue* centerY = nullptr;
    consumeOneOrTwoValuedPosition(args, cssParserMode, UnitlessQuirk::Forbid, centerX, centerY);
    if ((centerX || centerY) && !consumeCommaIncludingWhitespace(args))
        return nullptr;

    result->setFirstX(toCSSPrimitiveValue(centerX));
    result->setSecondX(toCSSPrimitiveValue(centerX));
    result->setFirstY(toCSSPrimitiveValue(centerY));
    result->setSecondY(toCSSPrimitiveValue(centerY));

    CSSPrimitiveValue* shape = consumeIdent<CSSValueCircle, CSSValueEllipse>(args);
    CSSPrimitiveValue* sizeKeyword = consumeIdent<CSSValueClosestSide, CSSValueClosestCorner, CSSValueFarthestSide, CSSValueFarthestCorner, CSSValueContain, CSSValueCover>(args);
    if (!shape)
        shape = consumeIdent<CSSValueCircle, CSSValueEllipse>(args);
    result->setShape(shape);
    result->setSizingBehavior(sizeKeyword);

    // Or, two lengths or percentages
    if (!shape && !sizeKeyword) {
        CSSPrimitiveValue* horizontalSize = consumeLengthOrPercent(args, cssParserMode, ValueRangeAll);
        CSSPrimitiveValue* verticalSize = nullptr;
        if (horizontalSize) {
            verticalSize = consumeLengthOrPercent(args, cssParserMode, ValueRangeAll);
            if (!verticalSize)
                return nullptr;
            consumeCommaIncludingWhitespace(args);
            result->setEndHorizontalSize(horizontalSize);
            result->setEndVerticalSize(verticalSize);
        }
    } else {
        consumeCommaIncludingWhitespace(args);
    }
    if (!consumeGradientColorStops(args, cssParserMode, result))
        return nullptr;

    return result;
}

static CSSValue* consumeRadialGradient(CSSParserTokenRange& args, CSSParserMode cssParserMode, CSSGradientRepeat repeating)
{
    CSSRadialGradientValue* result = CSSRadialGradientValue::create(repeating, CSSRadialGradient);

    CSSPrimitiveValue* shape = nullptr;
    CSSPrimitiveValue* sizeKeyword = nullptr;
    CSSPrimitiveValue* horizontalSize = nullptr;
    CSSPrimitiveValue* verticalSize = nullptr;

    // First part of grammar, the size/shape clause:
    // [ circle || <length> ] |
    // [ ellipse || [ <length> | <percentage> ]{2} ] |
    // [ [ circle | ellipse] || <size-keyword> ]
    for (int i = 0; i < 3; ++i) {
        if (args.peek().type() == IdentToken) {
            CSSValueID id = args.peek().id();
            if (id == CSSValueCircle || id == CSSValueEllipse) {
                if (shape)
                    return nullptr;
                shape = consumeIdent(args);
            } else if (id == CSSValueClosestSide || id == CSSValueClosestCorner || id == CSSValueFarthestSide || id == CSSValueFarthestCorner) {
                if (sizeKeyword)
                    return nullptr;
                sizeKeyword = consumeIdent(args);
            } else {
                break;
            }
        } else {
            CSSPrimitiveValue* center = consumeLengthOrPercent(args, cssParserMode, ValueRangeAll);
            if (!center)
                break;
            if (horizontalSize)
                return nullptr;
            horizontalSize = center;
            center = consumeLengthOrPercent(args, cssParserMode, ValueRangeAll);
            if (center) {
                verticalSize = center;
                ++i;
            }
        }
    }

    // You can specify size as a keyword or a length/percentage, not both.
    if (sizeKeyword && horizontalSize)
        return nullptr;
    // Circles must have 0 or 1 lengths.
    if (shape && shape->getValueID() == CSSValueCircle && verticalSize)
        return nullptr;
    // Ellipses must have 0 or 2 length/percentages.
    if (shape && shape->getValueID() == CSSValueEllipse && horizontalSize && !verticalSize)
        return nullptr;
    // If there's only one size, it must be a length.
    // TODO(timloh): Calcs with both lengths and percentages should be rejected.
    if (!verticalSize && horizontalSize && horizontalSize->isPercentage())
        return nullptr;

    result->setShape(shape);
    result->setSizingBehavior(sizeKeyword);
    result->setEndHorizontalSize(horizontalSize);
    result->setEndVerticalSize(verticalSize);

    CSSValue* centerX = nullptr;
    CSSValue* centerY = nullptr;
    if (args.peek().id() == CSSValueAt) {
        args.consumeIncludingWhitespace();
        consumePosition(args, cssParserMode, UnitlessQuirk::Forbid, centerX, centerY);
        if (!(centerX && centerY))
            return nullptr;
        result->setFirstX(centerX);
        result->setFirstY(centerY);
        // Right now, CSS radial gradients have the same start and end centers.
        result->setSecondX(centerX);
        result->setSecondY(centerY);
    }

    if ((shape || sizeKeyword || horizontalSize || centerX || centerY) && !consumeCommaIncludingWhitespace(args))
        return nullptr;
    if (!consumeGradientColorStops(args, cssParserMode, result))
        return nullptr;
    return result;
}

static CSSValue* consumeLinearGradient(CSSParserTokenRange& args, CSSParserMode cssParserMode, CSSGradientRepeat repeating, CSSGradientType gradientType)
{
    CSSLinearGradientValue* result = CSSLinearGradientValue::create(repeating, gradientType);

    bool expectComma = true;
    CSSPrimitiveValue* angle = consumeAngle(args);
    if (angle) {
        result->setAngle(angle);
    } else if (gradientType == CSSPrefixedLinearGradient || consumeIdent<CSSValueTo>(args)) {
        CSSPrimitiveValue* endX = consumeIdent<CSSValueLeft, CSSValueRight>(args);
        CSSPrimitiveValue* endY = consumeIdent<CSSValueBottom, CSSValueTop>(args);
        if (!endX && !endY) {
            if (gradientType == CSSLinearGradient)
                return nullptr;
            endY = cssValuePool().createIdentifierValue(CSSValueTop);
            expectComma = false;
        } else if (!endX) {
            endX = consumeIdent<CSSValueLeft, CSSValueRight>(args);
        }

        result->setFirstX(endX);
        result->setFirstY(endY);
    } else {
        expectComma = false;
    }

    if (expectComma && !consumeCommaIncludingWhitespace(args))
        return nullptr;
    if (!consumeGradientColorStops(args, cssParserMode, result))
        return nullptr;
    return result;
}

static CSSValue* consumeImageOrNone(CSSParserTokenRange&, CSSParserContext);

static CSSValue* consumeCrossFade(CSSParserTokenRange& args, CSSParserContext context)
{
    CSSValue* fromImageValue = consumeImageOrNone(args, context);
    if (!fromImageValue || !consumeCommaIncludingWhitespace(args))
        return nullptr;
    CSSValue* toImageValue = consumeImageOrNone(args, context);
    if (!toImageValue || !consumeCommaIncludingWhitespace(args))
        return nullptr;

    CSSPrimitiveValue* percentage = nullptr;
    const CSSParserToken& percentageArg = args.consumeIncludingWhitespace();
    if (percentageArg.type() == PercentageToken)
        percentage = cssValuePool().createValue(clampTo<double>(percentageArg.numericValue() / 100, 0, 1), CSSPrimitiveValue::UnitType::Number);
    else if (percentageArg.type() == NumberToken)
        percentage = cssValuePool().createValue(clampTo<double>(percentageArg.numericValue(), 0, 1), CSSPrimitiveValue::UnitType::Number);

    if (!percentage)
        return nullptr;
    return CSSCrossfadeValue::create(fromImageValue, toImageValue, percentage);
}

static CSSValue* consumePaint(CSSParserTokenRange& args, CSSParserContext context)
{
    ASSERT(RuntimeEnabledFeatures::cssPaintAPIEnabled());

    CSSCustomIdentValue* name = consumeCustomIdent(args);
    if (!name)
        return nullptr;

    return CSSPaintValue::create(name);
}

static CSSValue* consumeGeneratedImage(CSSParserTokenRange& range, CSSParserContext context)
{
    CSSValueID id = range.peek().functionId();
    CSSParserTokenRange rangeCopy = range;
    CSSParserTokenRange args = consumeFunction(rangeCopy);
    CSSValue* result = nullptr;
    if (id == CSSValueRadialGradient) {
        result = consumeRadialGradient(args, context.mode(), NonRepeating);
    } else if (id == CSSValueRepeatingRadialGradient) {
        result = consumeRadialGradient(args, context.mode(), Repeating);
    } else if (id == CSSValueWebkitLinearGradient) {
        // FIXME: This should send a deprecation message.
        if (context.useCounter())
            context.useCounter()->count(UseCounter::DeprecatedWebKitLinearGradient);
        result = consumeLinearGradient(args, context.mode(), NonRepeating, CSSPrefixedLinearGradient);
    } else if (id == CSSValueWebkitRepeatingLinearGradient) {
        // FIXME: This should send a deprecation message.
        if (context.useCounter())
            context.useCounter()->count(UseCounter::DeprecatedWebKitRepeatingLinearGradient);
        result = consumeLinearGradient(args, context.mode(), Repeating, CSSPrefixedLinearGradient);
    } else if (id == CSSValueRepeatingLinearGradient) {
        result = consumeLinearGradient(args, context.mode(), Repeating, CSSLinearGradient);
    } else if (id == CSSValueLinearGradient) {
        result = consumeLinearGradient(args, context.mode(), NonRepeating, CSSLinearGradient);
    } else if (id == CSSValueWebkitGradient) {
        // FIXME: This should send a deprecation message.
        if (context.useCounter())
            context.useCounter()->count(UseCounter::DeprecatedWebKitGradient);
        result = consumeDeprecatedGradient(args, context.mode());
    } else if (id == CSSValueWebkitRadialGradient) {
        // FIXME: This should send a deprecation message.
        if (context.useCounter())
            context.useCounter()->count(UseCounter::DeprecatedWebKitRadialGradient);
        result = consumeDeprecatedRadialGradient(args, context.mode(), NonRepeating);
    } else if (id == CSSValueWebkitRepeatingRadialGradient) {
        if (context.useCounter())
            context.useCounter()->count(UseCounter::DeprecatedWebKitRepeatingRadialGradient);
        result = consumeDeprecatedRadialGradient(args, context.mode(), Repeating);
    } else if (id == CSSValueWebkitCrossFade) {
        result = consumeCrossFade(args, context);
    } else if (id == CSSValuePaint) {
        result = RuntimeEnabledFeatures::cssPaintAPIEnabled() ? consumePaint(args, context) : nullptr;
    }
    if (!result || !args.atEnd())
        return nullptr;
    range = rangeCopy;
    return result;
}

static bool isGeneratedImage(CSSValueID id)
{
    return id == CSSValueLinearGradient || id == CSSValueRadialGradient
        || id == CSSValueRepeatingLinearGradient || id == CSSValueRepeatingRadialGradient
        || id == CSSValueWebkitLinearGradient || id == CSSValueWebkitRadialGradient
        || id == CSSValueWebkitRepeatingLinearGradient || id == CSSValueWebkitRepeatingRadialGradient
        || id == CSSValueWebkitGradient || id == CSSValueWebkitCrossFade || id == CSSValuePaint;
}

static CSSValue* consumeImage(CSSParserTokenRange& range, CSSParserContext context)
{
    AtomicString uri(consumeUrl(range));
    if (!uri.isNull())
        return createCSSImageValue(uri, context);
    if (range.peek().type() == FunctionToken) {
        CSSValueID id = range.peek().functionId();
        if (id == CSSValueWebkitImageSet)
            return consumeImageSet(range, context);
        if (isGeneratedImage(id))
            return consumeGeneratedImage(range, context);
    }
    return nullptr;
}

static CSSValue* consumeImageOrNone(CSSParserTokenRange& range, CSSParserContext context)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);
    return consumeImage(range, context);
}

static CSSValue* consumeAttr(CSSParserTokenRange args, CSSParserContext context)
{
    if (args.peek().type() != IdentToken)
        return nullptr;

    String attrName = args.consumeIncludingWhitespace().value();
    // CSS allows identifiers with "-" at the start, like "-webkit-mask-image".
    // But HTML attribute names can't have those characters, and we should not
    // even parse them inside attr().
    // TODO(timloh): We should allow any <ident-token> here.
    if (attrName[0] == '-' || !args.atEnd())
        return nullptr;

    if (context.isHTMLDocument())
        attrName = attrName.lower();

    CSSFunctionValue* attrValue = CSSFunctionValue::create(CSSValueAttr);
    attrValue->append(CSSCustomIdentValue::create(attrName));
    return attrValue;
}

static CSSValue* consumeCounterContent(CSSParserTokenRange args, bool counters)
{
    CSSCustomIdentValue* identifier = consumeCustomIdent(args);
    if (!identifier)
        return nullptr;

    // TODO(timloh): Make this a CSSStringValue.
    CSSCustomIdentValue* separator = nullptr;
    if (!counters) {
        separator = CSSCustomIdentValue::create(String());
    } else {
        if (!consumeCommaIncludingWhitespace(args))
            return nullptr;
        if (args.peek().type() != StringToken)
            return nullptr;
        separator = CSSCustomIdentValue::create(args.consumeIncludingWhitespace().value());
    }

    CSSPrimitiveValue* listStyle = nullptr;
    if (consumeCommaIncludingWhitespace(args)) {
        CSSValueID id = args.peek().id();
        if ((id != CSSValueNone && (id < CSSValueDisc || id > CSSValueKatakanaIroha)))
            return nullptr;
        listStyle = consumeIdent(args);
    } else {
        listStyle = cssValuePool().createIdentifierValue(CSSValueDecimal);
    }

    if (!args.atEnd())
        return nullptr;
    return CSSCounterValue::create(identifier, listStyle, separator);
}

static CSSValue* consumeContent(CSSParserTokenRange& range, CSSParserContext context)
{
    if (identMatches<CSSValueNone, CSSValueNormal>(range.peek().id()))
        return consumeIdent(range);

    CSSValueList* values = CSSValueList::createSpaceSeparated();

    do {
        CSSValue* parsedValue = consumeImage(range, context);
        if (!parsedValue)
            parsedValue = consumeIdent<CSSValueOpenQuote, CSSValueCloseQuote, CSSValueNoOpenQuote, CSSValueNoCloseQuote>(range);
        if (!parsedValue)
            parsedValue = consumeString(range);
        if (!parsedValue) {
            if (range.peek().functionId() == CSSValueAttr)
                parsedValue = consumeAttr(consumeFunction(range), context);
            else if (range.peek().functionId() == CSSValueCounter)
                parsedValue = consumeCounterContent(consumeFunction(range), false);
            else if (range.peek().functionId() == CSSValueCounters)
                parsedValue = consumeCounterContent(consumeFunction(range), true);
            if (!parsedValue)
                return nullptr;
        }
        values->append(parsedValue);
    } while (!range.atEnd());

    return values;
}

static CSSPrimitiveValue* consumePerspective(CSSParserTokenRange& range, CSSParserMode cssParserMode, CSSPropertyID unresolvedProperty)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);
    CSSPrimitiveValue* parsedValue = consumeLength(range, cssParserMode, ValueRangeAll);
    if (!parsedValue && (unresolvedProperty == CSSPropertyAliasWebkitPerspective)) {
        double perspective;
        if (!consumeNumberRaw(range, perspective))
            return nullptr;
        parsedValue = cssValuePool().createValue(perspective, CSSPrimitiveValue::UnitType::Pixels);
    }
    if (parsedValue && (parsedValue->isCalculated() || parsedValue->getDoubleValue() > 0))
        return parsedValue;
    return nullptr;
}

static CSSValueList* consumePositionList(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    CSSValueList* positions = CSSValueList::createCommaSeparated();
    do {
        CSSValue* position = consumePosition(range, cssParserMode, UnitlessQuirk::Forbid);
        if (!position)
            return nullptr;
        positions->append(position);
    } while (consumeCommaIncludingWhitespace(range));
    return positions;
}

static CSSValue* consumeScrollSnapCoordinate(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);
    return consumePositionList(range, cssParserMode);
}

static CSSValue* consumeScrollSnapPoints(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);
    if (range.peek().functionId() == CSSValueRepeat) {
        CSSParserTokenRange args = consumeFunction(range);
        CSSPrimitiveValue* parsedValue = consumeLengthOrPercent(args, cssParserMode, ValueRangeNonNegative);
        if (args.atEnd() && parsedValue && (parsedValue->isCalculated() || parsedValue->getDoubleValue() > 0)) {
            CSSFunctionValue* result = CSSFunctionValue::create(CSSValueRepeat);
            result->append(parsedValue);
            return result;
        }
    }
    return nullptr;
}

static CSSValue* consumeBorderRadiusCorner(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    CSSValue* parsedValue1 = consumeLengthOrPercent(range, cssParserMode, ValueRangeNonNegative);
    if (!parsedValue1)
        return nullptr;
    CSSValue* parsedValue2 = consumeLengthOrPercent(range, cssParserMode, ValueRangeNonNegative);
    if (!parsedValue2)
        parsedValue2 = parsedValue1;
    return CSSValuePair::create(parsedValue1, parsedValue2, CSSValuePair::DropIdenticalValues);
}

static CSSPrimitiveValue* consumeVerticalAlign(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    CSSPrimitiveValue* parsedValue = consumeIdentRange(range, CSSValueBaseline, CSSValueWebkitBaselineMiddle);
    if (!parsedValue)
        parsedValue = consumeLengthOrPercent(range, cssParserMode, ValueRangeAll, UnitlessQuirk::Allow);
    return parsedValue;
}

static CSSPrimitiveValue* consumeShapeRadius(CSSParserTokenRange& args, CSSParserMode cssParserMode)
{
    if (identMatches<CSSValueClosestSide, CSSValueFarthestSide>(args.peek().id()))
        return consumeIdent(args);
    return consumeLengthOrPercent(args, cssParserMode, ValueRangeNonNegative);
}

static CSSBasicShapeCircleValue* consumeBasicShapeCircle(CSSParserTokenRange& args, const CSSParserContext& context)
{
    // spec: https://drafts.csswg.org/css-shapes/#supported-basic-shapes
    // circle( [<shape-radius>]? [at <position>]? )
    CSSBasicShapeCircleValue* shape = CSSBasicShapeCircleValue::create();
    if (CSSPrimitiveValue* radius = consumeShapeRadius(args, context.mode()))
        shape->setRadius(radius);
    if (consumeIdent<CSSValueAt>(args)) {
        CSSValue* centerX = nullptr;
        CSSValue* centerY = nullptr;
        if (!consumePosition(args, context.mode(), UnitlessQuirk::Forbid, centerX, centerY))
            return nullptr;
        shape->setCenterX(centerX);
        shape->setCenterY(centerY);
    }
    return shape;
}

static CSSBasicShapeEllipseValue* consumeBasicShapeEllipse(CSSParserTokenRange& args, const CSSParserContext& context)
{
    // spec: https://drafts.csswg.org/css-shapes/#supported-basic-shapes
    // ellipse( [<shape-radius>{2}]? [at <position>]? )
    CSSBasicShapeEllipseValue* shape = CSSBasicShapeEllipseValue::create();
    if (CSSPrimitiveValue* radiusX = consumeShapeRadius(args, context.mode())) {
        shape->setRadiusX(radiusX);
        if (CSSPrimitiveValue* radiusY = consumeShapeRadius(args, context.mode()))
            shape->setRadiusY(radiusY);
    }
    if (consumeIdent<CSSValueAt>(args)) {
        CSSValue* centerX = nullptr;
        CSSValue* centerY = nullptr;
        if (!consumePosition(args, context.mode(), UnitlessQuirk::Forbid, centerX, centerY))
            return nullptr;
        shape->setCenterX(centerX);
        shape->setCenterY(centerY);
    }
    return shape;
}

static CSSBasicShapePolygonValue* consumeBasicShapePolygon(CSSParserTokenRange& args, const CSSParserContext& context)
{
    CSSBasicShapePolygonValue* shape = CSSBasicShapePolygonValue::create();
    if (identMatches<CSSValueEvenodd, CSSValueNonzero>(args.peek().id())) {
        shape->setWindRule(args.consumeIncludingWhitespace().id() == CSSValueEvenodd ? RULE_EVENODD : RULE_NONZERO);
        if (!consumeCommaIncludingWhitespace(args))
            return nullptr;
    }

    do {
        CSSPrimitiveValue* xLength = consumeLengthOrPercent(args, context.mode(), ValueRangeAll);
        if (!xLength)
            return nullptr;
        CSSPrimitiveValue* yLength = consumeLengthOrPercent(args, context.mode(), ValueRangeAll);
        if (!yLength)
            return nullptr;
        shape->appendPoint(xLength, yLength);
    } while (consumeCommaIncludingWhitespace(args));
    return shape;
}

static void complete4Sides(CSSPrimitiveValue* side[4])
{
    if (side[3])
        return;
    if (!side[2]) {
        if (!side[1])
            side[1] = side[0];
        side[2] = side[0];
    }
    side[3] = side[1];
}

static bool consumeRadii(CSSPrimitiveValue* horizontalRadii[4], CSSPrimitiveValue* verticalRadii[4], CSSParserTokenRange& range, CSSParserMode cssParserMode, bool useLegacyParsing)
{
#if ENABLE(OILPAN)
    // Unconditionally zero initialize the arrays of raw pointers.
    memset(horizontalRadii, 0, 4 * sizeof(horizontalRadii[0]));
    memset(verticalRadii, 0, 4 * sizeof(verticalRadii[0]));
#endif
    unsigned i = 0;
    for (; i < 4 && !range.atEnd() && range.peek().type() != DelimiterToken; ++i) {
        horizontalRadii[i] = consumeLengthOrPercent(range, cssParserMode, ValueRangeNonNegative);
        if (!horizontalRadii[i])
            return false;
    }
    if (!horizontalRadii[0])
        return false;
    if (range.atEnd()) {
        // Legacy syntax: -webkit-border-radius: l1 l2; is equivalent to border-radius: l1 / l2;
        if (useLegacyParsing && i == 2) {
            verticalRadii[0] = horizontalRadii[1];
            horizontalRadii[1] = nullptr;
        } else {
            complete4Sides(horizontalRadii);
            for (unsigned i = 0; i < 4; ++i)
                verticalRadii[i] = horizontalRadii[i];
            return true;
        }
    } else {
        if (!consumeSlashIncludingWhitespace(range))
            return false;
        for (i = 0; i < 4 && !range.atEnd(); ++i) {
            verticalRadii[i] = consumeLengthOrPercent(range, cssParserMode, ValueRangeNonNegative);
            if (!verticalRadii[i])
                return false;
        }
        if (!verticalRadii[0] || !range.atEnd())
            return false;
    }
    complete4Sides(horizontalRadii);
    complete4Sides(verticalRadii);
    return true;
}

static CSSBasicShapeInsetValue* consumeBasicShapeInset(CSSParserTokenRange& args, const CSSParserContext& context)
{
    CSSBasicShapeInsetValue* shape = CSSBasicShapeInsetValue::create();
    CSSPrimitiveValue* top = consumeLengthOrPercent(args, context.mode(), ValueRangeAll);
    if (!top)
        return nullptr;
    CSSPrimitiveValue* right = consumeLengthOrPercent(args, context.mode(), ValueRangeAll);
    CSSPrimitiveValue* bottom = nullptr;
    CSSPrimitiveValue* left = nullptr;
    if (right) {
        bottom = consumeLengthOrPercent(args, context.mode(), ValueRangeAll);
        if (bottom)
            left = consumeLengthOrPercent(args, context.mode(), ValueRangeAll);
    }
    if (left)
        shape->updateShapeSize4Values(top, right, bottom, left);
    else if (bottom)
        shape->updateShapeSize3Values(top, right, bottom);
    else if (right)
        shape->updateShapeSize2Values(top, right);
    else
        shape->updateShapeSize1Value(top);

    if (consumeIdent<CSSValueRound>(args)) {
        CSSPrimitiveValue* horizontalRadii[4];
        CSSPrimitiveValue* verticalRadii[4];
        if (!consumeRadii(horizontalRadii, verticalRadii, args, context.mode(), false))
            return nullptr;
        shape->setTopLeftRadius(CSSValuePair::create(horizontalRadii[0], verticalRadii[0], CSSValuePair::DropIdenticalValues));
        shape->setTopRightRadius(CSSValuePair::create(horizontalRadii[1], verticalRadii[1], CSSValuePair::DropIdenticalValues));
        shape->setBottomRightRadius(CSSValuePair::create(horizontalRadii[2], verticalRadii[2], CSSValuePair::DropIdenticalValues));
        shape->setBottomLeftRadius(CSSValuePair::create(horizontalRadii[3], verticalRadii[3], CSSValuePair::DropIdenticalValues));
    }
    return shape;
}

static CSSValue* consumeBasicShape(CSSParserTokenRange& range, const CSSParserContext& context)
{
    CSSValue* shape = nullptr;
    if (range.peek().type() != FunctionToken)
        return nullptr;
    CSSValueID id = range.peek().functionId();
    CSSParserTokenRange rangeCopy = range;
    CSSParserTokenRange args = consumeFunction(rangeCopy);
    if (id == CSSValueCircle)
        shape = consumeBasicShapeCircle(args, context);
    else if (id == CSSValueEllipse)
        shape = consumeBasicShapeEllipse(args, context);
    else if (id == CSSValuePolygon)
        shape = consumeBasicShapePolygon(args, context);
    else if (id == CSSValueInset)
        shape = consumeBasicShapeInset(args, context);
    if (!shape || !args.atEnd())
        return nullptr;
    range = rangeCopy;
    return shape;
}

static CSSValue* consumeClipPath(CSSParserTokenRange& range, const CSSParserContext& context)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);
    String url = consumeUrl(range);
    if (!url.isNull())
        return CSSURIValue::create(url);
    return consumeBasicShape(range, context);
}

static CSSValue* consumeShapeOutside(CSSParserTokenRange& range, const CSSParserContext& context)
{
    if (CSSValue* imageValue = consumeImageOrNone(range, context))
        return imageValue;
    CSSValueList* list = CSSValueList::createSpaceSeparated();
    if (CSSValue* boxValue = consumeIdent<CSSValueContentBox, CSSValuePaddingBox, CSSValueBorderBox, CSSValueMarginBox>(range))
        list->append(boxValue);
    if (CSSValue* shapeValue = consumeBasicShape(range, context)) {
        list->append(shapeValue);
        if (list->length() < 2) {
            if (CSSValue* boxValue = consumeIdent<CSSValueContentBox, CSSValuePaddingBox, CSSValueBorderBox, CSSValueMarginBox>(range))
                list->append(boxValue);
        }
    }
    if (!list->length())
        return nullptr;
    return list;
}

static CSSValue* consumeContentDistributionOverflowPosition(CSSParserTokenRange& range)
{
    if (identMatches<CSSValueNormal, CSSValueBaseline, CSSValueLastBaseline>(range.peek().id()))
        return CSSContentDistributionValue::create(CSSValueInvalid, range.consumeIncludingWhitespace().id(), CSSValueInvalid);

    CSSValueID distribution = CSSValueInvalid;
    CSSValueID position = CSSValueInvalid;
    CSSValueID overflow = CSSValueInvalid;
    do {
        CSSValueID id = range.peek().id();
        if (identMatches<CSSValueSpaceBetween, CSSValueSpaceAround, CSSValueSpaceEvenly, CSSValueStretch>(id)) {
            if (distribution != CSSValueInvalid)
                return nullptr;
            distribution = id;
        } else if (identMatches<CSSValueStart, CSSValueEnd, CSSValueCenter, CSSValueFlexStart, CSSValueFlexEnd, CSSValueLeft, CSSValueRight>(id)) {
            if (position != CSSValueInvalid)
                return nullptr;
            position = id;
        } else if (identMatches<CSSValueUnsafe, CSSValueSafe>(id)) {
            if (overflow != CSSValueInvalid)
                return nullptr;
            overflow = id;
        } else {
            return nullptr;
        }
        range.consumeIncludingWhitespace();
    } while (!range.atEnd());

    // The grammar states that we should have at least <content-distribution> or <content-position>.
    if (position == CSSValueInvalid && distribution == CSSValueInvalid)
        return nullptr;

    // The grammar states that <overflow-position> must be associated to <content-position>.
    if (overflow != CSSValueInvalid && position == CSSValueInvalid)
        return nullptr;

    return CSSContentDistributionValue::create(distribution, position, overflow);
}

static CSSPrimitiveValue* consumeBorderImageRepeatKeyword(CSSParserTokenRange& range)
{
    return consumeIdent<CSSValueStretch, CSSValueRepeat, CSSValueSpace, CSSValueRound>(range);
}

static CSSValue* consumeBorderImageRepeat(CSSParserTokenRange& range)
{
    CSSPrimitiveValue* horizontal = consumeBorderImageRepeatKeyword(range);
    if (!horizontal)
        return nullptr;
    CSSPrimitiveValue* vertical = consumeBorderImageRepeatKeyword(range);
    if (!vertical)
        vertical = horizontal;
    return CSSValuePair::create(horizontal, vertical, CSSValuePair::DropIdenticalValues);
}

static CSSValue* consumeBorderImageSlice(CSSPropertyID property, CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    bool fill = consumeIdent<CSSValueFill>(range);
    CSSPrimitiveValue* slices[4];
#if ENABLE(OILPAN)
    // Unconditionally zero initialize the arrays of raw pointers.
    memset(slices, 0, 4 * sizeof(slices[0]));
#endif
    for (size_t index = 0; index < 4; ++index) {
        CSSPrimitiveValue* value = consumePercent(range, ValueRangeNonNegative);
        if (!value)
            value = consumeNumber(range, ValueRangeNonNegative);
        if (!value)
            break;
        slices[index] = value;
    }
    if (!slices[0])
        return nullptr;
    if (consumeIdent<CSSValueFill>(range)) {
        if (fill)
            return nullptr;
        fill = true;
    }
    complete4Sides(slices);
    // FIXME: For backwards compatibility, -webkit-border-image, -webkit-mask-box-image and -webkit-box-reflect have to do a fill by default.
    // FIXME: What do we do with -webkit-box-reflect and -webkit-mask-box-image? Probably just have to leave them filling...
    if (property == CSSPropertyWebkitBorderImage || property == CSSPropertyWebkitMaskBoxImage || property == CSSPropertyWebkitBoxReflect)
        fill = true;
    return CSSBorderImageSliceValue::create(CSSQuadValue::create(slices[0], slices[1], slices[2], slices[3], CSSQuadValue::SerializeAsQuad), fill);
}

static CSSValue* consumeBorderImageOutset(CSSParserTokenRange& range)
{
    CSSPrimitiveValue* outsets[4];
#if ENABLE(OILPAN)
    // Unconditionally zero initialize the arrays of raw pointers.
    memset(outsets, 0, 4 * sizeof(outsets[0]));
#endif
    CSSPrimitiveValue* value = nullptr;
    for (size_t index = 0; index < 4; ++index) {
        value = consumeNumber(range, ValueRangeNonNegative);
        if (!value)
            value = consumeLength(range, HTMLStandardMode, ValueRangeNonNegative);
        if (!value)
            break;
        outsets[index] = value;
    }
    if (!outsets[0])
        return nullptr;
    complete4Sides(outsets);
    return CSSQuadValue::create(outsets[0], outsets[1], outsets[2], outsets[3], CSSQuadValue::SerializeAsQuad);
}

static CSSValue* consumeBorderImageWidth(CSSParserTokenRange& range)
{
    CSSPrimitiveValue* widths[4];
#if ENABLE(OILPAN)
    // Unconditionally zero initialize the arrays of raw pointers.
    memset(widths, 0, 4 * sizeof(widths[0]));
#endif
    CSSPrimitiveValue* value = nullptr;
    for (size_t index = 0; index < 4; ++index) {
        value = consumeNumber(range, ValueRangeNonNegative);
        if (!value)
            value = consumeLengthOrPercent(range, HTMLStandardMode, ValueRangeNonNegative, UnitlessQuirk::Forbid);
        if (!value)
            value = consumeIdent<CSSValueAuto>(range);
        if (!value)
            break;
        widths[index] = value;
    }
    if (!widths[0])
        return nullptr;
    complete4Sides(widths);
    return CSSQuadValue::create(widths[0], widths[1], widths[2], widths[3], CSSQuadValue::SerializeAsQuad);
}

static bool consumeBorderImageComponents(CSSPropertyID property, CSSParserTokenRange& range, const CSSParserContext& context, CSSValue*& source,
    CSSValue*& slice, CSSValue*& width, CSSValue*& outset, CSSValue*& repeat)
{
    do {
        if (!source) {
            source = consumeImageOrNone(range, context);
            if (source)
                continue;
        }
        if (!repeat) {
            repeat = consumeBorderImageRepeat(range);
            if (repeat)
                continue;
        }
        if (!slice) {
            slice = consumeBorderImageSlice(property, range, context.mode());
            if (slice) {
                ASSERT(!width && !outset);
                if (consumeSlashIncludingWhitespace(range)) {
                    width = consumeBorderImageWidth(range);
                    if (consumeSlashIncludingWhitespace(range)) {
                        outset = consumeBorderImageOutset(range);
                        if (!outset)
                            return false;
                    } else if (!width) {
                        return false;
                    }
                }
            } else {
                return false;
            }
        } else {
            return false;
        }
    } while (!range.atEnd());
    return true;
}

static CSSValue* consumeWebkitBorderImage(CSSPropertyID property, CSSParserTokenRange& range, const CSSParserContext& context)
{
    CSSValue* source = nullptr;
    CSSValue* slice = nullptr;
    CSSValue* width = nullptr;
    CSSValue* outset = nullptr;
    CSSValue* repeat = nullptr;
    if (consumeBorderImageComponents(property, range, context, source, slice, width, outset, repeat))
        return createBorderImageValue(source, slice, width, outset, repeat);
    return nullptr;
}

static CSSValue* consumeReflect(CSSParserTokenRange& range, const CSSParserContext& context)
{
    CSSPrimitiveValue* direction = consumeIdent<CSSValueAbove, CSSValueBelow, CSSValueLeft, CSSValueRight>(range);
    if (!direction)
        return nullptr;

    CSSPrimitiveValue* offset = nullptr;
    if (range.atEnd()) {
        offset = cssValuePool().createValue(0, CSSPrimitiveValue::UnitType::Pixels);
    } else {
        offset = consumeLengthOrPercent(range, context.mode(), ValueRangeAll, UnitlessQuirk::Forbid);
        if (!offset)
            return nullptr;
    }

    CSSValue* mask = nullptr;
    if (!range.atEnd()) {
        mask = consumeWebkitBorderImage(CSSPropertyWebkitBoxReflect, range, context);
        if (!mask)
            return nullptr;
    }
    return CSSReflectValue::create(direction, offset, mask);
}

static CSSValue* consumeFontSizeAdjust(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);
    return consumeNumber(range, ValueRangeNonNegative);
}

static CSSValue* consumeImageOrientation(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueFromImage)
        return consumeIdent(range);
    if (range.peek().type() != NumberToken) {
        CSSPrimitiveValue* angle = consumeAngle(range);
        if (angle && angle->getDoubleValue() == 0)
            return angle;
    }
    return nullptr;
}

static CSSValue* consumeBackgroundBlendMode(CSSParserTokenRange& range)
{
    CSSValueID id = range.peek().id();
    if (id == CSSValueNormal || id == CSSValueOverlay || (id >= CSSValueMultiply && id <= CSSValueLuminosity))
        return consumeIdent(range);
    return nullptr;
}

static CSSValue* consumeBackgroundAttachment(CSSParserTokenRange& range)
{
    return consumeIdent<CSSValueScroll, CSSValueFixed, CSSValueLocal>(range);
}

static CSSValue* consumeBackgroundBox(CSSParserTokenRange& range)
{
    return consumeIdent<CSSValueBorderBox, CSSValuePaddingBox, CSSValueContentBox>(range);
}

static CSSValue* consumeBackgroundComposite(CSSParserTokenRange& range)
{
    return consumeIdentRange(range, CSSValueClear, CSSValuePlusLighter);
}

static CSSValue* consumeMaskSourceType(CSSParserTokenRange& range)
{
    ASSERT(RuntimeEnabledFeatures::cssMaskSourceTypeEnabled());
    return consumeIdent<CSSValueAuto, CSSValueAlpha, CSSValueLuminance>(range);
}

static CSSValue* consumePrefixedBackgroundBox(CSSPropertyID property, CSSParserTokenRange& range, const CSSParserContext& context)
{
    // The values 'border', 'padding' and 'content' are deprecated and do not apply to the version of the property that has the -webkit- prefix removed.
    if (CSSValue* value = consumeIdentRange(range, CSSValueBorder, CSSValuePaddingBox))
        return value;
    if ((property == CSSPropertyWebkitBackgroundClip || property == CSSPropertyWebkitMaskClip) && range.peek().id() == CSSValueText)
        return consumeIdent(range);
    return nullptr;
}

static CSSValue* consumeBackgroundSize(CSSPropertyID unresolvedProperty, CSSParserTokenRange& range, CSSParserMode mode)
{
    if (identMatches<CSSValueContain, CSSValueCover>(range.peek().id()))
        return consumeIdent(range);

    CSSPrimitiveValue* horizontal = consumeIdent<CSSValueAuto>(range);
    if (!horizontal)
        horizontal = consumeLengthOrPercent(range, mode, ValueRangeAll, UnitlessQuirk::Forbid);

    CSSPrimitiveValue* vertical = nullptr;
    if (!range.atEnd()) {
        if (range.peek().id() == CSSValueAuto) // `auto' is the default
            range.consumeIncludingWhitespace();
        else
            vertical = consumeLengthOrPercent(range, mode, ValueRangeAll, UnitlessQuirk::Forbid);
    } else if (unresolvedProperty == CSSPropertyAliasWebkitBackgroundSize) {
        // Legacy syntax: "-webkit-background-size: 10px" is equivalent to "background-size: 10px 10px".
        vertical = horizontal;
    }
    if (!vertical)
        return horizontal;
    return CSSValuePair::create(horizontal, vertical, CSSValuePair::KeepIdenticalValues);
}

static CSSValue* consumeBackgroundComponent(CSSPropertyID unresolvedProperty, CSSParserTokenRange& range, const CSSParserContext& context)
{
    switch (unresolvedProperty) {
    case CSSPropertyBackgroundClip:
        return consumeBackgroundBox(range);
    case CSSPropertyBackgroundBlendMode:
        return consumeBackgroundBlendMode(range);
    case CSSPropertyBackgroundAttachment:
        return consumeBackgroundAttachment(range);
    case CSSPropertyBackgroundOrigin:
        return consumeBackgroundBox(range);
    case CSSPropertyWebkitMaskComposite:
        return consumeBackgroundComposite(range);
    case CSSPropertyMaskSourceType:
        return consumeMaskSourceType(range);
    case CSSPropertyWebkitBackgroundClip:
    case CSSPropertyWebkitBackgroundOrigin:
    case CSSPropertyWebkitMaskClip:
    case CSSPropertyWebkitMaskOrigin:
        return consumePrefixedBackgroundBox(unresolvedProperty, range, context);
    case CSSPropertyBackgroundImage:
    case CSSPropertyWebkitMaskImage:
        return consumeImageOrNone(range, context);
    case CSSPropertyBackgroundPositionX:
    case CSSPropertyWebkitMaskPositionX:
        return consumePositionX(range, context.mode());
    case CSSPropertyBackgroundPositionY:
    case CSSPropertyWebkitMaskPositionY:
        return consumePositionY(range, context.mode());
    case CSSPropertyBackgroundSize:
    case CSSPropertyAliasWebkitBackgroundSize:
    case CSSPropertyWebkitMaskSize:
        return consumeBackgroundSize(unresolvedProperty, range, context.mode());
    case CSSPropertyBackgroundColor:
        return consumeColor(range, context.mode());
    default:
        break;
    };
    return nullptr;
}

static void addBackgroundValue(CSSValue*& list, CSSValue* value)
{
    if (list) {
        if (!list->isBaseValueList()) {
            CSSValue* firstValue = list;
            list = CSSValueList::createCommaSeparated();
            toCSSValueList(list)->append(firstValue);
        }
        toCSSValueList(list)->append(value);
    } else {
        // To conserve memory we don't actually wrap a single value in a list.
        list = value;
    }
}

static CSSValue* consumeCommaSeparatedBackgroundComponent(CSSPropertyID unresolvedProperty, CSSParserTokenRange& range, const CSSParserContext& context)
{
    CSSValue* result = nullptr;
    do {
        CSSValue* value = consumeBackgroundComponent(unresolvedProperty, range, context);
        if (!value)
            return nullptr;
        addBackgroundValue(result, value);
    } while (consumeCommaIncludingWhitespace(range));
    return result;
}

static CSSPrimitiveValue* consumeSelfPositionKeyword(CSSParserTokenRange& range)
{
    CSSValueID id = range.peek().id();
    if (id == CSSValueStart || id == CSSValueEnd || id == CSSValueCenter
        || id == CSSValueSelfStart || id == CSSValueSelfEnd || id == CSSValueFlexStart
        || id == CSSValueFlexEnd || id == CSSValueLeft || id == CSSValueRight)
        return consumeIdent(range);
    return nullptr;
}

static CSSValue* consumeSelfPositionOverflowPosition(CSSParserTokenRange& range)
{
    if (identMatches<CSSValueAuto, CSSValueStretch, CSSValueBaseline, CSSValueLastBaseline>(range.peek().id()))
        return consumeIdent(range);

    CSSPrimitiveValue* overflowPosition = consumeIdent<CSSValueUnsafe, CSSValueSafe>(range);
    CSSPrimitiveValue* selfPosition = consumeSelfPositionKeyword(range);
    if (!selfPosition)
        return nullptr;
    if (!overflowPosition)
        overflowPosition = consumeIdent<CSSValueUnsafe, CSSValueSafe>(range);
    if (overflowPosition)
        return CSSValuePair::create(selfPosition, overflowPosition, CSSValuePair::DropIdenticalValues);
    return selfPosition;
}

static CSSValue* consumeJustifyItems(CSSParserTokenRange& range)
{
    CSSParserTokenRange rangeCopy = range;
    CSSPrimitiveValue* legacy = consumeIdent<CSSValueLegacy>(rangeCopy);
    CSSPrimitiveValue* positionKeyword = consumeIdent<CSSValueCenter, CSSValueLeft, CSSValueRight>(rangeCopy);
    if (!legacy)
        legacy = consumeIdent<CSSValueLegacy>(rangeCopy);
    if (legacy && positionKeyword) {
        range = rangeCopy;
        return CSSValuePair::create(legacy, positionKeyword, CSSValuePair::DropIdenticalValues);
    }
    return consumeSelfPositionOverflowPosition(range);
}

static CSSCustomIdentValue* consumeCustomIdentForGridLine(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueAuto || range.peek().id() == CSSValueSpan)
        return nullptr;
    return consumeCustomIdent(range);
}

static CSSValue* consumeGridLine(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueAuto)
        return consumeIdent(range);

    CSSPrimitiveValue* spanValue = nullptr;
    CSSCustomIdentValue* gridLineName = nullptr;
    CSSPrimitiveValue* numericValue = consumeInteger(range);
    if (numericValue) {
        gridLineName = consumeCustomIdentForGridLine(range);
        spanValue = consumeIdent<CSSValueSpan>(range);
    } else {
        spanValue = consumeIdent<CSSValueSpan>(range);
        if (spanValue) {
            numericValue = consumeInteger(range);
            gridLineName = consumeCustomIdentForGridLine(range);
            if (!numericValue)
                numericValue = consumeInteger(range);
        } else {
            gridLineName = consumeCustomIdentForGridLine(range);
            if (gridLineName) {
                numericValue = consumeInteger(range);
                spanValue = consumeIdent<CSSValueSpan>(range);
                if (!spanValue && !numericValue)
                    return gridLineName;
            } else {
                return nullptr;
            }
        }
    }

    if (spanValue && numericValue && numericValue->getIntValue() < 0)
        return nullptr; // Negative numbers are not allowed for span.
    if (numericValue && numericValue->getIntValue() == 0)
        return nullptr; // An <integer> value of zero makes the declaration invalid.

    CSSValueList* values = CSSValueList::createSpaceSeparated();
    if (spanValue)
        values->append(spanValue);
    if (numericValue)
        values->append(numericValue);
    if (gridLineName)
        values->append(gridLineName);
    ASSERT(values->length());
    return values;
}

static CSSPrimitiveValue* consumeGridBreadth(CSSParserTokenRange& range, CSSParserMode cssParserMode, TrackSizeRestriction restriction = AllowAll)
{
    if (restriction == AllowAll) {
        const CSSParserToken& token = range.peek();
        if (identMatches<CSSValueMinContent, CSSValueMaxContent, CSSValueAuto>(token.id()))
            return consumeIdent(range);
        if (token.type() == DimensionToken && token.unitType() == CSSPrimitiveValue::UnitType::Fraction) {
            if (range.peek().numericValue() < 0)
                return nullptr;
            return cssValuePool().createValue(range.consumeIncludingWhitespace().numericValue(), CSSPrimitiveValue::UnitType::Fraction);
        }
    }
    return consumeLengthOrPercent(range, cssParserMode, ValueRangeNonNegative, UnitlessQuirk::Allow);
}

// TODO(rob.buis): This needs a bool parameter so we can disallow <auto-track-list> for the grid shorthand.
static CSSValue* consumeGridTrackSize(CSSParserTokenRange& range, CSSParserMode cssParserMode, TrackSizeRestriction restriction = AllowAll)
{
    const CSSParserToken& token = range.peek();
    if (restriction == AllowAll && identMatches<CSSValueAuto>(token.id()))
        return consumeIdent(range);

    if (token.functionId() == CSSValueMinmax) {
        CSSParserTokenRange rangeCopy = range;
        CSSParserTokenRange args = consumeFunction(rangeCopy);
        CSSPrimitiveValue* minTrackBreadth = consumeGridBreadth(args, cssParserMode, restriction);
        if (!minTrackBreadth || !consumeCommaIncludingWhitespace(args))
            return nullptr;
        CSSPrimitiveValue* maxTrackBreadth = consumeGridBreadth(args, cssParserMode);
        if (!maxTrackBreadth || !args.atEnd())
            return nullptr;
        range = rangeCopy;
        CSSFunctionValue* result = CSSFunctionValue::create(CSSValueMinmax);
        result->append(minTrackBreadth);
        result->append(maxTrackBreadth);
        return result;
    }
    return consumeGridBreadth(range, cssParserMode, restriction);
}

// Appends to the passed in CSSGridLineNamesValue if any, otherwise creates a new one.
static CSSGridLineNamesValue* consumeGridLineNames(CSSParserTokenRange& range, CSSGridLineNamesValue* lineNames = nullptr)
{
    CSSParserTokenRange rangeCopy = range;
    if (rangeCopy.consumeIncludingWhitespace().type() != LeftBracketToken)
        return nullptr;
    if (!lineNames)
        lineNames = CSSGridLineNamesValue::create();
    while (CSSCustomIdentValue* lineName = consumeCustomIdentForGridLine(rangeCopy))
        lineNames->append(lineName);
    if (rangeCopy.consumeIncludingWhitespace().type() != RightBracketToken)
        return nullptr;
    range = rangeCopy;
    return lineNames;
}

static bool consumeGridTrackRepeatFunction(CSSParserTokenRange& range, CSSParserMode cssParserMode, CSSValueList& list, bool& isAutoRepeat)
{
    CSSParserTokenRange args = consumeFunction(range);
    // The number of repetitions for <auto-repeat> is not important at parsing level
    // because it will be computed later, let's set it to 1.
    size_t repetitions = 1;
    isAutoRepeat = identMatches<CSSValueAutoFill, CSSValueAutoFit>(args.peek().id());
    CSSValueList* repeatedValues;
    if (isAutoRepeat) {
        repeatedValues = CSSGridAutoRepeatValue::create(args.consumeIncludingWhitespace().id());
    } else {
        // TODO(rob.buis): a consumeIntegerRaw would be more efficient here.
        CSSPrimitiveValue* repetition = consumePositiveInteger(args);
        if (!repetition)
            return false;
        repetitions = clampTo<size_t>(repetition->getDoubleValue(), 0, kGridMaxTracks);
        repeatedValues = CSSValueList::createSpaceSeparated();
    }
    if (!consumeCommaIncludingWhitespace(args))
        return false;
    CSSGridLineNamesValue* lineNames = consumeGridLineNames(args);
    if (lineNames)
        repeatedValues->append(lineNames);

    size_t numberOfTracks = 0;
    TrackSizeRestriction restriction = isAutoRepeat ? FixedSizeOnly : AllowAll;
    while (!args.atEnd()) {
        if (isAutoRepeat && numberOfTracks)
            return false;
        CSSValue* trackSize = consumeGridTrackSize(args, cssParserMode, restriction);
        if (!trackSize)
            return false;
        repeatedValues->append(trackSize);
        ++numberOfTracks;
        lineNames = consumeGridLineNames(args);
        if (lineNames)
            repeatedValues->append(lineNames);
    }
    // We should have found at least one <track-size> or else it is not a valid <track-list>.
    if (!numberOfTracks)
        return false;

    if (isAutoRepeat) {
        list.append(repeatedValues);
    } else {
        // We clamp the repetitions to a multiple of the repeat() track list's size, while staying below the max grid size.
        repetitions = std::min(repetitions, kGridMaxTracks / numberOfTracks);
        for (size_t i = 0; i < repetitions; ++i) {
            for (size_t j = 0; j < repeatedValues->length(); ++j)
                list.append(repeatedValues->item(j));
        }
    }
    return true;
}

static CSSValue* consumeGridTrackList(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    CSSValueList* values = CSSValueList::createSpaceSeparated();
    CSSGridLineNamesValue* lineNames = consumeGridLineNames(range);
    if (lineNames)
        values->append(lineNames);

    bool seenAutoRepeat = false;
    // TODO(rob.buis): <line-names> should not be able to directly precede <auto-repeat>.
    do {
        bool isAutoRepeat;
        if (range.peek().functionId() == CSSValueRepeat) {
            if (!consumeGridTrackRepeatFunction(range, cssParserMode, *values, isAutoRepeat))
                return nullptr;
            if (isAutoRepeat && seenAutoRepeat)
                return nullptr;
            seenAutoRepeat = seenAutoRepeat || isAutoRepeat;
        } else if (CSSValue* value = consumeGridTrackSize(range, cssParserMode, seenAutoRepeat ? FixedSizeOnly : AllowAll)) {
            values->append(value);
        } else {
            return nullptr;
        }
        lineNames = consumeGridLineNames(range);
        if (lineNames)
            values->append(lineNames);
    } while (!range.atEnd() && range.peek().type() != DelimiterToken);
    // <auto-repeat> requires definite minimum track sizes in order to compute the number of repetitions.
    // The above while loop detects those appearances after the <auto-repeat> but not the ones before.
    if (seenAutoRepeat && !allTracksAreFixedSized(*values))
        return nullptr;
    return values;
}

static CSSValue* consumeGridTemplatesRowsOrColumns(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);
    return consumeGridTrackList(range, cssParserMode);
}

static CSSValue* consumeGridTemplateAreas(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);

    NamedGridAreaMap gridAreaMap;
    size_t rowCount = 0;
    size_t columnCount = 0;

    while (range.peek().type() == StringToken) {
        if (!parseGridTemplateAreasRow(range.consumeIncludingWhitespace().value(), gridAreaMap, rowCount, columnCount))
            return nullptr;
        ++rowCount;
    }

    if (rowCount == 0)
        return nullptr;
    ASSERT(columnCount);
    return CSSGridTemplateAreasValue::create(gridAreaMap, rowCount, columnCount);
}

CSSValue* CSSPropertyParser::parseSingleValue(CSSPropertyID unresolvedProperty)
{
    CSSPropertyID property = resolveCSSPropertyID(unresolvedProperty);
    if (CSSParserFastPaths::isKeywordPropertyID(property)) {
        if (!CSSParserFastPaths::isValidKeywordPropertyAndValue(property, m_range.peek().id()))
            return nullptr;
        return consumeIdent(m_range);
    }
    switch (property) {
    case CSSPropertyWillChange:
        return consumeWillChange(m_range);
    case CSSPropertyPage:
        return consumePage(m_range);
    case CSSPropertyQuotes:
        return consumeQuotes(m_range);
    case CSSPropertyWebkitHighlight:
        return consumeWebkitHighlight(m_range);
    case CSSPropertyFontVariantLigatures:
        return consumeFontVariantLigatures(m_range);
    case CSSPropertyFontFeatureSettings:
        return consumeFontFeatureSettings(m_range);
    case CSSPropertyFontVariant:
        return consumeFontVariant(m_range);
    case CSSPropertyFontFamily:
        return consumeFontFamily(m_range);
    case CSSPropertyFontWeight:
        return consumeFontWeight(m_range);
    case CSSPropertyLetterSpacing:
    case CSSPropertyWordSpacing:
        return consumeSpacing(m_range, m_context.mode());
    case CSSPropertyTabSize:
        return consumeTabSize(m_range, m_context.mode());
    case CSSPropertyFontSize:
        return consumeFontSize(m_range, m_context.mode(), UnitlessQuirk::Allow);
    case CSSPropertyLineHeight:
        return consumeLineHeight(m_range, m_context.mode());
    case CSSPropertyRotate:
        return consumeRotation(m_range);
    case CSSPropertyScale:
        return consumeScale(m_range, m_context.mode());
    case CSSPropertyTranslate:
        return consumeTranslate(m_range, m_context.mode());
    case CSSPropertyWebkitBorderHorizontalSpacing:
    case CSSPropertyWebkitBorderVerticalSpacing:
        return consumeLength(m_range, m_context.mode(), ValueRangeNonNegative);
    case CSSPropertyCounterIncrement:
    case CSSPropertyCounterReset:
        return consumeCounter(m_range, m_context.mode(), property == CSSPropertyCounterIncrement ? 1 : 0);
    case CSSPropertySize:
        return consumeSize(m_range, m_context.mode());
    case CSSPropertySnapHeight:
        return consumeSnapHeight(m_range, m_context.mode());
    case CSSPropertyTextIndent:
        return consumeTextIndent(m_range, m_context.mode());
    case CSSPropertyMaxWidth:
    case CSSPropertyMaxHeight:
        return consumeMaxWidthOrHeight(m_range, m_context, UnitlessQuirk::Allow);
    case CSSPropertyWebkitMaxLogicalWidth:
    case CSSPropertyWebkitMaxLogicalHeight:
        return consumeMaxWidthOrHeight(m_range, m_context);
    case CSSPropertyMinWidth:
    case CSSPropertyMinHeight:
    case CSSPropertyWidth:
    case CSSPropertyHeight:
        return consumeWidthOrHeight(m_range, m_context, UnitlessQuirk::Allow);
    case CSSPropertyWebkitMinLogicalWidth:
    case CSSPropertyWebkitMinLogicalHeight:
    case CSSPropertyWebkitLogicalWidth:
    case CSSPropertyWebkitLogicalHeight:
        return consumeWidthOrHeight(m_range, m_context);
    case CSSPropertyMarginTop:
    case CSSPropertyMarginRight:
    case CSSPropertyMarginBottom:
    case CSSPropertyMarginLeft:
    case CSSPropertyBottom:
    case CSSPropertyLeft:
    case CSSPropertyRight:
    case CSSPropertyTop:
        return consumeMarginOrOffset(m_range, m_context.mode(), UnitlessQuirk::Allow);
    case CSSPropertyWebkitMarginStart:
    case CSSPropertyWebkitMarginEnd:
    case CSSPropertyWebkitMarginBefore:
    case CSSPropertyWebkitMarginAfter:
        return consumeMarginOrOffset(m_range, m_context.mode(), UnitlessQuirk::Forbid);
    case CSSPropertyPaddingTop:
    case CSSPropertyPaddingRight:
    case CSSPropertyPaddingBottom:
    case CSSPropertyPaddingLeft:
        return consumeLengthOrPercent(m_range, m_context.mode(), ValueRangeNonNegative, UnitlessQuirk::Allow);
    case CSSPropertyWebkitPaddingStart:
    case CSSPropertyWebkitPaddingEnd:
    case CSSPropertyWebkitPaddingBefore:
    case CSSPropertyWebkitPaddingAfter:
        return consumeLengthOrPercent(m_range, m_context.mode(), ValueRangeNonNegative, UnitlessQuirk::Forbid);
    case CSSPropertyClip:
        return consumeClip(m_range, m_context.mode());
    case CSSPropertyTouchAction:
        return consumeTouchAction(m_range);
    case CSSPropertyScrollSnapDestination:
    case CSSPropertyObjectPosition:
    case CSSPropertyPerspectiveOrigin:
        return consumePosition(m_range, m_context.mode(), UnitlessQuirk::Forbid);
    case CSSPropertyWebkitLineClamp:
        return consumeLineClamp(m_range);
    case CSSPropertyWebkitFontSizeDelta:
        return consumeLength(m_range, m_context.mode(), ValueRangeAll, UnitlessQuirk::Allow);
    case CSSPropertyWebkitHyphenateCharacter:
    case CSSPropertyWebkitLocale:
        return consumeLocale(m_range);
    case CSSPropertyColumnWidth:
        return consumeColumnWidth(m_range);
    case CSSPropertyColumnCount:
        return consumeColumnCount(m_range);
    case CSSPropertyColumnGap:
        return consumeColumnGap(m_range, m_context.mode());
    case CSSPropertyColumnSpan:
        return consumeColumnSpan(m_range, m_context.mode());
    case CSSPropertyZoom:
        return consumeZoom(m_range, m_context);
    case CSSPropertyAnimationDelay:
    case CSSPropertyTransitionDelay:
    case CSSPropertyAnimationDirection:
    case CSSPropertyAnimationDuration:
    case CSSPropertyTransitionDuration:
    case CSSPropertyAnimationFillMode:
    case CSSPropertyAnimationIterationCount:
    case CSSPropertyAnimationName:
    case CSSPropertyAnimationPlayState:
    case CSSPropertyTransitionProperty:
    case CSSPropertyAnimationTimingFunction:
    case CSSPropertyTransitionTimingFunction:
        return consumeAnimationPropertyList(property, m_range, m_context, unresolvedProperty == CSSPropertyAliasWebkitAnimationName);
    case CSSPropertyGridColumnGap:
    case CSSPropertyGridRowGap:
        return consumeLength(m_range, m_context.mode(), ValueRangeNonNegative);
    case CSSPropertyShapeMargin:
        return consumeLengthOrPercent(m_range, m_context.mode(), ValueRangeNonNegative);
    case CSSPropertyShapeImageThreshold:
        return consumeNumber(m_range, ValueRangeAll);
    case CSSPropertyWebkitBoxOrdinalGroup:
        return consumePositiveInteger(m_range);
    case CSSPropertyOrphans:
    case CSSPropertyWidows:
        return consumeWidowsOrOrphans(m_range);
    case CSSPropertyTextDecorationColor:
        ASSERT(RuntimeEnabledFeatures::css3TextDecorationsEnabled());
        return consumeColor(m_range, m_context.mode());
    case CSSPropertyWebkitTextStrokeWidth:
        return consumeTextStrokeWidth(m_range, m_context.mode());
    case CSSPropertyWebkitTextFillColor:
    case CSSPropertyWebkitTapHighlightColor:
    case CSSPropertyWebkitTextEmphasisColor:
    case CSSPropertyWebkitBorderStartColor:
    case CSSPropertyWebkitBorderEndColor:
    case CSSPropertyWebkitBorderBeforeColor:
    case CSSPropertyWebkitBorderAfterColor:
    case CSSPropertyWebkitTextStrokeColor:
    case CSSPropertyStopColor:
    case CSSPropertyFloodColor:
    case CSSPropertyLightingColor:
    case CSSPropertyColumnRuleColor:
        return consumeColor(m_range, m_context.mode());
    case CSSPropertyColor:
    case CSSPropertyBackgroundColor:
        return consumeColor(m_range, m_context.mode(), inQuirksMode());
    case CSSPropertyWebkitBorderStartWidth:
    case CSSPropertyWebkitBorderEndWidth:
    case CSSPropertyWebkitBorderBeforeWidth:
    case CSSPropertyWebkitBorderAfterWidth:
        return consumeBorderWidth(m_range, m_context.mode(), UnitlessQuirk::Forbid);
    case CSSPropertyBorderBottomColor:
    case CSSPropertyBorderLeftColor:
    case CSSPropertyBorderRightColor:
    case CSSPropertyBorderTopColor: {
        bool allowQuirkyColors = inQuirksMode()
            && (m_currentShorthand == CSSPropertyInvalid || m_currentShorthand == CSSPropertyBorderColor);
        return consumeColor(m_range, m_context.mode(), allowQuirkyColors);
    }
    case CSSPropertyBorderBottomWidth:
    case CSSPropertyBorderLeftWidth:
    case CSSPropertyBorderRightWidth:
    case CSSPropertyBorderTopWidth: {
        bool allowQuirkyLengths = inQuirksMode()
            && (m_currentShorthand == CSSPropertyInvalid || m_currentShorthand == CSSPropertyBorderWidth);
        UnitlessQuirk unitless = allowQuirkyLengths ? UnitlessQuirk::Allow : UnitlessQuirk::Forbid;
        return consumeBorderWidth(m_range, m_context.mode(), unitless);
    }
    case CSSPropertyZIndex:
        return consumeZIndex(m_range);
    case CSSPropertyTextShadow: // CSS2 property, dropped in CSS2.1, back in CSS3, so treat as CSS3
    case CSSPropertyBoxShadow:
        return consumeShadow(m_range, m_context.mode(), property == CSSPropertyBoxShadow);
    case CSSPropertyWebkitFilter:
    case CSSPropertyBackdropFilter:
        return consumeFilter(m_range, m_context.mode());
    case CSSPropertyTextDecoration:
        ASSERT(!RuntimeEnabledFeatures::css3TextDecorationsEnabled());
        // fallthrough
    case CSSPropertyWebkitTextDecorationsInEffect:
    case CSSPropertyTextDecorationLine:
        return consumeTextDecorationLine(m_range);
    case CSSPropertyD:
    case CSSPropertyMotionPath:
        return consumePathOrNone(m_range);
    case CSSPropertyMotionOffset:
        return consumeLengthOrPercent(m_range, m_context.mode(), ValueRangeAll);
    case CSSPropertyMotionRotation:
        return consumeMotionRotation(m_range);
    case CSSPropertyWebkitTextEmphasisStyle:
        return consumeTextEmphasisStyle(m_range);
    case CSSPropertyOutlineColor:
        return consumeOutlineColor(m_range, m_context.mode());
    case CSSPropertyOutlineOffset:
        return consumeLength(m_range, m_context.mode(), ValueRangeAll);
    case CSSPropertyOutlineWidth:
        return consumeLineWidth(m_range, m_context.mode(), UnitlessQuirk::Forbid);
    case CSSPropertyTransform:
        return consumeTransform(m_range, m_context.mode(), unresolvedProperty == CSSPropertyAliasWebkitTransform);
    case CSSPropertyWebkitTransformOriginX:
    case CSSPropertyWebkitPerspectiveOriginX:
        return consumePositionX(m_range, m_context.mode());
    case CSSPropertyWebkitTransformOriginY:
    case CSSPropertyWebkitPerspectiveOriginY:
        return consumePositionY(m_range, m_context.mode());
    case CSSPropertyWebkitTransformOriginZ:
        return consumeLength(m_range, m_context.mode(), ValueRangeAll);
    case CSSPropertyFill:
    case CSSPropertyStroke:
        return consumePaintStroke(m_range, m_context.mode());
    case CSSPropertyPaintOrder:
        return consumePaintOrder(m_range);
    case CSSPropertyMarkerStart:
    case CSSPropertyMarkerMid:
    case CSSPropertyMarkerEnd:
    case CSSPropertyClipPath:
    case CSSPropertyFilter:
    case CSSPropertyMask:
        return consumeNoneOrURI(m_range);
    case CSSPropertyFlexBasis:
        return consumeFlexBasis(m_range, m_context.mode());
    case CSSPropertyFlexGrow:
    case CSSPropertyFlexShrink:
        return consumeNumber(m_range, ValueRangeNonNegative);
    case CSSPropertyStrokeDasharray:
        return consumeStrokeDasharray(m_range);
    case CSSPropertyColumnRuleWidth:
        return consumeColumnRuleWidth(m_range, m_context.mode());
    case CSSPropertyStrokeOpacity:
    case CSSPropertyFillOpacity:
    case CSSPropertyStopOpacity:
    case CSSPropertyFloodOpacity:
    case CSSPropertyOpacity:
    case CSSPropertyWebkitBoxFlex:
        return consumeNumber(m_range, ValueRangeAll);
    case CSSPropertyBaselineShift:
        return consumeBaselineShift(m_range);
    case CSSPropertyStrokeMiterlimit:
        return consumeNumber(m_range, ValueRangeNonNegative);
    case CSSPropertyStrokeWidth:
    case CSSPropertyStrokeDashoffset:
    case CSSPropertyCx:
    case CSSPropertyCy:
    case CSSPropertyX:
    case CSSPropertyY:
    case CSSPropertyR:
    case CSSPropertyRx:
    case CSSPropertyRy:
        return consumeLengthOrPercent(m_range, SVGAttributeMode, ValueRangeAll, UnitlessQuirk::Forbid);
    case CSSPropertyCursor:
        return consumeCursor(m_range, m_context, inQuirksMode());
    case CSSPropertyContain:
        return consumeContain(m_range);
    case CSSPropertyTransformOrigin:
        return consumeTransformOrigin(m_range, m_context.mode(), UnitlessQuirk::Forbid);
    case CSSPropertyContent:
        return consumeContent(m_range, m_context);
    case CSSPropertyListStyleImage:
    case CSSPropertyBorderImageSource:
    case CSSPropertyWebkitMaskBoxImageSource:
        return consumeImageOrNone(m_range, m_context);
    case CSSPropertyPerspective:
        return consumePerspective(m_range, m_context.mode(), unresolvedProperty);
    case CSSPropertyScrollSnapCoordinate:
        return consumeScrollSnapCoordinate(m_range, m_context.mode());
    case CSSPropertyScrollSnapPointsX:
    case CSSPropertyScrollSnapPointsY:
        return consumeScrollSnapPoints(m_range, m_context.mode());
    case CSSPropertyBorderTopRightRadius:
    case CSSPropertyBorderTopLeftRadius:
    case CSSPropertyBorderBottomLeftRadius:
    case CSSPropertyBorderBottomRightRadius:
        return consumeBorderRadiusCorner(m_range, m_context.mode());
    case CSSPropertyWebkitBoxFlexGroup:
        return consumeInteger(m_range, 0);
    case CSSPropertyOrder:
        return consumeInteger(m_range);
    case CSSPropertyTextUnderlinePosition:
        // auto | [ under || [ left | right ] ], but we only support auto | under for now
        ASSERT(RuntimeEnabledFeatures::css3TextDecorationsEnabled());
        return consumeIdent<CSSValueAuto, CSSValueUnder>(m_range);
    case CSSPropertyVerticalAlign:
        return consumeVerticalAlign(m_range, m_context.mode());
    case CSSPropertyShapeOutside:
        return consumeShapeOutside(m_range, m_context);
    case CSSPropertyWebkitClipPath:
        return consumeClipPath(m_range, m_context);
    case CSSPropertyJustifyContent:
    case CSSPropertyAlignContent:
        ASSERT(RuntimeEnabledFeatures::cssGridLayoutEnabled());
        return consumeContentDistributionOverflowPosition(m_range);
    case CSSPropertyBorderImageRepeat:
    case CSSPropertyWebkitMaskBoxImageRepeat:
        return consumeBorderImageRepeat(m_range);
    case CSSPropertyBorderImageSlice:
    case CSSPropertyWebkitMaskBoxImageSlice:
        return consumeBorderImageSlice(property, m_range, m_context.mode());
    case CSSPropertyBorderImageOutset:
    case CSSPropertyWebkitMaskBoxImageOutset:
        return consumeBorderImageOutset(m_range);
    case CSSPropertyBorderImageWidth:
    case CSSPropertyWebkitMaskBoxImageWidth:
        return consumeBorderImageWidth(m_range);
    case CSSPropertyWebkitBorderImage:
        return consumeWebkitBorderImage(property, m_range, m_context);
    case CSSPropertyWebkitBoxReflect:
        return consumeReflect(m_range, m_context);
    case CSSPropertyFontSizeAdjust:
        ASSERT(RuntimeEnabledFeatures::cssFontSizeAdjustEnabled());
        return consumeFontSizeAdjust(m_range);
    case CSSPropertyImageOrientation:
        ASSERT(RuntimeEnabledFeatures::imageOrientationEnabled());
        return consumeImageOrientation(m_range);
    case CSSPropertyBackgroundAttachment:
    case CSSPropertyBackgroundBlendMode:
    case CSSPropertyBackgroundClip:
    case CSSPropertyBackgroundImage:
    case CSSPropertyBackgroundOrigin:
    case CSSPropertyBackgroundPositionX:
    case CSSPropertyBackgroundPositionY:
    case CSSPropertyBackgroundSize:
    case CSSPropertyMaskSourceType:
    case CSSPropertyWebkitBackgroundClip:
    case CSSPropertyWebkitBackgroundOrigin:
    case CSSPropertyWebkitMaskClip:
    case CSSPropertyWebkitMaskComposite:
    case CSSPropertyWebkitMaskImage:
    case CSSPropertyWebkitMaskOrigin:
    case CSSPropertyWebkitMaskPositionX:
    case CSSPropertyWebkitMaskPositionY:
    case CSSPropertyWebkitMaskSize:
        return consumeCommaSeparatedBackgroundComponent(unresolvedProperty, m_range, m_context);
    case CSSPropertyWebkitMaskRepeatX:
    case CSSPropertyWebkitMaskRepeatY:
        return nullptr;
    case CSSPropertyJustifySelf:
    case CSSPropertyAlignSelf:
    case CSSPropertyAlignItems:
        ASSERT(RuntimeEnabledFeatures::cssGridLayoutEnabled());
        return consumeSelfPositionOverflowPosition(m_range);
    case CSSPropertyJustifyItems:
        ASSERT(RuntimeEnabledFeatures::cssGridLayoutEnabled());
        return consumeJustifyItems(m_range);
    case CSSPropertyGridColumnEnd:
    case CSSPropertyGridColumnStart:
    case CSSPropertyGridRowEnd:
    case CSSPropertyGridRowStart:
        ASSERT(RuntimeEnabledFeatures::cssGridLayoutEnabled());
        return consumeGridLine(m_range);
    case CSSPropertyGridAutoColumns:
    case CSSPropertyGridAutoRows:
        ASSERT(RuntimeEnabledFeatures::cssGridLayoutEnabled());
        return consumeGridTrackSize(m_range, m_context.mode());
    case CSSPropertyGridTemplateColumns:
    case CSSPropertyGridTemplateRows:
        ASSERT(RuntimeEnabledFeatures::cssGridLayoutEnabled());
        return consumeGridTemplatesRowsOrColumns(m_range, m_context.mode());
    case CSSPropertyGridTemplateAreas:
        ASSERT(RuntimeEnabledFeatures::cssGridLayoutEnabled());
        return consumeGridTemplateAreas(m_range);
    default:
        CSSParserValueList valueList(m_range);
        if (valueList.size()) {
            m_valueList = &valueList;
            if (CSSValue* result = legacyParseValue(unresolvedProperty)) {
                while (!m_range.atEnd())
                    m_range.consume();
                return result;
            }
        }
        return nullptr;
    }
}

static CSSValueList* consumeFontFaceUnicodeRange(CSSParserTokenRange& range)
{
    CSSValueList* values = CSSValueList::createCommaSeparated();

    do {
        const CSSParserToken& token = range.consumeIncludingWhitespace();
        if (token.type() != UnicodeRangeToken)
            return nullptr;

        UChar32 start = token.unicodeRangeStart();
        UChar32 end = token.unicodeRangeEnd();
        if (start > end)
            return nullptr;
        values->append(CSSUnicodeRangeValue::create(start, end));
    } while (consumeCommaIncludingWhitespace(range));

    return values;
}

static CSSValue* consumeFontFaceSrcURI(CSSParserTokenRange& range, const CSSParserContext& context)
{
    String url = consumeUrl(range);
    if (url.isNull())
        return nullptr;
    CSSFontFaceSrcValue* uriValue(CSSFontFaceSrcValue::create(url, context.completeURL(url), context.shouldCheckContentSecurityPolicy()));

    if (range.peek().functionId() != CSSValueFormat)
        return uriValue;

    // FIXME: https://drafts.csswg.org/css-fonts says that format() contains a comma-separated list of strings,
    // but CSSFontFaceSrcValue stores only one format. Allowing one format for now.
    // FIXME: IdentToken should not be supported here.
    CSSParserTokenRange args = consumeFunction(range);
    const CSSParserToken& arg = args.consumeIncludingWhitespace();
    if ((arg.type() != StringToken && arg.type() != IdentToken) || !args.atEnd())
        return nullptr;
    uriValue->setFormat(arg.value());
    return uriValue;
}

static CSSValue* consumeFontFaceSrcLocal(CSSParserTokenRange& range, const CSSParserContext& context)
{
    CSSParserTokenRange args = consumeFunction(range);
    ContentSecurityPolicyDisposition shouldCheckContentSecurityPolicy = context.shouldCheckContentSecurityPolicy();
    if (args.peek().type() == StringToken) {
        const CSSParserToken& arg = args.consumeIncludingWhitespace();
        if (!args.atEnd())
            return nullptr;
        return CSSFontFaceSrcValue::createLocal(arg.value(), shouldCheckContentSecurityPolicy);
    }
    if (args.peek().type() == IdentToken) {
        String familyName = concatenateFamilyName(args);
        if (!args.atEnd())
            return nullptr;
        return CSSFontFaceSrcValue::createLocal(familyName, shouldCheckContentSecurityPolicy);
    }
    return nullptr;
}

static CSSValueList* consumeFontFaceSrc(CSSParserTokenRange& range, const CSSParserContext& context)
{
    CSSValueList* values = CSSValueList::createCommaSeparated();

    do {
        const CSSParserToken& token = range.peek();
        CSSValue* parsedValue = nullptr;
        if (token.functionId() == CSSValueLocal)
            parsedValue = consumeFontFaceSrcLocal(range, context);
        else
            parsedValue = consumeFontFaceSrcURI(range, context);
        if (!parsedValue)
            return nullptr;
        values->append(parsedValue);
    } while (consumeCommaIncludingWhitespace(range));
    return values;
}

bool CSSPropertyParser::parseFontFaceDescriptor(CSSPropertyID propId)
{
    CSSValue* parsedValue = nullptr;
    switch (propId) {
    case CSSPropertyFontFamily:
        if (consumeGenericFamily(m_range))
            return false;
        parsedValue = consumeFamilyName(m_range);
        break;
    case CSSPropertySrc: // This is a list of urls or local references.
        parsedValue = consumeFontFaceSrc(m_range, m_context);
        break;
    case CSSPropertyUnicodeRange:
        parsedValue = consumeFontFaceUnicodeRange(m_range);
        break;
    case CSSPropertyFontDisplay:
    case CSSPropertyFontStretch:
    case CSSPropertyFontStyle: {
        CSSValueID id = m_range.consumeIncludingWhitespace().id();
        if (!CSSParserFastPaths::isValidKeywordPropertyAndValue(propId, id))
            return false;
        parsedValue = cssValuePool().createIdentifierValue(id);
        break;
    }
    case CSSPropertyFontVariant:
        parsedValue = consumeFontVariantList(m_range);
        break;
    case CSSPropertyFontWeight:
        parsedValue = consumeFontWeight(m_range);
        break;
    case CSSPropertyFontFeatureSettings:
        parsedValue = consumeFontFeatureSettings(m_range);
        break;
    default:
        break;
    }

    if (!parsedValue || !m_range.atEnd())
        return false;

    addProperty(propId, parsedValue, false);
    return true;
}

bool CSSPropertyParser::consumeSystemFont(bool important)
{
    CSSValueID systemFontID = m_range.consumeIncludingWhitespace().id();
    ASSERT(systemFontID >= CSSValueCaption && systemFontID <= CSSValueStatusBar);
    if (!m_range.atEnd())
        return false;

    FontStyle fontStyle = FontStyleNormal;
    FontWeight fontWeight = FontWeightNormal;
    float fontSize = 0;
    AtomicString fontFamily;
    LayoutTheme::theme().systemFont(systemFontID, fontStyle, fontWeight, fontSize, fontFamily);

    addProperty(CSSPropertyFontStyle, cssValuePool().createIdentifierValue(fontStyle == FontStyleItalic ? CSSValueItalic : CSSValueNormal), important);
    addProperty(CSSPropertyFontWeight, cssValuePool().createValue(fontWeight), important);
    addProperty(CSSPropertyFontSize, cssValuePool().createValue(fontSize, CSSPrimitiveValue::UnitType::Pixels), important);
    CSSValueList* fontFamilyList = CSSValueList::createCommaSeparated();
    fontFamilyList->append(cssValuePool().createFontFamilyValue(fontFamily));
    addProperty(CSSPropertyFontFamily, fontFamilyList, important);

    addProperty(CSSPropertyFontStretch, cssValuePool().createIdentifierValue(CSSValueNormal), important);
    addProperty(CSSPropertyFontVariant, cssValuePool().createIdentifierValue(CSSValueNormal), important);
    addProperty(CSSPropertyLineHeight, cssValuePool().createIdentifierValue(CSSValueNormal), important);
    return true;
}

bool CSSPropertyParser::consumeFont(bool important)
{
    // Let's check if there is an inherit or initial somewhere in the shorthand.
    CSSParserTokenRange range = m_range;
    while (!range.atEnd()) {
        CSSValueID id = range.consumeIncludingWhitespace().id();
        if (id == CSSValueInherit || id == CSSValueInitial)
            return false;
    }
    // Optional font-style, font-variant, font-stretch and font-weight.
    CSSPrimitiveValue* fontStyle = nullptr;
    CSSPrimitiveValue* fontVariant = nullptr;
    CSSPrimitiveValue* fontWeight = nullptr;
    CSSPrimitiveValue* fontStretch = nullptr;
    while (!m_range.atEnd()) {
        CSSValueID id = m_range.peek().id();
        if (!fontStyle && CSSParserFastPaths::isValidKeywordPropertyAndValue(CSSPropertyFontStyle, id)) {
            fontStyle = consumeIdent(m_range);
            continue;
        }
        if (!fontVariant) {
            // Font variant in the shorthand is particular, it only accepts normal or small-caps.
            fontVariant = consumeFontVariant(m_range);
            if (fontVariant)
                continue;
        }
        if (!fontWeight) {
            fontWeight = consumeFontWeight(m_range);
            if (fontWeight)
                continue;
        }
        if (!fontStretch && CSSParserFastPaths::isValidKeywordPropertyAndValue(CSSPropertyFontStretch, id))
            fontStretch = consumeIdent(m_range);
        else
            break;
    }

    if (m_range.atEnd())
        return false;

    addProperty(CSSPropertyFontStyle, fontStyle ? fontStyle : cssValuePool().createIdentifierValue(CSSValueNormal), important);
    addProperty(CSSPropertyFontVariant, fontVariant ? fontVariant : cssValuePool().createIdentifierValue(CSSValueNormal), important);
    addProperty(CSSPropertyFontWeight, fontWeight ? fontWeight : cssValuePool().createIdentifierValue(CSSValueNormal), important);
    addProperty(CSSPropertyFontStretch, fontStretch ? fontStretch : cssValuePool().createIdentifierValue(CSSValueNormal), important);

    // Now a font size _must_ come.
    CSSValue* fontSize = consumeFontSize(m_range, m_context.mode());
    if (!fontSize || m_range.atEnd())
        return false;

    addProperty(CSSPropertyFontSize, fontSize, important);

    if (consumeSlashIncludingWhitespace(m_range)) {
        CSSPrimitiveValue* lineHeight = consumeLineHeight(m_range, m_context.mode());
        if (!lineHeight)
            return false;
        addProperty(CSSPropertyLineHeight, lineHeight, important);
    } else {
        addProperty(CSSPropertyLineHeight, cssValuePool().createIdentifierValue(CSSValueNormal), important);
    }

    // Font family must come now.
    CSSValue* parsedFamilyValue = consumeFontFamily(m_range);
    if (!parsedFamilyValue)
        return false;

    addProperty(CSSPropertyFontFamily, parsedFamilyValue, important);

    // FIXME: http://www.w3.org/TR/2011/WD-css3-fonts-20110324/#font-prop requires that
    // "font-stretch", "font-size-adjust", and "font-kerning" be reset to their initial values
    // but we don't seem to support them at the moment. They should also be added here once implemented.
    return m_range.atEnd();
}

bool CSSPropertyParser::consumeBorderSpacing(bool important)
{
    CSSValue* horizontalSpacing = consumeLength(m_range, m_context.mode(), ValueRangeNonNegative, UnitlessQuirk::Allow);
    if (!horizontalSpacing)
        return false;
    CSSValue* verticalSpacing = horizontalSpacing;
    if (!m_range.atEnd())
        verticalSpacing = consumeLength(m_range, m_context.mode(), ValueRangeNonNegative, UnitlessQuirk::Allow);
    if (!verticalSpacing || !m_range.atEnd())
        return false;
    addProperty(CSSPropertyWebkitBorderHorizontalSpacing, horizontalSpacing, important);
    addProperty(CSSPropertyWebkitBorderVerticalSpacing, verticalSpacing, important);
    return true;
}

static CSSValue* consumeSingleViewportDescriptor(CSSParserTokenRange& range, CSSPropertyID propId, CSSParserMode cssParserMode)
{
    CSSValueID id = range.peek().id();
    switch (propId) {
    case CSSPropertyMinWidth:
    case CSSPropertyMaxWidth:
    case CSSPropertyMinHeight:
    case CSSPropertyMaxHeight:
        if (id == CSSValueAuto || id == CSSValueInternalExtendToZoom)
            return consumeIdent(range);
        return consumeLengthOrPercent(range, cssParserMode, ValueRangeNonNegative);
    case CSSPropertyMinZoom:
    case CSSPropertyMaxZoom:
    case CSSPropertyZoom: {
        if (id == CSSValueAuto)
            return consumeIdent(range);
        CSSValue* parsedValue = consumeNumber(range, ValueRangeNonNegative);
        if (parsedValue)
            return parsedValue;
        return consumePercent(range, ValueRangeNonNegative);
    }
    case CSSPropertyUserZoom:
        return consumeIdent<CSSValueZoom, CSSValueFixed>(range);
    case CSSPropertyOrientation:
        return consumeIdent<CSSValueAuto, CSSValuePortrait, CSSValueLandscape>(range);
    default:
        ASSERT_NOT_REACHED();
        break;
    }

    ASSERT_NOT_REACHED();
    return nullptr;
}

bool CSSPropertyParser::parseViewportDescriptor(CSSPropertyID propId, bool important)
{
    ASSERT(RuntimeEnabledFeatures::cssViewportEnabled() || isUASheetBehavior(m_context.mode()));

    switch (propId) {
    case CSSPropertyWidth: {
        CSSValue* minWidth = consumeSingleViewportDescriptor(m_range, CSSPropertyMinWidth, m_context.mode());
        if (!minWidth)
            return false;
        CSSValue* maxWidth = minWidth;
        if (!m_range.atEnd())
            maxWidth = consumeSingleViewportDescriptor(m_range, CSSPropertyMaxWidth, m_context.mode());
        if (!maxWidth || !m_range.atEnd())
            return false;
        addProperty(CSSPropertyMinWidth, minWidth, important);
        addProperty(CSSPropertyMaxWidth, maxWidth, important);
        return true;
    }
    case CSSPropertyHeight: {
        CSSValue* minHeight = consumeSingleViewportDescriptor(m_range, CSSPropertyMinHeight, m_context.mode());
        if (!minHeight)
            return false;
        CSSValue* maxHeight = minHeight;
        if (!m_range.atEnd())
            maxHeight = consumeSingleViewportDescriptor(m_range, CSSPropertyMaxHeight, m_context.mode());
        if (!maxHeight || !m_range.atEnd())
            return false;
        addProperty(CSSPropertyMinHeight, minHeight, important);
        addProperty(CSSPropertyMaxHeight, maxHeight, important);
        return true;
    }
    case CSSPropertyMinWidth:
    case CSSPropertyMaxWidth:
    case CSSPropertyMinHeight:
    case CSSPropertyMaxHeight:
    case CSSPropertyMinZoom:
    case CSSPropertyMaxZoom:
    case CSSPropertyZoom:
    case CSSPropertyUserZoom:
    case CSSPropertyOrientation: {
        CSSValue* parsedValue = consumeSingleViewportDescriptor(m_range, propId, m_context.mode());
        if (!parsedValue || !m_range.atEnd())
            return false;
        addProperty(propId, parsedValue, important);
        return true;
    }
    default:
        return false;
    }
}

static bool consumeColumnWidthOrCount(CSSParserTokenRange& range, CSSParserMode cssParserMode, CSSValue*& columnWidth, CSSValue*& columnCount)
{
    if (range.peek().id() == CSSValueAuto) {
        consumeIdent(range);
        return true;
    }
    if (!columnWidth) {
        columnWidth = consumeColumnWidth(range);
        if (columnWidth)
            return true;
    }
    if (!columnCount)
        columnCount = consumeColumnCount(range);
    return columnCount;
}

bool CSSPropertyParser::consumeColumns(bool important)
{
    CSSValue* columnWidth = nullptr;
    CSSValue* columnCount = nullptr;
    if (!consumeColumnWidthOrCount(m_range, m_context.mode(), columnWidth, columnCount))
        return false;
    consumeColumnWidthOrCount(m_range, m_context.mode(), columnWidth, columnCount);
    if (!m_range.atEnd())
        return false;
    if (!columnWidth)
        columnWidth = cssValuePool().createIdentifierValue(CSSValueAuto);
    if (!columnCount)
        columnCount = cssValuePool().createIdentifierValue(CSSValueAuto);
    addProperty(CSSPropertyColumnWidth, columnWidth, important);
    addProperty(CSSPropertyColumnCount, columnCount, important);
    return true;
}

bool CSSPropertyParser::consumeShorthandGreedily(const StylePropertyShorthand& shorthand, bool important)
{
    ASSERT(shorthand.length() <= 6); // Existing shorthands have at most 6 longhands.
    CSSValue* longhands[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
    const CSSPropertyID* shorthandProperties = shorthand.properties();
    do {
        bool foundLonghand = false;
        for (size_t i = 0; !foundLonghand && i < shorthand.length(); ++i) {
            if (longhands[i])
                continue;
            longhands[i] = parseSingleValue(shorthandProperties[i]);
            if (longhands[i])
                foundLonghand = true;
        }
        if (!foundLonghand)
            return false;
    } while (!m_range.atEnd());

    for (size_t i = 0; i < shorthand.length(); ++i) {
        if (longhands[i])
            addProperty(shorthandProperties[i], longhands[i], important);
        else
            addProperty(shorthandProperties[i], cssValuePool().createImplicitInitialValue(), important);
    }
    return true;
}

bool CSSPropertyParser::consumeFlex(bool important)
{
    static const double unsetValue = -1;
    double flexGrow = unsetValue;
    double flexShrink = unsetValue;
    CSSPrimitiveValue* flexBasis = nullptr;

    if (m_range.peek().id() == CSSValueNone) {
        flexGrow = 0;
        flexShrink = 0;
        flexBasis = cssValuePool().createIdentifierValue(CSSValueAuto);
        m_range.consumeIncludingWhitespace();
    } else {
        unsigned index = 0;
        while (!m_range.atEnd() && index++ < 3) {
            double num;
            if (consumeNumberRaw(m_range, num)) {
                if (num < 0)
                    return false;
                if (flexGrow == unsetValue)
                    flexGrow = num;
                else if (flexShrink == unsetValue)
                    flexShrink = num;
                else if (!num) // flex only allows a basis of 0 (sans units) if flex-grow and flex-shrink values have already been set.
                    flexBasis = cssValuePool().createValue(0, CSSPrimitiveValue::UnitType::Pixels);
                else
                    return false;
            } else if (!flexBasis) {
                if (m_range.peek().id() == CSSValueAuto)
                    flexBasis = consumeIdent(m_range);
                if (!flexBasis)
                    flexBasis = consumeLengthOrPercent(m_range, m_context.mode(), ValueRangeNonNegative);
                if (index == 2 && !m_range.atEnd())
                    return false;
            }
        }
        if (index == 0)
            return false;
        if (flexGrow == unsetValue)
            flexGrow = 1;
        if (flexShrink == unsetValue)
            flexShrink = 1;
        if (!flexBasis)
            flexBasis = cssValuePool().createValue(0, CSSPrimitiveValue::UnitType::Percentage);
    }

    if (!m_range.atEnd())
        return false;
    addProperty(CSSPropertyFlexGrow, cssValuePool().createValue(clampTo<float>(flexGrow), CSSPrimitiveValue::UnitType::Number), important);
    addProperty(CSSPropertyFlexShrink, cssValuePool().createValue(clampTo<float>(flexShrink), CSSPrimitiveValue::UnitType::Number), important);
    addProperty(CSSPropertyFlexBasis, flexBasis, important);
    return true;
}

bool CSSPropertyParser::consumeBorder(bool important)
{
    CSSValue* width = nullptr;
    CSSValue* style = nullptr;
    CSSValue* color = nullptr;

    while (!width || !style || !color) {
        if (!width) {
            width = consumeLineWidth(m_range, m_context.mode(), UnitlessQuirk::Forbid);
            if (width)
                continue;
        }
        if (!style) {
            style = parseSingleValue(CSSPropertyBorderLeftStyle);
            if (style)
                continue;
        }
        if (!color) {
            color = consumeColor(m_range, m_context.mode());
            if (color)
                continue;
        }
        break;
    }

    if (!width && !style && !color)
        return false;

    if (!width)
        width = cssValuePool().createImplicitInitialValue();
    if (!style)
        style = cssValuePool().createImplicitInitialValue();
    if (!color)
        color = cssValuePool().createImplicitInitialValue();

    addExpandedPropertyForValue(CSSPropertyBorderWidth, width, important);
    addExpandedPropertyForValue(CSSPropertyBorderStyle, style, important);
    addExpandedPropertyForValue(CSSPropertyBorderColor, color, important);
    addExpandedPropertyForValue(CSSPropertyBorderImage, cssValuePool().createImplicitInitialValue(), important);

    return m_range.atEnd();
}

bool CSSPropertyParser::consume4Values(const StylePropertyShorthand& shorthand, bool important)
{
    ASSERT(shorthand.length() == 4);
    const CSSPropertyID* longhands = shorthand.properties();
    CSSValue* top = parseSingleValue(longhands[0]);
    if (!top)
        return false;

    CSSValue* right = parseSingleValue(longhands[1]);
    CSSValue* bottom = nullptr;
    CSSValue* left = nullptr;
    if (right) {
        bottom = parseSingleValue(longhands[2]);
        if (bottom)
            left = parseSingleValue(longhands[3]);
    }

    if (!right)
        right = top;
    if (!bottom)
        bottom = top;
    if (!left)
        left = right;

    addProperty(longhands[0], top, important);
    addProperty(longhands[1], right, important);
    addProperty(longhands[2], bottom, important);
    addProperty(longhands[3], left, important);

    return m_range.atEnd();
}

bool CSSPropertyParser::consumeBorderImage(CSSPropertyID property, bool important)
{
    CSSValue* source = nullptr;
    CSSValue* slice = nullptr;
    CSSValue* width = nullptr;
    CSSValue* outset = nullptr;
    CSSValue* repeat = nullptr;
    if (consumeBorderImageComponents(property, m_range, m_context, source, slice, width, outset, repeat)) {
        switch (property) {
        case CSSPropertyWebkitMaskBoxImage:
            addProperty(CSSPropertyWebkitMaskBoxImageSource, source ? source : cssValuePool().createImplicitInitialValue(), important);
            addProperty(CSSPropertyWebkitMaskBoxImageSlice, slice ? slice : cssValuePool().createImplicitInitialValue(), important);
            addProperty(CSSPropertyWebkitMaskBoxImageWidth, width ? width : cssValuePool().createImplicitInitialValue(), important);
            addProperty(CSSPropertyWebkitMaskBoxImageOutset, outset ? outset : cssValuePool().createImplicitInitialValue(), important);
            addProperty(CSSPropertyWebkitMaskBoxImageRepeat, repeat ? repeat : cssValuePool().createImplicitInitialValue(), important);
            return true;
        case CSSPropertyBorderImage:
            addProperty(CSSPropertyBorderImageSource, source ? source : cssValuePool().createImplicitInitialValue(), important);
            addProperty(CSSPropertyBorderImageSlice, slice ? slice : cssValuePool().createImplicitInitialValue(), important);
            addProperty(CSSPropertyBorderImageWidth, width ? width : cssValuePool().createImplicitInitialValue(), important);
            addProperty(CSSPropertyBorderImageOutset, outset ? outset : cssValuePool().createImplicitInitialValue(), important);
            addProperty(CSSPropertyBorderImageRepeat, repeat ? repeat : cssValuePool().createImplicitInitialValue(), important);
            return true;
        default:
            ASSERT_NOT_REACHED();
            return false;
        }
    }
    return false;
}

static inline CSSValueID mapFromPageBreakBetween(CSSValueID value)
{
    if (value == CSSValueAlways)
        return CSSValuePage;
    if (value == CSSValueAuto || value == CSSValueAvoid || value == CSSValueLeft || value == CSSValueRight)
        return value;
    return CSSValueInvalid;
}

static inline CSSValueID mapFromColumnBreakBetween(CSSValueID value)
{
    if (value == CSSValueAlways)
        return CSSValueColumn;
    if (value == CSSValueAuto || value == CSSValueAvoid)
        return value;
    return CSSValueInvalid;
}

static inline CSSValueID mapFromColumnOrPageBreakInside(CSSValueID value)
{
    if (value == CSSValueAuto || value == CSSValueAvoid)
        return value;
    return CSSValueInvalid;
}

static inline CSSPropertyID mapFromLegacyBreakProperty(CSSPropertyID property)
{
    if (property == CSSPropertyPageBreakAfter || property == CSSPropertyWebkitColumnBreakAfter)
        return CSSPropertyBreakAfter;
    if (property == CSSPropertyPageBreakBefore || property == CSSPropertyWebkitColumnBreakBefore)
        return CSSPropertyBreakBefore;
    ASSERT(property == CSSPropertyPageBreakInside || property == CSSPropertyWebkitColumnBreakInside);
    return CSSPropertyBreakInside;
}

bool CSSPropertyParser::consumeLegacyBreakProperty(CSSPropertyID property, bool important)
{
    // The fragmentation spec says that page-break-(after|before|inside) are to be treated as
    // shorthands for their break-(after|before|inside) counterparts. We'll do the same for the
    // non-standard properties -webkit-column-break-(after|before|inside).
    CSSPrimitiveValue* keyword = consumeIdent(m_range);
    if (!keyword)
        return false;
    if (!m_range.atEnd())
        return false;
    CSSValueID value = keyword->getValueID();
    switch (property) {
    case CSSPropertyPageBreakAfter:
    case CSSPropertyPageBreakBefore:
        value = mapFromPageBreakBetween(value);
        break;
    case CSSPropertyWebkitColumnBreakAfter:
    case CSSPropertyWebkitColumnBreakBefore:
        value = mapFromColumnBreakBetween(value);
        break;
    case CSSPropertyPageBreakInside:
    case CSSPropertyWebkitColumnBreakInside:
        value = mapFromColumnOrPageBreakInside(value);
        break;
    default:
        ASSERT_NOT_REACHED();
    }
    if (value == CSSValueInvalid)
        return false;

    CSSPropertyID genericBreakProperty = mapFromLegacyBreakProperty(property);
    addProperty(genericBreakProperty, cssValuePool().createIdentifierValue(value), important);
    return true;
}

static bool consumeBackgroundPosition(CSSParserTokenRange& range, const CSSParserContext& context, UnitlessQuirk unitless, CSSValue*& resultX, CSSValue*& resultY)
{
    do {
        CSSValue* positionX = nullptr;
        CSSValue* positionY = nullptr;
        if (!consumePosition(range, context.mode(), unitless, positionX, positionY))
            return false;
        addBackgroundValue(resultX, positionX);
        addBackgroundValue(resultY, positionY);
    } while (consumeCommaIncludingWhitespace(range));
    return true;
}

static bool consumeRepeatStyleComponent(CSSParserTokenRange& range, CSSValue*& value1, CSSValue*& value2, bool& implicit)
{
    if (consumeIdent<CSSValueRepeatX>(range)) {
        value1 = cssValuePool().createIdentifierValue(CSSValueRepeat);
        value2 = cssValuePool().createIdentifierValue(CSSValueNoRepeat);
        implicit = true;
        return true;
    }
    if (consumeIdent<CSSValueRepeatY>(range)) {
        value1 = cssValuePool().createIdentifierValue(CSSValueNoRepeat);
        value2 = cssValuePool().createIdentifierValue(CSSValueRepeat);
        implicit = true;
        return true;
    }
    value1 = consumeIdent<CSSValueRepeat, CSSValueNoRepeat, CSSValueRound, CSSValueSpace>(range);
    if (!value1)
        return false;

    value2 = consumeIdent<CSSValueRepeat, CSSValueNoRepeat, CSSValueRound, CSSValueSpace>(range);
    if (!value2) {
        value2 = value1;
        implicit = true;
    }
    return true;
}

static bool consumeRepeatStyle(CSSParserTokenRange& range, CSSValue*& resultX, CSSValue*& resultY, bool& implicit)
{
    do {
        CSSValue* repeatX = nullptr;
        CSSValue* repeatY = nullptr;
        if (!consumeRepeatStyleComponent(range, repeatX, repeatY, implicit))
            return false;
        addBackgroundValue(resultX, repeatX);
        addBackgroundValue(resultY, repeatY);
    } while (consumeCommaIncludingWhitespace(range));
    return true;
}

// Note: consumeBackgroundShorthand assumes y properties (for example background-position-y) follow
// the x properties in the shorthand array.
bool CSSPropertyParser::consumeBackgroundShorthand(const StylePropertyShorthand& shorthand, bool important)
{
    const unsigned longhandCount = shorthand.length();
    CSSValue* longhands[10];
    ASSERT(longhandCount <= 10);
#if ENABLE(OILPAN)
    // Zero initialize the array of raw pointers.
    memset(&longhands, 0, sizeof(longhands));
#endif
    bool implicit = false;
    do {
        bool parsedLonghand[10] = { false };
        CSSValue* originValue = nullptr;
        do {
            bool foundProperty = false;
            for (size_t i = 0; i < longhandCount; ++i) {
                if (parsedLonghand[i])
                    continue;

                CSSValue* value = nullptr;
                CSSValue* valueY = nullptr;
                CSSPropertyID property = shorthand.properties()[i];
                if (property == CSSPropertyBackgroundRepeatX || property == CSSPropertyWebkitMaskRepeatX) {
                    consumeRepeatStyleComponent(m_range, value, valueY, implicit);
                } else if (property == CSSPropertyBackgroundPositionX || property == CSSPropertyWebkitMaskPositionX) {
                    CSSParserTokenRange rangeCopy = m_range;
                    if (!consumePosition(rangeCopy, m_context.mode(), UnitlessQuirk::Forbid, value, valueY))
                        continue;
                    m_range = rangeCopy;
                } else if (property == CSSPropertyBackgroundSize || property == CSSPropertyWebkitMaskSize) {
                    if (!consumeSlashIncludingWhitespace(m_range))
                        continue;
                    value = consumeBackgroundSize(property, m_range, m_context.mode());
                    if (!value || !parsedLonghand[i - 1]) // Position must have been parsed in the current layer.
                        return false;
                } else if (property == CSSPropertyBackgroundPositionY || property == CSSPropertyBackgroundRepeatY
                    || property == CSSPropertyWebkitMaskPositionY || property == CSSPropertyWebkitMaskRepeatY) {
                    continue;
                } else {
                    value = consumeBackgroundComponent(property, m_range, m_context);
                }
                if (value) {
                    if (property == CSSPropertyBackgroundOrigin || property == CSSPropertyWebkitMaskOrigin)
                        originValue = value;
                    parsedLonghand[i] = true;
                    foundProperty = true;
                    addBackgroundValue(longhands[i], value);
                    if (valueY) {
                        parsedLonghand[i + 1] = true;
                        addBackgroundValue(longhands[i + 1], valueY);
                    }
                }
            }
            if (!foundProperty)
                return false;
        } while (!m_range.atEnd() && m_range.peek().type() != CommaToken);

        // TODO(timloh): This will make invalid longhands, see crbug.com/386459
        for (size_t i = 0; i < longhandCount; ++i) {
            CSSPropertyID property = shorthand.properties()[i];
            if (property == CSSPropertyBackgroundColor && !m_range.atEnd()) {
                if (parsedLonghand[i])
                    return false; // Colors are only allowed in the last layer.
                continue;
            }
            if ((property == CSSPropertyBackgroundClip || property == CSSPropertyWebkitMaskClip) && !parsedLonghand[i] && originValue) {
                addBackgroundValue(longhands[i], originValue);
                continue;
            }
            if (!parsedLonghand[i])
                addBackgroundValue(longhands[i], cssValuePool().createImplicitInitialValue());
        }
    } while (consumeCommaIncludingWhitespace(m_range));
    if (!m_range.atEnd())
        return false;

    for (size_t i = 0; i < longhandCount; ++i) {
        CSSPropertyID property = shorthand.properties()[i];
        if (property == CSSPropertyBackgroundSize && longhands[i] && m_context.useLegacyBackgroundSizeShorthandBehavior())
            continue;
        addProperty(property, longhands[i], important, implicit);
    }
    return true;
}

bool CSSPropertyParser::consumeGridItemPositionShorthand(CSSPropertyID shorthandId, bool important)
{
    ASSERT(RuntimeEnabledFeatures::cssGridLayoutEnabled());
    const StylePropertyShorthand& shorthand = shorthandForProperty(shorthandId);
    ASSERT(shorthand.length() == 2);
    CSSValue* startValue = consumeGridLine(m_range);
    if (!startValue)
        return false;

    CSSValue* endValue = nullptr;
    if (consumeSlashIncludingWhitespace(m_range)) {
        endValue = consumeGridLine(m_range);
        if (!endValue)
            return false;
    } else {
        endValue = startValue->isCustomIdentValue() ? startValue : cssValuePool().createIdentifierValue(CSSValueAuto);
    }
    if (!m_range.atEnd())
        return false;
    addProperty(shorthand.properties()[0], startValue, important);
    addProperty(shorthand.properties()[1], endValue, important);
    return true;
}

bool CSSPropertyParser::consumeGridAreaShorthand(bool important)
{
    ASSERT(RuntimeEnabledFeatures::cssGridLayoutEnabled());
    ASSERT(gridAreaShorthand().length() == 4);
    CSSValue* rowStartValue = consumeGridLine(m_range);
    if (!rowStartValue)
        return false;
    CSSValue* columnStartValue = nullptr;
    CSSValue* rowEndValue = nullptr;
    CSSValue* columnEndValue = nullptr;
    if (consumeSlashIncludingWhitespace(m_range)) {
        columnStartValue = consumeGridLine(m_range);
        if (!columnStartValue)
            return false;
        if (consumeSlashIncludingWhitespace(m_range)) {
            rowEndValue = consumeGridLine(m_range);
            if (!rowEndValue)
                return false;
            if (consumeSlashIncludingWhitespace(m_range)) {
                columnEndValue = consumeGridLine(m_range);
                if (!columnEndValue)
                    return false;
            }
        }
    }
    if (!m_range.atEnd())
        return false;
    if (!columnStartValue)
        columnStartValue = rowStartValue->isCustomIdentValue() ? rowStartValue : cssValuePool().createIdentifierValue(CSSValueAuto);
    if (!rowEndValue)
        rowEndValue = rowStartValue->isCustomIdentValue() ? rowStartValue : cssValuePool().createIdentifierValue(CSSValueAuto);
    if (!columnEndValue)
        columnEndValue = columnStartValue->isCustomIdentValue() ? columnStartValue : cssValuePool().createIdentifierValue(CSSValueAuto);

    addProperty(CSSPropertyGridRowStart, rowStartValue, important);
    addProperty(CSSPropertyGridColumnStart, columnStartValue, important);
    addProperty(CSSPropertyGridRowEnd, rowEndValue, important);
    addProperty(CSSPropertyGridColumnEnd, columnEndValue, important);
    return true;
}

bool CSSPropertyParser::consumeGridTemplateRowsAndAreasAndColumns(bool important)
{
    NamedGridAreaMap gridAreaMap;
    size_t rowCount = 0;
    size_t columnCount = 0;
    CSSValueList* templateRows = CSSValueList::createSpaceSeparated();

    // Persists between loop iterations so we can use the same value for
    // consecutive <line-names> values
    CSSGridLineNamesValue* lineNames = nullptr;

    do {
        // Handle leading <custom-ident>*.
        bool hasPreviousLineNames = lineNames;
        lineNames = consumeGridLineNames(m_range, lineNames);
        if (lineNames && !hasPreviousLineNames)
            templateRows->append(lineNames);

        // Handle a template-area's row.
        if (m_range.peek().type() != StringToken || !parseGridTemplateAreasRow(m_range.consumeIncludingWhitespace().value(), gridAreaMap, rowCount, columnCount))
            return false;
        ++rowCount;

        // Handle template-rows's track-size.
        CSSValue* value = consumeGridTrackSize(m_range, m_context.mode());
        if (!value)
            value = cssValuePool().createIdentifierValue(CSSValueAuto);
        templateRows->append(value);

        // This will handle the trailing/leading <custom-ident>* in the grammar.
        lineNames = consumeGridLineNames(m_range);
        if (lineNames)
            templateRows->append(lineNames);
    } while (!m_range.atEnd() && !(m_range.peek().type() == DelimiterToken && m_range.peek().delimiter() == '/'));

    CSSValue* columnsValue = nullptr;
    if (!m_range.atEnd()) {
        if (!consumeSlashIncludingWhitespace(m_range))
            return false;
        columnsValue = consumeGridTrackList(m_range, m_context.mode());
        if (!columnsValue || !m_range.atEnd())
            return false;
    } else {
        columnsValue = cssValuePool().createIdentifierValue(CSSValueNone);
    }
    addProperty(CSSPropertyGridTemplateRows, templateRows, important);
    addProperty(CSSPropertyGridTemplateColumns, columnsValue, important);
    addProperty(CSSPropertyGridTemplateAreas, CSSGridTemplateAreasValue::create(gridAreaMap, rowCount, columnCount), important);
    return true;
}

bool CSSPropertyParser::consumeGridTemplateShorthand(bool important)
{
    ASSERT(RuntimeEnabledFeatures::cssGridLayoutEnabled());
    ASSERT(gridTemplateShorthand().length() == 3);

    CSSParserTokenRange rangeCopy = m_range;
    CSSValue* rowsValue = consumeIdent<CSSValueNone>(m_range);

    // 1- 'none' case.
    if (rowsValue && m_range.atEnd()) {
        addProperty(CSSPropertyGridTemplateRows, cssValuePool().createIdentifierValue(CSSValueNone), important);
        addProperty(CSSPropertyGridTemplateColumns, cssValuePool().createIdentifierValue(CSSValueNone), important);
        addProperty(CSSPropertyGridTemplateAreas, cssValuePool().createIdentifierValue(CSSValueNone), important);
        return true;
    }

    // 2- <grid-template-rows> / <grid-template-columns>
    if (!rowsValue)
        rowsValue = consumeGridTrackList(m_range, m_context.mode());

    if (rowsValue) {
        if (!consumeSlashIncludingWhitespace(m_range))
            return false;
        CSSValue* columnsValue = consumeGridTemplatesRowsOrColumns(m_range, m_context.mode());
        if (!columnsValue || !m_range.atEnd())
            return false;

        addProperty(CSSPropertyGridTemplateRows, rowsValue, important);
        addProperty(CSSPropertyGridTemplateColumns, columnsValue, important);
        addProperty(CSSPropertyGridTemplateAreas, cssValuePool().createIdentifierValue(CSSValueNone), important);
        return true;
    }

    // 3- [ <line-names>? <string> <track-size>? <line-names>? ]+ [ / <track-list> ]?
    m_range = rangeCopy;
    return consumeGridTemplateRowsAndAreasAndColumns(important);
}

bool CSSPropertyParser::parseShorthand(CSSPropertyID unresolvedProperty, bool important)
{
    CSSPropertyID property = resolveCSSPropertyID(unresolvedProperty);

    CSSPropertyID oldShorthand = m_currentShorthand;
    // TODO(rob.buis): Remove this when the legacy property parser is gone
    m_currentShorthand = property;
    switch (property) {
    case CSSPropertyWebkitMarginCollapse: {
        CSSValueID id = m_range.consumeIncludingWhitespace().id();
        if (!CSSParserFastPaths::isValidKeywordPropertyAndValue(CSSPropertyWebkitMarginBeforeCollapse, id))
            return false;
        CSSValue* beforeCollapse = cssValuePool().createIdentifierValue(id);
        addProperty(CSSPropertyWebkitMarginBeforeCollapse, beforeCollapse, important);
        if (m_range.atEnd()) {
            addProperty(CSSPropertyWebkitMarginAfterCollapse, beforeCollapse, important);
            return true;
        }
        id = m_range.consumeIncludingWhitespace().id();
        if (!CSSParserFastPaths::isValidKeywordPropertyAndValue(CSSPropertyWebkitMarginAfterCollapse, id))
            return false;
        addProperty(CSSPropertyWebkitMarginAfterCollapse, cssValuePool().createIdentifierValue(id), important);
        return true;
    }
    case CSSPropertyOverflow: {
        CSSValueID id = m_range.consumeIncludingWhitespace().id();
        if (!CSSParserFastPaths::isValidKeywordPropertyAndValue(CSSPropertyOverflowY, id))
            return false;
        if (!m_range.atEnd())
            return false;
        CSSValue* overflowYValue = cssValuePool().createIdentifierValue(id);

        CSSValue* overflowXValue = nullptr;

        // FIXME: -webkit-paged-x or -webkit-paged-y only apply to overflow-y. If this value has been
        // set using the shorthand, then for now overflow-x will default to auto, but once we implement
        // pagination controls, it should default to hidden. If the overflow-y value is anything but
        // paged-x or paged-y, then overflow-x and overflow-y should have the same value.
        if (id == CSSValueWebkitPagedX || id == CSSValueWebkitPagedY)
            overflowXValue = cssValuePool().createIdentifierValue(CSSValueAuto);
        else
            overflowXValue = overflowYValue;
        addProperty(CSSPropertyOverflowX, overflowXValue, important);
        addProperty(CSSPropertyOverflowY, overflowYValue, important);
        return true;
    }
    case CSSPropertyFont: {
        const CSSParserToken& token = m_range.peek();
        if (token.id() >= CSSValueCaption && token.id() <= CSSValueStatusBar)
            return consumeSystemFont(important);
        return consumeFont(important);
    }
    case CSSPropertyBorderSpacing:
        return consumeBorderSpacing(important);
    case CSSPropertyColumns: {
        // TODO(rwlbuis): investigate if this shorthand hack can be removed.
        m_currentShorthand = oldShorthand;
        return consumeColumns(important);
    }
    case CSSPropertyAnimation:
        return consumeAnimationShorthand(animationShorthandForParsing(), unresolvedProperty == CSSPropertyAliasWebkitAnimation, important);
    case CSSPropertyTransition:
        return consumeAnimationShorthand(transitionShorthandForParsing(), false, important);
    case CSSPropertyTextDecoration:
        ASSERT(RuntimeEnabledFeatures::css3TextDecorationsEnabled());
        return consumeShorthandGreedily(textDecorationShorthand(), important);
    case CSSPropertyMargin:
        return consume4Values(marginShorthand(), important);
    case CSSPropertyPadding:
        return consume4Values(paddingShorthand(), important);
    case CSSPropertyMotion:
        return consumeShorthandGreedily(motionShorthand(), important);
    case CSSPropertyWebkitTextEmphasis:
        return consumeShorthandGreedily(webkitTextEmphasisShorthand(), important);
    case CSSPropertyOutline:
        return consumeShorthandGreedily(outlineShorthand(), important);
    case CSSPropertyWebkitBorderStart:
        return consumeShorthandGreedily(webkitBorderStartShorthand(), important);
    case CSSPropertyWebkitBorderEnd:
        return consumeShorthandGreedily(webkitBorderEndShorthand(), important);
    case CSSPropertyWebkitBorderBefore:
        return consumeShorthandGreedily(webkitBorderBeforeShorthand(), important);
    case CSSPropertyWebkitBorderAfter:
        return consumeShorthandGreedily(webkitBorderAfterShorthand(), important);
    case CSSPropertyWebkitTextStroke:
        return consumeShorthandGreedily(webkitTextStrokeShorthand(), important);
    case CSSPropertyMarker: {
        CSSValue* marker = parseSingleValue(CSSPropertyMarkerStart);
        if (!marker || !m_range.atEnd())
            return false;
        addProperty(CSSPropertyMarkerStart, marker, important);
        addProperty(CSSPropertyMarkerMid, marker, important);
        addProperty(CSSPropertyMarkerEnd, marker, important);
        return true;
    }
    case CSSPropertyFlex:
        return consumeFlex(important);
    case CSSPropertyFlexFlow:
        return consumeShorthandGreedily(flexFlowShorthand(), important);
    case CSSPropertyColumnRule:
        return consumeShorthandGreedily(columnRuleShorthand(), important);
    case CSSPropertyListStyle:
        return consumeShorthandGreedily(listStyleShorthand(), important);
    case CSSPropertyBorderRadius: {
        CSSPrimitiveValue* horizontalRadii[4];
        CSSPrimitiveValue* verticalRadii[4];
        if (!consumeRadii(horizontalRadii, verticalRadii, m_range, m_context.mode(), unresolvedProperty == CSSPropertyAliasWebkitBorderRadius))
            return false;
        addProperty(CSSPropertyBorderTopLeftRadius, CSSValuePair::create(horizontalRadii[0], verticalRadii[0], CSSValuePair::DropIdenticalValues), important);
        addProperty(CSSPropertyBorderTopRightRadius, CSSValuePair::create(horizontalRadii[1], verticalRadii[1], CSSValuePair::DropIdenticalValues), important);
        addProperty(CSSPropertyBorderBottomRightRadius, CSSValuePair::create(horizontalRadii[2], verticalRadii[2], CSSValuePair::DropIdenticalValues), important);
        addProperty(CSSPropertyBorderBottomLeftRadius, CSSValuePair::create(horizontalRadii[3], verticalRadii[3], CSSValuePair::DropIdenticalValues), important);
        return true;
    }
    case CSSPropertyBorderColor:
        return consume4Values(borderColorShorthand(), important);
    case CSSPropertyBorderStyle:
        return consume4Values(borderStyleShorthand(), important);
    case CSSPropertyBorderWidth:
        return consume4Values(borderWidthShorthand(), important);
    case CSSPropertyBorderTop:
        return consumeShorthandGreedily(borderTopShorthand(), important);
    case CSSPropertyBorderRight:
        return consumeShorthandGreedily(borderRightShorthand(), important);
    case CSSPropertyBorderBottom:
        return consumeShorthandGreedily(borderBottomShorthand(), important);
    case CSSPropertyBorderLeft:
        return consumeShorthandGreedily(borderLeftShorthand(), important);
    case CSSPropertyBorder:
        return consumeBorder(important);
    case CSSPropertyBorderImage:
    case CSSPropertyWebkitMaskBoxImage:
        return consumeBorderImage(property, important);
    case CSSPropertyPageBreakAfter:
    case CSSPropertyPageBreakBefore:
    case CSSPropertyPageBreakInside:
    case CSSPropertyWebkitColumnBreakAfter:
    case CSSPropertyWebkitColumnBreakBefore:
    case CSSPropertyWebkitColumnBreakInside:
        return consumeLegacyBreakProperty(property, important);
    case CSSPropertyWebkitMaskPosition:
    case CSSPropertyBackgroundPosition: {
        CSSValue* resultX = nullptr;
        CSSValue* resultY = nullptr;
        if (!consumeBackgroundPosition(m_range, m_context, UnitlessQuirk::Allow, resultX, resultY) || !m_range.atEnd())
            return false;
        addProperty(property == CSSPropertyBackgroundPosition ? CSSPropertyBackgroundPositionX : CSSPropertyWebkitMaskPositionX, resultX, important);
        addProperty(property == CSSPropertyBackgroundPosition ? CSSPropertyBackgroundPositionY : CSSPropertyWebkitMaskPositionY, resultY, important);
        return true;
    }
    case CSSPropertyBackgroundRepeat:
    case CSSPropertyWebkitMaskRepeat: {
        CSSValue* resultX = nullptr;
        CSSValue* resultY = nullptr;
        bool implicit = false;
        if (!consumeRepeatStyle(m_range, resultX, resultY, implicit) || !m_range.atEnd())
            return false;
        addProperty(property == CSSPropertyBackgroundRepeat ? CSSPropertyBackgroundRepeatX : CSSPropertyWebkitMaskRepeatX, resultX, important, implicit);
        addProperty(property == CSSPropertyBackgroundRepeat ? CSSPropertyBackgroundRepeatY : CSSPropertyWebkitMaskRepeatY, resultY, important, implicit);
        return true;
    }
    case CSSPropertyBackground:
        return consumeBackgroundShorthand(backgroundShorthand(), important);
    case CSSPropertyWebkitMask:
        return consumeBackgroundShorthand(webkitMaskShorthand(), important);
    case CSSPropertyGridGap: {
        ASSERT(RuntimeEnabledFeatures::cssGridLayoutEnabled() && shorthandForProperty(CSSPropertyGridGap).length() == 2);
        CSSValue* rowGap = consumeLength(m_range, m_context.mode(), ValueRangeNonNegative);
        CSSValue* columnGap = consumeLength(m_range, m_context.mode(), ValueRangeNonNegative);
        if (!rowGap || !m_range.atEnd())
            return false;
        if (!columnGap)
            columnGap = rowGap;
        addProperty(CSSPropertyGridRowGap, rowGap, important);
        addProperty(CSSPropertyGridColumnGap, columnGap, important);
        return true;
    }
    case CSSPropertyGridColumn:
    case CSSPropertyGridRow:
        return consumeGridItemPositionShorthand(property, important);
    case CSSPropertyGridArea:
        return consumeGridAreaShorthand(important);
    case CSSPropertyGridTemplate:
        return consumeGridTemplateShorthand(important);
    default:
        m_currentShorthand = oldShorthand;
        CSSParserValueList valueList(m_range);
        if (!valueList.size())
            return false;
        m_valueList = &valueList;
        return legacyParseShorthand(unresolvedProperty, important);
    }
}

} // namespace blink
