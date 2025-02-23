/*
 * Copyright (C) 2011, 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef CSSCalculationValue_h
#define CSSCalculationValue_h

#include "core/CoreExport.h"
#include "core/css/CSSPrimitiveValue.h"
#include "core/css/CSSValue.h"
#include "core/css/parser/CSSParserTokenRange.h"
#include "platform/CalculationValue.h"
#include "wtf/PassOwnPtr.h"
#include "wtf/RefCounted.h"
#include "wtf/RefPtr.h"

namespace blink {

class CalculationValue;

enum CalcOperator {
    CalcAdd = '+',
    CalcSubtract = '-',
    CalcMultiply = '*',
    CalcDivide = '/'
};

// The order of this enum should not change since its elements are used as indices
// in the addSubtractResult matrix.
enum CalculationCategory {
    CalcNumber = 0,
    CalcLength,
    CalcPercent,
    CalcPercentNumber,
    CalcPercentLength,
    CalcAngle,
    CalcTime,
    CalcFrequency,
    CalcOther
};

class CSSCalcExpressionNode : public GarbageCollected<CSSCalcExpressionNode> {
public:
    enum Type {
        CssCalcPrimitiveValue = 1,
        CssCalcBinaryOperation
    };

    virtual bool isZero() const = 0;
    virtual double doubleValue() const = 0;
    virtual double computeLengthPx(const CSSToLengthConversionData&) const = 0;
    virtual void accumulateLengthArray(CSSLengthArray&, double multiplier) const = 0;
    virtual void accumulatePixelsAndPercent(const CSSToLengthConversionData&, PixelsAndPercent&, float multiplier = 1) const = 0;
    virtual String customCSSText() const = 0;
    virtual bool equals(const CSSCalcExpressionNode& other) const { return m_category == other.m_category && m_isInteger == other.m_isInteger; }
    virtual Type getType() const = 0;

    CalculationCategory category() const { return m_category; }
    virtual CSSPrimitiveValue::UnitType typeWithCalcResolved() const = 0;
    bool isInteger() const { return m_isInteger; }

    DEFINE_INLINE_VIRTUAL_TRACE() { }

protected:
    CSSCalcExpressionNode(CalculationCategory category, bool isInteger)
        : m_category(category)
        , m_isInteger(isInteger)
    {
        ASSERT(category != CalcOther);
    }

    CalculationCategory m_category;
    bool m_isInteger;
};

class CORE_EXPORT CSSCalcValue : public GarbageCollected<CSSCalcValue> {
public:
    static CSSCalcValue* create(const CSSParserTokenRange&, ValueRange);
    static CSSCalcValue* create(CSSCalcExpressionNode*, ValueRange = ValueRangeAll);

    static CSSCalcExpressionNode* createExpressionNode(CSSPrimitiveValue*, bool isInteger = false);
    static CSSCalcExpressionNode* createExpressionNode(CSSCalcExpressionNode*, CSSCalcExpressionNode*, CalcOperator);
    static CSSCalcExpressionNode* createExpressionNode(double pixels, double percent);

    PassRefPtr<CalculationValue> toCalcValue(const CSSToLengthConversionData& conversionData) const
    {
        PixelsAndPercent value(0, 0);
        m_expression->accumulatePixelsAndPercent(conversionData, value);
        return CalculationValue::create(value, m_nonNegative ? ValueRangeNonNegative : ValueRangeAll);
    }
    CalculationCategory category() const { return m_expression->category(); }
    bool isInt() const { return m_expression->isInteger(); }
    double doubleValue() const;
    bool isNegative() const { return m_expression->doubleValue() < 0; }
    ValueRange permittedValueRange() { return m_nonNegative ? ValueRangeNonNegative : ValueRangeAll; }
    double computeLengthPx(const CSSToLengthConversionData&) const;
    void accumulateLengthArray(CSSLengthArray& lengthArray, double multiplier) const { m_expression->accumulateLengthArray(lengthArray, multiplier); }
    CSSCalcExpressionNode* expressionNode() const { return m_expression.get(); }

    String customCSSText() const;
    bool equals(const CSSCalcValue&) const;

    DEFINE_INLINE_TRACE()
    {
        visitor->trace(m_expression);
    }

private:
    CSSCalcValue(CSSCalcExpressionNode* expression, ValueRange range)
        : m_expression(expression)
        , m_nonNegative(range == ValueRangeNonNegative)
    {
    }

    double clampToPermittedRange(double) const;

    const Member<CSSCalcExpressionNode> m_expression;
    const bool m_nonNegative;
};

} // namespace blink

#endif // CSSCalculationValue_h
