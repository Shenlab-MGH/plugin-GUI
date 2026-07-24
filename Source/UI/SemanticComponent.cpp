/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2024 Open Ephys

    ------------------------------------------------------------------

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "SemanticComponent.h"

namespace
{
class ReadOnlyProgressValue final : public AccessibilityRangedNumericValueInterface
{
public:
    explicit ReadOnlyProgressValue (std::function<double()> getValueIn)
        : getValue (std::move (getValueIn))
    {
    }

    bool isReadOnly() const override { return true; }
    void setValue (double) override { jassertfalse; }
    double getCurrentValue() const override
    {
        return jlimit (0.0, 1.0, getValue());
    }
    AccessibleValueRange getRange() const override
    {
        return { { 0.0, 1.0 }, 0.001 };
    }

private:
    std::function<double()> getValue;
};
} // namespace

bool isValidSemanticId (StringRef id)
{
    const String value (id);

    if (! value.startsWith ("oe.") || value.endsWithChar ('.'))
        return false;

    bool previousWasDot = false;

    for (int index = 0; index < value.length(); ++index)
    {
        const auto character = value[index];

        if (character == '.')
        {
            if (previousWasDot)
                return false;

            previousWasDot = true;
            continue;
        }

        previousWasDot = false;

        const bool isLowercaseLetter = character >= 'a' && character <= 'z';
        const bool isDigit = character >= '0' && character <= '9';

        if (! isLowercaseLetter && ! isDigit && character != '_')
            return false;
    }

    return true;
}

String sanitiseSemanticSegment (StringRef segment)
{
    const String value (segment);
    String result;

    for (int index = 0; index < value.length(); ++index)
    {
        auto character = value[index];

        if (character >= 'A' && character <= 'Z')
            character = character - 'A' + 'a';

        const bool isLowercaseLetter = character >= 'a' && character <= 'z';
        const bool isDigit = character >= '0' && character <= '9';

        if (isLowercaseLetter || isDigit)
        {
            result += character;
        }
        else if (result.isNotEmpty() && ! result.endsWithChar ('_'))
        {
            result += '_';
        }
    }

    result = result.trimCharactersAtEnd ("_");
    return result.isNotEmpty() ? result : "unnamed";
}

String createProcessorControlSemanticId (int nodeId, StringRef controlName)
{
    return "oe.processor." + String (nodeId) + "."
           + sanitiseSemanticSegment (controlName);
}

std::unique_ptr<AccessibilityHandler>
createReadOnlyProgressAccessibilityHandler (
    Component& component,
    std::function<double()> getValue)
{
    return std::make_unique<AccessibilityHandler> (
        component,
        AccessibilityRole::progressBar,
        AccessibilityActions {},
        AccessibilityHandler::Interfaces {
            std::make_unique<ReadOnlyProgressValue> (std::move (getValue)) });
}

void applySemanticMetadata (Component& component,
                            StringRef id,
                            StringRef title,
                            StringRef description,
                            StringRef help)
{
    if (! isValidSemanticId (id))
        return;

    component.setComponentID (String (id));
    component.setTitle (String (title));
    component.setDescription (String (description));
    component.setHelpText (String (help));
    component.setAccessible (true);
    component.invalidateAccessibilityHandler();
}
