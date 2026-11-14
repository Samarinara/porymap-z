# Dialogue editing (first milestone)

Open a pokeemerald project in Porymap-Z, select a map, and choose **Dialogue** in the main tab bar beside Map, Events, Header, Connections, and Wild Pokemon. This embedded workspace follows the active scene.

1. Leave the scope on **Current scene** to see the selected map's UTF-8 `.inc` and `.s` files. Its resolved script file is selected first; scenes that share scripts use the shared map's folder. Sibling and nested script/text files in that folder are included. Choose **Whole project** to browse dialogue elsewhere under `data/`.
2. Search labels or text within that file and select a dialogue label.
3. Edit the quoted text. Each physical editor line corresponds to one assembly `.string` operand. Game escape sequences remain visible: `\n` is a game line break, `\l` scrolls a line, and `\p` advances a paragraph. Keep placeholders such as `{PLAYER}`, formatting controls, and the final `$` where the script requires them. Escape a quote as `\"` and a literal backslash as `\\`.
4. Click **Save Dialogue**, or use the main Save/Save Project action (Ctrl+S). Saves take effect directly in the source file. Build your game and test the result in your normal emulator.

For example, this source:

```asm
LittlerootTown_Text_Welcome::
    .string "Welcome, {PLAYER}!\n"
    .string "Enjoy your adventure!$"
```

appears as two editable lines:

```text
Welcome, {PLAYER}!\n
Enjoy your adventure!$
```

The editor patches quoted operands and retains labels, comments, commands, unrelated text, UTF-8, and existing line endings. Adding physical editor lines adds `.string` directives. Shortening a block leaves empty operands in the unused directives so attached comments survive; these empty strings contribute no text.

Switching entries, files, edit views, scopes, maps, or layouts, reloading, or closing the project offers Save/Discard/Cancel for pending edits. Switching between the main tabs keeps edits in place. The Dialogue tab and main window show an asterisk while edits are pending. Saves use atomic file replacement. If the file changed externally, saving is refused: copy your pending text, reload, and reapply it to the updated source. Keep the game project in version control so edits can be reviewed and reverted.

## Scene script editing

Select **Script** within the Dialogue tab to edit the complete selected source file, including event commands and script labels. Files with no dialogue remain available in Current scene scope. This is a plain source editor: it does not validate assembly commands or game logic. Use **Save Script** or Ctrl+S to save, then compile and test your game. Script saves preserve the file's original UTF-8 BOM and LF/CRLF convention. Switching back to Dialogue reparses the saved strings; saving dialogue refreshes the script view. Script-label suggestions are refreshed after saves.

The workspace is cleared when a project closes and disabled in layout-only mode. Unsupported games or Poryscript projects display an explanation rather than exposing editable generated source.

## Supported scope

- Labelled blocks of single quoted `.string` operands, optionally separated by blank lines or `@` comments. Both `Label:` and `Label::` work.
- Search is within the selected file, not across the entire game.
- Assembly source only. Projects configured for Poryscript are declined, and generated `.inc` files with adjacent `.pory` source files are omitted.
- Unsupported assembly expressions are excluded rather than rewritten. C strings, inline label-plus-directive syntax, and macro-generated dialogue are not editable in this milestone.
- Current scene scope uses the project's resolved map script path, including custom map folders and shared scripts. Whole project scope uses the standard `data/` layout. Symlink files and directories outside the project are not indexed.
- This is a source editor. It does not validate every game control code, character encoding, box width, or script reference, provide a rendered game preview, or compile the game. The dialogue-by-label view cannot create new labels; use Script for source changes. Preserve source conventions and verify edits in game.

The inherited map tools continue to support Porymap's other games; the new dialogue editor currently requires the pokeemerald project setting.
