#include "../../Source/UI/DefaultConfig.h"
#include "gtest/gtest.h"

TEST (DefaultConfigAccessibilityTests, ExposesConfigurationChoicesAndLoadAction)
{
    Component content;
    Label instructions;
    ImageButton acquisitionBoard;
    Label acquisitionBoardLabel;
    ImageButton fileReader;
    Label fileReaderLabel;
    ImageButton neuropixels;
    Label neuropixelsLabel;
    TextButton load;

    configureDefaultConfigAccessibility (content,
                                         instructions,
                                         acquisitionBoard,
                                         acquisitionBoardLabel,
                                         fileReader,
                                         fileReaderLabel,
                                         neuropixels,
                                         neuropixelsLabel,
                                         load);

    EXPECT_EQ (content.getComponentID(), "oe.dialog.default_config.content");
    EXPECT_EQ (instructions.getComponentID(), "oe.dialog.default_config.instructions");
    EXPECT_EQ (acquisitionBoard.getComponentID(), "oe.dialog.default_config.acquisition_board");
    EXPECT_EQ (acquisitionBoardLabel.getComponentID(), "oe.dialog.default_config.acquisition_board_label");
    EXPECT_EQ (fileReader.getComponentID(), "oe.dialog.default_config.file_reader");
    EXPECT_EQ (fileReaderLabel.getComponentID(), "oe.dialog.default_config.file_reader_label");
    EXPECT_EQ (neuropixels.getComponentID(), "oe.dialog.default_config.neuropixels");
    EXPECT_EQ (neuropixelsLabel.getComponentID(), "oe.dialog.default_config.neuropixels_label");
    EXPECT_EQ (load.getComponentID(), "oe.dialog.default_config.load");

    EXPECT_EQ (fileReader.getTitle(), "File reader");
    EXPECT_FALSE (fileReader.getHelpText().isEmpty());
    EXPECT_EQ (load.getTitle(), "Load configuration");
    EXPECT_FALSE (load.getHelpText().isEmpty());
}

TEST (DefaultConfigAccessibilityTests, ExposesTheDialogWindow)
{
    Component window;

    configureDefaultConfigWindowAccessibility (window);

    EXPECT_EQ (window.getComponentID(), "oe.dialog.default_config");
    EXPECT_EQ (window.getTitle(), "Default configuration");
    EXPECT_TRUE (window.isAccessible());
}
