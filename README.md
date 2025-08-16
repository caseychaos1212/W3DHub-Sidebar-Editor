# Sidebar Editor — Quick Guide

Alpha 0.1.1
## What this tool does

Curate and edit **Purchase Settings Presets** from the global definitions, then push those changes back to:

* the **global** `Database/Global/Definitions/GlobalSettings.json`, and/or
* one or more **levels** under `Database/Levels/<Level>/Definitions/GlobalSettings.json`.

You can also assign a default **camo** (forest/desert/urban/snow) per level and reorder textures accordingly.

---

## Menu actions

### Choose Source Lists

Opens a picker of Purchase Settings Presets from the global definitions.
Check the lists you want to work with; those become the tabs in the main window.

### Retarget LevelEdit Folder

Pick a new **LevelEdit** root.

> Note: currently **APB** layouts only.

### Update Global From Current Tabs (Selected Lists)

Patches the **global** definitions for the lists you selected.
Only fields you edit are updated (cost/tech/etc.); structure and unrelated lists are preserved.

> If you select multiple presets from the **same team/type** (e.g., Allied Vehicles Forest + Snow), items are **merged by unit** (base preset + alts) using a canonical key so you don’t get duplicate units.

### Update Levels From Current Tabs

Writes your opened lists into one or more **levels**.

* Creates or updates “Sidebar Editor Custom {Team} {Type} List” sections.
* Assigns **unique DEF\_IDs** automatically (guards against duplicates).


### Propagate Changes to All

Propagates edited unit fields **across every preset** that contains that unit (e.g., bump APC cost in all camos/lists).
*(This does **not** create new temp lists.)*

### Assign Camouflage to Levels

Opens a dialog to map each RA\_\* level to a default camo:

* **Apply to Selected**: tags the level in the list so you can see the assignment.
* **Save Profile**: saves your map→camo mapping.
* **Apply camo to level files**: reorders `TEXTURE` and `ALT_TEXTURES` **within the level** to prioritize the assigned camo, preserving up to 3 alts and indentation.
  *(This does **not** create new temp lists.)*

---

## Main window

* **Click an icon**: cycles the camo preview (visual only).
* **Double-click an item** to edit:

  * `COST`
  * `TECH_LEVEL`
  * `SPECIAL_TECH_NUMBER`
  * `UNIT_LIMIT`
  * `FACTORY`
  * `TECH_BUILDING`
  * `FACTORY_NOT_REQUIRED`
  


---

## Typical workflow

1. **Choose Source Lists** you want to edit.
2. Make changes in the **Main window** (double-click items).
3. **Update Global From Current Tabs** to patch the global file, **and/or**
   **Update Levels From Current Tabs** to write to specific levels.
4. (Optional) **Assign Camouflage to Levels** and **Apply camo to level files**.

---

## Paths & files

* **Global definitions** (source):
  `LevelEdit/Database/Global/Definitions/GlobalSettings.json`
* **Level definitions** (target):
  `LevelEdit/Database/Levels/<Level>/Definitions/GlobalSettings.json`
* **Level presets** (auto-generated):
  `LevelEdit/Database/Levels/<Level>/Presets/GlobalSettings.json`
* **LevelEdit location** is saved via QSettings:

  * Windows Registry: `HKCU\Software\SidebarTool\SidebarEditor\LevelEditRoot`

> Tip: back up your LevelEdit database folder before bulk updates.

---

## Bundled utility: texconv (.dds → .png)

This repo includes **Microsoft’s `texconv`** to help convert `.dds` textures to `.png` (useful for previews/exports).

Example usage:

```bat
texconv -ft png -o "out" "in\*.dds"
```

Common flags:

* `-ft png` — output format
* `-o <dir>` — output directory
* `-y` — overwrite existing files

---

## Known limitations (planned to address)

* No support yet for **Team Purchase Settings**, **Neutral Vehicles**, or **Secret Characters**.
* No manual **reordering** of purchase items (beyond camo reorder).
* Camo update and Propagate All **doesn’t create** new temp lists; it only reorders textures in existing entries.
* APB-only LevelEdit layouts.

---

## Glossary

* **Team**: `0 = Allied`, `1 = Soviet`
* **Type**: `0 Infantry`, `1 Vehicles`, `4 Extra Vehicles`, `5 Air`, `7 Extra Air`, `6 Navy`
* **Preset group**: base `PRESET_ID` + up to 3 `ALT_PRESETIDS` treated as one unit for merging/deduping.
