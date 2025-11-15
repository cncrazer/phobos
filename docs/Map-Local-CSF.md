# Map-Local CSF Strings

Phobos allows defining CSF labels directly inside a map (scenario) file. These labels are injected into the live string table when the scenario loads and are available anywhere the engine resolves CSF strings, such as `UIName` for types and `Briefing` text.

## Syntax (map INI)

```ini
[Phobos.MapCSF]
NAME:MyCustomUnit=Prototype Battle Tank
BRIEFING:MyMission=Commander, take and hold the bridge.\n\nReinforcements are en route.
GUI:Resume=Resume
```

- Keys are CSF labels (max 31 characters). Values are plain text.
- Escape sequences supported in values: `\n` (newline), `\r`, `\t`, `\\`.
- Labels exist only for the currently loaded scenario and are removed automatically when it clears.

## Usage examples

- Use `UIName=NAME:MyCustomUnit` in `rulesmd.ini` or a map-local type override, and define the display name per-map with `[Phobos.MapCSF]`.
- Set `[Basic] -> Briefing=BRIEFING:MyMission` to provide mission briefing text without shipping a separate `.csf`.

## Notes

- Map-local entries are merged with the existing global CSF table for the duration of the scenario.
- If a label is also present globally, the engine's normal resolution applies. Map-local entries primarily help when a label is otherwise missing.
