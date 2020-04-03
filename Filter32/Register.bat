cd /d %~dp0
for %%i in (./*.ax) do regsvr32 /s %%i
pause