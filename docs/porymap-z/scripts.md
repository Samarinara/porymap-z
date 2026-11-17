# Map script popup

In Porymap-Z, open a map and choose **Open Map Scripts** in the Events toolbar, or click an event's script button to jump to its definition. Assembly projects, including pokeemerald-expansion, open the script popup. Maps whose source is a `.pory` file retain the external editor. Assembly maps open the popup even when Poryscript is enabled in project settings.

The map selector uses the project's configured map directories and each map's `map.json`. Object, coordinate, and background events appear as separate entry points. Map scripts include the local script labels or the `shared_scripts_map` owner's labels. The popup follows label references through shared source files into a **Referenced scripts and text** group. Missing definitions remain visible. **All project scripts** lists indexed definitions by source file, including scripts that no map directly references. Search filters labels, groups, and paths.

Select a label to edit its assembly source. **Save Script** (Ctrl+S) replaces that label's source block, preserving the rest of the file and its newline convention. Changes save directly to the decomp, independently of map saves. The editor checks for external changes before writing and prompts to save, discard, or cancel when navigating or closing with unsaved changes.

Use **Reload Scripts** after adding or renaming labels or changing map JSON to refresh the index. The popup indexes assembly label blocks; it does not validate command semantics or compile scripts. Generated assembly with a matching `.pory` source is excluded. A script's commands, comments, and directives up to the next label appear together; following labels can be edited separately. Shared definitions affect every map that uses them.

Run the focused tests with `qmake tests/porymapz/scripts.pro` in a separate build directory, then `make` and `QT_QPA_PLATFORM=offscreen ./tst_scripts`. Optionally set `PORYMAP_SCRIPT_TEST_PROJECT` to a local pokeemerald-expansion checkout to exercise project-wide indexing without modifying that checkout.
