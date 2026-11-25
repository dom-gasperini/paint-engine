// Qt Installer Framework component script for Paint Matcher.

function Component()
{
}

Component.prototype.createOperations = function()
{
    // Copies everything under ../data into the target directory.
    component.createOperations();

    if (systemInfo.productType !== "windows")
        return;

    var targetDir = installer.value("TargetDir");
    var nativeTarget = targetDir.replace(/\//g, "\\");

    component.addOperation("CreateShortcut",
                           "@TargetDir@/PaintMatcher.exe",
                           "@StartMenuDir@/Paint Matcher.lnk",
                           "workingDirectory=@TargetDir@",
                           "iconPath=@TargetDir@/PaintMatcher.exe",
                           "iconId=0",
                           "description=Find the closest matching paint products");

    component.addOperation("CreateShortcut",
                           "@TargetDir@/PaintMatcherMaintenanceTool.exe",
                           "@StartMenuDir@/Uninstall Paint Matcher.lnk",
                           "workingDirectory=@TargetDir@",
                           "description=Update or remove Paint Matcher");

    component.addOperation("CreateShortcut",
                           "@TargetDir@/PaintMatcher.exe",
                           "@DesktopDir@/Paint Matcher.lnk",
                           "workingDirectory=@TargetDir@",
                           "iconPath=@TargetDir@/PaintMatcher.exe",
                           "iconId=0",
                           "description=Find the closest matching paint products");

    // settings.ini is created by the application at runtime, so the uninstaller does not know
    // about it. Remove it on uninstall so the install folder can be deleted completely.
    component.addOperation("Execute",
                           "cmd", "/C", "exit 0",
                           "UNDOEXECUTE",
                           "cmd", "/C", "if exist \"" + nativeTarget + "\\settings.ini\" del /F /Q \"" + nativeTarget + "\\settings.ini\"");
}
