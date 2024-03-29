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

class Slider::Pimpl   : public AsyncUpdater,
                        public ButtonListener,  // (can't use Button::Listener due to idiotic VC2005 bug)
                        public LabelListener,
                        public ValueListener
{
public:
    Pimpl (Slider& owner_, SliderStyle style_, TextEntryBoxPosition textBoxPosition)
      : owner (owner_),
        style (style_),
        lastCurrentValue (0), lastValueMin (0), lastValueMax (0),
        minimum (0), maximum (10), interval (0),
        skewFactor (1.0), velocityModeSensitivity (1.0),
        velocityModeOffset (0.0), velocityModeThreshold (1),
        rotaryStart (float_Pi * 1.2f),
        rotaryEnd (float_Pi * 2.8f),
        sliderRegionStart (0), sliderRegionSize (1), sliderBeingDragged (-1),
        pixelsForFullDragExtent (250),
        textBoxPos (textBoxPosition),
        numDecimalPlaces (7),
        textBoxWidth (80), textBoxHeight (20),
        incDecButtonMode (incDecButtonsNotDraggable),
        editableText (true),
        doubleClickToValue (false),
        isVelocityBased (false),
        userKeyOverridesVelocity (true),
        rotaryStop (true),
        incDecButtonsSideBySide (false),
        sendChangeOnlyOnRelease (false),
        popupDisplayEnabled (false),
        menuEnabled (false),
        menuShown (false),
        scrollWheelEnabled (true),
        snapsToMousePos (true),
        parentForPopupDisplay (nullptr)
    {
    }

    ~Pimpl()
    {
        currentValue.removeListener (this);
        valueMin.removeListener (this);
        valueMax.removeListener (this);
        popupDisplay = nullptr;
    }

    //==============================================================================
    void init()
    {
        currentValue.addListener (this);
        valueMin.addListener (this);
        valueMax.addListener (this);
    }

    bool isHorizontal() const noexcept
    {
        return style == LinearHorizontal
            || style == LinearBar
            || style == TwoValueHorizontal
            || style == ThreeValueHorizontal;
    }

    bool isVertical() const noexcept
    {
        return style == LinearVertical
            || style == TwoValueVertical
            || style == ThreeValueVertical;
    }

    float getPositionOfValue (const double value)
    {
        if (isHorizontal() || isVertical())
        {
            return getLinearSliderPos (value);
        }
        else
        {
            jassertfalse; // not a valid call on a slider that doesn't work linearly!
            return 0.0f;
        }
    }

    void setRange (const double newMin,
                   const double newMax,
                   const double newInt)
    {
        if (minimum != newMin
            || maximum != newMax
            || interval != newInt)
        {
            minimum = newMin;
            maximum = newMax;
            interval = newInt;

            // figure out the number of DPs needed to display all values at this
            // interval setting.
            numDecimalPlaces = 7;

            if (newInt != 0)
            {
                int v = abs ((int) (newInt * 10000000));

                while ((v % 10) == 0)
                {
                    --numDecimalPlaces;
                    v /= 10;
                }
            }

            // keep the current values inside the new range..
            if (style != TwoValueHorizontal && style != TwoValueVertical)
            {
                setValue (getValue(), false, false);
            }
            else
            {
                setMinValue (getMinValue(), false, false, false);
                setMaxValue (getMaxValue(), false, false, false);
            }

            updateText();
        }
    }

    double getValue() const
    {
        // for a two-value style slider, you should use the getMinValue() and getMaxValue()
        // methods to get the two values.
        jassert (style != TwoValueHorizontal && style != TwoValueVertical);

        return currentValue.getValue();
    }

    void setValue (double newValue,
                   const bool sendUpdateMessage,
                   const bool sendMessageSynchronously)
    {
        // for a two-value style slider, you should use the setMinValue() and setMaxValue()
        // methods to set the two values.
        jassert (style != TwoValueHorizontal && style != TwoValueVertical);

        newValue = constrainedValue (newValue);

        if (style == ThreeValueHorizontal || style == ThreeValueVertical)
        {
            jassert ((double) valueMin.getValue() <= (double) valueMax.getValue());

            newValue = jlimit ((double) valueMin.getValue(),
                               (double) valueMax.getValue(),
                               newValue);
        }

        if (newValue != lastCurrentValue)
        {
            if (valueBox != nullptr)
                valueBox->hideEditor (true);

            lastCurrentValue = newValue;

            // (need to do this comparison because the Value will use equalsWithSameType to compare
            // the new and old values, so will generate unwanted change events if the type changes)
            if (currentValue != newValue)
                currentValue = newValue;

            updateText();
            owner.repaint();

            if (popupDisplay != nullptr)
                popupDisplay->updatePosition (owner.getTextFromValue (newValue));

            if (sendUpdateMessage)
                triggerChangeMessage (sendMessageSynchronously);
        }
    }

    void setMinValue (double newValue, const bool sendUpdateMessage,
                      const bool sendMessageSynchronously, const bool allowNudgingOfOtherValues)
    {
        // The minimum value only applies to sliders that are in two- or three-value mode.
        jassert (style == TwoValueHorizontal || style == TwoValueVertical
                  || style == ThreeValueHorizontal || style == ThreeValueVertical);

        newValue = constrainedValue (newValue);

        if (style == TwoValueHorizontal || style == TwoValueVertical)
        {
            if (allowNudgingOfOtherValues && newValue > (double) valueMax.getValue())
                setMaxValue (newValue, sendUpdateMessage, sendMessageSynchronously, false);

            newValue = jmin ((double) valueMax.getValue(), newValue);
        }
        else
        {
            if (allowNudgingOfOtherValues && newValue > lastCurrentValue)
                setValue (newValue, sendUpdateMessage, sendMessageSynchronously);

            newValue = jmin (lastCurrentValue, newValue);
        }

        if (lastValueMin != newValue)
        {
            lastValueMin = newValue;
            valueMin = newValue;
            owner.repaint();

            if (popupDisplay != nullptr)
                popupDisplay->updatePosition (owner.getTextFromValue (newValue));

            if (sendUpdateMessage)
                triggerChangeMessage (sendMessageSynchronously);
        }
    }

    void setMaxValue (double newValue, const bool sendUpdateMessage, const bool sendMessageSynchronously, const bool allowNudgingOfOtherValues)
    {
        // The maximum value only applies to sliders that are in two- or three-value mode.
        jassert (style == TwoValueHorizontal || style == TwoValueVertical
                  || style == ThreeValueHorizontal || style == ThreeValueVertical);

        newValue = constrainedValue (newValue);

        if (style == TwoValueHorizontal || style == TwoValueVertical)
        {
            if (allowNudgingOfOtherValues && newValue < (double) valueMin.getValue())
                setMinValue (newValue, sendUpdateMessage, sendMessageSynchronously, false);

            newValue = jmax ((double) valueMin.getValue(), newValue);
        }
        else
        {
            if (allowNudgingOfOtherValues && newValue < lastCurrentValue)
                setValue (newValue, sendUpdateMessage, sendMessageSynchronously);

            newValue = jmax (lastCurrentValue, newValue);
        }

        if (lastValueMax != newValue)
        {
            lastValueMax = newValue;
            valueMax = newValue;
            owner.repaint();

            if (popupDisplay != nullptr)
                popupDisplay->updatePosition (owner.getTextFromValue (valueMax.getValue()));

            if (sendUpdateMessage)
                triggerChangeMessage (sendMessageSynchronously);
        }
    }

    void setMinAndMaxValues (double newMinValue, double newMaxValue, bool sendUpdateMessage, bool sendMessageSynchronously)
    {
        // The maximum value only applies to sliders that are in two- or three-value mode.
        jassert (style == TwoValueHorizontal || style == TwoValueVertical
                  || style == ThreeValueHorizontal || style == ThreeValueVertical);

        if (newMaxValue < newMinValue)
            std::swap (newMaxValue, newMinValue);

        newMinValue = constrainedValue (newMinValue);
        newMaxValue = constrainedValue (newMaxValue);

        if (lastValueMax != newMaxValue || lastValueMin != newMinValue)
        {
            lastValueMax = newMaxValue;
            lastValueMin = newMinValue;
            valueMin = newMinValue;
            valueMax = newMaxValue;
            owner.repaint();

            if (sendUpdateMessage)
                triggerChangeMessage (sendMessageSynchronously);
        }
    }

    double getMinValue() const
    {
        // The minimum value only applies to sliders that are in two- or three-value mode.
        jassert (style == TwoValueHorizontal || style == TwoValueVertical
                  || style == ThreeValueHorizontal || style == ThreeValueVertical);

        return valueMin.getValue();
    }

    double getMaxValue() const
    {
        // The maximum value only applies to sliders that are in two- or three-value mode.
        jassert (style == TwoValueHorizontal || style == TwoValueVertical
                  || style == ThreeValueHorizontal || style == ThreeValueVertical);

        return valueMax.getValue();
    }

    void triggerChangeMessage (const bool synchronous)
    {
        if (synchronous)
            handleAsyncUpdate();
        else
            triggerAsyncUpdate();

        owner.valueChanged();
    }

    void handleAsyncUpdate()
    {
        cancelPendingUpdate();

        Component::BailOutChecker checker (&owner);
        Slider* slider = &owner; // (must use an intermediate variable here to avoid a VS2005 compiler bug)
        listeners.callChecked (checker, &SliderListener::sliderValueChanged, slider);  // (can't use Slider::Listener due to idiotic VC2005 bug)
    }

    void sendDragStart()
    {
        owner.startedDragging();

        Component::BailOutChecker checker (&owner);
        Slider* slider = &owner; // (must use an intermediate variable here to avoid a VS2005 compiler bug)
        listeners.callChecked (checker, &SliderListener::sliderDragStarted, slider);
    }

    void sendDragEnd()
    {
        owner.stoppedDragging();

        sliderBeingDragged = -1;

        Component::BailOutChecker checker (&owner);
        Slider* slider = &owner; // (must use an intermediate variable here to avoid a VS2005 compiler bug)
        listeners.callChecked (checker, &SliderListener::sliderDragEnded, slider);
    }

    void buttonClicked (Button* button)
    {
        if (style == IncDecButtons)
        {
            sendDragStart();

            if (button == incButton)
                setValue (owner.snapValue (getValue() + interval, false), true, true);
            else if (button == decButton)
                setValue (owner.snapValue (getValue() - interval, false), true, true);

            sendDragEnd();
        }
    }

    void valueChanged (Value& value)
    {
        if (value.refersToSameSourceAs (currentValue))
        {
            if (style != TwoValueHorizontal && style != TwoValueVertical)
                setValue (currentValue.getValue(), false, false);
        }
        else if (value.refersToSameSourceAs (valueMin))
            setMinValue (valueMin.getValue(), false, false, true);
        else if (value.refersToSameSourceAs (valueMax))
            setMaxValue (valueMax.getValue(), false, false, true);
    }

    void labelTextChanged (Label* label)
    {
        const double newValue = owner.snapValue (owner.getValueFromText (label->getText()), false);

        if (newValue != (double) currentValue.getValue())
        {
            sendDragStart();
            setValue (newValue, true, true);
            sendDragEnd();
        }

        updateText(); // force a clean-up of the text, needed in case setValue() hasn't done this.
    }

    void updateText()
    {
        if (valueBox != nullptr)
            valueBox->setText (owner.getTextFromValue (currentValue.getValue()), false);
    }

    double constrainedValue (double value) const
    {
        if (interval > 0)
            value = minimum + interval * std::floor ((value - minimum) / interval + 0.5);

        if (value <= minimum || maximum <= minimum)
            value = minimum;
        else if (value >= maximum)
            value = maximum;

        return value;
    }

    float getLinearSliderPos (const double value)
    {
        double sliderPosProportional;

        if (maximum > minimum)
        {
            if (value < minimum)
            {
                sliderPosProportional = 0.0;
            }
            else if (value > maximum)
            {
                sliderPosProportional = 1.0;
            }
            else
            {
                sliderPosProportional = owner.valueToProportionOfLength (value);
                jassert (sliderPosProportional >= 0 && sliderPosProportional <= 1.0);
            }
        }
        else
        {
            sliderPosProportional = 0.5;
        }

        if (isVertical() || style == IncDecButtons)
            sliderPosProportional = 1.0 - sliderPosProportional;

        return (float) (sliderRegionStart + sliderPosProportional * sliderRegionSize);
    }

    void setSliderStyle (const SliderStyle newStyle)
    {
        if (style != newStyle)
        {
            style = newStyle;
            owner.repaint();
            owner.lookAndFeelChanged();
        }
    }

    void setRotaryParameters (const float startAngleRadians,
                              const float endAngleRadians,
                              const bool stopAtEnd)
    {
        // make sure the values are sensible..
        jassert (rotaryStart >= 0 && rotaryEnd >= 0);
        jassert (rotaryStart < float_Pi * 4.0f && rotaryEnd < float_Pi * 4.0f);
        jassert (rotaryStart < rotaryEnd);

        rotaryStart = startAngleRadians;
        rotaryEnd = endAngleRadians;
        rotaryStop = stopAtEnd;
    }

    void setVelocityModeParameters (const double sensitivity, const int threshold,
                                    const double offset, const bool userCanPressKeyToSwapMode)
    {
        velocityModeSensitivity = sensitivity;
        velocityModeOffset = offset;
        velocityModeThreshold = threshold;
        userKeyOverridesVelocity = userCanPressKeyToSwapMode;
    }

    void setSkewFactorFromMidPoint (const double sliderValueToShowAtMidPoint)
    {
        if (maximum > minimum)
            skewFactor = log (0.5) / log ((sliderValueToShowAtMidPoint - minimum)
                                            / (maximum - minimum));
    }

    void setIncDecButtonsMode (const IncDecButtonMode mode)
    {
        if (incDecButtonMode != mode)
        {
            incDecButtonMode = mode;
            owner.lookAndFeelChanged();
        }
    }

    void setTextBoxStyle (const TextEntryBoxPosition newPosition,
                          const bool isReadOnly,
                          const int textEntryBoxWidth,
                          const int textEntryBoxHeight)
    {
        if (textBoxPos != newPosition
             || editableText != (! isReadOnly)
             || textBoxWidth != textEntryBoxWidth
             || textBoxHeight != textEntryBoxHeight)
        {
            textBoxPos = newPosition;
            editableText = ! isReadOnly;
            textBoxWidth = textEntryBoxWidth;
            textBoxHeight = textEntryBoxHeight;

            owner.repaint();
            owner.lookAndFeelChanged();
        }
    }

    void setTextBoxIsEditable (const bool shouldBeEditable)
    {
        editableText = shouldBeEditable;

        if (valueBox != nullptr)
            valueBox->setEditable (shouldBeEditable && owner.isEnabled());
    }

    void showTextBox()
    {
        jassert (editableText); // this should probably be avoided in read-only sliders.

        if (valueBox != nullptr)
            valueBox->showEditor();
    }

    void hideTextBox (const bool discardCurrentEditorContents)
    {
        if (valueBox != nullptr)
        {
            valueBox->hideEditor (discardCurrentEditorContents);

            if (discardCurrentEditorContents)
                updateText();
        }
    }

    void setTextValueSuffix (const String& suffix)
    {
        if (textSuffix != suffix)
        {
            textSuffix = suffix;
            updateText();
        }
    }

    void lookAndFeelChanged (LookAndFeel& lf)
    {
        if (textBoxPos != NoTextBox)
        {
            const String previousTextBoxContent (valueBox != nullptr ? valueBox->getText()
                                                                     : owner.getTextFromValue (currentValue.getValue()));

            valueBox = nullptr;
            owner.addAndMakeVisible (valueBox = lf.createSliderTextBox (owner));

            valueBox->setWantsKeyboardFocus (false);
            valueBox->setText (previousTextBoxContent, false);

            if (valueBox->isEditable() != editableText) // (avoid overriding the single/double click flags unless we have to)
                valueBox->setEditable (editableText && owner.isEnabled());

            valueBox->addListener (this);

            if (style == LinearBar)
                valueBox->addMouseListener (&owner, false);
            else
                valueBox->setTooltip (owner.getTooltip());
        }
        else
        {
            valueBox = nullptr;
        }

        if (style == IncDecButtons)
        {
            owner.addAndMakeVisible (incButton = lf.createSliderButton (true));
            incButton->addListener (this);

            owner.addAndMakeVisible (decButton = lf.createSliderButton (false));
            decButton->addListener (this);

            if (incDecButtonMode != incDecButtonsNotDraggable)
            {
                incButton->addMouseListener (&owner, false);
                decButton->addMouseListener (&owner, false);
            }
            else
            {
                incButton->setRepeatSpeed (300, 100, 20);
                incButton->addMouseListener (decButton, false);

                decButton->setRepeatSpeed (300, 100, 20);
                decButton->addMouseListener (incButton, false);
            }

            const String tooltip (owner.getTooltip());
            incButton->setTooltip (tooltip);
            decButton->setTooltip (tooltip);
        }
        else
        {
            incButton = nullptr;
            decButton = nullptr;
        }

        owner.setComponentEffect (lf.getSliderEffect());

        owner.resized();
        owner.repaint();
    }

    bool incDecDragDirectionIsHorizontal() const
    {
        return incDecButtonMode == incDecButtonsDraggable_Horizontal
                || (incDecButtonMode == incDecButtonsDraggable_AutoDirection && incDecButtonsSideBySide);
    }

    void showPopupMenu()
    {
        menuShown = true;

        PopupMenu m;
        m.setLookAndFeel (&owner.getLookAndFeel());
        m.addItem (1, TRANS ("velocity-sensitive mode"), true, isVelocityBased);
        m.addSeparator();

        if (style == Rotary || style == RotaryHorizontalDrag || style == RotaryVerticalDrag)
        {
            PopupMenu rotaryMenu;
            rotaryMenu.addItem (2, TRANS ("use circular dragging"), true, style == Rotary);
            rotaryMenu.addItem (3, TRANS ("use left-right dragging"), true, style == RotaryHorizontalDrag);
            rotaryMenu.addItem (4, TRANS ("use up-down dragging"), true, style == RotaryVerticalDrag);

            m.addSubMenu (TRANS ("rotary mode"), rotaryMenu);
        }

        m.showMenuAsync (PopupMenu::Options(),
                         ModalCallbackFunction::forComponent (sliderMenuCallback, &owner));
    }

    int getThumbIndexAt (const MouseEvent& e)
    {
        const bool isTwoValue   = (style == TwoValueHorizontal   || style == TwoValueVertical);
        const bool isThreeValue = (style == ThreeValueHorizontal || style == ThreeValueVertical);

        if (isTwoValue || isThreeValue)
        {
            const float mousePos = (float) (isVertical() ? e.y : e.x);

            const float normalPosDistance = std::abs (getLinearSliderPos (currentValue.getValue()) - mousePos);
            const float minPosDistance    = std::abs (getLinearSliderPos (valueMin.getValue()) - 0.1f - mousePos);
            const float maxPosDistance    = std::abs (getLinearSliderPos (valueMax.getValue()) + 0.1f - mousePos);

            if (isTwoValue)
                return maxPosDistance <= minPosDistance ? 2 : 1;

            if (normalPosDistance >= minPosDistance && maxPosDistance >= minPosDistance)
                return 1;
            else if (normalPosDistance >= maxPosDistance)
                return 2;
        }

        return 0;
    }

    //==============================================================================
    void handleRotaryDrag (const MouseEvent& e)
    {
        const int dx = e.x - sliderRect.getCentreX();
        const int dy = e.y - sliderRect.getCentreY();

        if (dx * dx + dy * dy > 25)
        {
            double angle = std::atan2 ((double) dx, (double) -dy);
            while (angle < 0.0)
                angle += double_Pi * 2.0;

            if (rotaryStop && ! e.mouseWasClicked())
            {
                if (std::abs (angle - lastAngle) > double_Pi)
                {
                    if (angle >= lastAngle)
                        angle -= double_Pi * 2.0;
                    else
                        angle += double_Pi * 2.0;
                }

                if (angle >= lastAngle)
                    angle = jmin (angle, (double) jmax (rotaryStart, rotaryEnd));
                else
                    angle = jmax (angle, (double) jmin (rotaryStart, rotaryEnd));
            }
            else
            {
                while (angle < rotaryStart)
                    angle += double_Pi * 2.0;

                if (angle > rotaryEnd)
                {
                    if (smallestAngleBetween (angle, rotaryStart)
                         <= smallestAngleBetween (angle, rotaryEnd))
                        angle = rotaryStart;
                    else
                        angle = rotaryEnd;
                }
            }

            const double proportion = (angle - rotaryStart) / (rotaryEnd - rotaryStart);
            valueWhenLastDragged = owner.proportionOfLengthToValue (jlimit (0.0, 1.0, proportion));
            lastAngle = angle;
        }
    }

    void handleAbsoluteDrag (const MouseEvent& e)
    {
        const int mousePos = (isHorizontal() || style == RotaryHorizontalDrag) ? e.x : e.y;

        double scaledMousePos = (mousePos - sliderRegionStart) / (double) sliderRegionSize;

        if (style == RotaryHorizontalDrag
            || style == RotaryVerticalDrag
            || style == IncDecButtons
            || ((style == LinearHorizontal || style == LinearVertical || style == LinearBar)
                && ! snapsToMousePos))
        {
            const int mouseDiff = (style == RotaryHorizontalDrag
                                     || style == LinearHorizontal
                                     || style == LinearBar
                                     || (style == IncDecButtons && incDecDragDirectionIsHorizontal()))
                                    ? e.x - mouseDragStartPos.x
                                    : mouseDragStartPos.y - e.y;

            double newPos = owner.valueToProportionOfLength (valueOnMouseDown)
                               + mouseDiff * (1.0 / pixelsForFullDragExtent);

            valueWhenLastDragged = owner.proportionOfLengthToValue (jlimit (0.0, 1.0, newPos));

            if (style == IncDecButtons)
            {
                incButton->setState (mouseDiff < 0 ? Button::buttonNormal : Button::buttonDown);
                decButton->setState (mouseDiff > 0 ? Button::buttonNormal : Button::buttonDown);
            }
        }
        else
        {
            if (isVertical())
                scaledMousePos = 1.0 - scaledMousePos;

            valueWhenLastDragged = owner.proportionOfLengthToValue (jlimit (0.0, 1.0, scaledMousePos));
        }
    }

    void handleVelocityDrag (const MouseEvent& e)
    {
        const int mouseDiff = (isHorizontal() || style == RotaryHorizontalDrag
                                 || (style == IncDecButtons && incDecDragDirectionIsHorizontal()))
                                ? e.x - mousePosWhenLastDragged.x
                                : e.y - mousePosWhenLastDragged.y;

        const double maxSpeed = jmax (200, sliderRegionSize);
        double speed = jlimit (0.0, maxSpeed, (double) abs (mouseDiff));

        if (speed != 0)
        {
            speed = 0.2 * velocityModeSensitivity
                      * (1.0 + std::sin (double_Pi * (1.5 + jmin (0.5, velocityModeOffset
                                                                    + jmax (0.0, (double) (speed - velocityModeThreshold))
                                                                        / maxSpeed))));

            if (mouseDiff < 0)
                speed = -speed;

            if (isVertical() || style == RotaryVerticalDrag
                 || (style == IncDecButtons && ! incDecDragDirectionIsHorizontal()))
                speed = -speed;

            const double currentPos = owner.valueToProportionOfLength (valueWhenLastDragged);

            valueWhenLastDragged = owner.proportionOfLengthToValue (jlimit (0.0, 1.0, currentPos + speed));

            e.source.enableUnboundedMouseMovement (true, false);
            mouseWasHidden = true;
        }
    }

    void mouseDown (const MouseEvent& e)
    {
        mouseWasHidden = false;
        incDecDragged = false;
        mouseDragStartPos = mousePosWhenLastDragged = e.getPosition();

        if (owner.isEnabled())
        {
            if (e.mods.isPopupMenu() && menuEnabled)
            {
                showPopupMenu();
            }
            else if (maximum > minimum)
            {
                menuShown = false;

                if (valueBox != nullptr)
                    valueBox->hideEditor (true);

                sliderBeingDragged = getThumbIndexAt (e);

                minMaxDiff = (double) valueMax.getValue() - (double) valueMin.getValue();

                lastAngle = rotaryStart + (rotaryEnd - rotaryStart)
                                            * owner.valueToProportionOfLength (currentValue.getValue());

                valueWhenLastDragged = (sliderBeingDragged == 2 ? valueMax
                                                                : (sliderBeingDragged == 1 ? valueMin
                                                                                           : currentValue)).getValue();
                valueOnMouseDown = valueWhenLastDragged;

                if (popupDisplayEnabled)
                {
                    PopupDisplayComponent* const popup = new PopupDisplayComponent (owner);
                    popupDisplay = popup;

                    if (parentForPopupDisplay != nullptr)
                        parentForPopupDisplay->addChildComponent (popup);
                    else
                        popup->addToDesktop (0);

                    popup->setVisible (true);
                }

                sendDragStart();
                mouseDrag (e);
            }
        }
    }

    void mouseDrag (const MouseEvent& e)
    {
        if ((! menuShown)
             && maximum > minimum
             && ! (style == LinearBar && e.mouseWasClicked() && valueBox != nullptr && valueBox->isEditable()))
        {
            if (style == Rotary)
            {
                handleRotaryDrag (e);
            }
            else
            {
                if (style == IncDecButtons && ! incDecDragged)
                {
                    if (e.getDistanceFromDragStart() < 10 || e.mouseWasClicked())
                        return;

                    incDecDragged = true;
                    mouseDragStartPos = e.getPosition();
                }

                if (isVelocityBased == (userKeyOverridesVelocity && e.mods.testFlags (ModifierKeys::ctrlModifier
                                                                                        | ModifierKeys::commandModifier
                                                                                        | ModifierKeys::altModifier))
                     || (maximum - minimum) / sliderRegionSize < interval)
                    handleAbsoluteDrag (e);
                else
                    handleVelocityDrag (e);
            }

            valueWhenLastDragged = jlimit (minimum, maximum, valueWhenLastDragged);

            if (sliderBeingDragged == 0)
            {
                setValue (owner.snapValue (valueWhenLastDragged, true),
                          ! sendChangeOnlyOnRelease, true);
            }
            else if (sliderBeingDragged == 1)
            {
                setMinValue (owner.snapValue (valueWhenLastDragged, true),
                             ! sendChangeOnlyOnRelease, false, true);

                if (e.mods.isShiftDown())
                    setMaxValue (getMinValue() + minMaxDiff, false, false, true);
                else
                    minMaxDiff = (double) valueMax.getValue() - (double) valueMin.getValue();
            }
            else if (sliderBeingDragged == 2)
            {
                setMaxValue (owner.snapValue (valueWhenLastDragged, true),
                             ! sendChangeOnlyOnRelease, false, true);

                if (e.mods.isShiftDown())
                    setMinValue (getMaxValue() - minMaxDiff, false, false, true);
                else
                    minMaxDiff = (double) valueMax.getValue() - (double) valueMin.getValue();
            }

            mousePosWhenLastDragged = e.getPosition();
        }
    }

    void mouseUp()
    {
        if (owner.isEnabled()
             && (! menuShown)
             && (maximum > minimum)
             && (style != IncDecButtons || incDecDragged))
        {
            restoreMouseIfHidden();

            if (sendChangeOnlyOnRelease && valueOnMouseDown != (double) currentValue.getValue())
                triggerChangeMessage (false);

            sendDragEnd();
            popupDisplay = nullptr;

            if (style == IncDecButtons)
            {
                incButton->setState (Button::buttonNormal);
                decButton->setState (Button::buttonNormal);
            }
        }
        else if (popupDisplay != nullptr)
        {
            popupDisplay->startTimer (2000);
        }
    }

    void mouseDoubleClick()
    {
        if (style != IncDecButtons
             && minimum <= doubleClickReturnValue
             && maximum >= doubleClickReturnValue)
        {
            sendDragStart();
            setValue (doubleClickReturnValue, true, true);
            sendDragEnd();
        }
    }

    bool mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
    {
        if (scrollWheelEnabled
             && style != TwoValueHorizontal
             && style != TwoValueVertical)
        {
            if (maximum > minimum && ! e.mods.isAnyMouseButtonDown())
            {
                if (valueBox != nullptr)
                    valueBox->hideEditor (false);

                const double value = (double) currentValue.getValue();
                const double proportionDelta = (wheel.deltaX != 0 ? -wheel.deltaX : wheel.deltaY)
                                                   * (wheel.isReversed ? -0.15f : 0.15f);
                const double currentPos = owner.valueToProportionOfLength (value);
                const double newValue = owner.proportionOfLengthToValue (jlimit (0.0, 1.0, currentPos + proportionDelta));

                double delta = (newValue != value) ? jmax (std::abs (newValue - value), interval) : 0;
                if (value > newValue)
                    delta = -delta;

                sendDragStart();
                setValue (owner.snapValue (value + delta, false), true, true);
                sendDragEnd();
            }

            return true;
        }

        return false;
    }

    void modifierKeysChanged (const ModifierKeys& modifiers)
    {
        if (style != IncDecButtons
             && style != Rotary
             && isVelocityBased == modifiers.isAnyModifierKeyDown())
        {
            restoreMouseIfHidden();
        }
    }

    void restoreMouseIfHidden()
    {
        if (mouseWasHidden)
        {
            mouseWasHidden = false;

            for (int i = Desktop::getInstance().getNumMouseSources(); --i >= 0;)
                Desktop::getInstance().getMouseSource(i)->enableUnboundedMouseMovement (false);

            const double pos = sliderBeingDragged == 2 ? getMaxValue()
                                                       : (sliderBeingDragged == 1 ? getMinValue()
                                                                                  : (double) currentValue.getValue());
            Point<int> mousePos;

            if (style == RotaryHorizontalDrag || style == RotaryVerticalDrag)
            {
                mousePos = Desktop::getLastMouseDownPosition();

                if (style == RotaryHorizontalDrag)
                {
                    const double posDiff = owner.valueToProportionOfLength (pos)
                                            - owner.valueToProportionOfLength (valueOnMouseDown);
                    mousePos += Point<int> (roundToInt (pixelsForFullDragExtent * posDiff), 0);
                }
                else
                {
                    const double posDiff = owner.valueToProportionOfLength (valueOnMouseDown)
                                            - owner.valueToProportionOfLength (pos);
                    mousePos += Point<int> (0, roundToInt (pixelsForFullDragExtent * posDiff));
                }
            }
            else
            {
                const int pixelPos = (int) getLinearSliderPos (pos);

                mousePos = owner.localPointToGlobal (Point<int> (isHorizontal() ? pixelPos : (owner.getWidth() / 2),
                                                                 isVertical()   ? pixelPos : (owner.getHeight() / 2)));
            }

            Desktop::setMousePosition (mousePos);
        }
    }

    //==============================================================================
    void paint (Graphics& g, LookAndFeel& lf)
    {
        if (style != IncDecButtons)
        {
            if (style == Rotary || style == RotaryHorizontalDrag || style == RotaryVerticalDrag)
            {
                const float sliderPos = (float) owner.valueToProportionOfLength (lastCurrentValue);
                jassert (sliderPos >= 0 && sliderPos <= 1.0f);

                lf.drawRotarySlider (g,
                                     sliderRect.getX(), sliderRect.getY(),
                                     sliderRect.getWidth(), sliderRect.getHeight(),
                                     sliderPos, rotaryStart, rotaryEnd, owner);
            }
            else
            {
                lf.drawLinearSlider (g,
                                     sliderRect.getX(), sliderRect.getY(),
                                     sliderRect.getWidth(), sliderRect.getHeight(),
                                     getLinearSliderPos (lastCurrentValue),
                                     getLinearSliderPos (lastValueMin),
                                     getLinearSliderPos (lastValueMax),
                                     style, owner);
            }

            if (style == LinearBar && valueBox == nullptr)
            {
                g.setColour (owner.findColour (Slider::textBoxOutlineColourId));
                g.drawRect (0, 0, owner.getWidth(), owner.getHeight(), 1);
            }
        }
    }

    void resized (const Rectangle<int>& localBounds, LookAndFeel& lf)
    {
        int minXSpace = 0;
        int minYSpace = 0;

        if (textBoxPos == TextBoxLeft || textBoxPos == TextBoxRight)
            minXSpace = 30;
        else
            minYSpace = 15;

        const int tbw = jmax (0, jmin (textBoxWidth,  localBounds.getWidth() - minXSpace));
        const int tbh = jmax (0, jmin (textBoxHeight, localBounds.getHeight() - minYSpace));

        if (style == LinearBar)
        {
            if (valueBox != nullptr)
                valueBox->setBounds (localBounds);
        }
        else
        {
            if (textBoxPos == NoTextBox)
            {
                sliderRect = localBounds;
            }
            else if (textBoxPos == TextBoxLeft)
            {
                valueBox->setBounds (0, (localBounds.getHeight() - tbh) / 2, tbw, tbh);
                sliderRect.setBounds (tbw, 0, localBounds.getWidth() - tbw, localBounds.getHeight());
            }
            else if (textBoxPos == TextBoxRight)
            {
                valueBox->setBounds (localBounds.getWidth() - tbw, (localBounds.getHeight() - tbh) / 2, tbw, tbh);
                sliderRect.setBounds (0, 0, localBounds.getWidth() - tbw, localBounds.getHeight());
            }
            else if (textBoxPos == TextBoxAbove)
            {
                valueBox->setBounds ((localBounds.getWidth() - tbw) / 2, 0, tbw, tbh);
                sliderRect.setBounds (0, tbh, localBounds.getWidth(), localBounds.getHeight() - tbh);
            }
            else if (textBoxPos == TextBoxBelow)
            {
                valueBox->setBounds ((localBounds.getWidth() - tbw) / 2, localBounds.getHeight() - tbh, tbw, tbh);
                sliderRect.setBounds (0, 0, localBounds.getWidth(), localBounds.getHeight() - tbh);
            }
        }

        const int indent = lf.getSliderThumbRadius (owner);

        if (style == LinearBar)
        {
            const int barIndent = 1;
            sliderRegionStart = barIndent;
            sliderRegionSize = localBounds.getWidth() - barIndent * 2;

            sliderRect.setBounds (sliderRegionStart, barIndent,
                                  sliderRegionSize, localBounds.getHeight() - barIndent * 2);
        }
        else if (isHorizontal())
        {
            sliderRegionStart = sliderRect.getX() + indent;
            sliderRegionSize = jmax (1, sliderRect.getWidth() - indent * 2);

            sliderRect.setBounds (sliderRegionStart, sliderRect.getY(),
                                  sliderRegionSize, sliderRect.getHeight());
        }
        else if (isVertical())
        {
            sliderRegionStart = sliderRect.getY() + indent;
            sliderRegionSize = jmax (1, sliderRect.getHeight() - indent * 2);

            sliderRect.setBounds (sliderRect.getX(), sliderRegionStart,
                                  sliderRect.getWidth(), sliderRegionSize);
        }
        else
        {
            sliderRegionStart = 0;
            sliderRegionSize = 100;
        }

        if (style == IncDecButtons)
            resizeIncDecButtons();
    }

    void resizeIncDecButtons()
    {
        Rectangle<int> buttonRect (sliderRect);

        if (textBoxPos == TextBoxLeft || textBoxPos == TextBoxRight)
            buttonRect.expand (-2, 0);
        else
            buttonRect.expand (0, -2);

        incDecButtonsSideBySide = buttonRect.getWidth() > buttonRect.getHeight();

        if (incDecButtonsSideBySide)
        {
            decButton->setBounds (buttonRect.removeFromLeft (buttonRect.getWidth() / 2));
            decButton->setConnectedEdges (Button::ConnectedOnRight);
            incButton->setConnectedEdges (Button::ConnectedOnLeft);
        }
        else
        {
            decButton->setBounds (buttonRect.removeFromBottom (buttonRect.getHeight() / 2));
            decButton->setConnectedEdges (Button::ConnectedOnTop);
            incButton->setConnectedEdges (Button::ConnectedOnBottom);
        }

        incButton->setBounds (buttonRect);
    }

    //==============================================================================
    Slider& owner;
    SliderStyle style;

    ListenerList <SliderListener> listeners;
    Value currentValue, valueMin, valueMax;
    double lastCurrentValue, lastValueMin, lastValueMax;
    double minimum, maximum, interval, doubleClickReturnValue;
    double valueWhenLastDragged, valueOnMouseDown, skewFactor, lastAngle;
    double velocityModeSensitivity, velocityModeOffset, minMaxDiff;
    int velocityModeThreshold;
    float rotaryStart, rotaryEnd;
    Point<int> mouseDragStartPos, mousePosWhenLastDragged;
    int sliderRegionStart, sliderRegionSize;
    int sliderBeingDragged;
    int pixelsForFullDragExtent;
    Rectangle<int> sliderRect;

    TextEntryBoxPosition textBoxPos;
    String textSuffix;
    int numDecimalPlaces;
    int textBoxWidth, textBoxHeight;
    IncDecButtonMode incDecButtonMode;

    bool editableText : 1;
    bool doubleClickToValue : 1;
    bool isVelocityBased : 1;
    bool userKeyOverridesVelocity : 1;
    bool rotaryStop : 1;
    bool incDecButtonsSideBySide : 1;
    bool sendChangeOnlyOnRelease : 1;
    bool popupDisplayEnabled : 1;
    bool menuEnabled : 1;
    bool menuShown : 1;
    bool mouseWasHidden : 1;
    bool incDecDragged : 1;
    bool scrollWheelEnabled : 1;
    bool snapsToMousePos : 1;

    ScopedPointer<Label> valueBox;
    ScopedPointer<Button> incButton, decButton;

    //==============================================================================
    class PopupDisplayComponent  : public BubbleComponent,
                                   public Timer
    {
    public:
        PopupDisplayComponent (Slider& owner_)
            : owner (owner_),
              font (15.0f, Font::bold)
        {
            setAlwaysOnTop (true);
        }

        void paintContent (Graphics& g, int w, int h)
        {
            g.setFont (font);
            g.setColour (findColour (TooltipWindow::textColourId, true));
            g.drawFittedText (text, 0, 0, w, h, Justification::centred, 1);
        }

        void getContentSize (int& w, int& h)
        {
            w = font.getStringWidth (text) + 18;
            h = (int) (font.getHeight() * 1.6f);
        }

        void updatePosition (const String& newText)
        {
            text = newText;
            BubbleComponent::setPosition (&owner);
            repaint();
        }

        void timerCallback()
        {
            owner.pimpl->popupDisplay = nullptr;
        }

    private:
        Slider& owner;
        Font font;
        String text;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PopupDisplayComponent);
    };

    ScopedPointer <PopupDisplayComponent> popupDisplay;
    Component* parentForPopupDisplay;

    //==============================================================================
    static double smallestAngleBetween (const double a1, const double a2) noexcept
    {
        return jmin (std::abs (a1 - a2),
                     std::abs (a1 + double_Pi * 2.0 - a2),
                     std::abs (a2 + double_Pi * 2.0 - a1));
    }

    static void sliderMenuCallback (const int result, Slider* slider)
    {
        if (slider != nullptr)
        {
            switch (result)
            {
                case 1:   slider->setVelocityBasedMode (! slider->getVelocityBasedMode()); break;
                case 2:   slider->setSliderStyle (Rotary); break;
                case 3:   slider->setSliderStyle (RotaryHorizontalDrag); break;
                case 4:   slider->setSliderStyle (RotaryVerticalDrag); break;
                default:  break;
            }
        }
    }
};


//==============================================================================
Slider::Slider()
{
    init (LinearHorizontal, TextBoxLeft);
}

Slider::Slider (const String& name)  : Component (name)
{
    init (LinearHorizontal, TextBoxLeft);
}

Slider::Slider (SliderStyle style, TextEntryBoxPosition textBoxPos)
{
    init (style, textBoxPos);
}

void Slider::init (SliderStyle style, TextEntryBoxPosition textBoxPos)
{
    setWantsKeyboardFocus (false);
    setRepaintsOnMouseActivity (true);

    pimpl = new Pimpl (*this, style, textBoxPos);

    Slider::lookAndFeelChanged();
    updateText();

    pimpl->init();
}

Slider::~Slider() {}

//==============================================================================
void Slider::addListener (SliderListener* const listener)       { pimpl->listeners.add (listener); }
void Slider::removeListener (SliderListener* const listener)    { pimpl->listeners.remove (listener); }

//==============================================================================
Slider::SliderStyle Slider::getSliderStyle() const noexcept     { return pimpl->style; }
void Slider::setSliderStyle (const SliderStyle newStyle)        { pimpl->setSliderStyle (newStyle); }

void Slider::setRotaryParameters (const float startAngleRadians, const float endAngleRadians, const bool stopAtEnd)
{
    pimpl->setRotaryParameters (startAngleRadians, endAngleRadians, stopAtEnd);
}

void Slider::setVelocityBasedMode (bool vb)                 { pimpl->isVelocityBased = vb; }
bool Slider::getVelocityBasedMode() const noexcept          { return pimpl->isVelocityBased; }
bool Slider::getVelocityModeIsSwappable() const noexcept    { return pimpl->userKeyOverridesVelocity; }
int Slider::getVelocityThreshold() const noexcept           { return pimpl->velocityModeThreshold; }
double Slider::getVelocitySensitivity() const noexcept      { return pimpl->velocityModeSensitivity; }
double Slider::getVelocityOffset() const noexcept           { return pimpl->velocityModeOffset; }

void Slider::setVelocityModeParameters (const double sensitivity, const int threshold,
                                        const double offset, const bool userCanPressKeyToSwapMode)
{
    jassert (threshold >= 0);
    jassert (sensitivity > 0);
    jassert (offset >= 0);

    pimpl->setVelocityModeParameters (sensitivity, threshold, offset, userCanPressKeyToSwapMode);
}

double Slider::getSkewFactor() const noexcept               { return pimpl->skewFactor; }
void Slider::setSkewFactor (const double factor)            { pimpl->skewFactor = factor; }

void Slider::setSkewFactorFromMidPoint (const double sliderValueToShowAtMidPoint)
{
    pimpl->setSkewFactorFromMidPoint (sliderValueToShowAtMidPoint);
}

int Slider::getMouseDragSensitivity() const noexcept        { return pimpl->pixelsForFullDragExtent; }

void Slider::setMouseDragSensitivity (const int distanceForFullScaleDrag)
{
    jassert (distanceForFullScaleDrag > 0);

    pimpl->pixelsForFullDragExtent = distanceForFullScaleDrag;
}

void Slider::setIncDecButtonsMode (const IncDecButtonMode mode)             { pimpl->setIncDecButtonsMode (mode); }

Slider::TextEntryBoxPosition Slider::getTextBoxPosition() const noexcept    { return pimpl->textBoxPos; }
int Slider::getTextBoxWidth() const noexcept                                { return pimpl->textBoxWidth; }
int Slider::getTextBoxHeight() const noexcept                               { return pimpl->textBoxHeight; }

void Slider::setTextBoxStyle (const TextEntryBoxPosition newPosition, const bool isReadOnly,
                              const int textEntryBoxWidth, const int textEntryBoxHeight)
{
    pimpl->setTextBoxStyle (newPosition, isReadOnly, textEntryBoxWidth, textEntryBoxHeight);
}

bool Slider::isTextBoxEditable() const noexcept                     { return pimpl->editableText; }
void Slider::setTextBoxIsEditable (const bool shouldBeEditable)     { pimpl->setTextBoxIsEditable (shouldBeEditable); }
void Slider::showTextBox()                                          { pimpl->showTextBox(); }
void Slider::hideTextBox (const bool discardCurrentEditorContents)  { pimpl->hideTextBox (discardCurrentEditorContents); }

void Slider::setChangeNotificationOnlyOnRelease (bool onlyNotifyOnRelease)
{
    pimpl->sendChangeOnlyOnRelease = onlyNotifyOnRelease;
}

bool Slider::getSliderSnapsToMousePosition() const noexcept                 { return pimpl->snapsToMousePos; }
void Slider::setSliderSnapsToMousePosition (const bool shouldSnapToMouse)   { pimpl->snapsToMousePos = shouldSnapToMouse; }

void Slider::setPopupDisplayEnabled (const bool enabled, Component* const parentComponentToUse)
{
    pimpl->popupDisplayEnabled = enabled;
    pimpl->parentForPopupDisplay = parentComponentToUse;
}

Component* Slider::getCurrentPopupDisplay() const noexcept      { return pimpl->popupDisplay.get(); }

//==============================================================================
void Slider::colourChanged()        { lookAndFeelChanged(); }
void Slider::lookAndFeelChanged()   { pimpl->lookAndFeelChanged (getLookAndFeel()); }
void Slider::enablementChanged()    { repaint(); }

//==============================================================================
double Slider::getMaximum() const noexcept      { return pimpl->maximum; }
double Slider::getMinimum() const noexcept      { return pimpl->minimum; }
double Slider::getInterval() const noexcept     { return pimpl->interval; }

void Slider::setRange (double newMin, double newMax, double newInt)
{
    pimpl->setRange (newMin, newMax, newInt);
}

Value& Slider::getValueObject() noexcept        { return pimpl->currentValue; }
Value& Slider::getMinValueObject() noexcept     { return pimpl->valueMin; }
Value& Slider::getMaxValueObject() noexcept     { return pimpl->valueMax; }

double Slider::getValue() const                 { return pimpl->getValue(); }

void Slider::setValue (double newValue, bool sendUpdateMessage, bool sendMessageSynchronously)
{
    pimpl->setValue (newValue, sendUpdateMessage, sendMessageSynchronously);
}

double Slider::getMinValue() const      { return pimpl->getMinValue(); }
double Slider::getMaxValue() const      { return pimpl->getMaxValue(); }

void Slider::setMinValue (double newValue, bool sendUpdateMessage, bool sendMessageSynchronously, bool allowNudgingOfOtherValues)
{
    pimpl->setMinValue (newValue, sendUpdateMessage, sendMessageSynchronously, allowNudgingOfOtherValues);
}

void Slider::setMaxValue (double newValue, bool sendUpdateMessage, bool sendMessageSynchronously, bool allowNudgingOfOtherValues)
{
    pimpl->setMaxValue (newValue, sendUpdateMessage, sendMessageSynchronously, allowNudgingOfOtherValues);
}

void Slider::setMinAndMaxValues (double newMinValue, double newMaxValue, bool sendUpdateMessage, bool sendMessageSynchronously)
{
    pimpl->setMinAndMaxValues (newMinValue, newMaxValue, sendUpdateMessage, sendMessageSynchronously);
}

void Slider::setDoubleClickReturnValue (bool isDoubleClickEnabled,  double valueToSetOnDoubleClick)
{
    pimpl->doubleClickToValue = isDoubleClickEnabled;
    pimpl->doubleClickReturnValue = valueToSetOnDoubleClick;
}

double Slider::getDoubleClickReturnValue (bool& isEnabled_) const
{
    isEnabled_ = pimpl->doubleClickToValue;
    return pimpl->doubleClickReturnValue;
}

void Slider::updateText()
{
    pimpl->updateText();
}

void Slider::setTextValueSuffix (const String& suffix)
{
    pimpl->setTextValueSuffix (suffix);
}

String Slider::getTextValueSuffix() const
{
    return pimpl->textSuffix;
}

String Slider::getTextFromValue (double v)
{
    if (getNumDecimalPlacesToDisplay() > 0)
        return String (v, getNumDecimalPlacesToDisplay()) + getTextValueSuffix();
    else
        return String (roundToInt (v)) + getTextValueSuffix();
}

double Slider::getValueFromText (const String& text)
{
    String t (text.trimStart());

    if (t.endsWith (getTextValueSuffix()))
        t = t.substring (0, t.length() - getTextValueSuffix().length());

    while (t.startsWithChar ('+'))
        t = t.substring (1).trimStart();

    return t.initialSectionContainingOnly ("0123456789.,-")
            .getDoubleValue();
}

double Slider::proportionOfLengthToValue (double proportion)
{
    const double skew = getSkewFactor();

    if (skew != 1.0 && proportion > 0.0)
        proportion = exp (log (proportion) / skew);

    return getMinimum() + (getMaximum() - getMinimum()) * proportion;
}

double Slider::valueToProportionOfLength (double value)
{
    const double n = (value - getMinimum()) / (getMaximum() - getMinimum());
    const double skew = getSkewFactor();

    return skew == 1.0 ? n : pow (n, skew);
}

double Slider::snapValue (double attemptedValue, const bool)
{
    return attemptedValue;
}

int Slider::getNumDecimalPlacesToDisplay() const noexcept    { return pimpl->numDecimalPlaces; }

//==============================================================================
int Slider::getThumbBeingDragged() const noexcept            { return pimpl->sliderBeingDragged; }

void Slider::startedDragging() {}
void Slider::stoppedDragging() {}
void Slider::valueChanged() {}

//==============================================================================
void Slider::setPopupMenuEnabled (const bool menuEnabled_)  { pimpl->menuEnabled = menuEnabled_; }
void Slider::setScrollWheelEnabled (const bool enabled)     { pimpl->scrollWheelEnabled = enabled; }

bool Slider::isHorizontal() const noexcept   { return pimpl->isHorizontal(); }
bool Slider::isVertical() const noexcept     { return pimpl->isVertical(); }

float Slider::getPositionOfValue (const double value)   { return pimpl->getPositionOfValue (value); }

//==============================================================================
void Slider::paint (Graphics& g)        { pimpl->paint (g, getLookAndFeel()); }
void Slider::resized()                  { pimpl->resized (getLocalBounds(), getLookAndFeel()); }

void Slider::focusOfChildComponentChanged (FocusChangeType)     { repaint(); }

void Slider::mouseDown (const MouseEvent& e)    { pimpl->mouseDown (e); }
void Slider::mouseUp (const MouseEvent&)        { pimpl->mouseUp(); }

void Slider::modifierKeysChanged (const ModifierKeys& modifiers)
{
    if (isEnabled())
        pimpl->modifierKeysChanged (modifiers);
}

void Slider::mouseDrag (const MouseEvent& e)
{
    if (isEnabled())
        pimpl->mouseDrag (e);
}

void Slider::mouseDoubleClick (const MouseEvent&)
{
    if (isEnabled())
        pimpl->mouseDoubleClick();
}

void Slider::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    if (! (isEnabled() && pimpl->mouseWheelMove (e, wheel)))
        Component::mouseWheelMove (e, wheel);
}

void SliderListener::sliderDragStarted (Slider*)  {} // (can't write Slider::Listener due to idiotic VC2005 bug)
void SliderListener::sliderDragEnded (Slider*)    {}

//==============================================================================
const Identifier Slider::Ids::tagType ("SLIDER");
const Identifier Slider::Ids::min ("min");
const Identifier Slider::Ids::max ("max");
const Identifier Slider::Ids::interval ("interval");
const Identifier Slider::Ids::type ("type");
const Identifier Slider::Ids::editable ("editable");
const Identifier Slider::Ids::textBoxPos ("textBoxPos");
const Identifier Slider::Ids::textBoxWidth ("textBoxWidth");
const Identifier Slider::Ids::textBoxHeight ("textBoxHeight");
const Identifier Slider::Ids::skew ("skew");

void Slider::refreshFromValueTree (const ValueTree& state, ComponentBuilder&)
{
    ComponentBuilder::refreshBasicComponentProperties (*this, state);

    setRange (static_cast <double> (state [Ids::min]),
              static_cast <double> (state [Ids::max]),
              static_cast <double> (state [Ids::interval]));

    setSliderStyle ((SliderStyle) static_cast <int> (state [Ids::type]));

    setTextBoxStyle ((TextEntryBoxPosition) static_cast <int> (state [Ids::textBoxPos]),
                     ! static_cast <bool> (state [Ids::editable]),
                     static_cast <int> (state [Ids::textBoxWidth]),
                     static_cast <int> (state [Ids::textBoxHeight]));

    setSkewFactor (static_cast <double> (state [Ids::skew]));
}
