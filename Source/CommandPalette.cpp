// SPDX-License-Identifier: AGPL-3.0-only
#include "CommandPalette.h"

// Search field that forwards Up/Down/Enter/Esc to the palette so the list can
// be driven without leaving the text box.
class CommandPalette::SearchBox final : public juce::TextEditor
{
public:
    explicit SearchBox (CommandPalette& p) : owner (p)
    {
        setTitle ("Search commands");
        setTextToShowWhenEmpty ("Type to search commands...", juce::Colours::grey);
        onTextChange = [this] { owner.refilter(); };
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::upKey)     { owner.moveSelection (-1); return true; }
        if (key == juce::KeyPress::downKey)   { owner.moveSelection (1);  return true; }
        if (key == juce::KeyPress::returnKey) { owner.runRow (owner.list.getSelectedRow()); return true; }
        if (key == juce::KeyPress::escapeKey) { owner.close(); return true; }
        return TextEditor::keyPressed (key);
    }

private:
    CommandPalette& owner;
};

CommandPalette::CommandPalette (juce::ApplicationCommandManager& cm) : commands (cm)
{
    for (auto& category : commands.getCommandCategories())
    {
        for (auto id : commands.getCommandsInCategory (category))
        {
            if (auto* info = commands.getCommandForID (id))
            {
                juce::StringArray keys;
                for (auto& kp : commands.getKeyMappings()->getKeyPressesAssignedToCommand (id))
                    keys.add (kp.getTextDescription());

                all.add ({ id, category, info->shortName, keys.joinIntoString (", ") });
            }
        }
    }

    searchBox = std::make_unique<SearchBox> (*this);
    addAndMakeVisible (*searchBox);

    list.setRowHeight (26);
    list.setTitle ("Commands");
    addAndMakeVisible (list);

    refilter();
    setSize (560, 420);
}

void CommandPalette::show (juce::ApplicationCommandManager& cm, juce::Component* parent)
{
    auto* palette = new CommandPalette (cm);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (palette);
    options.dialogTitle = "Command Palette";
    options.componentToCentreAround = parent;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();

    juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<CommandPalette> (palette)]
    {
        if (sp != nullptr)
            sp->searchBox->grabKeyboardFocus();
    });
}

void CommandPalette::resized()
{
    auto area = getLocalBounds().reduced (8);
    searchBox->setBounds (area.removeFromTop (28));
    area.removeFromTop (6);
    list.setBounds (area);
}

void CommandPalette::refilter()
{
    const auto tokens = juce::StringArray::fromTokens (searchBox->getText(), true);
    filtered.clearQuick();

    for (auto& e : all)
    {
        const auto haystack = e.category + " " + e.name + " " + e.shortcut;
        if (std::all_of (tokens.begin(), tokens.end(),
                         [&] (const juce::String& t) { return haystack.containsIgnoreCase (t); }))
            filtered.add (e);
    }

    list.updateContent();
    list.selectRow (filtered.isEmpty() ? -1 : 0);
    list.repaint();
}

void CommandPalette::moveSelection (int delta)
{
    if (filtered.isEmpty())
        return;

    const auto row = juce::jlimit (0, filtered.size() - 1, list.getSelectedRow() + delta);
    list.selectRow (row);
    list.scrollToEnsureRowIsOnscreen (row);
}

void CommandPalette::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, filtered.size()))
        return;

    const auto& e = filtered.getReference (row);

    if (selected)
        g.fillAll (juce::Colour (0xff2e4a62));

    g.setColour (juce::Colours::grey);
    g.setFont (13.0f);
    g.drawText (e.category, 8, 0, 90, height, juce::Justification::centredLeft);

    g.setColour (juce::Colours::white);
    g.setFont (15.0f);
    g.drawText (e.name, 100, 0, width - 280, height, juce::Justification::centredLeft);

    g.setColour (juce::Colour (0xffffb74d));
    g.setFont (13.0f);
    g.drawText (e.shortcut, width - 180, 0, 172, height, juce::Justification::centredRight);
}

void CommandPalette::runRow (int row)
{
    if (! juce::isPositiveAndBelow (row, filtered.size()))
        return;

    const auto id = filtered.getReference (row).id;
    auto& cm = commands;
    close();

    // Run after the dialog has gone so focus is back on the main window.
    juce::MessageManager::callAsync ([&cm, id] { cm.invokeDirectly (id, true); });
}

void CommandPalette::close()
{
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState (0);
}
