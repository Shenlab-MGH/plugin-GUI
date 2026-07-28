/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-8-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

namespace juce
{

//==============================================================================
class UIAExpandCollapseProvider  : public UIAProviderBase,
                                   public ComBaseClassHelper<IExpandCollapseProvider>
{
public:
    using UIAProviderBase::UIAProviderBase;

    //==============================================================================
    JUCE_COMRESULT Expand() override
    {
        return setExpanded (true);
    }

    JUCE_COMRESULT Collapse() override
    {
        return setExpanded (false);
    }

    JUCE_COMRESULT get_ExpandCollapseState (ExpandCollapseState* pRetVal) override
    {
        return withCheckedComArgs (pRetVal, *this, [&]
        {
            *pRetVal = getHandler().getCurrentState().isExpanded()
                           ? ExpandCollapseState_Expanded
                           : ExpandCollapseState_Collapsed;

            return S_OK;
        });
    }

private:
    JUCE_COMRESULT setExpanded (bool shouldBeExpanded)
    {
        if (! isElementValid())
            return (HRESULT) UIA_E_ELEMENTNOTAVAILABLE;

        const auto& handler = getHandler();
        if (! handler.isEnabled())
            return (HRESULT) UIA_E_ELEMENTNOTENABLED;

        const auto directionalAction =
            shouldBeExpanded ? AccessibilityActionType::expand
                             : AccessibilityActionType::collapse;
        const auto actions = handler.getActions();
        bool invokedDirectionalAction = false;
        if (actions.contains (directionalAction))
        {
            if (! actions.invoke (directionalAction))
                return (HRESULT) UIA_E_NOTSUPPORTED;
            invokedDirectionalAction = true;
        }
        else
        {
            if (! actions.invoke (AccessibilityActionType::showMenu))
                return (HRESULT) UIA_E_NOTSUPPORTED;
        }

        if (! isElementValid())
            return (HRESULT) UIA_E_ELEMENTNOTAVAILABLE;

        const auto& updatedHandler = getHandler();
        if (invokedDirectionalAction
            && updatedHandler.getCurrentState().isExpanded() != shouldBeExpanded)
            return (HRESULT) UIA_E_INVALIDOPERATION;

        sendAccessibilityAutomationEvent (updatedHandler,
                                          updatedHandler.getCurrentState().isExpanded()
                                              ? UIA_MenuOpenedEventId
                                              : UIA_MenuClosedEventId);
        return S_OK;
    }

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UIAExpandCollapseProvider)
};

} // namespace juce
