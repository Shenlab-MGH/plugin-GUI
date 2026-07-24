#include "MessageCenterButton.h"

namespace
{
class MessageCenterButtonAccessibilityHandler final : public AccessibilityHandler
{
public:
    explicit MessageCenterButtonAccessibilityHandler (MessageCenterButton& buttonToWrap)
        : AccessibilityHandler (
              buttonToWrap,
              AccessibilityRole::button,
              AccessibilityActions()
                  .addAction (AccessibilityActionType::press,
                              [&buttonToWrap] { buttonToWrap.triggerClick(); })
                  .addAction (AccessibilityActionType::showMenu,
                              [&buttonToWrap] { buttonToWrap.triggerClick(); })),
          button (buttonToWrap)
    {
    }

    AccessibleState getCurrentState() const override
    {
        auto state = AccessibilityHandler::getCurrentState().withExpandable();
        return button.isExpandedState() ? state.withExpanded() : state.withCollapsed();
    }

private:
    MessageCenterButton& button;
};
} // namespace

std::unique_ptr<AccessibilityHandler> MessageCenterButton::createAccessibilityHandler()
{
    return std::make_unique<MessageCenterButtonAccessibilityHandler> (*this);
}
