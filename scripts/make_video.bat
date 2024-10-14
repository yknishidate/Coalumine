set release_dir=./build/vs/Release
ffmpeg -r 30 -i %release_dir%/%%03d.jpg %release_dir%/out.mp4
