make
cd ../examples
make
cd ../userprog/build
rm filesys.dsk
pintos-mkdisk filesys.dsk --filesys-size=2
pintos -f -q
pintos -p tests/userprog/multi-oom -a multi-oom -- -q
#pintos --gdb -- run 'echo hello world !'
pintos --gdb -- run 'multi-oom'
