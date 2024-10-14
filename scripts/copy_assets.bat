@echo off
set SRC=.\asset
set DST_DEBUG=.\build\vs\Debug\asset
set DST_RELEASE=.\build\vs\Release\asset

echo Copying files to Debug folder...
robocopy "%SRC%" "%DST_DEBUG%" /e /xo

echo Copying files to Release folder...
robocopy "%SRC%" "%DST_RELEASE%" /e /xo

echo Copy operation complete.
