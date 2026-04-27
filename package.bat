@echo off
set NAME=issex-0.0.1
set OUT=%NAME%.zip

if exist %OUT% del %OUT%
rmdir /s /q dist 2>nul

python ./man2html.py

mkdir dist\%NAME%

copy issex.exe dist\%NAME%\
copy README.md dist\%NAME%\
copy LICENSE dist\%NAME%\
xcopy docs dist\%NAME%\docs /E /I /Q

powershell -Command "Compress-Archive -Path dist\%NAME% -DestinationPath %OUT% -Force"

echo Done: %OUT%
