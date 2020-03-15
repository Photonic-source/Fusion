SET "dir=%~dp0"
REG ADD HKcr\Fusion /V "URL Protocol"
REG ADD HKcr\Fusion\shell\open\command /D "\"%dir%fusion.exe\" %%1"