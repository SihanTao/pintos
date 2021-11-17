make
cd ../examples
make
cd ../userprog/build
rm filesys.dsk
pintos-mkdisk filesys.dsk --filesys-size=2
pintos -f -q
pintos -p tests/userprog/multi-child-fd -a multi-child-fd -- -q
pintos -p tests/userprog/sample.txt -a sample.txt -- -q
pintos -p tests/userprog/child-close -a child-close -- -q
#pintos --gdb -- run 'echo hello world !'
pintos --gdb -- run 'multi-child-fd'


