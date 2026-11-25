# Paint Engine v1.0.1

Qt 6 Widgets application that finds the paint products in a CSV database that best match
a set of attribute criteria, ranked by a weighted similarity score.

## Layout

```
PaintEngine/
  CMakeLists.txt
  core/                 Qt-free C++17 library
    paint_match.hpp       matching engine (header only)
    csv_reader.*          RFC 4180 CSV parser
    paint_schema.*        attribute definitions, CSV column mapping, value parsing
  app/                  Qt 6 Widgets application
    main.cpp
    MainWindow.*          window, database loading, search, details pane, About
    CriteriaPanel.*       one input row per attribute (target, min, max, weight, required)
    InputWidgets.*        "Any"-able spin box, multi-select combo, wheel-safe inputs
    ResultsModel.*        ranked results table model + match-% bar delegate
    SettingsDialog.*      Options (database path, result count, default weights)
    AppSettings.*         settings.ini next to the exe (QSettings, INI format)
  resources/version.rc.in Windows file version (1.0.1) and exe icon
  resources/pwp_logo.*  application icon (.ico for the exe, .png for windows/taskbar)
  data/paints.csv       sample database exported from the Product Picker workbook
  tests/core_tests.cpp  console tests for the core library
  installer/            Qt Installer Framework config, package and build script
```

## The database

Value formats understood:

| Column | Accepted values |
|---|---|
| Avg. Price Point | `59`, `$59.00`, `$1,059.00` |
| Humidity Threshold on Substrate | `15%`, `15.00%`, or a fraction `0.15` |
| Barstow/Lawnwood Range | `3-5`, `5`, `3 to 5`; also Excel's date conversion (`2026-03-05`, `3/5/2026`, `5-Mar`) |
| Typical Dry Time | `1-2 to touch, 4 to recoat` (minutes or hours) -> two attributes: Dry to Touch, Recoat Time |
| Typical Cure Time | `14-30 days`, `7 days`, `2 weeks` |
| Yes/No columns | `TRUE/FALSE`, `Yes/No`, `Y/N`, `1/0` |
| List columns (Substrates, Coating Base, Sheens, Colors) | comma or semicolon separated |

Cells that cannot be read are treated as missing and listed under
*File > Database Load Report*.

## How matching works

* Leave an attribute at **Any** to ignore it.
* **Target**: scored 0-100 %. Numeric attributes are direction-aware: for "lower is better"
  (price, minimum application temperature, dry/recoat/cure time) a value below the target is
  barely penalised and a value above it is penalised harder; "higher is better" (coverage,
  humidity tolerance) is the reverse. A range value such as `14-30 days` is a perfect match
  when the target falls inside it.
* **Min / Max**: hard floor / ceiling on numeric attributes; products outside are excluded.
  A product with a range passes if any part of its range is inside the limits.
* **Weight**: relative importance in the overall score (0 = not scored). Defaults come from
  *Tools > Options > Default Weights*.
* **Req.**: the target must be fully met (numeric: per direction; text: exact or an
  equivalent such as "Both" for Interior).
* Partial credit: Interior/Exterior "Both" satisfies either side; related coating bases
  (latex~acrylic, oil~alkyd, epoxy~urethane); neighbouring sheens; "Any" color satisfies any
  color; "Order Ahead" accepts "Immediate"; same-side industry uses.
* Missing values score 50 % for that attribute.
* Overall match % = weighted mean of the attribute scores.

## Settings

`settings.ini` lives next to `PaintEngine.exe` and holds the database path (relative to the
exe folder when the file is inside it), the default weights, the default result count and
window layout. Missing keys are written with defaults on start-up. If the folder is not
writable the application says so; the installer therefore installs per user to
`%LOCALAPPDATA%\Programs\PaintEngine` rather than Program Files.
