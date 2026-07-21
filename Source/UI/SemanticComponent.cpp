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
