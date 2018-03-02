del /F *.nupkg
for /f %%f in ('dir /b *.nuspec') do nuget pack %%f
for /f %%f in ('dir /b *.nupkg') do nuget push %%f -Source odant
pause