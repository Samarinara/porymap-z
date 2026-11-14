# Dialogue editing (first milestone)

Open a pokeemerald project in Porymap-Z and choose **Tools → Dialogue Editor…**. The dialog is modal so the active project cannot change while dialogue edits are pending.

1. Choose a file from the list. The editor indexes UTF-8 `.inc` and `.s` files under the project's `data/` directory, including map scripts and shared text.
2. Search labels or text within that file and select a dialogue label.
3. Edit the quoted text. Each physical editor line corresponds to one assembly `.string` operand. Game escape sequences remain visible: `\n` is a game line break, `\l` scrolls a line, and `\p` advances a paragraph. Keep placeholders such as `{PLAYER}`, formatting controls, and the final `$` where the script requires them. Escape a quote as `\"` and a literal backslash as `\\`.
4. Click **Save Dialogue**. Saves take effect immediately in the source file, independently of the map's Save action. Build your game and test the result in your normal emulator.

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

Switching entries/files, reloading, or closing prompts before discarding an unsaved edit. Saves use atomic file replacement. If the file changed externally, saving is refused: copy your pending text, reload, and reapply it to the updated source. Keep the game project in version control so edits can be reviewed and reverted.

## Supported scope

- Labelled blocks of single quoted `.string` operands, optionally separated by blank lines or `@` comments. Both `Label:` and `Label::` work.
- Search is within the selected file, not across the entire game.
- Assembly source only. Projects configured for Poryscript are declined, and generated `.inc` files with adjacent `.pory` source files are omitted.
- Unsupported assembly expressions are excluded rather than rewritten. C strings, inline label-plus-directive syntax, and macro-generated dialogue are not editable in this milestone.
- The index uses the standard `data/` layout; custom project dialogue directories are not yet supported. Symlink files and directories outside the project are not indexed.
- This is a source editor. It does not validate every game control code, character encoding, box width, or script reference, provide a rendered game preview, create labels, or compile the game. Preserve source conventions and verify edits in game.

The inherited map tools continue to support Porymap's other games; the new dialogue editor currently requires the pokeemerald project setting.
