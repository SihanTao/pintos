make
cd ../examples
make
cd ../userprog/build
rm filesys.dsk
pintos-mkdisk filesys.dsk --filesys-size=2
pintos -f -q
pintos -p ../../tests/userprog/create-normal -a create-normal -- -q
#pintos --gdb -- run 'echo hello world !'
pintos --gdb -- run 'sc-bad-sp'
