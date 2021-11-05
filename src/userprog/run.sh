cd build
rm filesys.dsk
pintos-mkdisk filesys.dsk --filesys-size=2
pintos -f -q
pintos -p ../../examples/halt -a halt -- -q
pintos -q run 'halt'
