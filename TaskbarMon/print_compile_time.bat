REM 输出当前日期和时间，并保存到compile_time.txt文件中
del /F /Q "%~dp0compile_time.txt"
echo %date:~0,10% >> "%~dp0compile_time.txt"
echo %time:~0,8% >> "%~dp0compile_time.txt"