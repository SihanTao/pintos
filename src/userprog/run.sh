make
cd ../examples
make
cd ../userprog/build
rm filesys.dsk
pintos-mkdisk filesys.dsk --filesys-size=2
pintos -f -q
pintos -p ../../tests/userprog/sc-bad-sp -a sc-bad-sp -- -q
pintos -p ../../examples/echo -a echo -- -q
pintos -q run 'echo hello world !'
# pintos --gdb -- run 'sc-bad-sp'
