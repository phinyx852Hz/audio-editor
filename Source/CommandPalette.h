// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Fuzzy-searchable list of every registered command with its shortcut.
// Doubles as the keyboard shortcut reference.
class CommandPalette final : public juce::Component,
                             private juce::ListBoxModel
{
public:
    explicit CommandPalette (juce::ApplicationCommandManager&);

    static void show (juce::ApplicationCommandManager&, juce::Component* parent);

    void resized() override;

private:
    struct Entry
    {
        juce::CommandID id;
        juce::String category, name, shortcut;
    };

    class SearchBox;

    int getNumRows() override { return filtered.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { runRow (row); }
    void returnKeyPressed (int row) override                                 { runRow (row); }

    void refilter();
    void moveSelection (int delta);
    void runRow (int row);
    void close();

    juce::ApplicationCommandManager& commands;
    juce::Array<Entry> all, filtered;
    std::unique_ptr<SearchBox> searchBox;
    juce::ListBox list { "Commands", this };
};
