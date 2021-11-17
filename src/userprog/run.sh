make
cd ../examples
make
cd ../userprog/build
rm filesys.dsk
pintos-mkdisk filesys.dsk --filesys-size=2
pintos -f -q
pintos -p tests/userprog/exec-once -a exec-once -- -q
pintos -p tests/userprog/child-simple -a child-simple -- -q
#pintos --gdb -- run 'echo hello world !'
pintos --gdb -- run 'exec-once'


