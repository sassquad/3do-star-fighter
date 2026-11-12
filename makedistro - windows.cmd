@ECHO OFF

set directory=SF3000

rem tidy up previous dir
rmdir /S /Q %directory%
del /Q SF3000-win32-archive-X.X.X.zip

rem Top level files
mkdir "%directory%"
CALL :copyfile config.txt "%directory%"
CALL :copyfile starfighter.exe "%directory%"
CALL :copyfile README.md "%directory%"

mkdir "%directory%\SF_Resources"
powershell Copy-Item -Path "SF_Resources\*" -Destination "%directory%\SF_Resources" -Recurse

powershell Compress-Archive -Path SF3000 -DestinationPath SF3000-win32-archive-X.X.X.zip

GOTO :eof

:copyfile
IF NOT EXIST %1 (
  ECHO %1 not found
  exit /B
) ELSE (
  copy %1 "%2" /b
)

:eof