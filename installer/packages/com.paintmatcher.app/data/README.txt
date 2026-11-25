This folder is the payload of the installer package. Everything in it is installed into
the target directory.

Before running binarycreator it should contain:

    PaintMatcher.exe
    data\paints.csv
    (Qt DLLs, platforms\, styles\, ... from windeployqt)

Use "build_installer.bat stage <exe>" to copy the exe and sample database here, then run
windeployqt on PaintMatcher.exe in this folder. Delete this README.txt before building if
you do not want it installed.
