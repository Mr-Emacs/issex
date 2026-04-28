@echo off
set NAME=issex-0.0.1
set OUT=%NAME%.zip

if exist %OUT% del %OUT%
if exist issex.ilk del issex.ilk
if exist issex.pdb del issex.pdb
if exist main.obj  del main.obj
if exist nob.obj   del nob.obj
if exist vc140.pdb del vc140.pdb

rmdir /s /q dist 2>nul
python ./man2html.py

mkdir dist\%NAME%
copy issex.exe     dist\%NAME%\
copy README.md     dist\%NAME%\
copy LICENSE       dist\%NAME%\

xcopy docs dist\%NAME%\docs /E /I /Q
powershell -Command "Compress-Archive -Path dist\%NAME% -DestinationPath %OUT% -Force"

rmdir /s /q dist
echo Done: %OUT%
