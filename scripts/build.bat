@echo off
rem Lancer depuis la racine du depot, dans "x64 Native Tools Command Prompt for VS".
if not exist build mkdir build
cl /nologo /std:c++17 /O2 /EHsc /MT /LD /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN src\GlbThumbnail.cpp /Fo:build\ /Fe:build\GlbThumbnail.dll
if errorlevel 1 (echo ECHEC de la compilation & exit /b 1)
copy /y scripts\install.bat build\ >nul
copy /y scripts\uninstall.bat build\ >nul
echo OK : build\GlbThumbnail.dll genere.
