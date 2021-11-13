cd ../examples
make
cd ../userprog/
make
cd ./build
rm filesys.dsk
pintos-mkdisk filesys.dsk --filesys-size=2
pintos -f -q
pintos -p ../../examples/halt -a halt -- -q
pintos -p ../../examples/echo -a echo -- -q
pintos -q run 'echo foo bar'
