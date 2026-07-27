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

#include "ElectrodeButtons.h"
#include "../../UI/LookAndFeel/CustomLookAndFeel.h"

ElectrodeButton::ElectrodeButton (int chan_, Colour defaultColour_) : Button ("Electrode"),
                                                                      chan (chan_),
                                                                      defaultColour (defaultColour_)

{
    setClickingTogglesState (true);
    setToggleState (true, dontSendNotification);
    setButtonText (String (chan_));
}

ElectrodeButton::~ElectrodeButton() {}

namespace
{
class ElectrodeButtonAccessibilityHandler final
    : public AccessibilityHandler
{
public:
    explicit ElectrodeButtonAccessibilityHandler (
        ElectrodeButton& buttonToWrap)
        : AccessibilityHandler (
              buttonToWrap,
              buttonToWrap.getRadioGroupId() != 0
                  ? AccessibilityRole::radioButton
                  : AccessibilityRole::button,
              getActions (buttonToWrap),
              AccessibilityHandler::Interfaces {
                  std::make_unique<ChannelValue> (buttonToWrap) }),
          button (buttonToWrap)
    {
    }

    AccessibleState getCurrentState() const override
    {
        auto state = AccessibilityHandler::getCurrentState();

        if (button.isToggleable())
        {
            state = state.withCheckable();

            if (button.getToggleState())
                state = state.withChecked();
        }

        return state;
    }

    String getTitle() const override
    {
        const auto title = AccessibilityHandler::getTitle();
        return title.isNotEmpty() ? title : button.getButtonText();
    }

    String getHelp() const override { return button.getTooltip(); }

private:
    class ChannelValue final : public AccessibilityTextValueInterface
    {
    public:
        explicit ChannelValue (ElectrodeButton& buttonToWrap)
            : button (buttonToWrap)
        {
        }

        bool isReadOnly() const override { return true; }
        void setValueAsString (const String&) override {}
        String getCurrentValueAsString() const override
        {
            const int channel = button.getChannelNum();
            return channel >= 0 ? String (channel) : "None";
        }

    private:
        ElectrodeButton& button;
    };

    static AccessibilityActions getActions (ElectrodeButton& button)
    {
        auto actions = AccessibilityActions().addAction (
            AccessibilityActionType::press,
            [&button] { button.triggerClick(); });

        if (button.isToggleable())
        {
            actions = actions.addAction (
                AccessibilityActionType::toggle,
                [&button]
                {
                    button.setToggleState (
                        ! button.getToggleState(),
                        sendNotification);
                });
        }

        return actions;
    }

    ElectrodeButton& button;
};
}

std::unique_ptr<AccessibilityHandler>
ElectrodeButton::createAccessibilityHandler()
{
    return std::make_unique<ElectrodeButtonAccessibilityHandler> (*this);
}

int ElectrodeButton::getChannelNum()
{
    return chan;
}

void ElectrodeButton::paintButton (Graphics& g, bool isMouseOver, bool isButtonDown)
{
    Colour bgColour = findColour (ThemeColours::widgetBackground);

    if (getToggleState())
        bgColour = findColour (ThemeColours::highlightedFill);

    auto baseColour = bgColour.withMultipliedAlpha (isEnabled() ? 1.0f : 0.5f);

    if (isButtonDown || isMouseOver)
        baseColour = baseColour.contrasting (isButtonDown ? 0.2f : 0.1f);

    g.setColour (baseColour);
    g.fillRect (0, 0, getWidth(), getHeight());

    g.setColour (findColour (ThemeColours::outline).withAlpha (isEnabled() ? 1.0f : 0.5f));
    g.drawRect (0, 0, getWidth(), getHeight(), 1.0);

    g.setColour (baseColour.contrasting().withAlpha (isEnabled() ? 1.0f : 0.5f));

    if (chan < 100)
        g.setFont (10.f);
    else
        g.setFont (8.f);

    if (chan >= 0)
        g.drawText (getButtonText(),
                    0,
                    0,
                    getWidth(),
                    getHeight(),
                    Justification::centred,
                    true);
}

void ElectrodeButton::setChannelNum (int i)
{
    chan = i;

    setButtonText (String (chan));

    if (auto* handler = getAccessibilityHandler())
    {
        handler->notifyAccessibilityEvent (
            AccessibilityEvent::valueChanged);
        handler->notifyAccessibilityEvent (
            AccessibilityEvent::titleChanged);
    }
}

ElectrodeEditorButton::ElectrodeEditorButton (const String& name_) : Button ("Electrode Editor"),
                                                                     name (name_)
{
    if (name.equalsIgnoreCase ("edit") || name.equalsIgnoreCase ("monitor"))
        setClickingTogglesState (true);
}

ElectrodeEditorButton::~ElectrodeEditorButton() {}

void ElectrodeEditorButton::paintButton (Graphics& g, bool isMouseOver, bool isButtonDown)
{
    g.setFont (FontOptions ("Silkscreen", "Regular", 14));

    if (getToggleState() == true)
        g.setColour (Colours::darkgrey);
    else
        g.setColour (Colours::lightgrey);

    g.drawText (name, 0, 0, getWidth(), getHeight(), Justification::left, true);
}
