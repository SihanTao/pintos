make
cd ../examples
make
cd ../userprog/build
rm filesys.dsk
pintos-mkdisk filesys.dsk --filesys-size=2
pintos -f -q
#pintos -p tests/userprog/no-vm/multi-oom -a multi-oom -- -q
#pintos --gdb -- run 'echo hello world !'
#pintos -p ../../tests/userprog/sc-bad-sp -a sc-bad-sp -- -q
pintos -p ../../examples/echo -a echo -- -q
pintos -p ../../examples/halt -a halt -- -q
pintos -q run 'echo hello world !'
# pintos --gdb -- run 'multi-oom'
# pintos --gdb -- run 'sc-bad-sp'
