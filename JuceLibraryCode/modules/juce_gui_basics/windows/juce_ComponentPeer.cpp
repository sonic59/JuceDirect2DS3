/*
  ==============================================================================

   This file is part of the JUCE library - "Jules' Utility Class Extensions"
   Copyright 2004-11 by Raw Material Software Ltd.

  ------------------------------------------------------------------------------

   JUCE can be redistributed and/or modified under the terms of the GNU General
   Public License (Version 2), as published by the Free Software Foundation.
   A copy of the license is included in the JUCE distribution, or can be found
   online at www.gnu.org/licenses.

   JUCE is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
   A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  ------------------------------------------------------------------------------

   To release a closed-source product which uses JUCE, commercial licenses are
   available: visit www.rawmaterialsoftware.com/juce for more information.

  ==============================================================================
*/

//#define JUCE_ENABLE_REPAINT_DEBUGGING 1

//==============================================================================
static Array <ComponentPeer*> heavyweightPeers;
static uint32 lastUniqueID = 1;

//==============================================================================
ComponentPeer::ComponentPeer (Component* const component_, const int styleFlags_)
    : component (component_),
      styleFlags (styleFlags_),
      lastPaintTime (0),
      constrainer (nullptr),
      lastDragAndDropCompUnderMouse (nullptr),
      uniqueID (lastUniqueID += 2), // increment by 2 so that this can never hit 0
      fakeMouseMessageSent (false),
      isWindowMinimised (false)
{
    heavyweightPeers.add (this);
}

ComponentPeer::~ComponentPeer()
{
    heavyweightPeers.removeValue (this);
    Desktop::getInstance().triggerFocusCallback();
}

//==============================================================================
int ComponentPeer::getNumPeers() noexcept
{
    return heavyweightPeers.size();
}

ComponentPeer* ComponentPeer::getPeer (const int index) noexcept
{
    return heavyweightPeers [index];
}

ComponentPeer* ComponentPeer::getPeerFor (const Component* const component) noexcept
{
    for (int i = heavyweightPeers.size(); --i >= 0;)
    {
        ComponentPeer* const peer = heavyweightPeers.getUnchecked(i);

        if (peer->getComponent() == component)
            return peer;
    }

    return nullptr;
}

bool ComponentPeer::isValidPeer (const ComponentPeer* const peer) noexcept
{
    return heavyweightPeers.contains (const_cast <ComponentPeer*> (peer));
}

void ComponentPeer::updateCurrentModifiers() noexcept
{
    ModifierKeys::updateCurrentModifiers();
}

//==============================================================================
void ComponentPeer::handleMouseEvent (const int touchIndex, const Point<int>& positionWithinPeer, const ModifierKeys& newMods, const int64 time)
{
    MouseInputSource* const mouse = Desktop::getInstance().getMouseSource (touchIndex);
    jassert (mouse != nullptr); // not enough sources!

    mouse->handleEvent (this, positionWithinPeer, time, newMods);
}

void ComponentPeer::handleMouseWheel (const int touchIndex, const Point<int>& positionWithinPeer, const int64 time, const MouseWheelDetails& wheel)
{
    MouseInputSource* const mouse = Desktop::getInstance().getMouseSource (touchIndex);
    jassert (mouse != nullptr); // not enough sources!

    mouse->handleWheel (this, positionWithinPeer, time, wheel);
}

//==============================================================================
void ComponentPeer::handlePaint (LowLevelGraphicsContext& contextToPaintTo)
{
    Graphics g (&contextToPaintTo);

   #if JUCE_ENABLE_REPAINT_DEBUGGING
    g.saveState();
   #endif

    JUCE_TRY
    {
        component->paintEntireComponent (g, true);
    }
    JUCE_CATCH_EXCEPTION

   #if JUCE_ENABLE_REPAINT_DEBUGGING
    // enabling this code will fill all areas that get repainted with a colour overlay, to show
    // clearly when things are being repainted.
    g.restoreState();

    static Random rng;

    g.fillAll (Colour ((uint8) rng.nextInt (255),
                       (uint8) rng.nextInt (255),
                       (uint8) rng.nextInt (255),
                       (uint8) 0x50));
   #endif

    /** If this fails, it's probably be because your CPU floating-point precision mode has
        been set to low.. This setting is sometimes changed by things like Direct3D, and can
        mess up a lot of the calculations that the library needs to do.
    */
    jassert (roundToInt (10.1f) == 10);
}

bool ComponentPeer::handleKeyPress (const int keyCode,
                                    const juce_wchar textCharacter)
{
    updateCurrentModifiers();

    Component* target = Component::getCurrentlyFocusedComponent() != nullptr
                            ? Component::getCurrentlyFocusedComponent()
                            : component;

    if (target->isCurrentlyBlockedByAnotherModalComponent())
    {
        Component* const currentModalComp = Component::getCurrentlyModalComponent();

        if (currentModalComp != nullptr)
            target = currentModalComp;
    }

    const KeyPress keyInfo (keyCode,
                            ModifierKeys::getCurrentModifiers().getRawFlags()
                               & ModifierKeys::allKeyboardModifiers,
                            textCharacter);

    bool keyWasUsed = false;

    while (target != nullptr)
    {
        const WeakReference<Component> deletionChecker (target);
        const Array <KeyListener*>* const keyListeners = target->keyListeners;

        if (keyListeners != nullptr)
        {
            for (int i = keyListeners->size(); --i >= 0;)
            {
                keyWasUsed = keyListeners->getUnchecked(i)->keyPressed (keyInfo, target);

                if (keyWasUsed || deletionChecker == nullptr)
                    return keyWasUsed;

                i = jmin (i, keyListeners->size());
            }
        }

        keyWasUsed = target->keyPressed (keyInfo);

        if (keyWasUsed || deletionChecker == nullptr)
            break;

        Component* const currentlyFocused = Component::getCurrentlyFocusedComponent();

        if (currentlyFocused != nullptr)
        {
            const bool isTab      = (keyInfo == KeyPress (KeyPress::tabKey, ModifierKeys::noModifiers, 0));
            const bool isShiftTab = (keyInfo == KeyPress (KeyPress::tabKey, ModifierKeys::shiftModifier, 0));

            if (isTab || isShiftTab)
            {
                currentlyFocused->moveKeyboardFocusToSibling (isTab);
                keyWasUsed = (currentlyFocused != Component::getCurrentlyFocusedComponent());
                break;
            }
        }

        target = target->getParentComponent();
    }

    return keyWasUsed;
}

bool ComponentPeer::handleKeyUpOrDown (const bool isKeyDown)
{
    updateCurrentModifiers();

    Component* target = Component::getCurrentlyFocusedComponent() != nullptr
                            ? Component::getCurrentlyFocusedComponent()
                            : component;

    if (target->isCurrentlyBlockedByAnotherModalComponent())
    {
        Component* const currentModalComp = Component::getCurrentlyModalComponent();

        if (currentModalComp != nullptr)
            target = currentModalComp;
    }

    bool keyWasUsed = false;

    while (target != nullptr)
    {
        const WeakReference<Component> deletionChecker (target);

        keyWasUsed = target->keyStateChanged (isKeyDown);

        if (keyWasUsed || deletionChecker == nullptr)
            break;

        const Array <KeyListener*>* const keyListeners = target->keyListeners;

        if (keyListeners != nullptr)
        {
            for (int i = keyListeners->size(); --i >= 0;)
            {
                keyWasUsed = keyListeners->getUnchecked(i)->keyStateChanged (isKeyDown, target);

                if (keyWasUsed || deletionChecker == nullptr)
                    return keyWasUsed;

                i = jmin (i, keyListeners->size());
            }
        }

        target = target->getParentComponent();
    }

    return keyWasUsed;
}

void ComponentPeer::handleModifierKeysChange()
{
    updateCurrentModifiers();

    Component* target = Desktop::getInstance().getMainMouseSource().getComponentUnderMouse();

    if (target == nullptr)
        target = Component::getCurrentlyFocusedComponent();

    if (target == nullptr)
        target = component;

    if (target != nullptr)
        target->internalModifierKeysChanged();
}

TextInputTarget* ComponentPeer::findCurrentTextInputTarget()
{
    Component* const c = Component::getCurrentlyFocusedComponent();
    if (component->isParentOf (c))
    {
        TextInputTarget* const ti = dynamic_cast <TextInputTarget*> (c);
        if (ti != nullptr && ti->isTextInputActive())
            return ti;
    }

    return nullptr;
}

void ComponentPeer::dismissPendingTextInput()
{
}

//==============================================================================
void ComponentPeer::handleBroughtToFront()
{
    updateCurrentModifiers();

    if (component != nullptr)
        component->internalBroughtToFront();
}

void ComponentPeer::setConstrainer (ComponentBoundsConstrainer* const newConstrainer) noexcept
{
    constrainer = newConstrainer;
}

void ComponentPeer::handleMovedOrResized()
{
    updateCurrentModifiers();

    const bool nowMinimised = isMinimised();

    if (component->flags.hasHeavyweightPeerFlag && ! nowMinimised)
    {
        const WeakReference<Component> deletionChecker (component);

        const Rectangle<int> newBounds (getBounds());
        const bool wasMoved   = (component->getPosition() != newBounds.getPosition());
        const bool wasResized = (component->getWidth() != newBounds.getWidth() || component->getHeight() != newBounds.getHeight());

        if (wasMoved || wasResized)
        {
            component->bounds = newBounds;

            if (wasResized)
                component->repaint();

            component->sendMovedResizedMessages (wasMoved, wasResized);

            if (deletionChecker == nullptr)
                return;
        }
    }

    if (isWindowMinimised != nowMinimised)
    {
        isWindowMinimised = nowMinimised;
        component->minimisationStateChanged (nowMinimised);
        component->sendVisibilityChangeMessage();
    }

    if (! isFullScreen())
        lastNonFullscreenBounds = component->getBounds();
}

void ComponentPeer::handleFocusGain()
{
    updateCurrentModifiers();

    if (component->isParentOf (lastFocusedComponent))
    {
        Component::currentlyFocusedComponent = lastFocusedComponent;
        Desktop::getInstance().triggerFocusCallback();
        lastFocusedComponent->internalFocusGain (Component::focusChangedDirectly);
    }
    else
    {
        if (! component->isCurrentlyBlockedByAnotherModalComponent())
            component->grabKeyboardFocus();
        else
            ModalComponentManager::getInstance()->bringModalComponentsToFront();
    }
}

void ComponentPeer::handleFocusLoss()
{
    updateCurrentModifiers();

    if (component->hasKeyboardFocus (true))
    {
        lastFocusedComponent = Component::currentlyFocusedComponent;

        if (lastFocusedComponent != nullptr)
        {
            Component::currentlyFocusedComponent = nullptr;
            Desktop::getInstance().triggerFocusCallback();
            lastFocusedComponent->internalFocusLoss (Component::focusChangedByMouseClick);
        }
    }
}

Component* ComponentPeer::getLastFocusedSubcomponent() const noexcept
{
    return (component->isParentOf (lastFocusedComponent) && lastFocusedComponent->isShowing())
                ? static_cast <Component*> (lastFocusedComponent)
                : component;
}

void ComponentPeer::handleScreenSizeChange()
{
    updateCurrentModifiers();

    component->parentSizeChanged();
    handleMovedOrResized();
}

void ComponentPeer::setNonFullScreenBounds (const Rectangle<int>& newBounds) noexcept
{
    lastNonFullscreenBounds = newBounds;
}

const Rectangle<int>& ComponentPeer::getNonFullScreenBounds() const noexcept
{
    return lastNonFullscreenBounds;
}

Rectangle<int> ComponentPeer::localToGlobal (const Rectangle<int>& relativePosition)
{
    return relativePosition.withPosition (localToGlobal (relativePosition.getPosition()));
}

Rectangle<int> ComponentPeer::globalToLocal (const Rectangle<int>& screenPosition)
{
    return screenPosition.withPosition (globalToLocal (screenPosition.getPosition()));
}

//==============================================================================
namespace DragHelpers
{
    static bool isFileDrag (const ComponentPeer::DragInfo& info)
    {
        return info.files.size() > 0;
    }

    static bool isSuitableTarget (const ComponentPeer::DragInfo& info, Component* target)
    {
        return isFileDrag (info) ? dynamic_cast <FileDragAndDropTarget*> (target) != nullptr
                                 : dynamic_cast <TextDragAndDropTarget*> (target) != nullptr;
    }

    static bool isInterested (const ComponentPeer::DragInfo& info, Component* target)
    {
        return isFileDrag (info) ? dynamic_cast <FileDragAndDropTarget*> (target)->isInterestedInFileDrag (info.files)
                                 : dynamic_cast <TextDragAndDropTarget*> (target)->isInterestedInTextDrag (info.text);
    }

    static Component* findDragAndDropTarget (Component* c, const ComponentPeer::DragInfo& info, Component* lastOne)
    {
        for (; c != nullptr; c = c->getParentComponent())
            if (isSuitableTarget (info, c) && (c == lastOne || isInterested (info, c)))
                return c;

        return nullptr;
    }

    // We'll use an async message to deliver the drop, because if the target decides
    // to run a modal loop, it can gum-up the operating system..
    class AsyncDropMessage  : public CallbackMessage
    {
    public:
        AsyncDropMessage (Component* target_, const ComponentPeer::DragInfo& info_)
            : target (target_), info (info_)
        {}

        void messageCallback()
        {
            if (target.get() != nullptr)
            {
                if (isFileDrag (info))
                    dynamic_cast <FileDragAndDropTarget*> (target.get())->filesDropped (info.files, info.position.x, info.position.y);
                else
                    dynamic_cast <TextDragAndDropTarget*> (target.get())->textDropped (info.text, info.position.x, info.position.y);
            }
        }

    private:
        WeakReference<Component> target;
        const ComponentPeer::DragInfo info;

        JUCE_DECLARE_NON_COPYABLE (AsyncDropMessage);
    };
}

bool ComponentPeer::handleDragMove (const ComponentPeer::DragInfo& info)
{
    updateCurrentModifiers();

    Component* const compUnderMouse = component->getComponentAt (info.position);

    Component* const lastTarget = dragAndDropTargetComponent.get();
    Component* newTarget = nullptr;

    if (compUnderMouse != lastDragAndDropCompUnderMouse)
    {
        lastDragAndDropCompUnderMouse = compUnderMouse;
        newTarget = DragHelpers::findDragAndDropTarget (compUnderMouse, info, lastTarget);

        if (newTarget != lastTarget)
        {
            if (lastTarget != nullptr)
            {
                if (DragHelpers::isFileDrag (info))
                    dynamic_cast <FileDragAndDropTarget*> (lastTarget)->fileDragExit (info.files);
                else
                    dynamic_cast <TextDragAndDropTarget*> (lastTarget)->textDragExit (info.text);
            }

            dragAndDropTargetComponent = nullptr;

            if (DragHelpers::isSuitableTarget (info, newTarget))
            {
                dragAndDropTargetComponent = newTarget;
                const Point<int> pos (newTarget->getLocalPoint (component, info.position));

                if (DragHelpers::isFileDrag (info))
                    dynamic_cast <FileDragAndDropTarget*> (newTarget)->fileDragEnter (info.files, pos.x, pos.y);
                else
                    dynamic_cast <TextDragAndDropTarget*> (newTarget)->textDragEnter (info.text, pos.x, pos.y);
            }
        }
    }
    else
    {
        newTarget = lastTarget;
    }

    if (! DragHelpers::isSuitableTarget (info, newTarget))
        return false;

    const Point<int> pos (newTarget->getLocalPoint (component, info.position));

    if (DragHelpers::isFileDrag (info))
        dynamic_cast <FileDragAndDropTarget*> (newTarget)->fileDragMove (info.files, pos.x, pos.y);
    else
        dynamic_cast <TextDragAndDropTarget*> (newTarget)->textDragMove (info.text, pos.x, pos.y);

    return true;
}

bool ComponentPeer::handleDragExit (const ComponentPeer::DragInfo& info)
{
    DragInfo info2 (info);
    info2.position.setXY (-1, -1);
    const bool used = handleDragMove (info2);

    jassert (dragAndDropTargetComponent == nullptr);
    lastDragAndDropCompUnderMouse = nullptr;
    return used;
}

bool ComponentPeer::handleDragDrop (const ComponentPeer::DragInfo& info)
{
    handleDragMove (info);

    Component* const targetComp = dragAndDropTargetComponent;

    if (targetComp != nullptr)
    {
        dragAndDropTargetComponent = nullptr;
        lastDragAndDropCompUnderMouse = nullptr;

        if (DragHelpers::isSuitableTarget (info, targetComp))
        {
            if (targetComp->isCurrentlyBlockedByAnotherModalComponent())
            {
                targetComp->internalModalInputAttempt();

                if (targetComp->isCurrentlyBlockedByAnotherModalComponent())
                    return true;
            }

            (new DragHelpers::AsyncDropMessage (targetComp, info))->post();
            return true;
        }
    }

    return false;
}

//==============================================================================
void ComponentPeer::handleUserClosingWindow()
{
    updateCurrentModifiers();
    component->userTriedToCloseWindow();
}

//==============================================================================
void ComponentPeer::clearMaskedRegion()
{
    maskedRegion.clear();
}

void ComponentPeer::addMaskedRegion (const Rectangle<int>& area)
{
    maskedRegion.add (area);
}

//==============================================================================
StringArray ComponentPeer::getAvailableRenderingEngines()
{
    return StringArray ("Software Renderer");
}

int ComponentPeer::getCurrentRenderingEngine() const
{
    return 0;
}

void ComponentPeer::setCurrentRenderingEngine (int /*index*/)
{
}
